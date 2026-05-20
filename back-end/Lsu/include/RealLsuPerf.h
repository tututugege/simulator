#pragma once

#include "RealLsu.h"
#include <cstdint>

namespace lsu_perf {

uint64_t load_miss_mask(const LsuState &state);
void sample_queue_occupancy(SimContext *ctx, const LsuState &state,
                            uint32_t ldq_count);

void finish_mem_inst(SimContext *ctx, LdqEntry &entry);
void finish_mem_inst(SimContext *ctx, StqEntry &entry);
void start_mem_inst(SimContext *ctx, LdqEntry &entry);
void start_mem_inst(SimContext *ctx, StqEntry &entry);
void start_mem_inst_if_needed(SimContext *ctx, LdqEntry &entry);
void start_mem_inst_if_needed(SimContext *ctx, const StqEntry &cur_entry,
                              StqEntry &nxt_entry);

void reset_wait_start(WaitDcacheLDQEntry &entry);
void mark_wait_start(SimContext *ctx, WaitDcacheLDQEntry &entry);
void retry_load_resp_timeouts(SimContext *ctx, const LsuState &cur,
                              LsuState &nxt);

void clear_replay(LoadReq &req);
void clear_replay(StoreReq &req);
void set_replay(LoadReq &req, bool is_replay);
void set_replay(StoreReq &req, bool is_replay);

bool is_load_replay(const LdqEntry &entry);
bool is_store_replay(const StqEntry &entry);
void mark_load_hit(LdqEntry &entry);
void mark_load_replay(LdqEntry &entry, ReplayType replay);
void mark_store_hit(StqEntry &entry);
void mark_store_replay(StqEntry &entry, ReplayType replay);

void count_stlf_waiting_on_unknown_store(SimContext *ctx,
                                         const LsuState &state,
                                         uint32_t older_store_count);
void count_stlf_unknown_store_block(SimContext *ctx);
void count_stlf_check(SimContext *ctx);

void count_mmio_load_issue(SimContext *ctx);
void count_mmio_store_issue(SimContext *ctx);
void count_mmio_head_block(SimContext *ctx);
void count_load_dcache_issue(SimContext *ctx, bool is_replay);
void count_stale_load_resp(SimContext *ctx);
void count_store_dcache_hit(SimContext *ctx);
void count_store_replay_resp(SimContext *ctx, ReplayType replay);

void sample_stq_issue_window(SimContext *ctx, const LsuState &state,
                             uint32_t commit_count);
void count_same_addr_block(SimContext *ctx, StoreState older_state,
                           uint32_t distance);
void count_suppress_store_done(SimContext *ctx);
void count_store_dcache_issue(SimContext *ctx, bool is_replay);
void count_store_issue_none(SimContext *ctx, uint32_t commit_count,
                            int32_t issued_stq);
void count_store_retire(SimContext *ctx, uint32_t committed_count,
                        uint32_t retired_stq);

} // namespace lsu_perf
