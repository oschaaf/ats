#ifndef SSN_DATA_H
#define SSN_DATA_H

#include <time.h>
#include <ts/ts.h>
#include <string>
#include "debug_macros.h"

namespace ATS {

class SsnData {

 public:
  explicit SsnData()
  {
    //os: the increment and read must be atomic, since this is called from multiple threads
    int64_t newvalue = __sync_add_and_fetch(&s_ssn_number_, 1);
    set_ssn_number(newvalue);
  }

  void set_ssn_number(int64_t x) { ssn_number_ = x; }

  //incremented each time a  new SsnData is constructed
  int64_t ssn_number() { return ssn_number_; }

 private:
  int64_t ssn_number_;

  static int64_t s_ssn_number_;

  DISALLOW_COPY_AND_ASSIGN( SsnData );
};
}//namespace

#endif
