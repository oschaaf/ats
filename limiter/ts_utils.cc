#include <cstddef> //defined NULL
#include <string>
#include "ts_utils.h"
#include <ts/ts.h>
namespace ATS
{
  bool remove_header(TSMBuffer bufp, TSMLoc locp, const char * header, int header_length) 
  {
    TSAssert(bufp != NULL);
    TSAssert(locp != NULL);

    TSMLoc field_loc = TSMimeHdrFieldFind(bufp, locp, header, header_length);
    
    while (field_loc) {
      TSMLoc tmp_loc = TSMimeHdrFieldNextDup(bufp, locp, field_loc);
      if ( TSMimeHdrFieldDestroy(bufp, locp, field_loc) != TS_SUCCESS ) {
	dbg("failed to destroy a field!");
	return false;
      }
      TSHandleMLocRelease(bufp, locp, field_loc);
      field_loc = tmp_loc;
    }

    return true;
  }

  bool remove_header(HEADER_SOURCE source, TSHttpTxn txnp, const char * header, int header_length) 
  {
    TSAssert(txnp != NULL);
    TSAssert(header != NULL);
    
    TSMBuffer bufp = NULL;
    TSMLoc locp = NULL;
    
    switch(source) {
    case ATS::kClientRequest:
      if ( TSHttpTxnClientReqGet(txnp, &bufp, &locp) != TS_SUCCESS ) {
	dbg("Failed to retrieve the client request header");
	return false;
      }
      break;
    case ATS::kClientResponse:
      if ( TSHttpTxnClientRespGet(txnp, &bufp, &locp) != TS_SUCCESS ) {
	dbg("Failed to retrieve the client response header");
	return false;
      }
      break;
    default:
      TSAssert(!"invalid source specified");
      return false;
      break;
    }
    
    remove_header(bufp, locp, header, header_length);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, locp);

    return true;
  }


  std::string get_header(TSMBuffer bufp, TSMLoc locp, const char * header, int header_length) 
  {
    TSAssert(bufp != NULL);
    TSAssert(locp != NULL);

    TSMLoc field_loc = TSMimeHdrFieldFind(bufp, locp, header, header_length);
    std::string r;
    
    while (field_loc) {
      TSMLoc tmp_loc = TSMimeHdrFieldNextDup(bufp, locp, field_loc);
      
      int len;
      const char * val = TSMimeHdrFieldValueStringGet(bufp, locp, field_loc, -1, &len);

      if (val && len) { 
	if ( r != "" )r = "," + r;
	r = r + std::string(val,len);
      }

      TSHandleMLocRelease(bufp, locp, field_loc);
      field_loc = tmp_loc;
    }

    return r;
  }

  std::string get_header(HEADER_SOURCE source, TSHttpTxn txnp, const char * header, int header_length) 
  {
    TSAssert(txnp != NULL);
    TSAssert(header != NULL);
    
    TSMBuffer bufp = NULL;
    TSMLoc locp = NULL;
    
    switch(source) {
    case ATS::kClientRequest:
      if ( TSHttpTxnClientReqGet(txnp, &bufp, &locp) != TS_SUCCESS ) {
	dbg("Failed to retrieve the client request header");
	return std::string();
      }
      break;

    case ATS::kClientResponse:
      if ( TSHttpTxnClientRespGet(txnp, &bufp, &locp) != TS_SUCCESS ) {
	dbg("Failed to retrieve the client response header");
	return std::string();
      }
      break;
    default:
      TSAssert(!"invalid source specified");
      return std::string();
      break;
    }

    std::string r = get_header(bufp, locp, header, header_length);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, locp);
    return r;
  }


}
