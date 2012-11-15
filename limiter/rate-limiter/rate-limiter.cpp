#include "rate-limiter.h"
#include <ts/ts.h>
#include <stdio.h>
#include "../debug_macros.h"
#include "math.h"

namespace ATS_RL {
  //TODO: check for division by zero on misconfiguratio
  //TODO: bounds checks on everything that has to do with counters

  int RateLimiter::AddCounter(float max_rate, uint64_t milliseconds) {
    LimiterEntry * entry = new LimiterEntry(max_rate, milliseconds);
    counters_.push_back(entry);
    return counters_.size()-1;
  }

  uint64_t RateLimiter::Register(int counter_index, const char * key, uint64_t amount) {
    timeval now;
    gettimeofday(&now,NULL);
    return this->Register(counter_index, key, now, amount);
  }

  uint64_t RateLimiter::GetMaxUnits(int counter_index, const char * key, uint64_t amount) {
    timeval now;
    gettimeofday(&now,NULL);
    return this->GetMaxUnits(counter_index, key, now, amount);
  }

  float * RateLimiter::GetCounterArray() {
    size_t size = counters_.size() * sizeof(float);
    float * a = (float *)malloc(size);
    memset(a,0,size);
    for(size_t i = 0; i < counters_.size(); i++) { 
      a[i] = counters_[i]->max_rate();
    }
    return a;
  }

  timeval * RateLimiter::GetTimevalArray(timeval time) {
    size_t size = counters_.size() * sizeof(timeval);
    timeval * a = (timeval *)malloc(size);
    memset(a,0,size);
    for(size_t i = 0; i < counters_.size(); i++) { 
      a[i] = time;
    }
    return a;
  }


  uint64_t RateLimiter::GetMaxUnits(int counter_index, const char * key, const timeval& time, uint64_t amount) {
    LimiterEntry * limiter_entry = counters_[counter_index];

    TSReleaseAssert(!pthread_rwlock_rdlock(&rwlock_keymap_));

    std::map<const char *,LimiterState *>::iterator it = keymap_.find(key);
    LimiterState * state = NULL;

    TSReleaseAssert(!pthread_rwlock_unlock(&rwlock_keymap_));

    if ( it == keymap_.end() ) {
      char * key_copy = (char *)malloc( strlen(key) + 1  );
      strcpy(key_copy, key);
      //NOTE: consider letting only limiterentry construct new states
      state = new LimiterState(GetCounterArray(), GetTimevalArray(time));
      TSReleaseAssert(!pthread_rwlock_wrlock(&rwlock_keymap_));
      keymap_.insert( std::pair<const char *, LimiterState *> ( key_copy, state ));
      TSReleaseAssert(!pthread_rwlock_unlock(&rwlock_keymap_));    
    } else {
      state = it->second;
    }


    //******** fixme -> this should have a critical section per key
    TSMutexLock(update_mutex_);
    timeval elapsed;
    timeval stime = state->time(counter_index);
    timersub(&time, &stime, &elapsed);

    //FIXME: tv_usec seems to differ on different compilers/platforms :S
    float elapsed_ms = (elapsed.tv_sec * 1000.0f) + ( elapsed.tv_usec/1000.0f );
    float rate_timeslice = 1.0f - (limiter_entry->milliseconds() - elapsed_ms) / limiter_entry->milliseconds();
    //FIXME: -> this is very very ugly
    if (rate_timeslice<0) rate_timeslice=0;

    //FIXME: this one sometimes gets slightly negative ... shudder
    TSReleaseAssert(rate_timeslice >= 0.0f);

    float replenishment  = rate_timeslice * limiter_entry->max_rate();
    float newallowance   = state->allowance(counter_index) + replenishment;

    //clip the new allowance value at the max rate for this limiter
    newallowance = newallowance > limiter_entry->max_rate() ? limiter_entry->max_rate() : newallowance;
    TSReleaseAssert(newallowance >= 0.0f);

    int rv = amount;

    if (amount > newallowance) {
      amount = rv = newallowance;
    }

    TSReleaseAssert(rv >= 0);

    //now substract the specified amount
    newallowance -= amount;
    
    if (newallowance >= 0.0f  ) {
      //update the state
      state->set_allowance(counter_index, newallowance);
      state->set_time(counter_index, time);
    }
    TSMutexUnlock(update_mutex_);

    //******** end fixme 

    return rv;
  }



  void RateLimiter::Release(int counter_index, const char * key, uint64_t amount) {
    TSReleaseAssert(!pthread_rwlock_rdlock(&rwlock_keymap_));

    std::map<const char *,LimiterState *>::iterator it = keymap_.find(key);

    TSReleaseAssert(!pthread_rwlock_unlock(&rwlock_keymap_));
    TSReleaseAssert( it != keymap_.end() );
    LimiterState * state = it->second;


    TSMutexLock(update_mutex_);
    state->set_taken(counter_index, state->taken(counter_index) - amount);
    dbg("released amount, currently taken %f", state->taken(counter_index));
    TSMutexUnlock(update_mutex_);
  }

  //fixme: add to blacklist with a timestamp when exceeding max_debt_ms()?
  //whe should blacklist for a configurable amount of time, and stop
  //substracting allowance in the rate limiter for this ip

  //if the allowance drops too low, clamp it
  //FIXME: this should probably be a property on the limiterentry (hold_time)?
  //if (newallowance < (limiter_entry->max_rate() * -1))
  //newallowance = limiter_entry->max_rate() * -1;

  //TODO: thread safety!! this assumes the stl map is implemented using a r/b tree that does not change
  // during lookups. e.g. concurrent reads are allowed.
  //TODO: a hash table would be more appropriate then a std::map
  //this function updates the specified counter for the specified key, substring amount units at the specified time.
  //the return value is 0 when no throttling is needed. it returns the number of ms that needs te be waited for to allow the 
  //specified amount to be substracted from the remaining allowance
  uint64_t RateLimiter::Register(int counter_index, const char * key, const timeval& time, uint64_t amount) {
    LimiterEntry * limiter_entry = counters_[counter_index];

    TSReleaseAssert(!pthread_rwlock_rdlock(&rwlock_keymap_));

    std::map<const char *,LimiterState *>::iterator it = keymap_.find(key);
    LimiterState * state = NULL;

    TSReleaseAssert(!pthread_rwlock_unlock(&rwlock_keymap_));

    if ( it == keymap_.end() ) {
      char * key_copy = (char *)malloc( strlen(key) + 1  );
      strcpy(key_copy, key);
      //NOTE: consider letting only limiterentry construct new states
      state = new LimiterState(GetCounterArray(), GetTimevalArray(time));
      TSReleaseAssert(!pthread_rwlock_wrlock(&rwlock_keymap_));
      keymap_.insert( std::pair<const char *, LimiterState *> ( key_copy, state ));
      TSReleaseAssert(!pthread_rwlock_unlock(&rwlock_keymap_));    
    } else {
      state = it->second;
    }


    //******** fixme -> this could have a critical section per key, 
    //but that would generate a lot of lock structures
    TSMutexLock(update_mutex_);
    timeval elapsed;
    timeval stime = state->time(counter_index);
    timersub(&time, &stime, &elapsed);

    //FIXME: tv_usec seems to differ on different compilers/platforms :S
    float elapsed_ms = (elapsed.tv_sec * 1000.0f) + ( elapsed.tv_usec/1000.0f );
    float rate_timeslice = 1.0f - (limiter_entry->milliseconds() - elapsed_ms) / limiter_entry->milliseconds();
    //FIXME: -> this is very very ugly
    if (rate_timeslice<0) rate_timeslice=0;

    //FIXME: this one sometimes gets slightly negative ... shudder
    TSReleaseAssert(rate_timeslice >= 0.0f);

    float replenishment  = rate_timeslice * limiter_entry->max_rate();
    float newallowance   = state->allowance(counter_index) + replenishment;

    //clip the new allowance value at the max rate for this limiter
    newallowance = newallowance > limiter_entry->max_rate() ? limiter_entry->max_rate() : newallowance;

    //now substract the specified amount
    newallowance -= amount;
    
    if (newallowance >= 0.0f ) {
      //update the state
      state->set_allowance(counter_index, newallowance);
      state->set_time(counter_index, time);
    }
    state->set_taken(counter_index, state->taken(counter_index) + amount);
    dbg("added amount, currently taken %f", state->taken(counter_index));
    TSMutexUnlock(update_mutex_);

    //******** end fixme 


    if (newallowance >= 0.0f) {
      //dbg("OK  : rate timeslice: %f (elapsed ms: %f), replenish %f units, substract %ld, new allowance %f\n", rate_timeslice, elapsed_ms, replenishment, amount, newallowance);
      return 0;
    } else { 
      float debt_factor = 1.0f - ( (limiter_entry->max_rate() + newallowance) / limiter_entry->max_rate()  );
      uint64_t wait_ms = ceil((float)limiter_entry->milliseconds() * debt_factor);
      //clog("RL!!: rate timeslice: %f, replenish %f units, substract %ld, if allowed, debt would be %f (%ld ms)\n", rate_timeslice, replenishment, amount, newallowance, wait_ms);
      return wait_ms;
    }
  }

  LimiterEntry * RateLimiter::GetCounter(int index) {
    return counters_[index];
  }
}



