#include <string.h>
#include <ts/ts.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ts_utils.h"
#include "txn_data.h"
#include "ssn_data.h"
#include "rate-limiter/rate-limiter.h"
#include "configuration.h"

#define INT64_MAX 0x7fffffffffffffffLL

using namespace std;
using namespace ATS;
using namespace ATS_RL;

/*
benodigde input: transactie milestones (langdurige zaken afkappen)
stats/ip (#rq/s, #sess/s, mbits in/out)
stats/global ""
cache writes/s per ip  (cache poisoning)
resources per ip en globaal
known good (?) en bad ip database (?)

acties:
- transactie afkappen
- keepalive globaal uit
- tarpit / onmiddelijk closen connecties(?) / black hole?
- priority veranderen
- packet marking
 */

// -------------------------------------------- globals ;)

TSMutex management_mutex;;
TSCont management_contp;
const int MANAGEMENT_UPDATE_RETRY_TIME = 100;

Configuration * config = NULL;

//TODO: fill global rate limiter from the parsed configuratoin
RateLimiter * rate_limiter = new RateLimiter();

int global_ssn_index = rate_limiter->AddCounter(200.0f, 10000);
int global_txn_index = rate_limiter->AddCounter(200.0f, 1000);
int global_downstream_index = rate_limiter->AddCounter(1024.0f * 1024.0f, 1000);

// -------------------------------------------- implementations

static void
free_txn_data(TSCont contp, TSHttpTxn txnp)
{
  TxnData * data = (TxnData *)TSContDataGet(contp);
  TSAssert(data != NULL);

  dbg("[txn:%ld] free", data->txn_number());

  if ( data != NULL ) { 
    delete data;
    TSContDataSet(contp, NULL);
  } else {
    werr("close txn, no data to free");
    TSAssert(!"no data to free");
  }
}


static int
schedule_txn_reenable(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)TSContDataGet(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  dbg("reenabled throttled http transaction");
  TSContDestroy(contp);
  return 0;
}

static int
schedule_ssn_reenable(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = (TSHttpSsn)TSContDataGet(contp);
  TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  dbg("reenabled throttled http session");
  TSContDestroy(contp);
  return 0;
}

typedef struct
{
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
  TSHttpTxn txnp;
  bool ready;
} TransformData;

static TransformData *
my_data_alloc()
{
  TransformData *data;

  data = (TransformData *) TSmalloc(sizeof(TransformData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->txnp = NULL;
  data->ready = false;
  return data;
}

static void
my_data_destroy(TransformData * data)
{
  TSReleaseAssert(data);
  if (data) {
    if (data->output_buffer)
      TSIOBufferDestroy(data->output_buffer);
    TSfree(data);
  }
}

static void
handle_rate_limiting_transform(TSCont contp)
{
  TSVConn output_conn;
  TSIOBuffer buf_test;
  TSVIO input_vio;
  TransformData *data;
  int64_t towrite;
  int64_t avail;

  output_conn = TSTransformOutputVConnGet(contp);
  input_vio = TSVConnWriteVIOGet(contp);

  data = (TransformData *)TSContDataGet(contp);
  if (!data->ready) {
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT64_MAX);
    data->ready=true;
  }

  buf_test = TSVIOBufferGet(input_vio);

  if (!buf_test) {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
    return;
  }

  towrite = TSVIONTodoGet(input_vio);

  if (towrite > 0) {
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));

    if (towrite > avail) 
      towrite = avail;

    if (towrite > 0) {
      const struct sockaddr_in *addr_in= (struct sockaddr_in *) TSHttpTxnClientAddrGet( data->txnp );
      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);

      int64_t rl_max = rate_limiter->GetMaxUnits(global_downstream_index, ip, towrite);
      if (rl_max != towrite)
	dbg("throttle downstream from %ld bytes to %ld bytes", towrite, rl_max);
      towrite = rl_max;

      if (towrite) {
	TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, 0);
	TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite);
	TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite);
      } else {
	//FIXME: test this case
	//we don't wake the input or output in this case.
	//reschedule ourself, to see if the rate limiter will allow some data later on
	dbg("towrite == 0, schedule a pulse");
	TSContSchedule(contp, 100, TS_THREAD_POOL_DEFAULT);
	return;
      }
    }
  }

  if (TSVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      TSVIOReenable(data->output_vio);
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(input_vio));
    TSVIOReenable(data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }
}

static int
rate_limiting_transform(TSCont contp, TSEvent event, void *edata)
{
  if (TSVConnClosedGet(contp)) {
    my_data_destroy((TransformData *)TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR:
      {
        TSVIO input_vio = TSVConnWriteVIOGet(contp);
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
      }
      break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
    default:
      handle_rate_limiting_transform(contp);
      break;
    }
  }

  return 0;
}

static void
transform_add(TSHttpTxn txnp)
{
  TSVConn contp = TSTransformCreate(rate_limiting_transform, txnp);
  TransformData * data = my_data_alloc();
  data->txnp = txnp;
  TSContDataSet(contp, data);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
}

static int
txn_hooks(TSCont contp, TSEvent event, void *edata)
{
  switch(event) {
    case TS_EVENT_HTTP_READ_REQUEST_HDR: {
      TSHttpTxn txnp = (TSHttpTxn) edata;
      transform_add(txnp);
      std::string h = get_header(ATS::kClientRequest, txnp, "rl-debug", -1);
      if (h=="1")
	TSHttpTxnDebugSet(txnp, 1);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }  break;
    case TS_EVENT_HTTP_TXN_CLOSE:{ 
      TSHttpTxn txnp = (TSHttpTxn) edata;
      
      /*
      const struct sockaddr_in *addr_in= (struct sockaddr_in *) TSHttpTxnClientAddrGet( txnp );
      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);
      rate_limiter->Release(global_txn_index, ip, 1);
      */
      free_txn_data(contp,txnp);
      TSContDestroy(contp);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }  break;
    default: { //FIXME: TS_EVENT_ERROR should probably not exit TS
      werr("unexpected event received: %d", event);
      TSHttpTxn txnp = (TSHttpTxn) edata;
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }  break;
  }

  return 0;
}

static int
ssn_hooks(TSCont contp, TSEvent event, void *edata)
{
  TSAssert(event == TS_EVENT_HTTP_SSN_CLOSE);

  SsnData * data = (SsnData *)TSContDataGet(contp);

  TSAssert(data);

  /*  TSHttpSsn ssnp = (TSHttpSsn) edata;
  const struct sockaddr_in *addr_in= (struct sockaddr_in *) TSHttpSsnClientAddrGet( ssnp );
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);
  rate_limiter->Release(global_ssn_index, ip, 1);*/

  dbg("[ssn:%ld] free", data->ssn_number());

  delete data;
  TSContDestroy(contp);
  TSHttpSsnReenable((TSHttpSsn) edata, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

static int
ssn_start(TSCont contp, TSEvent event, void *edata)
{
  TSAssert(event == TS_EVENT_HTTP_SSN_START);
  TSHttpSsn ssnp = (TSHttpSsn) edata;
  const struct sockaddr_in *addr_in= (struct sockaddr_in *) TSHttpSsnClientAddrGet( ssnp );

  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);

  bool reschedule=false;
  uint64_t ms_left = rate_limiter->Register(global_ssn_index, ip, 1);

  if (ms_left) {
    if ( ms_left <= rate_limiter->max_debt_ms() ) {
      clog("throttle [%s]-> reschedule ssn %ld ms ahead", ip, ms_left);
      //fixme: disable keepalive for this ssn?
      reschedule=true;
    } else { 
      clog("throttle [%s] -> abort ssn, max debt exceeded (%ld)", ip, ms_left);
      TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_ERROR);
      return 0;
    }
  }

  TSCont ssn_contp = TSContCreate(ssn_hooks, TSMutexCreate());
  SsnData * data = new SsnData();

  dbg("[ssn:%ld] start", data->ssn_number());

  TSContDataSet(ssn_contp, data); 
  TSHttpSsnHookAdd(ssnp, TS_HTTP_SSN_CLOSE_HOOK, ssn_contp);

  if (reschedule) {
    TSCont rc = TSContCreate(schedule_ssn_reenable, NULL);
    TSContDataSet(rc, ssnp);
    TSContSchedule(rc, ms_left, TS_THREAD_POOL_DEFAULT);
  } else {
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  }

  return 0;
}

static int
txn_start(TSCont contp, TSEvent event, void *edata)
{
  TSAssert(event == TS_EVENT_HTTP_TXN_START);
  TSHttpTxn txnp = (TSHttpTxn) edata;

  const struct sockaddr_in *addr_in= (struct sockaddr_in *) TSHttpTxnClientAddrGet( txnp );
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr_in->sin_addr), ip, INET_ADDRSTRLEN);

  bool reschedule=false;

  uint64_t ms_left = rate_limiter->Register(global_txn_index, ip, 1);
  if (ms_left) {
    if ( ms_left <= rate_limiter->max_debt_ms() ) {
      clog("throttle [%s]-> reschedule txn %ld ms ahead", ip, ms_left);
      //fixme: disable keepalive for this txn?
      reschedule=true;
    } else { 
      clog("throttle [%s] -> abort transaction, max debt exceeded (%ld)", ip, ms_left);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      return 0;
    }
  }

  TSCont txn_contp = TSContCreate(txn_hooks, TSMutexCreate());
  TxnData * data = new TxnData();
  dbg("[txn:%ld] start", data->txn_number());
  TSContDataSet(txn_contp, data); 

  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, txn_contp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

  if (reschedule) {
    TSCont rc = TSContCreate(schedule_txn_reenable, NULL);
    TSContDataSet(rc, txnp);
    TSContSchedule(rc, ms_left, TS_THREAD_POOL_DEFAULT);
  } else {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  return 0;
}


static void
read_configuration(TSCont contp) {
  if ( TSMutexLockTry(management_mutex) != TS_SUCCESS ) {
    dbg("failed to grab the management mutex, reschedule and exit");
    TSContSchedule(contp, MANAGEMENT_UPDATE_RETRY_TIME, TS_THREAD_POOL_DEFAULT);
    return;
  }

  dbg("read configuration: mutex held, continue");

  //FIXME: hardcoded path
  Configuration * newconfig = Configuration::Parse("/home/oschaaf/code/limiter/test.limiter.config");

  //FIXME: thread safety. we need a read/write lock here to access config in events and over here
  if (config != NULL )
    delete config;
  config = newconfig;

  dbg("configuration update finished, unlock mutex");
  TSMutexUnlock(management_mutex);
}

static int
management_update(TSCont contp, TSEvent event, void *edata)
{
  TSReleaseAssert(event == TS_EVENT_MGMT_UPDATE || event == TS_EVENT_TIMEOUT);
  TSReleaseAssert(contp == management_contp);

  dbg("management update event received");
  read_configuration(contp);
  return 0;
}


void
TSPluginInit(int argc, const char *argv[])
{
  management_mutex = TSMutexCreate();
  management_contp = TSContCreate(management_update, management_mutex);

  TSMgmtUpdateRegister(management_contp, TAG);
  read_configuration(management_contp);

  TSCont txn_contp = TSContCreate(txn_start, NULL);
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, txn_contp);

  TSCont ssn_contp = TSContCreate(ssn_start, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssn_contp);

  clog("initialized");
}
