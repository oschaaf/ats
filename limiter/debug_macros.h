#ifndef __PHTTP_DBG_
#define __PHTTP_DBG_

#include <ts/ts.h>

#define TAG "limiter"
#define werr(fmt, args...) do {                                    \
  TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  TSDebug(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  TSReleaseAssert(0);\
  } while (0)

#define dbg(fmt, args...) do {                                    \
  TSDebug(TAG, "[%s:%d] [%s] DEBUG: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  } while (0)

#define clog(fmt, args...) do {                                    \
  TSDebug(TAG, fmt, ##args ); \
  } while (0)


#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

#endif //__!PHTTP_DBG_                                                                                                                                                           

