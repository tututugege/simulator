#ifndef FRONTEND_DIAG_CONFIG_H
#define FRONTEND_DIAG_CONFIG_H

#include <cstdio>

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif
#define DEBUG_LOG(fmt, ...)                                                    \
  do {                                                                         \
    if (DEBUG_PRINT)                                                           \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef DEBUG_PRINT_SMALL
#define DEBUG_PRINT_SMALL 0
#endif
#define DEBUG_LOG_SMALL(fmt, ...)                                              \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL)                                                     \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef DEBUG_PRINT_SMALL_2
#define DEBUG_PRINT_SMALL_2 0
#endif
#define DEBUG_LOG_SMALL_2(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_2)                                                   \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef DEBUG_PRINT_SMALL_3
#define DEBUG_PRINT_SMALL_3 0
#endif
#define DEBUG_LOG_SMALL_3(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_3)                                                   \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef DEBUG_PRINT_SMALL_4
#define DEBUG_PRINT_SMALL_4 0
#endif
#define DEBUG_LOG_SMALL_4(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_4)                                                   \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef DEBUG_PRINT_SMALL_5
#define DEBUG_PRINT_SMALL_5 0
#endif
#define DEBUG_LOG_SMALL_5(fmt, ...)                                            \
  do {                                                                         \
    if (DEBUG_PRINT_SMALL_5)                                                   \
      std::printf(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#ifndef FRONTEND_ENABLE_RUNTIME_STATS_SUMMARY
#define FRONTEND_ENABLE_RUNTIME_STATS_SUMMARY 0
#endif

#ifndef FRONTEND_ENABLE_FALCON_STATS
#define FRONTEND_ENABLE_FALCON_STATS 0
#endif

#ifndef FRONTEND_ENABLE_TRAINING_AREA_STATS
#define FRONTEND_ENABLE_TRAINING_AREA_STATS 0
#endif

#ifndef FRONTEND_ENABLE_HOST_PROFILE
#define FRONTEND_ENABLE_HOST_PROFILE 0
#endif

#ifndef FRONTEND_HOST_PROFILE_SAMPLE_SHIFT
#define FRONTEND_HOST_PROFILE_SAMPLE_SHIFT 8
#endif

#endif
