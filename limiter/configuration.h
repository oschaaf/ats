#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include <map>
#include "debug_macros.h"

namespace ATS  { 
  class Configuration
  {
  public:
    static Configuration * Parse(const char * path);

    bool WhitelistForNocache(const char * address);
    bool IsNocacheWhitelisted(unsigned long address);
  private:
    explicit Configuration()  {}

    std::map<unsigned long,bool> whitelist_;
    DISALLOW_COPY_AND_ASSIGN(Configuration);
  }; //class Configuration

}//namespace

#endif
