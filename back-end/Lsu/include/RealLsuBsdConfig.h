#pragma once

#include "config.h"
#include "util.h"
#include "wire_types.h"
#include <cstdint>
#include <cstdio>

#if !BSD_CONFIG
#define LSU_MEM_PERF_FIELDS                                                   \
  uint64_t perf_mem_start_cycle;                                              \
  wire<1> perf_mem_started

#define LSU_LDQ_CACHE_PERF_FIELDS wire<1> cache_miss

#define LSU_WAIT_DCACHE_LDQ_PERF_FIELDS uint64_t wait_start_cycle

#define LSU_NON_BSD_ASSERT(cond) Assert(cond)
#else
#define LSU_MEM_PERF_FIELDS
#define LSU_LDQ_CACHE_PERF_FIELDS
#define LSU_WAIT_DCACHE_LDQ_PERF_FIELDS
#define LSU_NON_BSD_ASSERT(cond)                                              \
  do {                                                                         \
    (void)sizeof(cond);                                                        \
  } while (0)
#endif

inline void lsu_report_load_req_gen_mismatch(
    int port, uint32_t req_id, uint32_t entry_idx, uint32_t resp_gen,
    uint32_t wait_gen, uint32_t wait_ldq, uint32_t head, uint32_t count) {
#if !BSD_CONFIG
  std::fprintf(stderr,
               "[LSU][REQ-GEN-MISMATCH] port=%d req_id=%u entry_idx=%u "
               "resp_gen=%u wait_gen=%u wait_ldq=%u head=%u count=%u\n",
               port, req_id, entry_idx, resp_gen, wait_gen, wait_ldq, head,
               count);
#else
  (void)port;
  (void)req_id;
  (void)entry_idx;
  (void)resp_gen;
  (void)wait_gen;
  (void)wait_ldq;
  (void)head;
  (void)count;
#endif
}
