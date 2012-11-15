#ifndef TXN_DATA_H
#define TXN_DATA_H

#include <time.h>
#include <ts/ts.h>
#include <string>
#include "debug_macros.h"

namespace ATS {

class TxnData {

 public:
  explicit TxnData()
  {
    //os: the increment and read must be atomic, since this is called from multiple threads
    int64_t newvalue = __sync_add_and_fetch(&s_txn_number_, 1);
    set_txn_number(newvalue);
  }

  void set_txn_number(int64_t x) { txn_number_ = x; }

  //incremented each time a  new TxnData is constructed
  int64_t txn_number() { return txn_number_; }

 private:
  int64_t txn_number_;

  static int64_t s_txn_number_;

  DISALLOW_COPY_AND_ASSIGN( TxnData );
};
}//namespace

#endif
