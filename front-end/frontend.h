#ifndef FRONTEND_H
#define FRONTEND_H
#include <config.h>

#define RESET_PC 0x00000000
#define PMEM_OFFSET RESET_PC

/* Whether to use true icache model */
// #define USE_IDEAL_ICACHE // use true icache model by default
#ifndef USE_IDEAL_ICACHE

#ifndef USE_TRUE_ICACHE
#define USE_TRUE_ICACHE
#endif

#endif

#ifndef ICACHE_LINE_SIZE
#define ICACHE_LINE_SIZE 32 // Size of a cache line in bytes
#endif

/*#define IO_version*/

// #define RAS_ENABLE  // if not defined, return address is predicted by BTB

#define DEBUG_PRINT 0
#define DEBUG_LOG(fmt, ...)                                                    \
  do {                                                                         \
    if (DEBUG_PRINT)                                                           \
      printf(fmt, ##__VA_ARGS__);                                              \
  } while (0)

#define DEBUG_PRINT_SMALL 0
#define DEBUG_LOG_SMALL(fmt, ...)                                              \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL)                                                     \
      printf(fmt, ##__VA_ARGS__);                                              \
  } while (0)

#define DEBUG_PRINT_SMALL_2 0
#define DEBUG_LOG_SMALL_2(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_2)                                                   \
      printf(fmt, ##__VA_ARGS__);                                              \
  } while (0)

#define DEBUG_PRINT_SMALL_3 0
#define DEBUG_LOG_SMALL_3(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_3)                                                   \
      printf(fmt, ##__VA_ARGS__);                                              \
  } while (0)
// #define IO_GEN_MODE
/*#define MISS_MODE*/
extern int io_gen_cnt;
#endif
