#pragma once
#include <cstdint>
#include <cstdio>

extern long long sim_time; // Global simulation time

#ifndef SIM_DEBUG_PRINT
#define SIM_DEBUG_PRINT 0
#endif

#ifndef SIM_DEBUG_PRINT_CYCLE_BEGIN
#define SIM_DEBUG_PRINT_CYCLE_BEGIN 0LL
#endif

#ifndef SIM_DEBUG_PRINT_CYCLE_END
#define SIM_DEBUG_PRINT_CYCLE_END 0LL
#endif

#define SIM_DEBUG_PRINT_ACTIVE                                                \
  (SIM_DEBUG_PRINT &&                                                         \
   sim_time >= static_cast<long long>(SIM_DEBUG_PRINT_CYCLE_BEGIN) &&        \
   sim_time <= static_cast<long long>(SIM_DEBUG_PRINT_CYCLE_END))

// ============================================================
// [Debug Logging]
// ============================================================

constexpr uint64_t LOG_START = 0;
constexpr uint64_t BACKEND_LOG_START = 20000000000; // Effectively disabled
constexpr uint64_t MEMORY_LOG_START = LOG_START;
constexpr uint64_t LSU_MEM_LOG_START = MEMORY_LOG_START;
constexpr uint64_t DCACHE_LOG_START = LOG_START;
constexpr uint64_t MMU_LOG_START = LOG_START;

// Master log enable
// #define LOG_ENABLE
// Domain enables (effective only when LOG_ENABLE is enabled)
// #define LOG_MEMORY_ENABLE
// #define LOG_LSU_MEM_ENABLE
// #define LOG_DCACHE_ENABLE
// #define LOG_MMU_ENABLE

#ifdef LOG_ENABLE
#define BACKEND_LOG (sim_time >= BACKEND_LOG_START)

#ifdef LOG_MEMORY_ENABLE
#define MEM_LOG (sim_time >= MEMORY_LOG_START)
#else
#define MEM_LOG (0)
#endif

#ifdef LOG_LSU_MEM_ENABLE
#define LSU_MEM_LOG (sim_time >= LSU_MEM_LOG_START)
#else
#define LSU_MEM_LOG (0)
#endif

#ifdef LOG_DCACHE_ENABLE
#define DCACHE_LOG (sim_time >= DCACHE_LOG_START)
#else
#define DCACHE_LOG (0)
#endif

#ifdef LOG_MMU_ENABLE
#define MMU_LOG (sim_time >= MMU_LOG_START)
#else
#define MMU_LOG (0)
#endif

#else
#define BACKEND_LOG (0)
#define MEM_LOG (0)
#define LSU_MEM_LOG (0)
#define DCACHE_LOG (0)
#define MMU_LOG (0)
#endif

#define DBG_PRINTF(fmt, ...)                                                   \
  do {                                                                         \
    if (BACKEND_LOG) {                                                         \
      std::printf(fmt, ##__VA_ARGS__);                                         \
    }                                                                          \
  } while (0)

#define LSU_MEM_DBG_PRINTF(fmt, ...)                                           \
  do {                                                                         \
    if (LSU_MEM_LOG) {                                                         \
      std::printf(fmt, ##__VA_ARGS__);                                         \
    }                                                                          \
  } while (0)

#define LSU_MEM_DBG_FPRINTF(stream, fmt, ...)                                  \
  do {                                                                         \
    if (LSU_MEM_LOG) {                                                         \
      std::fprintf(stream, fmt, ##__VA_ARGS__);                                \
    }                                                                          \
  } while (0)

// ============================================================
// [Perf Trace / Snapshot]
// ============================================================
// Targeted LSU trace (1-based sequence id):
// 0 = disabled, N = trace the Nth load/store entering LDQ/STQ.

// Perf snapshot at a specific sim-time(cycle):
// 0 = disabled, N > 0 = capture committed(total/load/store) at cycle N.
#ifndef CONFIG_PERF_SNAPSHOT_SIM_TIME
#define CONFIG_PERF_SNAPSHOT_SIM_TIME 0
#endif

// Periodic perf snapshots:
// - INTERVAL=0: disabled
// - BEGIN/END: capture window (inclusive)
// - MAX: max snapshot records kept (avoid huge logs)
#ifndef CONFIG_PERF_PERIODIC_SNAPSHOT_INTERVAL
#define CONFIG_PERF_PERIODIC_SNAPSHOT_INTERVAL 0
#endif

#ifndef CONFIG_PERF_PERIODIC_SNAPSHOT_BEGIN
#define CONFIG_PERF_PERIODIC_SNAPSHOT_BEGIN 26000
#endif

#ifndef CONFIG_PERF_PERIODIC_SNAPSHOT_END
#define CONFIG_PERF_PERIODIC_SNAPSHOT_END 35000
#endif

#ifndef CONFIG_PERF_PERIODIC_SNAPSHOT_MAX
#define CONFIG_PERF_PERIODIC_SNAPSHOT_MAX 256
#endif

// Diagnostic switch:
// When enabled, clear backend internal stage IO structs at the beginning of
// BackTop::comb() before any comb_* runs. This helps detect hidden dependence
// on previous-cycle IO values (latch-like behavior in combinational paths).
// Keep disabled for normal runs.
#ifndef CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN
#define CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN 0
#endif

#ifndef CONFIG_DIFF_DEBUG_MEMTRACE_DUMP_COUNT
#define CONFIG_DIFF_DEBUG_MEMTRACE_DUMP_COUNT 5000
#endif

#ifndef CONFIG_DIFF_DEBUG_MEMTRACE_BUFFER_SIZE
#define CONFIG_DIFF_DEBUG_MEMTRACE_BUFFER_SIZE 262144
#endif

#ifndef CONFIG_DEADLOCK_REPLAY_TRACE_DUMP_COUNT
#define CONFIG_DEADLOCK_REPLAY_TRACE_DUMP_COUNT 0
#endif

#ifndef CONFIG_DEADLOCK_REPLAY_TRACE_BUFFER_SIZE
#define CONFIG_DEADLOCK_REPLAY_TRACE_BUFFER_SIZE 131072
#endif

#ifndef CONFIG_DEBUG_SATP_WRITE_LOG_MAX
#define CONFIG_DEBUG_SATP_WRITE_LOG_MAX 32
#endif

#ifndef CONFIG_DEBUG_PTW_WALK_RESP_DETAIL_MAX
#define CONFIG_DEBUG_PTW_WALK_RESP_DETAIL_MAX 128
#endif

// Lightweight LSU STQ invariant checks.
// Set to 1 when debugging queue/pointer consistency issues.
#ifndef LSU_LIGHT_ASSERT
#define LSU_LIGHT_ASSERT 0
#endif
