#include "txn_data.h"
#include "ts_utils.h"
#include "debug_macros.h"

namespace ATS { 
  using namespace std;
  int64_t TxnData::s_txn_number_ = 0;
}
