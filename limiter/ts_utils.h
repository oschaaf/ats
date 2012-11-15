#ifndef TS_UTILS_H
#define TS_UTILS_H

#include <string>
#include <ts/ts.h>
#include "debug_macros.h"


namespace ATS 
{
  enum HEADER_SOURCE {
    kClientRequest,
    kClientResponse
  };

  bool remove_header(TSMBuffer bufp, TSMLoc locp, const char * header, int header_length);
  bool remove_header(HEADER_SOURCE source, TSHttpTxn txnp, const char * header, int header_length);


  std::string get_header(TSMBuffer bufp, TSMLoc locp, const char * header, int header_length);
  std::string get_header(HEADER_SOURCE source, TSHttpTxn txnp, const char * header, int header_length);

}

#endif
