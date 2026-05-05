#include "BPU/BPU.h"
#include "front_IO.h"
#include "front_module.h"
#include "host_profile.h"
#include "predecode.h"
#include "predecode_checker.h"
#include "train_IO.h"
#include <RISCV.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef CONFIG_FRONT_FOCUS_PC_BEGIN
#define CONFIG_FRONT_FOCUS_PC_BEGIN 0u
#endif

#ifndef CONFIG_FRONT_FOCUS_PC_END
#define CONFIG_FRONT_FOCUS_PC_END 0u
#endif

// ============================================================================
// 全局状态（寄存器锁存值）
// ============================================================================
static bool predecode_refetch = false;
static uint32_t predecode_refetch_address = 0;
static uint32_t front_sim_time = 0;

// FIFO 状态锁存
static bool fetch_addr_fifo_full_latch = false;
static bool fetch_addr_fifo_empty_latch = true;
static bool fifo_full_latch = false;
static bool fifo_empty_latch = true;
static bool ptab_full_latch = false;
static bool ptab_empty_latch = true;
static bool front2back_fifo_full_latch = false;
static bool front2back_fifo_empty_latch = true;
static SimContext *front_ctx = nullptr;

void front_set_context(SimContext *ctx) { front_ctx = ctx; }

static FrontRuntimeStats front_stats;
static bool falcon_window_active = false;
static uint64_t falcon_warmup_cycles = 0;
static bool falcon_recovery_pending = false;
enum class FalconRecoverySrc : uint8_t {
  NONE = 0,
  BACKEND_REFETCH = 1,
  FRONTEND_FLUSH = 2,
};
static FalconRecoverySrc falcon_recovery_src = FalconRecoverySrc::NONE;
static constexpr uint64_t kFalconColdMissWindowCycles = 100000;

struct FrontDeadlockSnapshot {
  bool valid = false;
  bool global_reset = false;
  bool global_refetch = false;
  bool icache_ready = false;
  bool icache_ready_2 = false;
  bool fetch_addr_read_enable_slot0 = false;
  bool fetch_addr_read_enable_slot1 = false;
  bool inst_fifo_read_enable = false;
  bool ptab_read_enable = false;
  bool front2back_read_enable = false;
  icache_out icache = {};
  instruction_FIFO_out fifo = {};
  PTAB_out ptab = {};
  front2back_FIFO_out front2back = {};
  front_top_out out = {};
};

static FrontDeadlockSnapshot front_deadlock_snapshot;

static double front_stats_pct(uint64_t num, uint64_t den) {
  if (den == 0) {
    return 0.0;
  }
  return (static_cast<double>(num) * 100.0) / static_cast<double>(den);
}

static double front_stats_ratio(uint64_t num, uint64_t den) {
  if (den == 0) {
    return 0.0;
  }
  return static_cast<double>(num) / static_cast<double>(den);
}

static bool falcon_measurement_window_active() {
  if (front_ctx == nullptr) {
    return true;
  }
  if (!front_ctx->is_ckpt) {
    return true;
  }
  return front_ctx->perf.perf_start;
}

static inline bool front_focus_pc(uint32_t pc) {
  const uint32_t begin = static_cast<uint32_t>(CONFIG_FRONT_FOCUS_PC_BEGIN);
  const uint32_t end = static_cast<uint32_t>(CONFIG_FRONT_FOCUS_PC_END);
  return end > begin && (pc - begin) < (end - begin);
}

static bool front_focus_any_fifo_pc(const instruction_FIFO_out &fifo_out) {
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    if (fifo_out.inst_valid[i] && front_focus_pc(fifo_out.pc[i])) {
      return true;
    }
  }
  return false;
}

static bool front_focus_any_out_pc(const front_top_out &out) {
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    if (out.inst_valid[i] && front_focus_pc(out.pc[i])) {
      return true;
    }
  }
  return false;
}

static void dump_front_focus_source(const instruction_FIFO_out &fifo_out,
                                    const PTAB_out &ptab_out,
                                    bool global_refetch,
                                    bool predecode_can_run,
                                    bool front2back_can_write,
                                    bool use_front2back_output_bypass) {
  if (!SIM_DEBUG_PRINT_ACTIVE || !front_focus_any_fifo_pc(fifo_out)) {
    return;
  }
  std::printf(
      "[FRONT][TRACE][SRC] cyc=%lld global_refetch=%d predecode_can_run=%d "
      "front2back_can_write=%d bypass=%d seq_next=0x%08x predict_next=0x%08x\n",
      (long long)sim_time, static_cast<int>(global_refetch),
      static_cast<int>(predecode_can_run), static_cast<int>(front2back_can_write),
      static_cast<int>(use_front2back_output_bypass), fifo_out.seq_next_pc,
      ptab_out.predict_next_fetch_address);
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    std::printf(
        "[FRONT][TRACE][SRC] lane=%d inst_valid=%d fifo_pc=0x%08x fifo_inst=0x%08x "
        "ptab_pc=0x%08x pdir=%d\n",
        i, static_cast<int>(fifo_out.inst_valid[i]), fifo_out.pc[i],
        fifo_out.instructions[i], ptab_out.predict_base_pc[i],
        static_cast<int>(ptab_out.predict_dir[i]));
  }
}

static void dump_front_focus_output(const front_top_out &out) {
  if (!SIM_DEBUG_PRINT_ACTIVE || !front_focus_any_out_pc(out)) {
    return;
  }
  std::printf(
      "[FRONT][TRACE][OUT] cyc=%lld valid=%d predict_next=0x%08x\n",
      (long long)sim_time, static_cast<int>(out.FIFO_valid),
      out.predict_next_fetch_address);
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    std::printf(
        "[FRONT][TRACE][OUT] lane=%d inst_valid=%d pc=0x%08x inst=0x%08x pf=%d\n",
        i, static_cast<int>(out.inst_valid[i]), out.pc[i], out.instructions[i],
        static_cast<int>(out.page_fault_inst[i]));
  }
}

void front_dump_debug_state() {
  std::printf(
      "[DEADLOCK][FRONT] sim_time=%u fetch_addr_fifo{full=%d empty=%d} "
      "inst_fifo{full=%d empty=%d} ptab{full=%d empty=%d} front2back{full=%d "
      "empty=%d}\n",
      front_sim_time, static_cast<int>(fetch_addr_fifo_full_latch),
      static_cast<int>(fetch_addr_fifo_empty_latch),
      static_cast<int>(fifo_full_latch), static_cast<int>(fifo_empty_latch),
      static_cast<int>(ptab_full_latch), static_cast<int>(ptab_empty_latch),
      static_cast<int>(front2back_fifo_full_latch),
      static_cast<int>(front2back_fifo_empty_latch));
  std::printf(
      "[DEADLOCK][FRONT][STATS] cycles=%llu active=%llu backend_demand=%llu "
      "backend_bubble=%llu icache_retry=%llu wait_walk=%llu local_walker=%llu\n",
      static_cast<unsigned long long>(front_stats.cycles),
      static_cast<unsigned long long>(front_stats.active_cycles),
      static_cast<unsigned long long>(front_stats.backend_demand_cycles),
      static_cast<unsigned long long>(front_stats.backend_bubble_cycles),
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_cycles),
      static_cast<unsigned long long>(
          front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles),
      static_cast<unsigned long long>(
          front_stats.bubble_icache_tlb_retry_local_walker_cycles));
  if (!front_deadlock_snapshot.valid) {
    std::printf("[DEADLOCK][FRONT] no deadlock snapshot captured yet\n");
    return;
  }

  const auto &s = front_deadlock_snapshot;
  std::printf(
      "[DEADLOCK][FRONT][SNAP] reset=%d refetch=%d ic_ready={%d,%d} "
      "read_en={fa0=%d fa1=%d fifo=%d ptab=%d f2b=%d} fifo_valid=%d "
      "predict_next=0x%08x\n",
      static_cast<int>(s.global_reset), static_cast<int>(s.global_refetch),
      static_cast<int>(s.icache_ready), static_cast<int>(s.icache_ready_2),
      static_cast<int>(s.fetch_addr_read_enable_slot0),
      static_cast<int>(s.fetch_addr_read_enable_slot1),
      static_cast<int>(s.inst_fifo_read_enable),
      static_cast<int>(s.ptab_read_enable),
      static_cast<int>(s.front2back_read_enable),
      static_cast<int>(s.out.FIFO_valid), s.out.predict_next_fetch_address);
  std::printf(
      "[DEADLOCK][FRONT][ICACHE] fetch_pc={0x%08x,0x%08x} req_fire=%d "
      "req_blocked=%d resp_fire=%d miss=%d miss_busy=%d outstanding=%d "
      "itlb{hit=%d miss=%d fault=%d retry=%d other_walk=%d req_blocked=%d "
      "wait_walk=%d local_walker=%d}\n",
      s.icache.fetch_pc, s.icache.fetch_pc_2,
      static_cast<int>(s.icache.perf_req_fire),
      static_cast<int>(s.icache.perf_req_blocked),
      static_cast<int>(s.icache.perf_resp_fire),
      static_cast<int>(s.icache.perf_miss_event),
      static_cast<int>(s.icache.perf_miss_busy),
      static_cast<int>(s.icache.perf_outstanding_req),
      static_cast<int>(s.icache.perf_itlb_hit),
      static_cast<int>(s.icache.perf_itlb_miss),
      static_cast<int>(s.icache.perf_itlb_fault),
      static_cast<int>(s.icache.perf_itlb_retry),
      static_cast<int>(s.icache.perf_itlb_retry_other_walk),
      static_cast<int>(s.icache.perf_itlb_retry_walk_req_blocked),
      static_cast<int>(s.icache.perf_itlb_retry_wait_walk_resp),
      static_cast<int>(s.icache.perf_itlb_retry_local_walker_busy));
  icache_dump_debug_state();
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    std::printf(
        "[DEADLOCK][FRONT][OUT %d] valid=%d pc=0x%08x inst=0x%08x pf=%d "
        "fifo_pc=0x%08x fifo_inst=0x%08x fifo_valid=%d\n",
        i, static_cast<int>(s.out.inst_valid[i]), s.out.pc[i],
        s.out.instructions[i], static_cast<int>(s.out.page_fault_inst[i]),
        s.fifo.pc[i], s.fifo.instructions[i],
        static_cast<int>(s.fifo.inst_valid[i]));
  }
}

static void falcon_print_summary() {
#if !FRONTEND_ENABLE_FALCON_STATS
  return;
#else
  if (front_stats.cycles == 0) {
    return;
  }

  const uint64_t bubble_total = front_stats.backend_bubble_cycles;
  const uint64_t demand_total = front_stats.backend_demand_cycles;
  const uint64_t active_total = front_stats.active_cycles;
  const uint64_t req_fire_total = front_stats.icache_req_fire_cycles;
  const uint64_t miss_total = front_stats.icache_miss_event_cycles;
  const uint64_t icache_latency_total = front_stats.bubble_icache_latency_cycles;
  const uint64_t itlb_retry_total = front_stats.bubble_icache_tlb_retry_cycles;

  std::printf("\n=== FALCON Front-end Report ===\n");
  std::printf(
      "config: true_icache=%d ideal_dual_req=%d two_ahead=%d\n",
#ifdef USE_TRUE_ICACHE
      1,
#else
      0,
#endif
      FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE,
#ifdef ENABLE_2AHEAD
      1
#else
      0
#endif
  );

  std::printf(
      "window: is_ckpt=%d perf_start=%d warmup_cycles=%llu measured_cycles=%llu aligned_with_tma=%d\n",
      (front_ctx != nullptr && front_ctx->is_ckpt) ? 1 : 0,
      (front_ctx != nullptr && front_ctx->perf.perf_start) ? 1 : 0,
      static_cast<unsigned long long>(falcon_warmup_cycles),
      static_cast<unsigned long long>(front_stats.cycles),
      falcon_measurement_window_active() ? 1 : 0);

  std::printf(
      "cycles: total=%llu active=%llu reset=%llu refetch(ext=%llu delayed=%llu global=%llu)\n",
      static_cast<unsigned long long>(front_stats.cycles),
      static_cast<unsigned long long>(front_stats.active_cycles),
      static_cast<unsigned long long>(front_stats.reset_cycles),
      static_cast<unsigned long long>(front_stats.ext_refetch_cycles),
      static_cast<unsigned long long>(front_stats.delayed_refetch_cycles),
      static_cast<unsigned long long>(front_stats.global_refetch_cycles));

  std::printf(
      "throughput: demand=%llu deliver=%llu bubble=%llu bubble_pct=%.2f%% groups=%llu insts=%llu inst/cycle=%.6f inst/active=%.6f inst/demand=%.6f\n",
      static_cast<unsigned long long>(front_stats.backend_demand_cycles),
      static_cast<unsigned long long>(front_stats.backend_deliver_cycles),
      static_cast<unsigned long long>(front_stats.backend_bubble_cycles),
      front_stats_pct(front_stats.backend_bubble_cycles, demand_total),
      static_cast<unsigned long long>(front_stats.delivered_groups),
      static_cast<unsigned long long>(front_stats.delivered_insts),
      front_stats_ratio(front_stats.delivered_insts, front_stats.cycles),
      front_stats_ratio(front_stats.delivered_insts, active_total),
      front_stats_ratio(front_stats.delivered_insts, demand_total));

  std::printf(
      "bubble_root_share(on demand bubbles): reset=%llu(%.2f%%) refetch=%llu(%.2f%%) icache_miss=%llu(%.2f%%) icache_latency=%llu(%.2f%%) bpu_stall=%llu(%.2f%%) fetch_addr_empty=%llu(%.2f%%) ptab_empty=%llu(%.2f%%) dummy_ptab=%llu(%.2f%%) inst_fifo_other=%llu(%.2f%%) other=%llu(%.2f%%)\n",
      static_cast<unsigned long long>(front_stats.bubble_reset_cycles),
      front_stats_pct(front_stats.bubble_reset_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_refetch_cycles),
      front_stats_pct(front_stats.bubble_refetch_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_miss_cycles),
      front_stats_pct(front_stats.bubble_icache_miss_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_latency_cycles),
      front_stats_pct(front_stats.bubble_icache_latency_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_bpu_stall_cycles),
      front_stats_pct(front_stats.bubble_bpu_stall_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_fetch_addr_empty_cycles),
      front_stats_pct(front_stats.bubble_fetch_addr_empty_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_ptab_empty_cycles),
      front_stats_pct(front_stats.bubble_ptab_empty_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_dummy_ptab_cycles),
      front_stats_pct(front_stats.bubble_dummy_ptab_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_inst_fifo_empty_other_cycles),
      front_stats_pct(front_stats.bubble_inst_fifo_empty_other_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble_other_cycles),
      front_stats_pct(front_stats.bubble_other_cycles, bubble_total));

  const uint64_t bubble2_sum =
      front_stats.bubble2_reset_cycles +
      front_stats.bubble2_recovery_backend_refetch_cycles +
      front_stats.bubble2_recovery_frontend_flush_cycles +
      front_stats.bubble2_fetch_stall_cycles +
      front_stats.bubble2_glue_or_fifo_cycles +
      front_stats.bubble2_bpu_side_cycles + front_stats.bubble2_other_cycles;
  const long long bubble2_delta =
      static_cast<long long>(bubble_total) - static_cast<long long>(bubble2_sum);

  std::printf(
      "bubble_root2_conservation(on demand bubbles): bubble=%llu sum=%llu delta=%lld\n",
      static_cast<unsigned long long>(bubble_total),
      static_cast<unsigned long long>(bubble2_sum),
      static_cast<long long>(bubble2_delta));

  std::printf(
      "bubble_root2_share(on demand bubbles): reset=%llu(%.2f%%) recovery_backend_refetch=%llu(%.2f%%) recovery_frontend_flush=%llu(%.2f%%) fetch_stall=%llu(%.2f%%) glue_or_fifo=%llu(%.2f%%) bpu_side=%llu(%.2f%%) other=%llu(%.2f%%)\n",
      static_cast<unsigned long long>(front_stats.bubble2_reset_cycles),
      front_stats_pct(front_stats.bubble2_reset_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_recovery_backend_refetch_cycles),
      front_stats_pct(front_stats.bubble2_recovery_backend_refetch_cycles,
                      bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_recovery_frontend_flush_cycles),
      front_stats_pct(front_stats.bubble2_recovery_frontend_flush_cycles,
                      bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_fetch_stall_cycles),
      front_stats_pct(front_stats.bubble2_fetch_stall_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_glue_or_fifo_cycles),
      front_stats_pct(front_stats.bubble2_glue_or_fifo_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_bpu_side_cycles),
      front_stats_pct(front_stats.bubble2_bpu_side_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble2_other_cycles),
      front_stats_pct(front_stats.bubble2_other_cycles, bubble_total));

  const uint64_t bubble3_sum =
      front_stats.bubble3_reset_cycles +
      front_stats.bubble3_recovery_backend_refetch_cycles +
      front_stats.bubble3_recovery_frontend_flush_cycles +
      front_stats.bubble3_fetch_stall_cycles +
      front_stats.bubble3_glue_or_fifo_cycles +
      front_stats.bubble3_bpu_side_cycles + front_stats.bubble3_other_cycles;
  const long long bubble3_delta =
      static_cast<long long>(bubble_total) - static_cast<long long>(bubble3_sum);

  std::printf(
      "bubble_root3_conservation(on demand bubbles): bubble=%llu sum=%llu delta=%lld\n",
      static_cast<unsigned long long>(bubble_total),
      static_cast<unsigned long long>(bubble3_sum),
      static_cast<long long>(bubble3_delta));

  std::printf(
      "bubble_root3_share(on demand bubbles): reset=%llu(%.2f%%) recovery_backend_refetch=%llu(%.2f%%) recovery_frontend_flush=%llu(%.2f%%) fetch_stall=%llu(%.2f%%) glue_or_fifo=%llu(%.2f%%) bpu_side=%llu(%.2f%%) other=%llu(%.2f%%)\n",
      static_cast<unsigned long long>(front_stats.bubble3_reset_cycles),
      front_stats_pct(front_stats.bubble3_reset_cycles, bubble_total),
      static_cast<unsigned long long>(
          front_stats.bubble3_recovery_backend_refetch_cycles),
      front_stats_pct(front_stats.bubble3_recovery_backend_refetch_cycles,
                      bubble_total),
      static_cast<unsigned long long>(
          front_stats.bubble3_recovery_frontend_flush_cycles),
      front_stats_pct(front_stats.bubble3_recovery_frontend_flush_cycles,
                      bubble_total),
      static_cast<unsigned long long>(front_stats.bubble3_fetch_stall_cycles),
      front_stats_pct(front_stats.bubble3_fetch_stall_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble3_glue_or_fifo_cycles),
      front_stats_pct(front_stats.bubble3_glue_or_fifo_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble3_bpu_side_cycles),
      front_stats_pct(front_stats.bubble3_bpu_side_cycles, bubble_total),
      static_cast<unsigned long long>(front_stats.bubble3_other_cycles),
      front_stats_pct(front_stats.bubble3_other_cycles, bubble_total));

  std::printf(
      "bpu_stage: can_run=%llu stall=%llu stall_pct(active)=%.2f%% stall_fetch_addr_full=%llu stall_ptab_full=%llu issue=%llu issue_pct(active)=%.2f%%\n",
      static_cast<unsigned long long>(front_stats.bpu_can_run_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_cycles),
      front_stats_pct(front_stats.bpu_stall_cycles, active_total),
      static_cast<unsigned long long>(front_stats.bpu_stall_fetch_addr_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_ptab_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_issue_cycles),
      front_stats_pct(front_stats.bpu_issue_cycles, active_total));

  std::printf(
      "icache_stage: req_seen=%llu req_fire=%llu req_blocked=%llu resp=%llu outstanding=%llu miss_event=%llu miss_rate(req_fire)=%.4f%% miss_busy=%llu miss_busy_pct(active)=%.2f%%\n",
      static_cast<unsigned long long>(front_stats.icache_req_slot0_cycles),
      static_cast<unsigned long long>(front_stats.icache_req_fire_cycles),
      static_cast<unsigned long long>(front_stats.icache_req_blocked_cycles),
      static_cast<unsigned long long>(front_stats.icache_resp_fire_cycles),
      static_cast<unsigned long long>(front_stats.icache_outstanding_req_cycles),
      static_cast<unsigned long long>(front_stats.icache_miss_event_cycles),
      front_stats_pct(miss_total, req_fire_total),
      static_cast<unsigned long long>(front_stats.icache_miss_busy_cycles),
      front_stats_pct(front_stats.icache_miss_busy_cycles, active_total));

  std::printf(
      "icache_cold_like: window_cycles=%llu miss_event_in_window=%llu miss_event_pct(req_fire)=%.4f%%\n",
      static_cast<unsigned long long>(kFalconColdMissWindowCycles),
      static_cast<unsigned long long>(front_stats.icache_miss_event_cold_window_cycles),
      front_stats_pct(front_stats.icache_miss_event_cold_window_cycles,
                      req_fire_total));

  std::printf(
      "icache_latency_detail(on icache_latency bubbles): tlb_retry=%llu(%.2f%%) tlb_fault=%llu(%.2f%%) cache_backpressure=%llu(%.2f%%) other=%llu(%.2f%%)\n",
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_retry_cycles, icache_latency_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_fault_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_fault_cycles, icache_latency_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_cache_backpressure_cycles),
      front_stats_pct(front_stats.bubble_icache_cache_backpressure_cycles, icache_latency_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_latency_other_cycles),
      front_stats_pct(front_stats.bubble_icache_latency_other_cycles, icache_latency_total));

  const char *icache_latency_top1_name = "tlb_retry";
  uint64_t icache_latency_top1_cycles = front_stats.bubble_icache_tlb_retry_cycles;
  if (front_stats.bubble_icache_tlb_fault_cycles > icache_latency_top1_cycles) {
    icache_latency_top1_name = "tlb_fault";
    icache_latency_top1_cycles = front_stats.bubble_icache_tlb_fault_cycles;
  }
  if (front_stats.bubble_icache_cache_backpressure_cycles >
      icache_latency_top1_cycles) {
    icache_latency_top1_name = "cache_backpressure";
    icache_latency_top1_cycles = front_stats.bubble_icache_cache_backpressure_cycles;
  }
  if (front_stats.bubble_icache_latency_other_cycles > icache_latency_top1_cycles) {
    icache_latency_top1_name = "other";
    icache_latency_top1_cycles = front_stats.bubble_icache_latency_other_cycles;
  }
  std::printf(
      "icache_latency_top1: reason=%s cycles=%llu share_in_icache_latency=%.2f%%\n",
      icache_latency_top1_name,
      static_cast<unsigned long long>(icache_latency_top1_cycles),
      front_stats_pct(icache_latency_top1_cycles, icache_latency_total));

  std::printf(
      "itlb_retry_detail(on tlb_retry bubbles): other_walk_active=%llu(%.2f%%) walk_req_blocked=%llu(%.2f%%) wait_walk_resp=%llu(%.2f%%) local_walker_busy=%llu(%.2f%%)\n",
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_other_walk_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_retry_other_walk_cycles, itlb_retry_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_walk_req_blocked_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_retry_walk_req_blocked_cycles, itlb_retry_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles, itlb_retry_total),
      static_cast<unsigned long long>(front_stats.bubble_icache_tlb_retry_local_walker_cycles),
      front_stats_pct(front_stats.bubble_icache_tlb_retry_local_walker_cycles, itlb_retry_total));

  const char *itlb_retry_top1_name = "other_walk_active";
  uint64_t itlb_retry_top1_cycles =
      front_stats.bubble_icache_tlb_retry_other_walk_cycles;
  if (front_stats.bubble_icache_tlb_retry_walk_req_blocked_cycles >
      itlb_retry_top1_cycles) {
    itlb_retry_top1_name = "walk_req_blocked";
    itlb_retry_top1_cycles =
        front_stats.bubble_icache_tlb_retry_walk_req_blocked_cycles;
  }
  if (front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles >
      itlb_retry_top1_cycles) {
    itlb_retry_top1_name = "wait_walk_resp";
    itlb_retry_top1_cycles =
        front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles;
  }
  if (front_stats.bubble_icache_tlb_retry_local_walker_cycles >
      itlb_retry_top1_cycles) {
    itlb_retry_top1_name = "local_walker_busy";
    itlb_retry_top1_cycles = front_stats.bubble_icache_tlb_retry_local_walker_cycles;
  }
  std::printf(
      "itlb_retry_top1: reason=%s cycles=%llu share_in_itlb_retry=%.2f%%\n",
      itlb_retry_top1_name,
      static_cast<unsigned long long>(itlb_retry_top1_cycles),
      front_stats_pct(itlb_retry_top1_cycles, itlb_retry_total));

  if (front_ctx != nullptr) {
    const auto &perf = front_ctx->perf;
    std::printf(
        "shared_ptw_itlb_stats: req=%llu grant=%llu resp=%llu blocked=%llu wait=%llu grant_rate=%.2f%% blocked_per_req=%.2f%%\n",
        static_cast<unsigned long long>(perf.ptw_itlb_req),
        static_cast<unsigned long long>(perf.ptw_itlb_grant),
        static_cast<unsigned long long>(perf.ptw_itlb_resp),
        static_cast<unsigned long long>(perf.ptw_itlb_blocked),
        static_cast<unsigned long long>(perf.ptw_itlb_wait_cycle),
        front_stats_pct(perf.ptw_itlb_grant, perf.ptw_itlb_req),
        front_stats_pct(perf.ptw_itlb_blocked, perf.ptw_itlb_req));
  }

  std::printf(
      "predecode_stage: run=%llu block_fifo_empty=%llu block_ptab_empty=%llu block_f2b_full=%llu block_dummy_ptab=%llu checker_run=%llu checker_flush=%llu\n",
      static_cast<unsigned long long>(front_stats.predecode_run_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_fifo_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_ptab_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_front2back_full_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_dummy_ptab_cycles),
      static_cast<unsigned long long>(front_stats.checker_run_cycles),
      static_cast<unsigned long long>(front_stats.checker_flush_cycles));

  std::printf(
      "queue_flow: fetch_addr_rd0=%llu wr_normal=%llu inst_fifo_wr0=%llu ptab_wr=%llu f2b_write=%llu f2b_valid_out=%llu bypass_f2i=%llu bypass_i2p=%llu bypass_f2o=%llu/%llu\n",
      static_cast<unsigned long long>(front_stats.fetch_addr_read_slot0_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_normal_cycles),
      static_cast<unsigned long long>(front_stats.inst_fifo_write_slot0_cycles),
      static_cast<unsigned long long>(front_stats.ptab_write_cycles),
      static_cast<unsigned long long>(front_stats.front2back_write_cycles),
      static_cast<unsigned long long>(front_stats.front2back_valid_out_cycles),
      static_cast<unsigned long long>(front_stats.bypass_fetch_to_icache_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_icache_to_predecode_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_hit_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_opportunity_cycles));

  std::printf("=== End FALCON Report ===\n");
#endif
}

static void front_stats_print_summary() {
#if !FRONTEND_ENABLE_RUNTIME_STATS_SUMMARY
  return;
#else
  if (front_stats.cycles == 0) {
    return;
  }
  std::printf(
      "\n[FRONT-STATS] cycles=%llu reset=%llu ext_refetch=%llu delayed_refetch=%llu global_refetch=%llu\n",
      static_cast<unsigned long long>(front_stats.cycles),
      static_cast<unsigned long long>(front_stats.reset_cycles),
      static_cast<unsigned long long>(front_stats.ext_refetch_cycles),
      static_cast<unsigned long long>(front_stats.delayed_refetch_cycles),
      static_cast<unsigned long long>(front_stats.global_refetch_cycles));
  std::printf(
      "[FRONT-STATS] bpu can_run=%llu stall=%llu stall_fetch_addr_full=%llu stall_ptab_full=%llu issue=%llu\n",
      static_cast<unsigned long long>(front_stats.bpu_can_run_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_fetch_addr_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_ptab_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_issue_cycles));
  std::printf(
      "[FRONT-STATS] fetch_addr rd0=%llu rd1=%llu wr_normal=%llu wr_2ahead=%llu skip_mini_flush_correct=%llu\n",
      static_cast<unsigned long long>(front_stats.fetch_addr_read_slot0_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_read_slot1_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_normal_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_twoahead_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_skip_by_mini_flush_correct_cycles));
  std::printf(
      "[FRONT-STATS] icache req0=%llu req1=%llu resp0=%llu resp1=%llu inst_fifo_wr0=%llu inst_fifo_wr1=%llu ptab_wr=%llu\n",
      static_cast<unsigned long long>(front_stats.icache_req_slot0_cycles),
      static_cast<unsigned long long>(front_stats.icache_req_slot1_cycles),
      static_cast<unsigned long long>(front_stats.icache_resp_slot0_cycles),
      static_cast<unsigned long long>(front_stats.icache_resp_slot1_cycles),
      static_cast<unsigned long long>(front_stats.inst_fifo_write_slot0_cycles),
      static_cast<unsigned long long>(front_stats.inst_fifo_write_slot1_cycles),
      static_cast<unsigned long long>(front_stats.ptab_write_cycles));
  std::printf(
      "[FRONT-STATS] predecode run=%llu block_fifo_empty=%llu block_ptab_empty=%llu block_f2b_full=%llu block_ptab_dummy=%llu checker_run=%llu checker_flush=%llu mini_req=%llu mini_correct=%llu\n",
      static_cast<unsigned long long>(front_stats.predecode_run_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_fifo_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_ptab_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_front2back_full_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_dummy_ptab_cycles),
      static_cast<unsigned long long>(front_stats.checker_run_cycles),
      static_cast<unsigned long long>(front_stats.checker_flush_cycles),
      static_cast<unsigned long long>(front_stats.mini_flush_req_cycles),
      static_cast<unsigned long long>(front_stats.mini_flush_correct_cycles));
  std::printf(
      "[FRONT-STATS] front2back write=%llu read_req=%llu valid_out=%llu\n",
      static_cast<unsigned long long>(front_stats.front2back_write_cycles),
      static_cast<unsigned long long>(front_stats.front2back_read_req_cycles),
      static_cast<unsigned long long>(front_stats.front2back_valid_out_cycles));
  std::printf(
      "[FRONT-STATS] bypass_opp fetch_to_icache=%llu icache_to_predecode=%llu f2b_to_output=%llu f2b_hit=%llu\n",
      static_cast<unsigned long long>(front_stats.bypass_fetch_to_icache_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_icache_to_predecode_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_hit_cycles));
#endif
}

struct FrontStatsAtExit {
  ~FrontStatsAtExit() {
    falcon_print_summary();
    front_stats_print_summary();
  }
};

static FrontStatsAtExit front_stats_at_exit;

static BPU_TOP bpu_instance;

// 定义全局指针，供TAGE访问BPU的GHR/FH
BPU_TOP *g_bpu_top = &bpu_instance;
// ============================================================================
// 辅助函数
// ============================================================================

static void front_bpu_input_comb(const FrontBpuInputCombIn &input,
                                 FrontBpuInputCombOut &output);

static void front_global_control_comb(const FrontGlobalControlCombIn &input,
                                      FrontGlobalControlCombOut &output) {
    std::memset(&output, 0, sizeof(output));
    output.global_reset = input.reset;
    output.global_refetch =
        input.backend_refetch || input.predecode_refetch_snapshot;
    output.refetch_address = input.backend_refetch
                                 ? input.backend_refetch_address
                                 : input.predecode_refetch_address_snapshot;
}

static void front_read_enable_comb(const FrontReadEnableCombIn &input,
                                   FrontReadEnableCombOut &output) {
    const wire1_t global_reset = input.global_reset;
    const wire1_t global_refetch = input.global_refetch;
    const wire1_t icache_ready = input.icache_ready;
    std::memset(&output, 0, sizeof(output));
    output.fetch_addr_fifo_read_enable_slot0 =
        icache_ready && !input.fetch_addr_fifo_empty_latch_snapshot && !global_reset &&
        !global_refetch;
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
    const wire1_t icache_ready_2 = input.icache_ready_2;
    output.fetch_addr_fifo_read_enable_slot1_candidate =
        output.fetch_addr_fifo_read_enable_slot0 && icache_ready_2;
#endif
    output.predecode_can_run_old =
        !input.fifo_empty_latch_snapshot && !input.ptab_empty_latch_snapshot &&
        !input.front2back_fifo_full_latch_snapshot && !global_reset &&
        !global_refetch;
    output.inst_fifo_read_enable = output.predecode_can_run_old;
    output.ptab_read_enable = output.predecode_can_run_old;
    output.front2back_read_enable = input.backend_fifo_read_enable;
}

static void front_read_stage_input_comb(const FrontReadStageInputCombIn &input,
                                        FrontReadStageInputCombOut &output) {
    const wire1_t global_reset = input.global_reset;
    const wire1_t global_refetch = input.global_refetch;
    const wire1_t fetch_addr_fifo_read_enable_slot0 =
        input.fetch_addr_fifo_read_enable_slot0;
    const wire1_t inst_fifo_read_enable = input.inst_fifo_read_enable;
    const wire1_t ptab_read_enable = input.ptab_read_enable;
    const wire1_t front2back_read_enable = input.front2back_read_enable;
    std::memset(&output, 0, sizeof(output));
    output.fetch_addr_fifo_reset = global_reset;
    output.fetch_addr_fifo_refetch = global_refetch;
    output.fetch_addr_fifo_read_enable = fetch_addr_fifo_read_enable_slot0;

    output.fifo_reset = global_reset;
    output.fifo_refetch = global_refetch;
    output.fifo_read_enable = inst_fifo_read_enable;

    output.ptab_reset = global_reset;
    output.ptab_refetch = global_refetch;
    output.ptab_read_enable = ptab_read_enable;

    output.front2back_fifo_reset = global_reset;
    output.front2back_fifo_refetch = input.backend_refetch;
    output.front2back_fifo_read_enable = front2back_read_enable;
}

static void front_bpu_control_comb(const FrontBpuControlCombIn &input,
                                   FrontBpuControlCombOut &output) {
    const wire1_t global_reset = input.global_reset;
    const wire1_t global_refetch = input.global_refetch;
    const fetch_addr_t refetch_address = input.refetch_address;
    std::memset(&output, 0, sizeof(output));
    output.bpu_stall =
        input.fetch_addr_fifo_full_latch_snapshot || input.ptab_full_latch_snapshot;
    output.bpu_can_run = !output.bpu_stall || global_reset || global_refetch;
    output.bpu_icache_ready = !input.fetch_addr_fifo_full_latch_snapshot;
    FrontBpuInputCombOut bpu_input_out{};
    front_bpu_input_comb(
        FrontBpuInputCombIn{input.bpu_in_seed, global_refetch, refetch_address,
                            output.bpu_icache_ready},
        bpu_input_out);
    output.bpu_in = bpu_input_out.bpu_in;
    if (!output.bpu_can_run) {
        output.bpu_in.icache_read_ready = false;
    }

    output.bpu_input.refetch = output.bpu_in.refetch;
    output.bpu_input.refetch_address = output.bpu_in.refetch_address;
    output.bpu_input.icache_read_ready = output.bpu_in.icache_read_ready;
    for (int i = 0; i < COMMIT_WIDTH; i++) {
        output.bpu_input.in_update_base_pc[i] = output.bpu_in.predict_base_pc[i];
        output.bpu_input.in_upd_valid[i] = output.bpu_in.back2front_valid[i];
        output.bpu_input.in_actual_dir[i] = output.bpu_in.actual_dir[i];
        output.bpu_input.in_actual_br_type[i] = output.bpu_in.actual_br_type[i];
        output.bpu_input.in_actual_targets[i] = output.bpu_in.actual_target[i];
        output.bpu_input.in_pred_dir[i] = output.bpu_in.predict_dir[i];
        output.bpu_input.in_alt_pred[i] = output.bpu_in.alt_pred[i];
        output.bpu_input.in_pcpn[i] = output.bpu_in.pcpn[i];
        output.bpu_input.in_altpcpn[i] = output.bpu_in.altpcpn[i];
        for (int j = 0; j < 4; j++) {
            output.bpu_input.in_tage_tags[i][j] = output.bpu_in.tage_tag[i][j];
            output.bpu_input.in_tage_idxs[i][j] = output.bpu_in.tage_idx[i][j];
        }
        output.bpu_input.in_sc_used[i] = output.bpu_in.sc_used[i];
        output.bpu_input.in_sc_pred[i] = output.bpu_in.sc_pred[i];
        output.bpu_input.in_sc_sum[i] = output.bpu_in.sc_sum[i];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            output.bpu_input.in_sc_idx[i][t] = output.bpu_in.sc_idx[i][t];
        }
        output.bpu_input.in_loop_used[i] = output.bpu_in.loop_used[i];
        output.bpu_input.in_loop_hit[i] = output.bpu_in.loop_hit[i];
        output.bpu_input.in_loop_pred[i] = output.bpu_in.loop_pred[i];
        output.bpu_input.in_loop_idx[i] = output.bpu_in.loop_idx[i];
        output.bpu_input.in_loop_tag[i] = output.bpu_in.loop_tag[i];
    }
}

static void front_ptab_write_comb(const FrontPtabWriteCombIn &input,
                                  FrontPtabWriteCombOut &output) {
    const BPU_TOP::OutputPayload &bpu_output = input.bpu_output;
    const wire1_t global_reset = input.global_reset;
    const wire1_t global_refetch = input.global_refetch;
    const wire1_t ptab_can_write = input.ptab_can_write;
    std::memset(&output, 0, sizeof(output));
    output.ptab_in.reset = global_reset;
    output.ptab_in.refetch = global_refetch;
    output.ptab_in.read_enable = false;
    output.ptab_in.write_enable = ptab_can_write;
    if (!ptab_can_write) {
        return;
    }
    for (int i = 0; i < FETCH_WIDTH; i++) {
        output.ptab_in.predict_dir[i] = bpu_output.out_pred_dir[i];
        output.ptab_in.predict_base_pc[i] = bpu_output.out_pred_base_pc + (i * 4);
        output.ptab_in.alt_pred[i] = bpu_output.out_alt_pred[i];
        output.ptab_in.altpcpn[i] = bpu_output.out_altpcpn[i];
        output.ptab_in.pcpn[i] = bpu_output.out_pcpn[i];
        for (int j = 0; j < 4; j++) {
            output.ptab_in.tage_idx[i][j] = bpu_output.out_tage_idxs[i][j];
            output.ptab_in.tage_tag[i][j] = bpu_output.out_tage_tags[i][j];
        }
        output.ptab_in.sc_used[i] = bpu_output.out_sc_used[i];
        output.ptab_in.sc_pred[i] = bpu_output.out_sc_pred[i];
        output.ptab_in.sc_sum[i] = bpu_output.out_sc_sum[i];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            output.ptab_in.sc_idx[i][t] = bpu_output.out_sc_idx[i][t];
        }
        output.ptab_in.loop_used[i] = bpu_output.out_loop_used[i];
        output.ptab_in.loop_hit[i] = bpu_output.out_loop_hit[i];
        output.ptab_in.loop_pred[i] = bpu_output.out_loop_pred[i];
        output.ptab_in.loop_idx[i] = bpu_output.out_loop_idx[i];
        output.ptab_in.loop_tag[i] = bpu_output.out_loop_tag[i];
    }
    output.ptab_in.predict_next_fetch_address = bpu_output.predict_next_fetch_address;
    output.ptab_in.need_mini_flush = bpu_output.mini_flush_req;
}

static void front_checker_input_comb(const FrontCheckerInputCombIn &input,
                                     FrontCheckerInputCombOut &output) {
    const instruction_FIFO_out &fifo_out = input.fifo_out;
    const PTAB_out &ptab_out = input.ptab_out;
    std::memset(&output, 0, sizeof(output));
    for (int i = 0; i < FETCH_WIDTH; i++) {
        output.checker_in.predict_dir[i] = ptab_out.predict_dir[i];
        output.checker_in.predecode_type[i] = fifo_out.predecode_type[i];
        output.checker_in.predecode_target_address[i] =
            fifo_out.predecode_target_address[i];
    }
    output.checker_in.seq_next_pc = fifo_out.seq_next_pc;
    output.checker_in.predict_next_fetch_address = ptab_out.predict_next_fetch_address;
}

static void front_front2back_write_comb(const FrontFront2backWriteCombIn &input,
                                        FrontFront2backWriteCombOut &output) {
    const instruction_FIFO_out &fifo_out = input.fifo_out;
    const PTAB_out &ptab_out = input.ptab_out;
    const predecode_checker_out &checker_out = input.checker_out;
    const wire1_t use_front2back_output_bypass =
        input.use_front2back_output_bypass;
    std::memset(&output, 0, sizeof(output));
    constexpr uint32_t kPcpnMask = (1u << pcpn_t_BITS) - 1u;
    constexpr uint32_t kTageIdxMask = (1u << tage_idx_t_BITS) - 1u;
    constexpr uint32_t kTageTagMask = (1u << tage_tag_t_BITS) - 1u;
    for (int i = 0; i < FETCH_WIDTH; i++) {
        output.front2back_fifo_in.fetch_group[i] = fifo_out.instructions[i];
        output.front2back_fifo_in.page_fault_inst[i] = fifo_out.page_fault_inst[i];
        output.front2back_fifo_in.inst_valid[i] = fifo_out.inst_valid[i];
        output.front2back_fifo_in.predict_dir_corrected[i] =
            checker_out.predict_dir_corrected[i];
        output.front2back_fifo_in.predict_base_pc[i] = ptab_out.predict_base_pc[i];
        output.front2back_fifo_in.alt_pred[i] = ptab_out.alt_pred[i];
        output.front2back_fifo_in.altpcpn[i] =
            static_cast<uint8_t>(ptab_out.altpcpn[i] & kPcpnMask);
        output.front2back_fifo_in.pcpn[i] =
            static_cast<uint8_t>(ptab_out.pcpn[i] & kPcpnMask);
        for (int j = 0; j < 4; j++) {
            output.front2back_fifo_in.tage_idx[i][j] =
                ptab_out.tage_idx[i][j] & kTageIdxMask;
            output.front2back_fifo_in.tage_tag[i][j] =
                ptab_out.tage_tag[i][j] & kTageTagMask;
        }
        output.front2back_fifo_in.sc_used[i] = ptab_out.sc_used[i];
        output.front2back_fifo_in.sc_pred[i] = ptab_out.sc_pred[i];
        output.front2back_fifo_in.sc_sum[i] = ptab_out.sc_sum[i];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            output.front2back_fifo_in.sc_idx[i][t] = ptab_out.sc_idx[i][t];
        }
        output.front2back_fifo_in.loop_used[i] = ptab_out.loop_used[i];
        output.front2back_fifo_in.loop_hit[i] = ptab_out.loop_hit[i];
        output.front2back_fifo_in.loop_pred[i] = ptab_out.loop_pred[i];
        output.front2back_fifo_in.loop_idx[i] = ptab_out.loop_idx[i];
        output.front2back_fifo_in.loop_tag[i] = ptab_out.loop_tag[i];
        if (use_front2back_output_bypass) {
            output.bypass_front2back_fifo_out.fetch_group[i] = fifo_out.instructions[i];
            output.bypass_front2back_fifo_out.page_fault_inst[i] =
                fifo_out.page_fault_inst[i];
            output.bypass_front2back_fifo_out.inst_valid[i] = fifo_out.inst_valid[i];
            output.bypass_front2back_fifo_out.predict_dir_corrected[i] =
                checker_out.predict_dir_corrected[i];
            output.bypass_front2back_fifo_out.predict_base_pc[i] =
                ptab_out.predict_base_pc[i];
            output.bypass_front2back_fifo_out.alt_pred[i] = ptab_out.alt_pred[i];
            output.bypass_front2back_fifo_out.altpcpn[i] = ptab_out.altpcpn[i];
            output.bypass_front2back_fifo_out.pcpn[i] = ptab_out.pcpn[i];
            for (int j = 0; j < 4; j++) {
                output.bypass_front2back_fifo_out.tage_idx[i][j] = ptab_out.tage_idx[i][j];
                output.bypass_front2back_fifo_out.tage_tag[i][j] = ptab_out.tage_tag[i][j];
            }
            output.bypass_front2back_fifo_out.sc_used[i] = ptab_out.sc_used[i];
            output.bypass_front2back_fifo_out.sc_pred[i] = ptab_out.sc_pred[i];
            output.bypass_front2back_fifo_out.sc_sum[i] = ptab_out.sc_sum[i];
            for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
                output.bypass_front2back_fifo_out.sc_idx[i][t] = ptab_out.sc_idx[i][t];
            }
            output.bypass_front2back_fifo_out.loop_used[i] = ptab_out.loop_used[i];
            output.bypass_front2back_fifo_out.loop_hit[i] = ptab_out.loop_hit[i];
            output.bypass_front2back_fifo_out.loop_pred[i] = ptab_out.loop_pred[i];
            output.bypass_front2back_fifo_out.loop_idx[i] = ptab_out.loop_idx[i];
            output.bypass_front2back_fifo_out.loop_tag[i] = ptab_out.loop_tag[i];
        }
    }
    output.front2back_fifo_in.predict_next_fetch_address_corrected =
        checker_out.predict_next_fetch_address_corrected;
    if (use_front2back_output_bypass) {
        output.bypass_front2back_fifo_out.front2back_FIFO_valid = true;
        output.bypass_front2back_fifo_out.predict_next_fetch_address_corrected =
            checker_out.predict_next_fetch_address_corrected;
    }
}

static void front_output_comb(const FrontOutputCombIn &input,
                              FrontOutputCombOut &output) {
    const front2back_FIFO_out &saved_front2back_fifo_out =
        input.saved_front2back_fifo_out;
    const front2back_FIFO_out &bypass_front2back_fifo_out =
        input.bypass_front2back_fifo_out;
    const wire1_t use_front2back_output_bypass =
        input.use_front2back_output_bypass;
    std::memset(&output, 0, sizeof(output));
    const struct front2back_FIFO_out *out_src = &saved_front2back_fifo_out;
    if (!saved_front2back_fifo_out.front2back_FIFO_valid &&
        use_front2back_output_bypass) {
        out_src = &bypass_front2back_fifo_out;
    }

    output.out.FIFO_valid = out_src->front2back_FIFO_valid;
    for (int i = 0; i < FETCH_WIDTH; i++) {
        output.out.instructions[i] = out_src->fetch_group[i];
        output.out.page_fault_inst[i] = out_src->page_fault_inst[i];
        output.out.predict_dir[i] = out_src->predict_dir_corrected[i];
        output.out.pc[i] = out_src->predict_base_pc[i];
        output.out.alt_pred[i] = out_src->alt_pred[i];
        output.out.altpcpn[i] = out_src->altpcpn[i];
        output.out.pcpn[i] = out_src->pcpn[i];
        for (int j = 0; j < 4; j++) {
            output.out.tage_idx[i][j] = out_src->tage_idx[i][j];
            output.out.tage_tag[i][j] = out_src->tage_tag[i][j];
        }
        output.out.sc_used[i] = out_src->sc_used[i];
        output.out.sc_pred[i] = out_src->sc_pred[i];
        output.out.sc_sum[i] = out_src->sc_sum[i];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            output.out.sc_idx[i][t] = out_src->sc_idx[i][t];
        }
        output.out.loop_used[i] = out_src->loop_used[i];
        output.out.loop_hit[i] = out_src->loop_hit[i];
        output.out.loop_pred[i] = out_src->loop_pred[i];
        output.out.loop_idx[i] = out_src->loop_idx[i];
        output.out.loop_tag[i] = out_src->loop_tag[i];
        output.out.inst_valid[i] = out_src->inst_valid[i];
    }
    output.out.predict_next_fetch_address = out_src->predict_next_fetch_address_corrected;
}

// 准备 BPU 输入
static void front_bpu_input_comb(const FrontBpuInputCombIn &input,
                                 FrontBpuInputCombOut &output) {
    std::memset(&output, 0, sizeof(output));
    output.bpu_in = input.bpu_seed;
    output.bpu_in.refetch = input.do_refetch;
    output.bpu_in.refetch_address = input.refetch_addr;
    output.bpu_in.icache_read_ready = input.icache_ready;
}

static void accumulate_fetch_addr_req(FetchAddrCombOut &acc,
                                      const FetchAddrCombOut &step) {
    acc.clear_fifo |= step.clear_fifo;
    if (step.pop_en) {
        if (acc.pop_en) {
            std::printf("[front_top] ERROR: fetch_address_FIFO pop requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.pop_en = true;
    }
    if (step.push_en) {
        if (acc.push_en) {
            std::printf("[front_top] ERROR: fetch_address_FIFO push requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.push_en = true;
        acc.push_data = step.push_data;
    }
}

static void accumulate_instruction_fifo_req(InstructionCombOut &acc,
                                            const InstructionCombOut &step) {
    acc.clear_fifo |= step.clear_fifo;
    if (step.pop_en) {
        if (acc.pop_en) {
            std::printf("[front_top] ERROR: instruction_FIFO pop requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.pop_en = true;
    }
    if (step.push_en) {
        if (acc.push_en) {
            std::printf("[front_top] ERROR: instruction_FIFO push requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.push_en = true;
        acc.push_entry = step.push_entry;
    }
}

static void accumulate_ptab_req(PtabCombOut &acc, const PtabCombOut &step) {
    acc.clear_ptab |= step.clear_ptab;
    if (step.pop_en) {
        if (acc.pop_en) {
            std::printf("[front_top] ERROR: PTAB pop requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.pop_en = true;
    }
    if (step.push_write_en) {
        if (acc.push_write_en) {
            std::printf("[front_top] ERROR: PTAB write push requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.push_write_en = true;
        acc.push_write_entry = step.push_write_entry;
    }
    if (step.push_dummy_en) {
        if (acc.push_dummy_en) {
            std::printf("[front_top] ERROR: PTAB dummy push requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.push_dummy_en = true;
        acc.push_dummy_entry = step.push_dummy_entry;
    }
}

static void accumulate_front2back_req(Front2BackCombOut &acc,
                                      const Front2BackCombOut &step) {
    acc.clear_fifo |= step.clear_fifo;
    if (step.pop_en) {
        if (acc.pop_en) {
            std::printf("[front_top] ERROR: front2back_FIFO pop requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.pop_en = true;
    }
    if (step.push_en) {
        if (acc.push_en) {
            std::printf("[front_top] ERROR: front2back_FIFO push requested multiple times in one cycle\n");
            std::exit(1);
        }
        acc.push_en = true;
        acc.push_entry = step.push_entry;
    }
}

// ============================================================================
// 主函数
// ============================================================================
void front_comb_calc(const struct front_top_in &inp, const FrontReadData &rd,
                     struct front_top_out &out, FrontUpdateRequest &req) {
    FRONTEND_HOST_PROFILE_SCOPE(FrontComb);
    struct front_top_in *in = const_cast<struct front_top_in *>(&inp);
    struct front_top_out *out_ptr = &out;
    PendingBpuSeqTxn &bpu_seq_txn_req = req.bpu_seq_txn;
    PendingFrontState &front_state_req = req.front_state;
    FetchAddrCombOut &fetch_addr_fifo_final_req = req.fetch_addr_fifo_req;
    InstructionCombOut &fifo_final_req = req.fifo_req;
    PtabCombOut &ptab_final_req = req.ptab_req;
    Front2BackCombOut &front2back_fifo_final_req = req.front2back_fifo_req;
    std::memset(&out, 0, sizeof(struct front_top_out));
    uint32_t front_sim_time = rd.front_sim_time_snapshot + 1;
    FrontRuntimeStats front_stats = rd.front_stats_snapshot;
    DEBUG_LOG_SMALL("--------front_top sim_time: %d----------------\n", front_sim_time);
    const bool window_active = falcon_measurement_window_active();
    if (!falcon_window_active && window_active) {
        front_stats = {};
        falcon_window_active = true;
        falcon_recovery_pending = false;
        falcon_recovery_src = FalconRecoverySrc::NONE;
    }
    if (!window_active) {
        falcon_warmup_cycles++;
    }
    front_stats.cycles++;
    front_state_req.valid = false;
    
    // ========================================================================
    // 阶段 0: 初始化所有模块的输入输出结构
    // ========================================================================
    struct BPU_in bpu_in;
    struct fetch_address_FIFO_in fetch_addr_fifo_in;
    struct fetch_address_FIFO_out fetch_addr_fifo_out;
    fetch_address_FIFO_read_data fetch_addr_fifo_rd = rd.fetch_addr_fifo_rd_snapshot;
    fetch_address_FIFO_read_data fetch_addr_fifo_next_rd = fetch_addr_fifo_rd;
    FetchAddrCombOut fetch_addr_fifo_req{};
    FetchAddrCombOut fetch_addr_fifo_step_req{};
    struct icache_in icache_in;
    struct icache_out icache_out;
    struct instruction_FIFO_in fifo_in;
    struct instruction_FIFO_out fifo_out;
    instruction_FIFO_read_data fifo_rd = rd.fifo_rd_snapshot;
    instruction_FIFO_read_data fifo_next_rd = fifo_rd;
    InstructionCombOut fifo_req{};
    InstructionCombOut fifo_step_req{};
    struct PTAB_in ptab_in;
    struct PTAB_out ptab_out;
    PTAB_read_data ptab_rd = rd.ptab_rd_snapshot;
    PTAB_read_data ptab_next_rd = ptab_rd;
    PtabCombOut ptab_req{};
    PtabCombOut ptab_step_req{};
    struct front2back_FIFO_in front2back_fifo_in;
    struct front2back_FIFO_out front2back_fifo_out;
    front2back_FIFO_read_data front2back_fifo_rd = rd.front2back_fifo_rd_snapshot;
    front2back_FIFO_read_data front2back_fifo_next_rd = front2back_fifo_rd;
    Front2BackCombOut front2back_fifo_req{};
    Front2BackCombOut front2back_fifo_step_req{};
    
    memset(&bpu_in, 0, sizeof(bpu_in));
    memset(&fetch_addr_fifo_in, 0, sizeof(fetch_addr_fifo_in));
    memset(&fetch_addr_fifo_out, 0, sizeof(fetch_addr_fifo_out));
    memset(&icache_in, 0, sizeof(icache_in));
    memset(&icache_out, 0, sizeof(icache_out));
    memset(&fifo_in, 0, sizeof(fifo_in));
    memset(&fifo_out, 0, sizeof(fifo_out));
    memset(&ptab_in, 0, sizeof(ptab_in));
    memset(&ptab_out, 0, sizeof(ptab_out));
    memset(&front2back_fifo_in, 0, sizeof(front2back_fifo_in));
    memset(&front2back_fifo_out, 0, sizeof(front2back_fifo_out));
    
    bool global_reset = false;
    bool global_refetch = false;
    uint32_t refetch_address = 0;
    bool icache_ready = false;
    bool icache_ready_2 = false;
    bool fetch_addr_fifo_read_enable_slot0 = false;
    bool fetch_addr_fifo_read_enable_slot1_candidate = false;
    bool inst_fifo_read_enable = false;
    bool ptab_read_enable = false;
    bool front2back_read_enable = false;
    struct fetch_address_FIFO_out saved_fetch_addr_fifo_out_0{};
    struct fetch_address_FIFO_out saved_fetch_addr_fifo_out_1{};

    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontReadStage);
        // ========================================================================
        // 阶段 1: 计算全局 flush/refetch 信号
        // ========================================================================
        FrontGlobalControlCombOut global_ctrl_out{};
        FrontGlobalControlCombIn global_ctrl_in{};
        global_ctrl_in.reset = in->reset;
        global_ctrl_in.backend_refetch = in->refetch;
        global_ctrl_in.backend_refetch_address = in->refetch_address;
        global_ctrl_in.predecode_refetch_snapshot = rd.predecode_refetch_snapshot;
        global_ctrl_in.predecode_refetch_address_snapshot =
            rd.predecode_refetch_address_snapshot;
        front_global_control_comb(global_ctrl_in, global_ctrl_out);
        global_reset = global_ctrl_out.global_reset;
        global_refetch = global_ctrl_out.global_refetch;
        refetch_address = global_ctrl_out.refetch_address;
        if (global_reset) {
            falcon_recovery_pending = false;
            falcon_recovery_src = FalconRecoverySrc::NONE;
        } else if (global_refetch) {
            falcon_recovery_pending = true;
            falcon_recovery_src = in->refetch ? FalconRecoverySrc::BACKEND_REFETCH
                                              : FalconRecoverySrc::FRONTEND_FLUSH;
        }
        if (global_reset) {
            front_stats.reset_cycles++;
        }
        if (in->refetch) {
            front_stats.ext_refetch_cycles++;
        }
        if (rd.predecode_refetch_snapshot) {
            front_stats.delayed_refetch_cycles++;
        }
        if (global_refetch) {
            front_stats.global_refetch_cycles++;
        }
        if (!global_reset && !global_refetch) {
            front_stats.active_cycles++;
        }
        
        // ========================================================================
        // 阶段 2: 确定各 FIFO 的读使能（在实际读取前先决策）
        // ========================================================================
        
        // fetch_address_FIFO 读使能：icache 准备好接收 且 FIFO 非空
        // 需要先获取 icache 的 ready 状态
#ifdef USE_TRUE_ICACHE
        icache_in.reset = global_reset;
        icache_in.refetch = global_refetch;
        icache_in.itlb_flush = in->itlb_flush;
        icache_in.fence_i = in->fence_i;
        icache_in.invalidate_req = false;
        icache_in.csr_status = in->csr_status;
        icache_peek_ready(&icache_in, &icache_out);
#endif
        icache_ready = icache_out.icache_read_ready;
        icache_ready_2 = icache_out.icache_read_ready_2;
#ifdef USE_IDEAL_ICACHE
        icache_ready = true;
        icache_ready_2 = true;
#endif
        DEBUG_LOG_SMALL_4("icache_ready: %d, icache_ready_2: %d\n", icache_ready, icache_ready_2);
        FrontReadEnableCombOut read_enable_out{};
        FrontReadEnableCombIn read_enable_in{};
        read_enable_in.backend_fifo_read_enable = in->FIFO_read_enable;
        read_enable_in.fetch_addr_fifo_empty_latch_snapshot =
            rd.fetch_addr_fifo_empty_latch_snapshot;
        read_enable_in.fifo_empty_latch_snapshot = rd.fifo_empty_latch_snapshot;
        read_enable_in.ptab_empty_latch_snapshot = rd.ptab_empty_latch_snapshot;
        read_enable_in.front2back_fifo_full_latch_snapshot =
            rd.front2back_fifo_full_latch_snapshot;
        read_enable_in.global_reset = global_reset;
        read_enable_in.global_refetch = global_refetch;
        read_enable_in.icache_ready = icache_ready;
        read_enable_in.icache_ready_2 = icache_ready_2;
        front_read_enable_comb(read_enable_in, read_enable_out);
        fetch_addr_fifo_read_enable_slot0 =
            read_enable_out.fetch_addr_fifo_read_enable_slot0;
        fetch_addr_fifo_read_enable_slot1_candidate =
            read_enable_out.fetch_addr_fifo_read_enable_slot1_candidate;
#ifdef USE_TRUE_ICACHE
        assert(!fetch_addr_fifo_read_enable_slot1_candidate);
#endif
        
        // instruction_FIFO 和 PTAB 读使能：predecode checker 可以工作
        bool predecode_can_run_old = read_enable_out.predecode_can_run_old;
        (void)predecode_can_run_old;
        inst_fifo_read_enable = read_enable_out.inst_fifo_read_enable;
        ptab_read_enable = read_enable_out.ptab_read_enable;
        
        // front2back_FIFO 读使能：后端请求读取
        front2back_read_enable = read_enable_out.front2back_read_enable;
        if (front2back_read_enable) {
            front_stats.front2back_read_req_cycles++;
            front_stats.backend_demand_cycles++;
        }
        
        // ========================================================================
        // 阶段 3: 执行所有 FIFO 的读操作（获取输出数据）
        // ========================================================================
        FrontReadStageInputCombOut read_stage_input_out{};
        FrontReadStageInputCombIn read_stage_input_in{};
        read_stage_input_in.backend_refetch = in->refetch;
        read_stage_input_in.global_reset = global_reset;
        read_stage_input_in.global_refetch = global_refetch;
        read_stage_input_in.fetch_addr_fifo_read_enable_slot0 =
            fetch_addr_fifo_read_enable_slot0;
        read_stage_input_in.inst_fifo_read_enable = inst_fifo_read_enable;
        read_stage_input_in.ptab_read_enable = ptab_read_enable;
        read_stage_input_in.front2back_read_enable = front2back_read_enable;
        front_read_stage_input_comb(read_stage_input_in, read_stage_input_out);
        fetch_addr_fifo_in.reset = read_stage_input_out.fetch_addr_fifo_reset;
        fetch_addr_fifo_in.refetch = read_stage_input_out.fetch_addr_fifo_refetch;
        fetch_addr_fifo_in.read_enable = read_stage_input_out.fetch_addr_fifo_read_enable;
        fetch_addr_fifo_in.write_enable = false;
        fetch_addr_fifo_in.fetch_address = 0;

        fifo_in.reset = read_stage_input_out.fifo_reset;
        fifo_in.refetch = read_stage_input_out.fifo_refetch;
        fifo_in.read_enable = read_stage_input_out.fifo_read_enable;
        fifo_in.write_enable = false;

        ptab_in.reset = read_stage_input_out.ptab_reset;
        ptab_in.refetch = read_stage_input_out.ptab_refetch;
        ptab_in.read_enable = read_stage_input_out.ptab_read_enable;
        ptab_in.write_enable = false;

        front2back_fifo_in.reset = read_stage_input_out.front2back_fifo_reset;
        front2back_fifo_in.refetch = read_stage_input_out.front2back_fifo_refetch;
        front2back_fifo_in.read_enable = read_stage_input_out.front2back_fifo_read_enable;
        front2back_fifo_in.write_enable = false;

        fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_rd,
                                     &fetch_addr_fifo_out, &fetch_addr_fifo_next_rd,
                                     &fetch_addr_fifo_step_req);
        accumulate_fetch_addr_req(fetch_addr_fifo_req, fetch_addr_fifo_step_req);
        fetch_addr_fifo_rd = fetch_addr_fifo_next_rd;

        saved_fetch_addr_fifo_out_0 = fetch_addr_fifo_out;

        bool fetch_addr_fifo_read_enable_slot1 = false;
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
        fetch_addr_fifo_read_enable_slot1 =
            fetch_addr_fifo_read_enable_slot1_candidate && !fetch_addr_fifo_out.empty;
#endif
        if (fetch_addr_fifo_read_enable_slot1) {
            fetch_addr_fifo_in.read_enable = true;
            fetch_addr_fifo_in.write_enable = false;
            fetch_addr_fifo_in.fetch_address = 0;
            fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_rd,
                                         &fetch_addr_fifo_out, &fetch_addr_fifo_next_rd,
                                         &fetch_addr_fifo_step_req);
            accumulate_fetch_addr_req(fetch_addr_fifo_req, fetch_addr_fifo_step_req);
            fetch_addr_fifo_rd = fetch_addr_fifo_next_rd;
            saved_fetch_addr_fifo_out_1 = fetch_addr_fifo_out;
        }
        if (saved_fetch_addr_fifo_out_0.read_valid) {
            front_stats.fetch_addr_read_slot0_cycles++;
        }
        if (saved_fetch_addr_fifo_out_1.read_valid) {
            front_stats.fetch_addr_read_slot1_cycles++;
        }
        
        instruction_FIFO_comb_calc(&fifo_in, &fifo_rd, &fifo_out, &fifo_next_rd,
                                   &fifo_step_req);
        accumulate_instruction_fifo_req(fifo_req, fifo_step_req);
        fifo_rd = fifo_next_rd;
        
        PTAB_comb_calc(&ptab_in, &ptab_rd, &ptab_out, &ptab_next_rd, &ptab_step_req);
        accumulate_ptab_req(ptab_req, ptab_step_req);
        ptab_rd = ptab_next_rd;
        
        front2back_FIFO_comb_calc(&front2back_fifo_in, &front2back_fifo_rd,
                                  &front2back_fifo_out, &front2back_fifo_next_rd,
                                  &front2back_fifo_step_req);
        accumulate_front2back_req(front2back_fifo_req, front2back_fifo_step_req);
        front2back_fifo_rd = front2back_fifo_next_rd;
    }
    
    // 保存读出的数据用于后续处理
    struct instruction_FIFO_out saved_fifo_out = fifo_out;
    struct PTAB_out saved_ptab_out = ptab_out;
    struct front2back_FIFO_out saved_front2back_fifo_out = front2back_fifo_out;
    struct instruction_FIFO_out bypass_fifo_out;
    memset(&bypass_fifo_out, 0, sizeof(bypass_fifo_out));
    struct front2back_FIFO_out bypass_front2back_fifo_out;
    memset(&bypass_front2back_fifo_out, 0, sizeof(bypass_front2back_fifo_out));
    bool use_icache_to_predecode_bypass = false;
    bool use_front2back_output_bypass = false;
    bool predecode_can_run = false;
    bool predecode_source_valid = inst_fifo_read_enable;
    
    bool bpu_stall = false;
    bool bpu_can_run = false;
    bool can_bypass_fetch_to_icache = false;
    BPU_TOP::OutputPayload bpu_output{};
    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontBpuStage);
        // ========================================================================
        // 阶段 4: BPU 控制逻辑
        // ========================================================================
        FrontBpuControlCombOut bpu_ctrl_out{};
        FrontBpuControlCombIn bpu_ctrl_in{};
        BPU_in bpu_in_seed{};
        std::memset(&bpu_in_seed, 0, sizeof(bpu_in_seed));
        bpu_in_seed.reset = in->reset;
        for (int i = 0; i < COMMIT_WIDTH; i++) {
            bpu_in_seed.back2front_valid[i] = in->back2front_valid[i];
            bpu_in_seed.predict_base_pc[i] = in->predict_base_pc[i];
            bpu_in_seed.actual_dir[i] = in->actual_dir[i];
            bpu_in_seed.actual_br_type[i] = in->actual_br_type[i];
            bpu_in_seed.actual_target[i] = in->actual_target[i];
            bpu_in_seed.predict_dir[i] = in->predict_dir[i];
            bpu_in_seed.alt_pred[i] = in->alt_pred[i];
            bpu_in_seed.altpcpn[i] = in->altpcpn[i];
            bpu_in_seed.pcpn[i] = in->pcpn[i];
            for (int j = 0; j < 4; j++) {
                bpu_in_seed.tage_idx[i][j] = in->tage_idx[i][j];
                bpu_in_seed.tage_tag[i][j] = in->tage_tag[i][j];
            }
            bpu_in_seed.sc_used[i] = in->sc_used[i];
            bpu_in_seed.sc_pred[i] = in->sc_pred[i];
            bpu_in_seed.sc_sum[i] = in->sc_sum[i];
            for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
                bpu_in_seed.sc_idx[i][t] = in->sc_idx[i][t];
            }
            bpu_in_seed.loop_used[i] = in->loop_used[i];
            bpu_in_seed.loop_hit[i] = in->loop_hit[i];
            bpu_in_seed.loop_pred[i] = in->loop_pred[i];
            bpu_in_seed.loop_idx[i] = in->loop_idx[i];
            bpu_in_seed.loop_tag[i] = in->loop_tag[i];
        }
        bpu_ctrl_in.bpu_in_seed = bpu_in_seed;
        bpu_ctrl_in.fetch_addr_fifo_full_latch_snapshot =
            rd.fetch_addr_fifo_full_latch_snapshot;
        bpu_ctrl_in.ptab_full_latch_snapshot = rd.ptab_full_latch_snapshot;
        bpu_ctrl_in.global_reset = global_reset;
        bpu_ctrl_in.global_refetch = global_refetch;
        bpu_ctrl_in.refetch_address = refetch_address;
        front_bpu_control_comb(bpu_ctrl_in, bpu_ctrl_out);
        bpu_stall = bpu_ctrl_out.bpu_stall;
        bpu_can_run = bpu_ctrl_out.bpu_can_run;
        if (bpu_can_run) {
            front_stats.bpu_can_run_cycles++;
        }
        if (bpu_stall) {
            front_stats.bpu_stall_cycles++;
            if (rd.fetch_addr_fifo_full_latch_snapshot) {
                front_stats.bpu_stall_fetch_addr_full_cycles++;
            }
            if (rd.ptab_full_latch_snapshot) {
                front_stats.bpu_stall_ptab_full_cycles++;
            }
        }
        
        bpu_in = bpu_ctrl_out.bpu_in;
        bpu_seq_txn_req.valid = false;
        bpu_seq_txn_req.reset = bpu_in.reset;
        BPU_TOP::InputPayload bpu_input = bpu_ctrl_out.bpu_input;
        if (bpu_in.reset) {
            bpu_seq_txn_req.valid = true;
            bpu_seq_txn_req.inp = bpu_input;
            bpu_seq_txn_req.req = BPU_TOP::UpdateRequest{};
            bpu_seq_txn_req.reset = true;
            bpu_output.fetch_address = RESET_PC;
            bpu_output.two_ahead_target = bpu_output.fetch_address + (FETCH_WIDTH * 4);
        } else {
            BPU_TOP::ReadData bpu_rd;
            BPU_TOP::UpdateRequest bpu_req;
            bpu_instance.bpu_seq_read(bpu_input, bpu_rd);
            bpu_instance.bpu_comb_calc(bpu_input, bpu_rd, bpu_output, bpu_req);
            bpu_seq_txn_req.valid = true;
            bpu_seq_txn_req.inp = bpu_input;
            bpu_seq_txn_req.req = bpu_req;
            bpu_seq_txn_req.reset = false;
        }
        if (bpu_output.mini_flush_req) {
            front_stats.mini_flush_req_cycles++;
        }
        if (bpu_output.mini_flush_correct) {
            front_stats.mini_flush_correct_cycles++;
        }
        
        if (bpu_output.icache_read_valid && bpu_can_run) {
            front_stats.bpu_issue_cycles++;
            DEBUG_LOG_SMALL("[front_top] sim_time: %d, bpu_out.fetch_address: %x\n",
                            front_sim_time, bpu_output.fetch_address);
        }
        
        // ========================================================================
        // 阶段 5: fetch_address_FIFO 写控制（支持双写和Mini Flush）
        // ========================================================================
        fetch_addr_fifo_in.reset = false;
        fetch_addr_fifo_in.refetch = false;
        fetch_addr_fifo_in.read_enable = false;
        bool normal_write_enable =
            bpu_output.icache_read_valid && bpu_can_run && !global_reset;
        bool refetch_write_enable = normal_write_enable && global_refetch; 
        (void)refetch_write_enable;
        DEBUG_LOG_SMALL_4("normal_write_enable: %d, bpu_out.mini_flush_correct: %d\n",
                          normal_write_enable, bpu_output.mini_flush_correct);
        can_bypass_fetch_to_icache =
            !saved_fetch_addr_fifo_out_0.read_valid && normal_write_enable &&
            !bpu_output.mini_flush_correct && icache_ready && !global_refetch;
#if !FRONTEND_ENABLE_FETCH_TO_ICACHE_BYPASS
        can_bypass_fetch_to_icache = false;
#endif
        if (can_bypass_fetch_to_icache) {
            front_stats.bypass_fetch_to_icache_opportunity_cycles++;
        }
        if (normal_write_enable && !bpu_output.mini_flush_correct &&
            !can_bypass_fetch_to_icache) {
            front_stats.fetch_addr_write_normal_cycles++;
            fetch_addr_fifo_in.write_enable = true;
            fetch_addr_fifo_in.fetch_address = bpu_output.fetch_address;
            DEBUG_LOG_SMALL_4("normal write enable: %x\n", bpu_output.fetch_address);
            fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_rd,
                                         &fetch_addr_fifo_out, &fetch_addr_fifo_next_rd,
                                         &fetch_addr_fifo_step_req);
            accumulate_fetch_addr_req(fetch_addr_fifo_req, fetch_addr_fifo_step_req);
            fetch_addr_fifo_rd = fetch_addr_fifo_next_rd;
        } else if (normal_write_enable && bpu_output.mini_flush_correct) {
            front_stats.fetch_addr_write_skip_by_mini_flush_correct_cycles++;
        }
#ifdef ENABLE_2AHEAD
        if (normal_write_enable) {
            front_stats.fetch_addr_write_twoahead_cycles++;
            fetch_addr_fifo_in.write_enable = true;
            fetch_addr_fifo_in.fetch_address = bpu_output.two_ahead_target;
            DEBUG_LOG_SMALL_4("2ahead write enable: %x\n", bpu_output.two_ahead_target);
            fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_rd,
                                         &fetch_addr_fifo_out, &fetch_addr_fifo_next_rd,
                                         &fetch_addr_fifo_step_req);
            accumulate_fetch_addr_req(fetch_addr_fifo_req, fetch_addr_fifo_step_req);
            fetch_addr_fifo_rd = fetch_addr_fifo_next_rd;
        }
#endif
    }

    bool icache_slot0_data_valid = false;
    bool icache_slot1_data_valid = false;
    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontIcacheStage);
        // ========================================================================
        // 阶段 6: icache 控制逻辑
        // ========================================================================
        icache_in.reset = global_reset;
        icache_in.refetch = global_refetch;
        icache_in.itlb_flush = in->itlb_flush;
        icache_in.fence_i = in->fence_i;
        icache_in.invalidate_req = false;
        icache_in.csr_status = in->csr_status;
        icache_in.run_comb_only = false;
        
        if (saved_fetch_addr_fifo_out_0.read_valid) {
            icache_in.icache_read_valid = true;
            icache_in.fetch_address = saved_fetch_addr_fifo_out_0.fetch_address;
            DEBUG_LOG_SMALL_4("icache_in.fetch_address: %x\n", icache_in.fetch_address);
        } else if (can_bypass_fetch_to_icache) {
            icache_in.icache_read_valid = true;
            icache_in.fetch_address = bpu_output.fetch_address;
            DEBUG_LOG_SMALL_4("icache_in.fetch_address bypass: %x\n", icache_in.fetch_address);
        } else {
            icache_in.icache_read_valid = false;
            icache_in.fetch_address = 0;
        }
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
        if (saved_fetch_addr_fifo_out_1.read_valid) {
            icache_in.icache_read_valid_2 = true;
            icache_in.fetch_address_2 = saved_fetch_addr_fifo_out_1.fetch_address;
            DEBUG_LOG_SMALL_4("icache_in.fetch_address_2: %x\n", icache_in.fetch_address_2);
        } else {
            icache_in.icache_read_valid_2 = false;
            icache_in.fetch_address_2 = 0;
        }
#else
        icache_in.icache_read_valid_2 = false;
        icache_in.fetch_address_2 = 0;
#endif
#ifdef USE_TRUE_ICACHE
        assert(!icache_in.icache_read_valid_2);
#endif
        if (icache_in.icache_read_valid) {
            front_stats.icache_req_slot0_cycles++;
        }
        if (icache_in.icache_read_valid_2) {
            front_stats.icache_req_slot1_cycles++;
        }
        
        icache_comb_calc(&icache_in, &icache_out);
        if (icache_out.perf_req_fire) {
            front_stats.icache_req_fire_cycles++;
        }
        if (icache_out.perf_req_blocked) {
            front_stats.icache_req_blocked_cycles++;
        }
        if (icache_out.perf_resp_fire) {
            front_stats.icache_resp_fire_cycles++;
        }
        if (icache_out.perf_miss_event) {
            front_stats.icache_miss_event_cycles++;
            if (front_stats.cycles <= kFalconColdMissWindowCycles) {
                front_stats.icache_miss_event_cold_window_cycles++;
            }
        }
        if (icache_out.perf_miss_busy) {
            front_stats.icache_miss_busy_cycles++;
        }
        if (icache_out.perf_outstanding_req) {
            front_stats.icache_outstanding_req_cycles++;
        }
        
        // ========================================================================
        // 阶段 7: instruction_FIFO 写控制（写入 icache 返回的数据）
        // ========================================================================
        fifo_in.reset = global_reset;
        fifo_in.refetch = global_refetch;
        fifo_in.read_enable = false;
        fifo_in.write_enable = false;

        icache_slot0_data_valid = icache_out.icache_read_complete;
#ifdef USE_IDEAL_ICACHE
        icache_slot0_data_valid &= icache_in.icache_read_valid;
#endif
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
        icache_slot1_data_valid =
            icache_out.icache_read_complete_2 && icache_in.icache_read_valid_2;
#endif
#ifdef USE_TRUE_ICACHE
        assert(!icache_slot1_data_valid);
#endif
        if (icache_slot0_data_valid) {
            front_stats.icache_resp_slot0_cycles++;
        }
        if (icache_slot1_data_valid) {
            front_stats.icache_resp_slot1_cycles++;
        }
    }

    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontPredecodeStage);
        bool inst_fifo_full_for_write = rd.fifo_full_latch_snapshot;
        bool can_bypass_icache_to_predecode =
            rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
            !rd.front2back_fifo_full_latch_snapshot && !global_reset &&
            !global_refetch && icache_slot0_data_valid;
#if !FRONTEND_ENABLE_ICACHE_TO_PREDECODE_BYPASS
        can_bypass_icache_to_predecode = false;
#endif
        if (can_bypass_icache_to_predecode) {
            front_stats.bypass_icache_to_predecode_opportunity_cycles++;
            ptab_in.reset = global_reset;
            ptab_in.refetch = global_refetch;
            ptab_in.read_enable = true;
            ptab_in.write_enable = false;
            PTAB_comb_calc(&ptab_in, &ptab_rd, &ptab_out, &ptab_next_rd,
                           &ptab_step_req);
            accumulate_ptab_req(ptab_req, ptab_step_req);
            ptab_rd = ptab_next_rd;
            saved_ptab_out = ptab_out;
            predecode_source_valid = true;

            if (!saved_ptab_out.dummy_entry) {
                use_icache_to_predecode_bypass = true;
                for (int i = 0; i < FETCH_WIDTH; i++) {
                    bypass_fifo_out.instructions[i] = icache_out.fetch_group[i];
                    bypass_fifo_out.pc[i] = icache_out.fetch_pc + (i * 4);
                    bypass_fifo_out.page_fault_inst[i] = icache_out.page_fault_inst[i];
                    bypass_fifo_out.inst_valid[i] = icache_out.inst_valid[i];

                    if (icache_out.inst_valid[i]) {
                        uint32_t current_pc = icache_out.fetch_pc + (i * 4);
                        predecode_in predecode_inp{icache_out.fetch_group[i], current_pc};
                        predecode_read_data predecode_rd{};
                        PredecodeResult result{};
                        predecode_seq_read(&predecode_inp, &predecode_rd);
                        predecode_comb(predecode_rd, result);
                        bypass_fifo_out.predecode_type[i] = result.type;
                        bypass_fifo_out.predecode_target_address[i] = result.target_address;
                    } else {
                        bypass_fifo_out.predecode_type[i] = PREDECODE_NON_BRANCH;
                        bypass_fifo_out.predecode_target_address[i] = 0;
                    }
                }

                uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
                bypass_fifo_out.seq_next_pc = icache_out.fetch_pc + (FETCH_WIDTH * 4);
                if ((bypass_fifo_out.seq_next_pc & mask) !=
                    (icache_out.fetch_pc & mask)) {
                    bypass_fifo_out.seq_next_pc &= mask;
                }
                saved_fifo_out = bypass_fifo_out;
            }
        }
        predecode_can_run =
            (predecode_source_valid && !saved_ptab_out.dummy_entry &&
             use_icache_to_predecode_bypass) ||
            (!saved_ptab_out.dummy_entry && inst_fifo_read_enable);
        if (predecode_can_run) {
            front_stats.predecode_run_cycles++;
        } else {
            if (rd.fifo_empty_latch_snapshot) {
                front_stats.predecode_block_fifo_empty_cycles++;
            }
            if (rd.ptab_empty_latch_snapshot) {
                front_stats.predecode_block_ptab_empty_cycles++;
            }
            if (rd.front2back_fifo_full_latch_snapshot) {
                front_stats.predecode_block_front2back_full_cycles++;
            }
            if (predecode_source_valid && saved_ptab_out.dummy_entry) {
                front_stats.predecode_block_dummy_ptab_cycles++;
            }
        }
        if (!inst_fifo_full_for_write && icache_slot0_data_valid && !global_reset &&
            !global_refetch && !use_icache_to_predecode_bypass) {
            front_stats.inst_fifo_write_slot0_cycles++;
            fifo_in.write_enable = true;
            for (int i = 0; i < FETCH_WIDTH; i++) {
                fifo_in.fetch_group[i] = icache_out.fetch_group[i];
                fifo_in.pc[i] = icache_out.fetch_pc + (i * 4);
                fifo_in.page_fault_inst[i] = icache_out.page_fault_inst[i];
                fifo_in.inst_valid[i] = icache_out.inst_valid[i];

                if (icache_out.inst_valid[i]) {
                    uint32_t current_pc = icache_out.fetch_pc + (i * 4);
                    predecode_in predecode_inp{icache_out.fetch_group[i], current_pc};
                    predecode_read_data predecode_rd{};
                    PredecodeResult result{};
                    predecode_seq_read(&predecode_inp, &predecode_rd);
                    predecode_comb(predecode_rd, result);
                    fifo_in.predecode_type[i] = result.type;
                    fifo_in.predecode_target_address[i] = result.target_address;
                    DEBUG_LOG_SMALL_4("[icache_out] sim_time: %d, pc: %x, inst: %x\n",
                                      front_sim_time, current_pc, icache_out.fetch_group[i]);
                } else {
                    fifo_in.predecode_type[i] = PREDECODE_NON_BRANCH;
                    fifo_in.predecode_target_address[i] = 0;
                }
            }

            uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
            fifo_in.seq_next_pc = icache_out.fetch_pc + (FETCH_WIDTH * 4);
            if ((fifo_in.seq_next_pc & mask) != (icache_out.fetch_pc & mask)) {
                fifo_in.seq_next_pc &= mask;
            }

            instruction_FIFO_comb_calc(&fifo_in, &fifo_rd, &fifo_out, &fifo_next_rd,
                                       &fifo_step_req);
            accumulate_instruction_fifo_req(fifo_req, fifo_step_req);
            fifo_rd = fifo_next_rd;
            inst_fifo_full_for_write = fifo_out.full;
        }

        bool can_write_slot1 = !inst_fifo_full_for_write && icache_slot1_data_valid
                               && !global_reset && !global_refetch;
        if (can_write_slot1) {
            front_stats.inst_fifo_write_slot1_cycles++;
            fifo_in.write_enable = true;
            for (int i = 0; i < FETCH_WIDTH; i++) {
                fifo_in.fetch_group[i] = icache_out.fetch_group_2[i];
                fifo_in.pc[i] = icache_out.fetch_pc_2 + (i * 4);
                fifo_in.page_fault_inst[i] = icache_out.page_fault_inst_2[i];
                fifo_in.inst_valid[i] = icache_out.inst_valid_2[i];

                if (icache_out.inst_valid_2[i]) {
                    uint32_t current_pc = icache_out.fetch_pc_2 + (i * 4);
                    predecode_in predecode_inp{icache_out.fetch_group_2[i], current_pc};
                    predecode_read_data predecode_rd{};
                    PredecodeResult result{};
                    predecode_seq_read(&predecode_inp, &predecode_rd);
                    predecode_comb(predecode_rd, result);
                    fifo_in.predecode_type[i] = result.type;
                    fifo_in.predecode_target_address[i] = result.target_address;
                    DEBUG_LOG_SMALL_4("[icache_out] sim_time: %d, pc: %x, inst: %x\n",
                                      front_sim_time, current_pc,
                                      icache_out.fetch_group_2[i]);
                } else {
                    fifo_in.predecode_type[i] = PREDECODE_NON_BRANCH;
                    fifo_in.predecode_target_address[i] = 0;
                }
            }

            uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
            fifo_in.seq_next_pc = icache_out.fetch_pc_2 + (FETCH_WIDTH * 4);
            if ((fifo_in.seq_next_pc & mask) != (icache_out.fetch_pc_2 & mask)) {
                fifo_in.seq_next_pc &= mask;
            }

            instruction_FIFO_comb_calc(&fifo_in, &fifo_rd, &fifo_out, &fifo_next_rd,
                                       &fifo_step_req);
            accumulate_instruction_fifo_req(fifo_req, fifo_step_req);
            fifo_rd = fifo_next_rd;
        }
    }
    
    bool do_predecode_flush = false;
    uint32_t predecode_flush_address = 0;
    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontF2bStage);
        bool ptab_can_write = bpu_output.PTAB_write_enable &&
                              !rd.ptab_full_latch_snapshot && !global_reset &&
                              !global_refetch;
        if (ptab_can_write) {
            front_stats.ptab_write_cycles++;
        }
        
        FrontPtabWriteCombOut ptab_write_out{};
        FrontPtabWriteCombIn ptab_write_in{};
        ptab_write_in.bpu_output = bpu_output;
        ptab_write_in.global_reset = global_reset;
        ptab_write_in.global_refetch = global_refetch;
        ptab_write_in.ptab_can_write = ptab_can_write;
        front_ptab_write_comb(ptab_write_in, ptab_write_out);
        ptab_in = ptab_write_out.ptab_in;

        if (ptab_can_write) {
            DEBUG_LOG_SMALL_3("bpu_out.predict_next_fetch_address: %x\n",
                              bpu_output.predict_next_fetch_address);
            
            PTAB_comb_calc(&ptab_in, &ptab_rd, &ptab_out, &ptab_next_rd,
                           &ptab_step_req);
            accumulate_ptab_req(ptab_req, ptab_step_req);
            ptab_rd = ptab_next_rd;
        }
        
        struct predecode_checker_out checker_out;
        memset(&checker_out, 0, sizeof(checker_out));
        
        if (predecode_can_run) {
            front_stats.checker_run_cycles++;
            for (int i = 0; i < FETCH_WIDTH; i++) {
                if (saved_fifo_out.pc[i] != saved_ptab_out.predict_base_pc[i]) {
                    printf("ERROR: fifo pc[%d]: %x != ptab pc[%d]: %x\n",
                           i, saved_fifo_out.pc[i], i, saved_ptab_out.predict_base_pc[i]);
                    exit(1);
                }
            }
            
            FrontCheckerInputCombOut checker_input_out{};
            FrontCheckerInputCombIn checker_input_in{};
            checker_input_in.fifo_out = saved_fifo_out;
            checker_input_in.ptab_out = saved_ptab_out;
            front_checker_input_comb(checker_input_in, checker_input_out);
            predecode_checker_read_data checker_rd{};
            predecode_checker_seq_read(&checker_input_out.checker_in, &checker_rd);
            predecode_checker_comb(checker_rd, checker_out);
            
            DEBUG_LOG_SMALL_4("[predecode on] seq_next_pc: %x, predict_next: %x\n",
                              saved_fifo_out.seq_next_pc,
                              saved_ptab_out.predict_next_fetch_address);
            
            if (checker_out.predecode_flush_enable) {
                front_stats.checker_flush_cycles++;
                do_predecode_flush = true;
                predecode_flush_address =
                    checker_out.predict_next_fetch_address_corrected;
            }
        }
        
        bool front2back_can_write = predecode_can_run &&
                                    !rd.front2back_fifo_full_latch_snapshot &&
                                    !global_reset;
        bool can_bypass_front2back_to_output =
            front2back_read_enable && rd.front2back_fifo_empty_latch_snapshot &&
            !saved_front2back_fifo_out.front2back_FIFO_valid && front2back_can_write;
        if (can_bypass_front2back_to_output) {
            front_stats.bypass_front2back_to_output_opportunity_cycles++;
            front_stats.bypass_front2back_to_output_hit_cycles++;
            use_front2back_output_bypass = true;
        }

        dump_front_focus_source(saved_fifo_out, saved_ptab_out, global_refetch,
                                predecode_can_run, front2back_can_write,
                                use_front2back_output_bypass);
        
        front2back_fifo_in.reset = global_reset;
        front2back_fifo_in.refetch = in->refetch;
        front2back_fifo_in.read_enable = false;
        front2back_fifo_in.write_enable =
            front2back_can_write && !can_bypass_front2back_to_output;
        if (front2back_fifo_in.write_enable) {
            front_stats.front2back_write_cycles++;
        }
        
        if (front2back_can_write) {
            FrontFront2backWriteCombOut front2back_write_out{};
            FrontFront2backWriteCombIn front2back_write_in{};
            front2back_write_in.fifo_out = saved_fifo_out;
            front2back_write_in.ptab_out = saved_ptab_out;
            front2back_write_in.checker_out = checker_out;
            front2back_write_in.use_front2back_output_bypass =
                use_front2back_output_bypass;
            front_front2back_write_comb(front2back_write_in, front2back_write_out);
            front2back_fifo_in.fetch_group[0] =
                front2back_write_out.front2back_fifo_in.fetch_group[0];
            front2back_fifo_in = front2back_write_out.front2back_fifo_in;
            front2back_fifo_in.reset = global_reset;
            front2back_fifo_in.refetch = in->refetch;
            front2back_fifo_in.read_enable = false;
            front2back_fifo_in.write_enable =
                front2back_can_write && !can_bypass_front2back_to_output;
            bypass_front2back_fifo_out = front2back_write_out.bypass_front2back_fifo_out;
            
            if (front2back_fifo_in.write_enable) {
                front2back_FIFO_comb_calc(&front2back_fifo_in, &front2back_fifo_rd,
                                          &front2back_fifo_out,
                                          &front2back_fifo_next_rd,
                                          &front2back_fifo_step_req);
                accumulate_front2back_req(front2back_fifo_req,
                                          front2back_fifo_step_req);
                front2back_fifo_rd = front2back_fifo_next_rd;
            }
        }
        
        front_state_req.valid = true;
        if (do_predecode_flush) {
            struct icache_in invalidate_req_in;
            memset(&invalidate_req_in, 0, sizeof(invalidate_req_in));
            invalidate_req_in.invalidate_req = true;
            invalidate_req_in.csr_status = in->csr_status;
            icache_comb_calc(&invalidate_req_in, &icache_out);

            front_state_req.next_predecode_refetch = true;
            front_state_req.next_predecode_refetch_address = predecode_flush_address;
            
            DEBUG_LOG_SMALL("[front_top] predecode flush to: %x\n", predecode_flush_address);
        } else {
            front_state_req.next_predecode_refetch = false;
            front_state_req.next_predecode_refetch_address = 0;
        }
    }
    
    {
        FRONTEND_HOST_PROFILE_SCOPE(FrontRefreshStage);
        front_state_req.next_fetch_addr_fifo_full =
            (fetch_addr_fifo_rd.size >= (FETCH_ADDR_FIFO_SIZE - 1));
        front_state_req.next_fetch_addr_fifo_empty = (fetch_addr_fifo_rd.size == 0);
        front_state_req.next_fifo_full = (fifo_rd.size == INSTRUCTION_FIFO_SIZE);
        front_state_req.next_fifo_empty = (fifo_rd.size == 0);
        front_state_req.next_ptab_full = (ptab_rd.size >= (PTAB_SIZE - 1));
        front_state_req.next_ptab_empty = (ptab_rd.size == 0);
        front_state_req.next_front2back_fifo_full =
            (front2back_fifo_rd.size == FRONT2BACK_FIFO_SIZE);
        front_state_req.next_front2back_fifo_empty = (front2back_fifo_rd.size == 0);
        
        FrontOutputCombOut final_output_out{};
        FrontOutputCombIn final_output_in{};
        final_output_in.saved_front2back_fifo_out = saved_front2back_fifo_out;
        final_output_in.bypass_front2back_fifo_out = bypass_front2back_fifo_out;
        final_output_in.use_front2back_output_bypass = use_front2back_output_bypass;
        front_output_comb(final_output_in, final_output_out);
        *out_ptr = final_output_out.out;
        out_ptr->commit_stall = bpu_output.update_queue_full;
        dump_front_focus_output(*out_ptr);

        for (int i = 0; i < FETCH_WIDTH; i++) {
            if (out_ptr->inst_valid[i]) {
                DEBUG_LOG_SMALL_4("[front_top] sim_time: %d, out->pc[%d]: %x, inst: %x\n",
                                  front_sim_time, i, out_ptr->pc[i],
                                  out_ptr->instructions[i]);
            }
        }
        
        if (out_ptr->FIFO_valid) {
            front_stats.front2back_valid_out_cycles++;
            if (front2back_read_enable) {
                front_stats.backend_deliver_cycles++;
                front_stats.delivered_groups++;
                if (falcon_recovery_pending) {
                    falcon_recovery_pending = false;
                    falcon_recovery_src = FalconRecoverySrc::NONE;
                }
                for (int i = 0; i < FETCH_WIDTH; i++) {
                    if (out_ptr->inst_valid[i]) {
                        front_stats.delivered_insts++;
                    }
                }
            }
            DEBUG_LOG_SMALL("[front_top] sim_time: %d, out->pc[0]: %x\n",
                            front_sim_time, out_ptr->pc[0]);
        } else if (front2back_read_enable) {
            front_stats.backend_bubble_cycles++;
            if (global_reset) {
                front_stats.bubble_reset_cycles++;
            } else if (global_refetch) {
                front_stats.bubble_refetch_cycles++;
            } else if (!rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
                       saved_ptab_out.dummy_entry) {
                front_stats.bubble_dummy_ptab_cycles++;
            } else if (rd.fifo_empty_latch_snapshot) {
                if (icache_out.perf_miss_busy) {
                    front_stats.bubble_icache_miss_cycles++;
                } else if (icache_out.perf_outstanding_req || icache_out.perf_req_blocked) {
                    front_stats.bubble_icache_latency_cycles++;
                    if (icache_out.perf_itlb_retry) {
                        front_stats.bubble_icache_tlb_retry_cycles++;
                        if (icache_out.perf_itlb_retry_other_walk) {
                            front_stats.bubble_icache_tlb_retry_other_walk_cycles++;
                        } else if (icache_out.perf_itlb_retry_walk_req_blocked) {
                            front_stats.bubble_icache_tlb_retry_walk_req_blocked_cycles++;
                        } else if (icache_out.perf_itlb_retry_wait_walk_resp) {
                            front_stats.bubble_icache_tlb_retry_wait_walk_resp_cycles++;
                        } else if (icache_out.perf_itlb_retry_local_walker_busy) {
                            front_stats.bubble_icache_tlb_retry_local_walker_cycles++;
                        }
                    } else if (icache_out.perf_itlb_fault) {
                        front_stats.bubble_icache_tlb_fault_cycles++;
                    } else if (icache_out.perf_req_blocked) {
                        front_stats.bubble_icache_cache_backpressure_cycles++;
                    } else {
                        front_stats.bubble_icache_latency_other_cycles++;
                    }
                } else if (bpu_stall) {
                    front_stats.bubble_bpu_stall_cycles++;
                } else if (rd.fetch_addr_fifo_empty_latch_snapshot) {
                    front_stats.bubble_fetch_addr_empty_cycles++;
                } else {
                    front_stats.bubble_inst_fifo_empty_other_cycles++;
                }
            } else if (rd.ptab_empty_latch_snapshot) {
                front_stats.bubble_ptab_empty_cycles++;
            } else {
                front_stats.bubble_other_cycles++;
            }

            if (global_reset) {
                front_stats.bubble2_reset_cycles++;
            } else if (global_refetch) {
                if (in->refetch) {
                    front_stats.bubble2_recovery_backend_refetch_cycles++;
                } else {
                    front_stats.bubble2_recovery_frontend_flush_cycles++;
                }
            } else if (!rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
                       saved_ptab_out.dummy_entry) {
                front_stats.bubble2_glue_or_fifo_cycles++;
            } else if (rd.fifo_empty_latch_snapshot) {
                if (icache_out.perf_miss_busy ||
                    icache_out.perf_outstanding_req ||
                    icache_out.perf_req_blocked) {
                    front_stats.bubble2_fetch_stall_cycles++;
                } else if (bpu_stall) {
                    front_stats.bubble2_bpu_side_cycles++;
                } else {
                    front_stats.bubble2_glue_or_fifo_cycles++;
                }
            } else if (rd.ptab_empty_latch_snapshot) {
                front_stats.bubble2_glue_or_fifo_cycles++;
            } else {
                front_stats.bubble2_other_cycles++;
            }

            if (global_reset) {
                front_stats.bubble3_reset_cycles++;
            } else if (falcon_recovery_pending) {
                if (falcon_recovery_src == FalconRecoverySrc::BACKEND_REFETCH) {
                    front_stats.bubble3_recovery_backend_refetch_cycles++;
                } else {
                    front_stats.bubble3_recovery_frontend_flush_cycles++;
                }
            } else if (!rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
                       saved_ptab_out.dummy_entry) {
                front_stats.bubble3_glue_or_fifo_cycles++;
            } else if (rd.fifo_empty_latch_snapshot) {
                if (icache_out.perf_miss_busy ||
                    icache_out.perf_outstanding_req ||
                    icache_out.perf_req_blocked) {
                    front_stats.bubble3_fetch_stall_cycles++;
                } else if (bpu_stall) {
                    front_stats.bubble3_bpu_side_cycles++;
                } else {
                    front_stats.bubble3_glue_or_fifo_cycles++;
                }
            } else if (rd.ptab_empty_latch_snapshot) {
                front_stats.bubble3_glue_or_fifo_cycles++;
            } else {
                front_stats.bubble3_other_cycles++;
            }
        }

        front_state_req.next_front_sim_time = front_sim_time;
        front_state_req.next_front_stats = front_stats;
        fetch_addr_fifo_final_req = fetch_addr_fifo_req;
        fifo_final_req = fifo_req;
        ptab_final_req = ptab_req;
        front2back_fifo_final_req = front2back_fifo_req;
        front_deadlock_snapshot.valid = true;
        front_deadlock_snapshot.global_reset = global_reset;
        front_deadlock_snapshot.global_refetch = global_refetch;
        front_deadlock_snapshot.icache_ready = icache_ready;
        front_deadlock_snapshot.icache_ready_2 = icache_ready_2;
        front_deadlock_snapshot.fetch_addr_read_enable_slot0 =
            fetch_addr_fifo_read_enable_slot0;
        front_deadlock_snapshot.fetch_addr_read_enable_slot1 =
            fetch_addr_fifo_read_enable_slot1_candidate;
        front_deadlock_snapshot.inst_fifo_read_enable = inst_fifo_read_enable;
        front_deadlock_snapshot.ptab_read_enable = ptab_read_enable;
        front_deadlock_snapshot.front2back_read_enable = front2back_read_enable;
        front_deadlock_snapshot.icache = icache_out;
        front_deadlock_snapshot.fifo = fifo_out;
        front_deadlock_snapshot.ptab = ptab_out;
        front_deadlock_snapshot.front2back = front2back_fifo_out;
        front_deadlock_snapshot.out = *out_ptr;
        req.out_regs = out;
    }
}

namespace {

void front_seq_read(const struct front_top_in &inp, FrontReadData &rd) {
  FRONTEND_HOST_PROFILE_SCOPE(FrontSeqRead);
  rd.predecode_refetch_snapshot = predecode_refetch;
  rd.predecode_refetch_address_snapshot = predecode_refetch_address;
  rd.front_sim_time_snapshot = front_sim_time;
  rd.front_stats_snapshot = front_stats;
  rd.fetch_addr_fifo_full_latch_snapshot = fetch_addr_fifo_full_latch;
  rd.fetch_addr_fifo_empty_latch_snapshot = fetch_addr_fifo_empty_latch;
  rd.fifo_full_latch_snapshot = fifo_full_latch;
  rd.fifo_empty_latch_snapshot = fifo_empty_latch;
  rd.ptab_full_latch_snapshot = ptab_full_latch;
  rd.ptab_empty_latch_snapshot = ptab_empty_latch;
  rd.front2back_fifo_full_latch_snapshot = front2back_fifo_full_latch;
  rd.front2back_fifo_empty_latch_snapshot = front2back_fifo_empty_latch;

  fetch_address_FIFO_in fetch_addr_in;
  instruction_FIFO_in fifo_in;
  PTAB_in ptab_in;
  front2back_FIFO_in front2back_in;
  icache_in icache_inp;
  icache_out icache_outp;
  std::memset(&fetch_addr_in, 0, sizeof(fetch_addr_in));
  std::memset(&fifo_in, 0, sizeof(fifo_in));
  std::memset(&ptab_in, 0, sizeof(ptab_in));
  std::memset(&front2back_in, 0, sizeof(front2back_in));
  std::memset(&icache_inp, 0, sizeof(icache_inp));
  icache_inp.csr_status = inp.csr_status;
  std::memset(&icache_outp, 0, sizeof(icache_outp));

  fetch_address_FIFO_seq_read(&fetch_addr_in, &rd.fetch_addr_fifo_rd_snapshot);
  instruction_FIFO_seq_read(&fifo_in, &rd.fifo_rd_snapshot);
  PTAB_seq_read(&ptab_in, &rd.ptab_rd_snapshot);
  front2back_FIFO_seq_read(&front2back_in, &rd.front2back_fifo_rd_snapshot);
  icache_seq_read(&icache_inp, &icache_outp);
  (void)inp;
}

void front_seq_write(const struct front_top_in &inp, const FrontUpdateRequest &req,
                     bool reset) {
  FRONTEND_HOST_PROFILE_SCOPE(FrontSeqWrite);
  (void)inp;
  (void)reset;
  if (req.bpu_seq_txn.valid) {
    bpu_instance.bpu_seq_write(req.bpu_seq_txn.inp, req.bpu_seq_txn.req,
                               req.bpu_seq_txn.reset);
  }
  fetch_address_FIFO_seq_write(&req.fetch_addr_fifo_req);
  instruction_FIFO_seq_write(&req.fifo_req);
  PTAB_seq_write(&req.ptab_req);
  front2back_FIFO_seq_write(&req.front2back_fifo_req);
  predecode_checker_seq_write();
  icache_seq_write();

  if (req.front_state.valid) {
    front_sim_time = req.front_state.next_front_sim_time;
    front_stats = req.front_state.next_front_stats;
    predecode_refetch = req.front_state.next_predecode_refetch;
    predecode_refetch_address = req.front_state.next_predecode_refetch_address;
    fetch_addr_fifo_full_latch = req.front_state.next_fetch_addr_fifo_full;
    fetch_addr_fifo_empty_latch = req.front_state.next_fetch_addr_fifo_empty;
    fifo_full_latch = req.front_state.next_fifo_full;
    fifo_empty_latch = req.front_state.next_fifo_empty;
    ptab_full_latch = req.front_state.next_ptab_full;
    ptab_empty_latch = req.front_state.next_ptab_empty;
    front2back_fifo_full_latch = req.front_state.next_front2back_fifo_full;
    front2back_fifo_empty_latch = req.front_state.next_front2back_fifo_empty;
  }
}

} // namespace

void front_top(struct front_top_in *in, struct front_top_out *out) {
  assert(in);
  assert(out);
  FRONTEND_HOST_PROFILE_SCOPE(FrontTop);

  FrontReadData rd;
  FrontUpdateRequest req;
  front_seq_read(*in, rd);
  front_comb_calc(*in, rd, *out, req);
  front_seq_write(*in, req, in->reset);
}
