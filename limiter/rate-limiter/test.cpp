#include "rate-limiter.h"
#include <pthread.h>
#include <unistd.h> //imports sleep()
#include <ts/ts.h>

////////////////////////////////////////////////////////////////////////
using namespace ATS_RL;

int main() {
  RateLimiter rl;
  int global_txn_index = rl.AddCounter(3.0f, 1000);
  //int global_upstream_index = rl.AddCounter(3.0f, 1000);
  //int global_downstream_index = rl.AddCounter(3.0f, 1000);

  timeval now;
  gettimeofday(&now,NULL);
  //printf("nu: %s", now);

  rl.Register(global_txn_index, "test", now,1);
  rl.Register(global_txn_index, "test", now,1);
  rl.Register(global_txn_index, "test2", now,1);
  rl.Register(global_txn_index, "test3", now,1);

  sleep(0.5);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now, 1);

  rl.Register(global_txn_index, "test4", now,1);
  rl.Register(global_txn_index, "test5", now,1);
  rl.Register(global_txn_index, "test6", now,1);
  rl.Register(global_txn_index, "test7", now,1);


  sleep(0.5);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  sleep(0.5);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  sleep(0.5);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);

  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now, 1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  sleep(1);

  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);

  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);

  rl.Register(global_txn_index, "test", now,1);

  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);
  gettimeofday(&now,NULL);
  rl.Register(global_txn_index, "test", now,1);

}
