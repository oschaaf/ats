//FIXME: switch the map to a hash map, which should perform better on big lists

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <map>
#include <sys/time.h>
#include <pthread.h>
#include <ts/ts.h>

#include "../debug_macros.h"

namespace ATS_RL {

  class LimiterEntry {
 public:
 LimiterEntry(uint64_t max_rate, uint64_t milliseconds) :
    max_rate_(max_rate),
        milliseconds_(milliseconds) 
        {
        }
    uint64_t max_rate() { return max_rate_; }
    uint64_t milliseconds() { return milliseconds_; }
 private:
    const uint64_t max_rate_;
    const uint64_t milliseconds_;
    DISALLOW_COPY_AND_ASSIGN(LimiterEntry);
  };//class LimiterEntry


  class LimiterState {
  public:
    explicit LimiterState(float allowances[], timeval times[]) : 
    allowances_(allowances) 
      ,times_(times)
      ,taken_(NULL)
      {
	taken_ = (float *)malloc(sizeof(allowances));
	memset(taken_,0, sizeof(allowances)); 
      }

    float allowance(int index) { return allowances_[index]; }
    void set_allowance(int index, float x) { allowances_[index] = x; }

    timeval time(int index) { return times_[index]; }
    void set_time(int index, timeval x) { times_[index] = x; }

    float taken(int index) { return taken_[index]; }
    void set_taken(int index, float amount) { taken_[index] += amount; }

    ~LimiterState() {
      free(allowances_);
      free(times_);
      free(taken_);
    }

  private:
    float * allowances_;
    timeval * times_;
    float * taken_;

    DISALLOW_COPY_AND_ASSIGN(LimiterState);
  };//class LimiterState

  struct cmp_str {
    bool operator()(char const *a, char const *b) {
      return strcmp(a,b) < 0;
    }
  };

  class RateLimiter {
 public:
    explicit RateLimiter() :
        max_debt_ms_(1000) /*fixme: this should be parsed from the configuration*/
      , update_mutex_(TSMutexCreate())
    { 
      int rc = pthread_rwlock_init(&rwlock_keymap_, NULL);
      TSReleaseAssert(!rc);
    }

    ~RateLimiter() {
      for(size_t i = 0; i < counters_.size(); i++) {
        delete counters_[i];
      }

      std::map<const char *,LimiterState *>::iterator it;

      for(it = keymap_.begin(); it != keymap_.end(); it++) {
        free((void *)it->first);
	delete it->second;
      }
      pthread_rwlock_destroy(&rwlock_keymap_);
    }

    int AddCounter(float max_rate, uint64_t milliseconds);

    uint64_t Register(int counter_index, const char * key, uint64_t amount);
    uint64_t Register(int counter_index, const char * key, const timeval& time, uint64_t amount);
    void Release(int counter_index, const char * key, uint64_t amount);

    uint64_t GetMaxUnits(int counter_index, const char * key, uint64_t amount);
    uint64_t GetMaxUnits(int counter_index, const char * key, const timeval& time,  uint64_t amount);

    LimiterEntry * GetCounter(int index);
    uint64_t max_debt_ms() { return max_debt_ms_; }
 private:
    float * GetCounterArray();
    timeval * GetTimevalArray(timeval tv);

    uint64_t max_debt_ms_;
    std::vector<LimiterEntry *> counters_;

    pthread_rwlock_t rwlock_keymap_;
    TSMutex update_mutex_;
    //both the key and value are owned by the rate limiter
    std::map<const char *, LimiterState *, cmp_str> keymap_; 



    DISALLOW_COPY_AND_ASSIGN(RateLimiter);
  };//class RateLimiter

} //namespace

#endif
 
