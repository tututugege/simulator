#include "RealLsu.h"
#include "AbstractLsu.h"
#include "DcacheConfig.h"
#include "DeadlockDebug.h"
#include "DeadlockReplayTrace.h"
#include "PhysMemory.h"
#include "TlbMmu.h"
#include "config.h"
#include "util.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
static constexpr int64_t REQ_WAIT_RETRY = 0x7FFFFFFFFFFFFFFF;
static constexpr int64_t REQ_WAIT_SEND = 0x7FFFFFFFFFFFFFFD;
static constexpr int64_t REQ_WAIT_RESP = 0x7FFFFFFFFFFFFFFE;
static constexpr int64_t REQ_WAIT_EXEC = 0x7FFFFFFFFFFFFFFC;
static constexpr uint64_t LD_RESP_STUCK_RETRY_CYCLES = 150;
static constexpr uint64_t LD_KILLED_GC_CYCLES = 2;
static inline bool is_amo_lr_uop(const MicroOp &uop) {
  return ((uop.dbg.instruction & 0x7Fu) == 0x2Fu) &&
         ((uop.func7 >> 2) == AmoOp::LR);
}
namespace {
RealLsu *g_deadlock_lsu = nullptr;

const char *ldq_wait_state_name(int64_t cplt_time) {
  if (cplt_time == REQ_WAIT_EXEC) {
    return "WAIT_EXEC";
  }
  if (cplt_time == REQ_WAIT_SEND) {
    return "WAIT_SEND";
  }
  if (cplt_time == REQ_WAIT_RESP) {
    return "WAIT_RESP";
  }
  if (cplt_time == REQ_WAIT_RETRY) {
    return "WAIT_RETRY";
  }
  return "READY_AT";
}

uint8_t ldq_wait_state_code(int64_t cplt_time) {
  if (cplt_time == REQ_WAIT_EXEC) {
    return 0;
  }
  if (cplt_time == REQ_WAIT_SEND) {
    return 1;
  }
  if (cplt_time == REQ_WAIT_RESP) {
    return 2;
  }
  if (cplt_time == REQ_WAIT_RETRY) {
    return 3;
  }
  return 4;
}

void deadlock_dump_lsu_cb() {
  if (g_deadlock_lsu != nullptr) {
    g_deadlock_lsu->dump_debug_state();
  }
}

inline bool stq_entry_matches_uop(const StqEntry &entry, const MicroOp &uop) {
  return entry.valid && entry.rob_idx == uop.rob_idx &&
         entry.rob_flag == uop.rob_flag;
}
} // namespace

RealLsu::RealLsu(SimContext *ctx) : AbstractLsu(ctx) {
  // Initialize MMU
#ifdef CONFIG_TLB_MMU
  mmu = std::make_unique<TlbMmu>(ctx, nullptr, nullptr, DTLB_ENTRIES);
#else
  mmu = std::make_unique<SimpleMmu>(ctx, this);
#endif
  g_deadlock_lsu = this;
  deadlock_debug::register_lsu_dump_cb(deadlock_dump_lsu_cb);

  init();
}

void RealLsu::init() {
  stq_head = 0;
  stq_tail = 0;
  stq_commit = 0;
  stq_count = 0;
  ldq_count = 0;
  ldq_alloc_tail = 0;
  stq_head_flag = false;
  finished_loads.clear();
  finished_sta_reqs.clear();
  pending_sta_addr_reqs.clear();
  pending_mmio_valid = false;
  pending_mmio_req = {};
  mmu->flush();

  reserve_valid = false;
  reserve_addr = 0;

  replay_type = 0;
  replay_count_ldq = 0;
  replay_count_stq = 0;

  mshr_replay_count_ldq = 0;
  mshr_replay_count_stq = 0;
  ldq_seq_counter = 0;
  stq_seq_counter = 0;

  memset(issued_stq_addr, 0, sizeof(issued_stq_addr));
  memset(issued_stq_addr_nxt, 0, sizeof(issued_stq_addr));
  memset(issued_stq_addr_valid, 0, sizeof(issued_stq_addr_valid));
  memset(issued_stq_addr_valid_nxt, 0, sizeof(issued_stq_addr_valid_nxt));
  memset(ldq_trace_seq, 0, sizeof(ldq_trace_seq));
  memset(stq_trace_seq, 0, sizeof(stq_trace_seq));
  memset(ldq_cache_wait_replay, 0, sizeof(ldq_cache_wait_replay));
  memset(stq_cache_wait_replay, 0, sizeof(stq_cache_wait_replay));

  // 初始化所有 STQ LDQ 条目，防止未初始化内存导致的破坏
  for (int i = 0; i < STQ_SIZE; i++) {
    stq[i].valid = false;
    stq[i].addr_valid = false;
    stq[i].data_valid = false;
    stq[i].committed = false;
    stq[i].done = false;
    stq[i].is_mmio = false;
    stq[i].send = false;
    stq[i].replay = 0;
    stq[i].addr = 0;
    stq[i].data = 0;
    stq[i].br_mask = 0;
    stq[i].rob_idx = 0;
    stq[i].rob_flag = 0;
    stq[i].func3 = 0;
    stq[i].is_mmio = false;
  }

  for (int i = 0; i < LDQ_SIZE; i++) {
    ldq[i].valid = false;
    ldq[i].killed = false;
    ldq[i].sent = false;
    ldq[i].waiting_resp = false;
    ldq[i].wait_resp_since = 0;
    ldq[i].tlb_retry = false;
    ldq[i].is_mmio_wait = false;
    ldq[i].uop = {};
    ldq[i].replay_priority = 0;
  }
}

// =========================================================
// 1. Dispatch 阶段: STQ 分配反馈
// =========================================================

void RealLsu::comb_lsu2dis_info() {
  const bool stq_tail_flag =
      (stq_count == STQ_SIZE || (stq_count > 0 && stq_tail < stq_head))
          ? !stq_head_flag
          : stq_head_flag;
  out.lsu2dis->stq_tail = this->stq_tail;
  out.lsu2dis->stq_tail_flag = stq_tail_flag;
  out.lsu2dis->stq_free = STQ_SIZE - this->stq_count;
  out.lsu2dis->ldq_free = LDQ_SIZE - this->ldq_count;

  for (auto &v : out.lsu2dis->ldq_alloc_idx) {
    v = -1;
  }
  for (auto &v : out.lsu2dis->ldq_alloc_valid) {
    v = false;
  }
  int scan_pos = ldq_alloc_tail;
  int produced = 0;
  for (int n = 0; n < LDQ_SIZE && produced < MAX_LDQ_DISPATCH_WIDTH; n++) {
    if (!ldq[scan_pos].valid) {
      out.lsu2dis->ldq_alloc_idx[produced] = scan_pos;
      out.lsu2dis->ldq_alloc_valid[produced] = true;
      produced++;
    }
    scan_pos = (scan_pos + 1) % LDQ_SIZE;
  }

  // Populate miss_mask (Phase 4)
  uint64_t mask = 0;
  for (int i = 0; i < LDQ_SIZE; i++) {
    const auto &entry = ldq[i];
    if (entry.valid && !entry.killed && entry.uop.tma.is_cache_miss) {
      mask |= (1ULL << entry.uop.rob_idx);
    }
  }
  out.lsu2rob->tma.miss_mask = mask;
  out.lsu2rob->committed_store_pending = has_committed_store_pending();
}

// =========================================================
// 2. Execute 阶段: 接收 AGU/SDU 请求 (多端口轮询)
// =========================================================
void RealLsu::comb_recv() {
  // 顶层当前采用直接变量赋值连线；这里每拍将端口硬连到 MMU。
  mmu->set_ptw_mem_port(ptw_mem_port);
  mmu->set_ptw_walk_port(ptw_walk_port);
  peripheral_io.in = {};
  PeripheralInIO mmio_req = {};
  bool mmio_req_used = false;

  if (pending_mmio_valid) {
    if (peripheral_io.out.ready) {
      peripheral_io.in = pending_mmio_req;
      // MMIO bridge is ready: hand off exactly once, then let inflight
      // tracking rely on LDQ/STQ waiting_resp/send state instead of
      // re-driving the same pending request forever.
      pending_mmio_valid = false;
      pending_mmio_req = {};
    } else {
      // Keep the request pending until the bridge becomes ready.
    }
    mmio_req_used = true;
  } else if (!peripheral_io.out.ready) {
    // Bridge is still busy with a previously accepted MMIO transaction.
    mmio_req_used = true;
  }

  //   Assert(out.dcache_req != nullptr && "out.dcache_req is not connected");
  //   Assert(out.dcache_wreq != nullptr && "out.dcache_wreq is not connected");
  //   Assert(in.dcache_resp != nullptr && "in.dcache_resp is not connected");
  //   Assert(in.dcache_wready != nullptr && "in.dcache_wready is not
  //   connected"); *out.dcache_req = {}; *out.dcache_wreq = {};

  // Retry STA address translations that previously returned MMU::RETRY.
  progress_pending_sta_addr();

  // 1. 优先级：Store Data (来自 SDU)
  // 确保在消费者检查之前数据就绪
  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    if (in.exe2lsu->sdu_req[i].valid) {
      handle_store_data(in.exe2lsu->sdu_req[i].uop.to_micro_op());
    }
  }

  // 2. 优先级：Store Addr (来自 AGU)
  // 确保地址对于别名检查有效
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_STA) {
        handle_store_addr(uop.to_micro_op());
      }
    }
  }

  // 3. 优先级：Loads (来自 AGU)
  // 最后处理 Load，使其能看到本周期最新的 Store (STLF)
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_LOAD) {
        handle_load_req(uop.to_micro_op());
      }
    }
  }

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    out.lsu2dcache->req_ports.load_ports[i].valid = false;
  }
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    out.lsu2dcache->req_ports.store_ports[i].valid = false;
  }

  // Lost-response recovery:
  // 1) non-killed load waits too long -> retry
  // 2) killed load waits too long -> drop the slot to avoid LDQ leak/deadlock
  for (int i = 0; i < LDQ_SIZE; i++) {
    auto &entry = ldq[i];
    if (!entry.valid || !entry.sent || !entry.waiting_resp) {
      continue;
    }
    if (entry.uop.cplt_time != REQ_WAIT_RESP) {
      continue;
    }
    if (sim_time < 0) {
      continue;
    }
    const uint64_t sim_time_u64 = static_cast<uint64_t>(sim_time);
    if (sim_time_u64 < entry.wait_resp_since) {
      continue;
    }
    const uint64_t wait_cycles = sim_time_u64 - entry.wait_resp_since;
    if (entry.killed) {
      if (wait_cycles >= LD_KILLED_GC_CYCLES) {
        LSU_MEM_DBG_PRINTF(
            "[LSU][KILLED LDQ GC] cyc=%lld ldq=%d rob=%u flag=%u pc=0x%08x "
            "paddr=0x%08x inst_idx=%lld wait=%llu\n",
            (long long)sim_time, i, (unsigned)entry.uop.rob_idx,
            (unsigned)entry.uop.rob_flag, entry.uop.dbg.pc, entry.uop.diag_val,
            (long long)entry.uop.dbg.inst_idx, (unsigned long long)wait_cycles);
        entry.sent = false;
        entry.waiting_resp = false;
        entry.wait_resp_since = 0;
        entry.uop.cplt_time = sim_time;
        entry.replay_priority = 0;
      }
      continue;
    }
    if (is_mmio_addr(entry.uop.diag_val)) {
      continue;
    }
    if (wait_cycles >= LD_RESP_STUCK_RETRY_CYCLES) {
      if (ctx != nullptr) {
        ctx->perf.ld_resp_timeout_retry_count++;
      }
      LSU_MEM_DBG_PRINTF("[LSU][LD RESP TIMEOUT] cyc=%lld ldq=%d rob=%u "
                         "pc=0x%08x paddr=0x%08x wait=%llu -> retry\n",
                         (long long)sim_time, i, (unsigned)entry.uop.rob_idx,
                         entry.uop.dbg.pc, entry.uop.diag_val,
                         (unsigned long long)wait_cycles);
      entry.sent = false;
      entry.waiting_resp = false;
      entry.wait_resp_since = 0;
      entry.uop.cplt_time = REQ_WAIT_SEND;
      entry.replay_priority = 3;
    }
  }

  replay_count_ldq = 0;
  replay_count_stq = 0;

  // Rebuild MSHR replay pressure from queue state every cycle.
  // This avoids stale counters after flush/mispredict/recovery paths.
  mshr_replay_count_ldq = 0;
  mshr_replay_count_stq = 0;
  for (int i = 0; i < LDQ_SIZE; i++) {
    const auto &e = ldq[i];
    if (!e.valid || e.killed || e.sent || e.waiting_resp) {
      continue;
    }
    if (e.replay_priority == 1) {
      mshr_replay_count_ldq++;
    }
  }
  for (int i = 0; i < STQ_SIZE; i++) {
    const auto &e = stq[i];
    if (!e.valid || !e.addr_valid || !e.data_valid || !e.committed || e.done ||
        e.send) {
      continue;
    }
    if (e.replay == 1) {
      mshr_replay_count_stq++;
    }
  }

  if (mshr_replay_count_stq > REPLAY_STORE_COUNT_UPPER_BOUND &&
      replay_type == 0) {
    replay_type = 1;
  } else if (mshr_replay_count_stq < REPLAY_STORE_COUNT_LOWER_BOUND &&
             replay_type == 1) {
    replay_type = 0;
  }

  if (mshr_replay_count_ldq == 0)
    replay_type = 1;

  bool has_replay = false;
  auto has_mmio_inflight = [&]() {
    if (pending_mmio_valid) {
      return true;
    }
    for (int idx = 0; idx < LDQ_SIZE; idx++) {
      const auto &e = ldq[idx];
      if (e.valid && e.waiting_resp && !e.killed && !e.is_mmio_wait &&
          is_mmio_addr(e.uop.diag_val)) {
        return true;
      }
    }
    for (int idx = 0; idx < STQ_SIZE; idx++) {
      const auto &e = stq[idx];
      if (e.valid && e.is_mmio && e.send && !e.done) {
        return true;
      }
    }
    return false;
  };

  const bool fill_wakeup = in.dcache2lsu->resp_ports.replay_resp.replay;
  const bool mshr_has_free =
      (in.dcache2lsu->resp_ports.replay_resp.free_slots > 0);

  if (fill_wakeup || mshr_has_free) {
    if (fill_wakeup) {
      deadlock_replay_trace::record(
          DeadlockReplayTraceKind::LsuBroadcast, 0,
          static_cast<uint8_t>(fill_wakeup), 0,
          static_cast<uint8_t>(replay_type), 0, 0, 0,
          static_cast<uint32_t>(
              in.dcache2lsu->resp_ports.replay_resp.replay_addr),
          static_cast<uint32_t>(
              in.dcache2lsu->resp_ports.replay_resp.free_slots),
          0);
    }
    for (int i = 0; i < LDQ_SIZE; i++) {
      auto &entry = ldq[i];
      if (!entry.valid || entry.killed || entry.sent || entry.waiting_resp) {
        continue;
      }
      if (fill_wakeup && entry.replay_priority == 2) {
        const bool line_match =
            cache_line_match(entry.uop.diag_val,
                             in.dcache2lsu->resp_ports.replay_resp.replay_addr);
        deadlock_replay_trace::record(
            DeadlockReplayTraceKind::LsuLdqCheck, static_cast<uint16_t>(i),
            static_cast<uint8_t>(entry.replay_priority),
            ldq_wait_state_code(entry.uop.cplt_time),
            static_cast<uint8_t>(line_match),
            static_cast<uint8_t>(entry.waiting_resp), entry.uop.dbg.pc,
            entry.uop.diag_val,
            static_cast<uint32_t>(
                in.dcache2lsu->resp_ports.replay_resp.replay_addr),
            static_cast<uint32_t>(entry.uop.rob_idx),
            static_cast<uint32_t>(entry.uop.dbg.inst_idx));
      }
      if (fill_wakeup && entry.replay_priority == 2 &&
          cache_line_match(entry.uop.diag_val,
                           in.dcache2lsu->resp_ports.replay_resp.replay_addr)) {
        deadlock_replay_trace::record(
            DeadlockReplayTraceKind::LsuLdqWake, static_cast<uint16_t>(i),
            static_cast<uint8_t>(entry.replay_priority),
            ldq_wait_state_code(entry.uop.cplt_time), 1, 0, entry.uop.dbg.pc,
            entry.uop.diag_val,
            static_cast<uint32_t>(
                in.dcache2lsu->resp_ports.replay_resp.replay_addr),
            static_cast<uint32_t>(entry.uop.rob_idx),
            static_cast<uint32_t>(entry.uop.dbg.inst_idx));
        entry.replay_priority = 5;
      }
      if (mshr_has_free && entry.replay_priority == 1 && replay_type == 0 &&
          !has_replay) {
        LSU_MEM_DBG_PRINTF("[LSU] Load replay triggered for LDQ entry %d (ROB "
                           "idx %u) mshr_replay_count_ldq:%d "
                           "mshr_replay_count_stq:%d replay_type=%d\n",
                           i, (unsigned)entry.uop.rob_idx,
                           mshr_replay_count_ldq, mshr_replay_count_stq,
                           replay_type);
        entry.replay_priority = 4;
        has_replay = true;
      }
    }
    for (int i = 0; i < STQ_SIZE; i++) {
      auto &entry = stq[(stq_head + i) % STQ_SIZE];
      if (!entry.valid || !entry.addr_valid || !entry.data_valid ||
          !entry.committed || entry.done || entry.send) {
        continue;
      }
      if (fill_wakeup && entry.replay == 2 &&
          cache_line_match(entry.p_addr,
                           in.dcache2lsu->resp_ports.replay_resp.replay_addr)) {
        entry.replay = 0;
      }
      if (mshr_has_free && entry.replay == 1 && replay_type == 1 &&
          !has_replay) {
        LSU_MEM_DBG_PRINTF("[LSU] Store replay triggered for STQ entry %d (ROB "
                           "idx %u) mshr_replay_count_ldq:%d "
                           "mshr_replay_count_stq:%d replay_type=%d\n",
                           (stq_head + i) % STQ_SIZE, (unsigned)entry.rob_idx,
                           mshr_replay_count_ldq, mshr_replay_count_stq,
                           replay_type);
        entry.replay = 0;
        has_replay = true;
      } // 可能有问题
    }
  }

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    int max_idx = -1;
    int max_priority = -1;
    int best_age = ROB_NUM + 1;
    for (int j = 0; j < LDQ_SIZE; j++) {
      auto &entry = ldq[j];
      if (!entry.valid || entry.killed || entry.sent || entry.waiting_resp) {
        continue;
      }
      // Only issue loads whose address translation / forwarding stage has
      // finished and explicitly marked them ready to send.
      if (entry.uop.cplt_time != REQ_WAIT_SEND) {
        continue;
      }
      // replay=1(mshr_full) and replay=2(mshr_hit) both wait for explicit
      // wakeup from MSHR fill events.
      if (entry.replay_priority == 1 || entry.replay_priority == 2) {
        continue;
      }
      // MMIO load 必须等到成为 ROB 当前最老的未提交指令后才发送，
      // 并且这条 load 之前的 store 已经从 STQ 排空，这样前面更老指令的
      // 提交副作用（尤其是 MMIO store）一定已经生效。
      if (entry.is_mmio_wait) {
        const bool mmio_can_issue =
            !mmio_req_used && !has_mmio_inflight() && in.rob_bcast->head_valid &&
            !has_older_store_pending(entry.uop) &&
            entry.uop.rob_idx == (uint32_t)in.rob_bcast->head_rob_idx &&
            peripheral_io.out.ready;
        if (!mmio_can_issue) {
          if (ctx != nullptr) {
            ctx->perf.mmio_head_block_cycles++;
          }
          continue;
        }
        // MMIO response path expects uop.rob_idx to carry an LDQ-local
        // token, not architectural ROB index.
        MicroOp mmio_uop = entry.uop;
        mmio_uop.rob_idx = j;
        mmio_req.is_mmio = 1;
        mmio_req.wen = 0; // Load 没有写使能
        mmio_req.mmio_addr = entry.uop.diag_val;
        mmio_req.mmio_wdata = 0; // Load 没有写数据
        mmio_req.uop = mmio_uop;
        LSU_MEM_DBG_PRINTF("[LSU][MMIO][LD ISSUE] cyc=%lld ldq=%d rob=%u "
                           "paddr=0x%08x func3=0x%x\n",
                           (long long)sim_time, j, (unsigned)entry.uop.rob_idx,
                           entry.uop.diag_val, (unsigned)entry.uop.func3);
        mmio_req_used = true;
        pending_mmio_valid = true;
        pending_mmio_req = mmio_req;
        entry.is_mmio_wait = false; // 已发出请求，重置等待标志
        entry.sent = true;
        entry.waiting_resp = true;
        entry.wait_resp_since = sim_time;
        entry.uop.cplt_time = REQ_WAIT_RESP;
        if (ctx != nullptr) {
          ctx->perf.trace_load_on_issue(ldq_trace_seq[j], sim_time);
        }
        break;
        // 这里直接调用外设接口，绕过正常的 Cache 请求流程
        // 以确保 MMIO 访问的原子性和顺序性
        // 注意：外设接口需要自行处理好与 ROB 的交互，确保在 MMIO load 到达 ROB
        // 队头时能正确响应并触发指令完成

        // 已到达 ROB 队头，允许发出
      }
      int rob_age = 0;
      if (in.rob_bcast->head_valid) {
        rob_age = (static_cast<int>(entry.uop.rob_idx) -
                   static_cast<int>(in.rob_bcast->head_rob_idx) + ROB_NUM) %
                  ROB_NUM;
      }
      if (entry.replay_priority > max_priority ||
          (entry.replay_priority == max_priority && rob_age < best_age)) {
        max_priority = entry.replay_priority;
        max_idx = j;
        best_age = rob_age;
      }
    }
    if (max_idx != -1) {
      MicroOp req_uop = ldq[max_idx].uop;
      req_uop.rob_idx = max_idx; // Local token: LDQ index
      out.lsu2dcache->req_ports.load_ports[i].valid = true;
      out.lsu2dcache->req_ports.load_ports[i].addr = ldq[max_idx].uop.diag_val;
      out.lsu2dcache->req_ports.load_ports[i].req_id = max_idx;
      out.lsu2dcache->req_ports.load_ports[i].uop = req_uop;
      // if (is_coremark_focus_addr(ldq[max_idx].uop.diag_val) ||
      //     is_coremark_focus_load_pc(ldq[max_idx].uop.dbg.pc))
      // {
      //     std::printf("[FOCUS][LSU][LD ISSUE] cyc=%lld port=%d ldq=%d
      //     req_id=%d rob=%u pc=0x%08x paddr=0x%08x func3=0x%x
      //     replay_pri=%u\n",
      //                 (long long)sim_time, i, max_idx, max_idx,
      //                 (unsigned)ldq[max_idx].uop.rob_idx,
      //                 ldq[max_idx].uop.dbg.pc, ldq[max_idx].uop.diag_val,
      //                 ldq[max_idx].uop.func3,
      //                 (unsigned)ldq[max_idx].replay_priority);
      // }
      ldq[max_idx].sent = true;
      ldq[max_idx].waiting_resp = true;
      ldq[max_idx].wait_resp_since = sim_time;
      ldq[max_idx].uop.cplt_time = REQ_WAIT_RESP;
      if (ctx != nullptr) {
        ctx->perf.trace_load_on_issue(ldq_trace_seq[max_idx], sim_time);
      }
      if (ldq[max_idx].replay_priority >= 4) {
        // replay_priority=4: replay=1(mshr_full) wakeup by free-slot.
        // replay_priority=5: replay=2(mshr_hit) wakeup by fill-match.
        ldq[max_idx].replay_priority = 0;
      }
    }
  }

  // Per cycle, each STA port can issue at most one real store request.
  // Scan STQ from head and pick the oldest issuable entries.
  int issued_sta = 0;
  memset(
      issued_stq_addr_valid_nxt, 0,
      sizeof(issued_stq_addr_valid_nxt)); // Clear next-cycle issued addresses
  for (int i = 0; i < stq_count && issued_sta < LSU_STA_COUNT; i++) {
    int stq_idx = (stq_head + i) % STQ_SIZE;
    auto &entry = stq[stq_idx];

    // Respect store ordering: younger stores cannot bypass an older
    // store whose addr/data/commit are not ready yet.
    if (!entry.valid || !entry.addr_valid || !entry.data_valid ||
        !entry.committed) {
      break;
    }

    if (entry.suppress_write) {
      continue;
    }
    // LSU_MEM_DBG_PRINTF("[STQ SCAN] cyc=%lld stq_head=%d stq_idx=%d
    // addr_valid=%d data_valid=%d committed=%d done=%d send=%d replay=%d
    // addr=0x%08x wdata=0x%08x\n",
    //             (long long)sim_time, stq_head, stq_idx, entry.addr_valid,
    //             entry.data_valid, entry.committed, entry.done, entry.send,
    //             entry.replay, entry.p_addr, entry.data);
    if (entry.done || entry.send || entry.replay) {
      continue;
    }
    // for(int j=0;j<LSU_STA_COUNT;j++){
    //     LSU_MEM_DBG_PRINTF("[STQ CHECK] cyc=%lld port=%d stq_idx=%d
    //     issued_stq_addr[%d]=0x%08x issued_stq_addr_valid[%d]=%d
    //     entry.addr=0x%08x\n",
    //                 (long long)sim_time, j, stq_idx, j, issued_stq_addr[j],
    //                 j, issued_stq_addr_valid[j], entry.addr);
    // }
    bool continue_flag = false;
    // for(int j=0;j<LSU_STA_COUNT;j++){
    //     if(issued_stq_addr[j] == entry.addr&&issued_stq_addr_valid[j]==1){
    //         continue_flag=true;
    //         break;
    //     }
    // }
    // if(continue_flag){
    //     continue;
    // }
    for (int j = 0; j < i; j++) {
      int older_stq_idx = (stq_head + j) % STQ_SIZE;
      auto &older_entry = stq[older_stq_idx];
      if (!older_entry.valid || !older_entry.addr_valid ||
          !older_entry.data_valid || !older_entry.committed ||
          older_entry.done || older_entry.suppress_write) {
        continue;
      }
      // Preserve program order for same-address stores until the older
      // store is fully acknowledged. Otherwise a bank-conflict replay can
      // let an older store reissue after a younger one and overwrite the
      // newer value.
      if (older_entry.p_addr == entry.p_addr) {
        continue_flag = true;
        break;
      }
    }
    if (continue_flag) {
      if (ctx != nullptr) {
        ctx->perf.stq_same_addr_block_count++;
      }
      continue;
    }
    if (entry.is_mmio) {
      if (mmio_req_used) {
        continue;
      }
      if (has_mmio_inflight()) {
        continue;
      }
      // MMIO store only needs STQ ordering. Once it is the oldest
      // committed/ready store reachable from stq_head, it can issue
      // even if the ROB head has already advanced past it.
      if (!peripheral_io.out.ready) {
        continue;
      }
      mmio_req.is_mmio = 1;
      mmio_req.wen = 1; // Store 有写使能
      mmio_req.mmio_addr = entry.p_addr;
      mmio_req.mmio_wdata = entry.data;
      mmio_req.uop = {};
      mmio_req.uop.op = UOP_STA;
      // MMIO response path uses uop.rob_idx as STQ slot token.
      mmio_req.uop.rob_idx = stq_idx;
      mmio_req.uop.func3 = entry.func3;
      LSU_MEM_DBG_PRINTF("[LSU][MMIO][ST ISSUE] cyc=%lld stq=%d rob=%u "
                         "paddr=0x%08x data=0x%08x func3=0x%x\n",
                         (long long)sim_time, stq_idx, (unsigned)entry.rob_idx,
                         entry.p_addr, entry.data, (unsigned)entry.func3);
      mmio_req_used = true;
      pending_mmio_valid = true;
      pending_mmio_req = mmio_req;
      entry.send = true;
      if (ctx != nullptr) {
        ctx->perf.trace_store_on_issue(stq_trace_seq[stq_idx], sim_time);
      }
      issued_sta++;
      continue;
    }
    issued_stq_addr_nxt[issued_sta] = entry.addr;
    issued_stq_addr_valid_nxt[issued_sta] = 1;
    change_store_info(entry, issued_sta, stq_idx);
    entry.send = true; // Mark only when the request is truly driven.
    if (ctx != nullptr) {
      ctx->perf.trace_store_on_issue(stq_trace_seq[stq_idx], sim_time);
    }
    issued_sta++;
  }

  // MMIO request is sent only in the cycle-begin pending path above.
  // Do not re-drive here, otherwise a newly enqueued pending request can be
  // sent twice (once at end of this cycle, once again next cycle).
}

// =========================================================
// 3. Writeback 阶段: 输出 Load 结果 (多端口写回)
// =========================================================
void RealLsu::comb_load_res() {
  // 1. 先清空所有写回端口
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.lsu2exe->wb_req[i].valid = false;
  }

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    if (in.dcache2lsu->resp_ports.load_resps[i].valid) {
      int idx =
          static_cast<int>(in.dcache2lsu->resp_ports.load_resps[i].req_id);
      if (idx >= 0 && idx < LDQ_SIZE) {
        auto &entry = ldq[idx];
        if (entry.valid && entry.sent && entry.waiting_resp) {
          const auto &resp_uop = in.dcache2lsu->resp_ports.load_resps[i].uop;
          const bool same_token =
              (entry.uop.dbg.inst_idx == resp_uop.dbg.inst_idx) &&
              (entry.uop.rob_flag == resp_uop.rob_flag);
          if (!same_token) {
            if (ctx != nullptr) {
              ctx->perf.ld_resp_stale_drop_count++;
            }
            LSU_MEM_DBG_PRINTF(
                "[LSU][LD RESP STALE] cyc=%lld port=%d ldq=%d replay=%u "
                "resp_inst=%lld cur_inst=%lld resp_flag=%u cur_flag=%u "
                "resp_pc=0x%08x cur_pc=0x%08x\n",
                (long long)sim_time, i, idx,
                (unsigned)in.dcache2lsu->resp_ports.load_resps[i].replay,
                (long long)resp_uop.dbg.inst_idx,
                (long long)entry.uop.dbg.inst_idx, (unsigned)resp_uop.rob_flag,
                (unsigned)entry.uop.rob_flag, resp_uop.dbg.pc,
                entry.uop.dbg.pc);
            continue;
          }
          if (!entry.killed) {
            if (in.dcache2lsu->resp_ports.load_resps[i].replay == 0) {
              uint32_t raw_data = in.dcache2lsu->resp_ports.load_resps[i].data;
              uint32_t extracted =
                  extract_data(raw_data, entry.uop.diag_val, entry.uop.func3);
              // if (is_coremark_focus_addr(entry.uop.diag_val) ||
              //     is_coremark_focus_load_pc(entry.uop.dbg.pc))
              // {
              //     std::printf("[FOCUS][LSU][LD WB] cyc=%lld port=%d ldq=%d
              //     req_id=%zu rob=%u pc=0x%08x paddr=0x%08x func3=0x%x
              //     raw=0x%08x result=0x%08x\n",
              //                 (long long)sim_time, i, idx,
              //                 in.dcache2lsu->resp_ports.load_resps[i].req_id,
              //                 (unsigned)entry.uop.rob_idx, entry.uop.dbg.pc,
              //                 entry.uop.diag_val, entry.uop.func3, raw_data,
              //                 extracted);
              // }
              if (is_amo_lr_uop(entry.uop)) {
                reserve_addr = entry.uop.diag_val;
                reserve_valid = true;
              }
              entry.uop.result = extracted;
              entry.uop.dbg.difftest_skip =
                  in.dcache2lsu->resp_ports.load_resps[i].uop.dbg.difftest_skip;
              entry.uop.cplt_time = sim_time;
              entry.uop.tma.is_cache_miss =
                  !in.dcache2lsu->resp_ports.load_resps[i]
                       .uop.tma.is_cache_miss;
              entry.replay_priority = 0;
              if (ctx != nullptr) {
                ctx->perf.trace_load_on_result(ldq_trace_seq[idx], sim_time);
                ctx->perf.trace_load_set_dcache_hit(
                    ldq_trace_seq[idx], !ldq_cache_wait_replay[idx]);
              }
              finished_loads.push_back(entry.uop);
              free_ldq_entry(idx);
            } else {
              // Handle load replay if needed (e.g., due to MSHR eviction)
              const uint8_t replay_code =
                  in.dcache2lsu->resp_ports.load_resps[i].replay;
              const bool line_match_cur_bcast =
                  in.dcache2lsu->resp_ports.replay_resp.replay &&
                  cache_line_match(
                      entry.uop.diag_val,
                      in.dcache2lsu->resp_ports.replay_resp.replay_addr);
              deadlock_replay_trace::record(
                  DeadlockReplayTraceKind::LsuLoadReplayLatch,
                  static_cast<uint16_t>(idx), static_cast<uint8_t>(replay_code),
                  ldq_wait_state_code(entry.uop.cplt_time),
                  static_cast<uint8_t>(
                      in.dcache2lsu->resp_ports.replay_resp.replay),
                  static_cast<uint8_t>(line_match_cur_bcast), entry.uop.dbg.pc,
                  entry.uop.diag_val,
                  static_cast<uint32_t>(
                      in.dcache2lsu->resp_ports.replay_resp.replay_addr),
                  static_cast<uint32_t>(
                      in.dcache2lsu->resp_ports.replay_resp.free_slots),
                  static_cast<uint32_t>(entry.uop.rob_idx));
              entry.replay_priority =
                  in.dcache2lsu->resp_ports.load_resps[i].replay;
              if (entry.replay_priority == 1 || entry.replay_priority == 2) {
                ldq_cache_wait_replay[idx] = true;
              }
              // replay=1(resource full) waits for a free-slot wakeup.
              // replay=2(mshr_hit) waits for matching line fill wakeup.
              entry.sent = false;
              entry.waiting_resp = false;
              entry.wait_resp_since = 0;
              entry.uop.cplt_time = REQ_WAIT_SEND;
            }
          } else {
            free_ldq_entry(idx);
          }
        }
      } else {
        Assert(false && "Invalid LDQ index in load response");
      }
    }
  }
  if (peripheral_io.out.is_mmio && peripheral_io.out.uop.op == UOP_LOAD) {
    int idx = peripheral_io.out.uop.rob_idx;
    if (idx >= 0 && idx < LDQ_SIZE) {
      auto &entry = ldq[idx];
      if (entry.valid && entry.sent && entry.waiting_resp) {
        if (!entry.killed) {
          entry.uop.result = peripheral_io.out.mmio_rdata;
          entry.uop.dbg.difftest_skip = peripheral_io.out.uop.dbg.difftest_skip;
          entry.uop.cplt_time = sim_time;
          entry.uop.tma.is_cache_miss = false; // MMIO 访问不算 Cache Miss
          LSU_MEM_DBG_PRINTF(
              "[LSU][MMIO][LD RESP] cyc=%lld ldq=%d rob=%u data=0x%08x\n",
              (long long)sim_time, idx, (unsigned)entry.uop.rob_idx,
              entry.uop.result);
          if (ctx != nullptr) {
            ctx->perf.trace_load_on_result(ldq_trace_seq[idx], sim_time);
          }
          finished_loads.push_back(entry.uop);
        }
      }
      free_ldq_entry(idx);
    } else {
      Assert(false && "Invalid LDQ index in MMIO load response");
    }
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.dcache2lsu->resp_ports.store_resps[i].valid) {
      int stq_idx = in.dcache2lsu->resp_ports.store_resps[i].req_id;
      if (stq_idx >= 0 && stq_idx < STQ_SIZE) {
        auto &entry = stq[stq_idx];
        if (entry.valid && !entry.done && entry.send) {
          if (in.dcache2lsu->resp_ports.store_resps[i].replay == 0) {
            entry.done = true;
            entry.replay = 0;
            entry.send = false;
            if (ctx != nullptr) {
              ctx->perf.trace_store_on_result(stq_trace_seq[stq_idx], sim_time);
              ctx->perf.trace_store_set_dcache_hit(
                  stq_trace_seq[stq_idx], !stq_cache_wait_replay[stq_idx]);
            }
          } else {
            // Handle store replay if needed (e.g., due to MSHR eviction)
            uint8_t replay_code =
                in.dcache2lsu->resp_ports.store_resps[i].replay;
            if (replay_code == 1 || replay_code == 2) {
              stq_cache_wait_replay[stq_idx] = true;
            }
            // replay=3 is bank-conflict: it should be retried directly
            // on the next cycle and must not freeze the STQ head.
            entry.replay = (replay_code == 3) ? 0 : replay_code;
            entry.send = false; // 重置发送标志，等待下次发送
          }
        }
      } else {
        Assert(false && "Invalid STQ index in store response");
      }
    }
  }

  if (peripheral_io.out.is_mmio && peripheral_io.out.uop.op == UOP_STA) {
    int stq_idx = peripheral_io.out.uop.rob_idx;
    if (stq_idx >= 0 && stq_idx < STQ_SIZE) {
      auto &entry = stq[stq_idx];
      if (entry.valid && !entry.done && entry.send) {
        entry.done = true;
        entry.send = false;
        if (ctx != nullptr) {
          ctx->perf.trace_store_on_result(stq_trace_seq[stq_idx], sim_time);
        }
        LSU_MEM_DBG_PRINTF("[LSU][MMIO][ST RESP] cyc=%lld stq=%d rob=%u "
                           "paddr=0x%08x data=0x%08x\n",
                           (long long)sim_time, stq_idx,
                           (unsigned)entry.rob_idx, entry.p_addr, entry.data);
      }
    } else {
      Assert(false && "Invalid STQ index in MMIO store response");
    }
  }

  // 2. 从完成队列填充端口 (Load)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (!finished_loads.empty()) {
      out.lsu2exe->wb_req[i].valid = true;
      out.lsu2exe->wb_req[i].uop =
          LsuExeIO::LsuExeRespUop::from_micro_op(finished_loads.front());

      finished_loads.pop_front();
    } else {
      break;
    }
  }

  // 3. 从完成队列填充端口 (STA)
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (!finished_sta_reqs.empty()) {
      out.lsu2exe->sta_wb_req[i].valid = true;
      out.lsu2exe->sta_wb_req[i].uop =
          LsuExeIO::LsuExeRespUop::from_micro_op(finished_sta_reqs.front());
      finished_sta_reqs.pop_front();
    } else {
      out.lsu2exe->sta_wb_req[i].valid = false;
    }
  }
}

void RealLsu::handle_load_req(const MicroOp &inst) {
  int ldq_idx = inst.ldq_idx;
  Assert(ldq_idx >= 0 && ldq_idx < LDQ_SIZE);
  if (!ldq[ldq_idx].valid || ldq[ldq_idx].killed) {
    return;
  }

  if (ctx != nullptr) {
    ctx->perf.trace_load_set_inst_idx(ldq_trace_seq[ldq_idx],
                                      inst.dbg.inst_idx);
  }

  MicroOp task = inst;
  task.tma.is_cache_miss = false; // Initialize to false
  uint32_t p_addr;
  auto mmu_ret = mmu->translate(p_addr, task.result, 1, in.csr_status);

  if (mmu_ret == AbstractMmu::Result::RETRY) {
    task.cplt_time = REQ_WAIT_EXEC;
    ldq[ldq_idx].tlb_retry = true;
    ldq[ldq_idx].uop = task;
    return;
  }

  if (mmu_ret == AbstractMmu::Result::FAULT) {
    task.page_fault_load = true;
    task.diag_val = task.result; // Store faulting virtual address
    task.cplt_time = sim_time + 1;
  } else {
    task.diag_val = p_addr;

    // [Fix] Disable Store-to-Load Forwarding for MMIO ranges
    // These addresses involve side effects and must read from consistent memory
    bool is_mmio = is_mmio_addr(p_addr);
    if (ctx != nullptr) {
      ctx->perf.trace_load_set_mmio(ldq_trace_seq[ldq_idx], is_mmio);
    }
    if (is_mmio && ctx != nullptr) {
      ctx->perf.mmio_inst_count++;
      ctx->perf.mmio_load_count++;
    }
    // task.flush_pipe = is_mmio;
    ldq[ldq_idx].is_mmio_wait = is_mmio; // 延迟发送：等待到达 ROB 队头后再发出
    auto fwd_res =
        is_mmio ? StoreForwardResult{} : check_store_forward(p_addr, inst);

    if (fwd_res.state == StoreForwardState::Hit) {
      if (!is_mmio && ctx != nullptr) {
        ctx->perf.trace_load_set_stlf(ldq_trace_seq[ldq_idx], true);
      }
      task.result = fwd_res.data;
      task.cplt_time = sim_time + 0; // 这一拍直接完成！
    } else if (fwd_res.state == StoreForwardState::NoHit) {
      if (!is_mmio && ctx != nullptr) {
        ctx->perf.trace_load_set_stlf(ldq_trace_seq[ldq_idx], false);
      }
      task.cplt_time = REQ_WAIT_SEND;
    } else {
      if (!is_mmio && ctx != nullptr) {
        ctx->perf.trace_load_set_stlf(ldq_trace_seq[ldq_idx], false);
      }
      task.cplt_time = REQ_WAIT_RETRY;
    }
  }

  ldq[ldq_idx].tlb_retry = false;
  ldq[ldq_idx].uop = task;
}

void RealLsu::handle_store_addr(const MicroOp &inst) {
  Assert(inst.stq_idx >= 0 && inst.stq_idx < STQ_SIZE);
  if (stq_entry_matches_uop(stq[inst.stq_idx], inst) && ctx != nullptr) {
    ctx->perf.trace_store_set_inst_idx(stq_trace_seq[inst.stq_idx],
                                       inst.dbg.inst_idx);
  }
  if (!finish_store_addr_once(inst)) {
    pending_sta_addr_reqs.push_back(inst);
  }
}

void RealLsu::handle_store_data(const MicroOp &inst) {
  Assert(inst.stq_idx >= 0 && inst.stq_idx < STQ_SIZE);
  if (!stq_entry_matches_uop(stq[inst.stq_idx], inst)) {
    const auto &entry = stq[inst.stq_idx];
    LSU_MEM_DBG_FPRINTF(
        stderr,
        "[LSU][STD_MISMATCH] cyc=%lld inst_idx=%lld pc=0x%08x stq_idx=%u "
        "stq_flag=%u rob=%u flag=%u result=0x%08x func3=0x%x\n",
        (long long)sim_time, (long long)inst.dbg.inst_idx,
        (uint32_t)inst.dbg.pc, (unsigned)inst.stq_idx, (unsigned)inst.stq_flag,
        (unsigned)inst.rob_idx, (unsigned)inst.rob_flag, (uint32_t)inst.result,
        (unsigned)inst.func3);
    LSU_MEM_DBG_FPRINTF(stderr,
                        "[LSU][STD_MISMATCH][STQ %02u] valid=%d addr_v=%d "
                        "data_v=%d committed=%d "
                        "done=%d send=%d replay=%u rob=%u flag=%u func3=0x%x "
                        "p_addr=0x%08x data=0x%08x\n",
                        (unsigned)inst.stq_idx, (int)entry.valid,
                        (int)entry.addr_valid, (int)entry.data_valid,
                        (int)entry.committed, (int)entry.done, (int)entry.send,
                        (unsigned)entry.replay, (unsigned)entry.rob_idx,
                        (unsigned)entry.rob_flag, (unsigned)entry.func3,
                        (uint32_t)entry.p_addr, (uint32_t)entry.data);
    dump_recent_stq_alloc_traces();
    std::fflush(stderr);
    return;
  }
  if (ctx != nullptr) {
    ctx->perf.trace_store_set_inst_idx(stq_trace_seq[inst.stq_idx],
                                       inst.dbg.inst_idx);
  }
  stq[inst.stq_idx].data = inst.result;
  stq[inst.stq_idx].data_valid = true;
}

bool RealLsu::reserve_stq_entry(mask_t br_mask, uint32_t rob_idx,
                                uint32_t rob_flag, uint32_t func3) {
  if (stq_count >= STQ_SIZE) {
    return false;
  }
  const int alloc_idx = stq_tail;
  stq[alloc_idx].valid = true;
  stq[alloc_idx].addr_valid = false;
  stq[alloc_idx].data_valid = false;
  stq[alloc_idx].committed = false;
  stq[alloc_idx].done = false;
  stq[alloc_idx].is_mmio = false;
  stq[alloc_idx].send = false;
  stq[alloc_idx].replay = 0;
  stq[alloc_idx].addr = 0;
  stq[alloc_idx].data = 0;
  stq[alloc_idx].suppress_write = 0;
  stq[alloc_idx].br_mask = br_mask;
  stq[alloc_idx].rob_idx = rob_idx;
  stq[alloc_idx].rob_flag = rob_flag;
  stq[alloc_idx].func3 = func3;
  stq_seq_counter++;
  stq_trace_seq[alloc_idx] = stq_seq_counter;
  stq_cache_wait_replay[alloc_idx] = false;
  if (ctx != nullptr) {
    ctx->perf.trace_store_on_stq_enter(stq_trace_seq[alloc_idx], sim_time);
  }
  record_stq_alloc_trace(alloc_idx, rob_idx, rob_flag, func3, br_mask);
  stq_tail = (stq_tail + 1) % STQ_SIZE;
  return true;
}

void RealLsu::record_stq_alloc_trace(int stq_idx, uint32_t rob_idx,
                                     uint32_t rob_flag, uint32_t func3,
                                     mask_t br_mask) {
  auto &trace = recent_stq_allocs[recent_stq_alloc_cursor];
  trace.valid = true;
  trace.cycle = static_cast<uint64_t>(sim_time);
  trace.stq_idx = stq_idx;
  trace.rob_idx = rob_idx;
  trace.rob_flag = rob_flag;
  trace.func3 = func3;
  trace.br_mask = br_mask;
  trace.seq = stq_trace_seq[stq_idx];
  recent_stq_alloc_cursor =
      (recent_stq_alloc_cursor + 1) % STQ_ALLOC_TRACE_DEPTH;
}

void RealLsu::dump_recent_stq_alloc_traces() const {
  LSU_MEM_DBG_FPRINTF(stderr, "[LSU][STQ_ALLOC_TRACE] recent allocations:\n");
  for (int n = 0; n < STQ_ALLOC_TRACE_DEPTH; n++) {
    const int idx = (recent_stq_alloc_cursor - 1 - n + STQ_ALLOC_TRACE_DEPTH) %
                    STQ_ALLOC_TRACE_DEPTH;
    const auto &trace = recent_stq_allocs[idx];
    if (!trace.valid) {
      continue;
    }
    LSU_MEM_DBG_FPRINTF(stderr,
                        "[LSU][STQ_ALLOC_TRACE][%02d] cyc=%llu stq_idx=%d "
                        "seq=%llu rob=%u flag=%u "
                        "func3=0x%x br_mask=0x%08x\n",
                        n, (unsigned long long)trace.cycle, trace.stq_idx,
                        (unsigned long long)trace.seq, (unsigned)trace.rob_idx,
                        (unsigned)trace.rob_flag, (unsigned)trace.func3,
                        (unsigned)trace.br_mask);
  }
}

void RealLsu::consume_stq_alloc_reqs(int &push_count) {
  for (int i = 0; i < MAX_STQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->alloc_req[i]) {
      continue;
    }
    bool ok = reserve_stq_entry(in.dis2lsu->br_mask[i], in.dis2lsu->rob_idx[i],
                                in.dis2lsu->rob_flag[i], in.dis2lsu->func3[i]);
    Assert(ok && "STQ allocate overflow");
    push_count++;
  }
}

bool RealLsu::reserve_ldq_entry(int idx, mask_t br_mask, uint32_t rob_idx,
                                uint32_t rob_flag) {
  Assert(idx >= 0 && idx < LDQ_SIZE);
  if (ldq[idx].valid) {
    return false;
  }
  ldq[idx].valid = true;
  ldq[idx].killed = false;
  ldq[idx].sent = false;
  ldq[idx].waiting_resp = false;
  ldq[idx].wait_resp_since = 0;
  ldq[idx].tlb_retry = false;
  ldq[idx].is_mmio_wait = false;
  ldq[idx].replay_priority = 0;
  ldq[idx].uop = {};
  ldq[idx].uop.br_mask = br_mask;
  ldq[idx].uop.rob_idx = rob_idx;
  ldq[idx].uop.rob_flag = rob_flag;
  ldq[idx].uop.ldq_idx = idx;
  ldq[idx].uop.cplt_time = REQ_WAIT_EXEC;
  ldq_seq_counter++;
  ldq_trace_seq[idx] = ldq_seq_counter;
  ldq_cache_wait_replay[idx] = false;
  if (ctx != nullptr) {
    ctx->perf.trace_load_on_ldq_enter(ldq_trace_seq[idx], sim_time);
  }
  ldq_count++;
  ldq_alloc_tail = (idx + 1) % LDQ_SIZE;
  return true;
}

void RealLsu::consume_ldq_alloc_reqs() {
  for (int i = 0; i < MAX_LDQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->ldq_alloc_req[i]) {
      continue;
    }
    bool ok = reserve_ldq_entry(
        in.dis2lsu->ldq_idx[i], in.dis2lsu->ldq_br_mask[i],
        in.dis2lsu->ldq_rob_idx[i], in.dis2lsu->ldq_rob_flag[i]);
    Assert(ok && "LDQ allocate collision");
  }
}

bool RealLsu::is_mmio_addr(uint32_t paddr) const {
  return ((paddr & UART_ADDR_MASK) == UART_ADDR_BASE) ||
         ((paddr & PLIC_ADDR_MASK) == PLIC_ADDR_BASE) ||
         ((paddr & CLINT_ADDR_MASK) == CLINT_ADDR_BASE) ||
         (paddr == OPENSBI_TIMER_LOW_ADDR) ||
         (paddr == OPENSBI_TIMER_HIGH_ADDR);
}
void RealLsu::change_store_info(StqEntry &head, int port, int store_index) {

  uint32_t alignment_mask = (head.func3 & 0x3) == 0   ? 0
                            : (head.func3 & 0x3) == 1 ? 1
                                                      : 3;
  Assert((head.p_addr & alignment_mask) == 0 &&
         "DUT: Store address misaligned at commit!");

  uint32_t byte_off = head.p_addr & 0x3;
  uint32_t wstrb = 0;
  uint32_t wdata = 0;
  switch (head.func3 & 0x3) {
  case 0:
    wstrb = (1u << byte_off);
    wdata = (head.data & 0xFFu) << (byte_off * 8);
    break;
  case 1:
    wstrb = (0x3u << byte_off);
    wdata = (head.data & 0xFFFFu) << (byte_off * 8);
    break;
  default:
    wstrb = 0xFu;
    wdata = head.data;
    break;
  }

  out.lsu2dcache->req_ports.store_ports[port].valid = true;
  out.lsu2dcache->req_ports.store_ports[port].addr = head.p_addr;
  out.lsu2dcache->req_ports.store_ports[port].strb = wstrb;
  out.lsu2dcache->req_ports.store_ports[port].data = wdata;
  out.lsu2dcache->req_ports.store_ports[port].uop = head;
  out.lsu2dcache->req_ports.store_ports[port].req_id = store_index;
  // if (is_coremark_focus_addr(head.p_addr))
  // {
  //     std::printf("[FOCUS][LSU][ST ISSUE] cyc=%lld port=%d stq=%d rob=%u
  //     paddr=0x%08x func3=0x%x data=0x%08x wdata=0x%08x wstrb=0x%x\n",
  //                 (long long)sim_time, port, store_index,
  //                 (unsigned)head.rob_idx, head.p_addr, head.func3, head.data,
  //                 wdata, wstrb);
  // }
}

void RealLsu::handle_global_flush() {
  const int active_count = count_active_stq_entries();
  const int committed_count = count_committed_stq_prefix();
  const int discard_count = active_count - committed_count;

  stq_tail = stq_commit;
  stq_count = committed_count;
  clear_stq_entries(stq_tail, discard_count);
  pending_sta_addr_reqs.clear();
  pending_mmio_valid = false;
  pending_mmio_req = {};
  reserve_addr = 0;
  reserve_valid = false;
}

void RealLsu::handle_mispred(mask_t mask) {
  auto is_killed = [&](const MicroOp &u) { return (u.br_mask & mask) != 0; };

  for (int i = 0; i < LDQ_SIZE; i++) {
    if (!ldq[i].valid) {
      continue;
    }
    if (is_killed(ldq[i].uop)) {
      if (ldq[i].sent) {

        ldq[i].killed = true;
      } else {

        free_ldq_entry(i);
      }
    }
  }

  auto it_sta = finished_sta_reqs.begin();
  while (it_sta != finished_sta_reqs.end()) {
    if (is_killed(*it_sta)) {
      it_sta = finished_sta_reqs.erase(it_sta);
    } else {
      ++it_sta;
    }
  }

  auto it_finished = finished_loads.begin();
  while (it_finished != finished_loads.end()) {
    if (is_killed(*it_finished)) {
      it_finished = finished_loads.erase(it_finished);
    } else {
      ++it_finished;
    }
  }

  auto it_sta_retry = pending_sta_addr_reqs.begin();
  while (it_sta_retry != pending_sta_addr_reqs.end()) {
    if (is_killed(*it_sta_retry)) {
      it_sta_retry = pending_sta_addr_reqs.erase(it_sta_retry);
    } else {
      ++it_sta_retry;
    }
  }

  if (pending_mmio_valid && (pending_mmio_req.uop.br_mask & mask) != 0) {
    pending_mmio_valid = false;
    pending_mmio_req = {};
  }

  int recovery_tail = find_recovery_tail(mask);
  if (recovery_tail == -1) {
    return;
  }

  const int active_count = count_active_stq_entries();
  stq_tail = recovery_tail;
  stq_count = count_stq_entries_until(recovery_tail);
  clear_stq_entries(stq_tail, active_count - stq_count);
}
void RealLsu::retire_stq_head_if_ready(int &pop_count) {
  const int retire_idx = stq_head;
  StqEntry &head = stq[retire_idx];
  const uint32_t retired_suppress_write = head.suppress_write;

  if (!head.valid) {
    return;
  }

  if (!head.suppress_write) {
    if (!(head.valid && head.addr_valid && head.data_valid && head.committed)) {
      return;
    }
    if (!head.done) {
      return;
    }
  }

  // Normal store: comb 阶段已完成写握手
  // Suppressed store: 跳过写握手直接 retire
  if (ctx != nullptr) {
    ctx->perf.trace_store_on_stq_exit(stq_trace_seq[retire_idx], sim_time);
  }
  head.valid = false;
  head.committed = false;
  head.addr_valid = false;
  head.data_valid = false;
  head.done = false;
  head.addr = 0;
  head.data = 0;
  head.br_mask = 0;
  head.send = false;
  head.replay = 0;
  // Keep the suppression tag on an invalid entry until the slot is reused so
  // commit-time difftest can still recognize a failed SC that intentionally
  // does not perform a memory write.
  head.suppress_write = retired_suppress_write;
  stq_trace_seq[retire_idx] = 0;
  stq_cache_wait_replay[retire_idx] = false;

  stq_head++;
  if (stq_head == STQ_SIZE) {
    stq_head = 0;
    stq_head_flag = !stq_head_flag;
  }
  pop_count++;
}

// void RealLsu::retire_stq_head_if_ready(int &pop_count)
// {
//     StqEntry &head = stq[stq_head];
//     if (!(head.valid && head.addr_valid && head.data_valid && head.committed
//     &&head.suppress_write))
//     {
//         return;
//     }
//     if (!(head.send && head.done))
//     {
//         return;
//     }

//     // Store write handshake succeeded in comb stage.
//     head.valid = false;
//     head.committed = false;
//     head.addr_valid = false;
//     head.data_valid = false;
//     head.done = false;
//     head.addr = 0;
//     head.data = 0;
//     head.br_mask = 0;
//     head.send = false;
//     head.replay = 0;
//     head.suppress_write = 0;

//     stq_head = (stq_head + 1) % STQ_SIZE;
//     pop_count++;
// }

void RealLsu::commit_stores_from_rob() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (!in.rob_commit->commit_entry[i].valid) {
      continue;
    }
    const auto &commit_uop = in.rob_commit->commit_entry[i].uop;
    if (ctx != nullptr && is_load(commit_uop)) {
      ctx->perf.trace_load_on_rob_exit(commit_uop.dbg.inst_idx, sim_time);
    }
    if (!is_store(commit_uop)) {
      continue;
    }
    int idx = commit_uop.stq_idx;
    Assert(idx >= 0 && idx < STQ_SIZE);
    if (idx == stq_commit) {
      stq[idx].committed = true;
      stq_commit = (stq_commit + 1) % STQ_SIZE;
    } else {
      Assert(0 && "Store commit out of order?");
    }
  }
}

void RealLsu::progress_ldq_entries() {
  for (int i = 0; i < LDQ_SIZE; i++) {
    auto &entry = ldq[i];
    if (!entry.valid) {
      continue;
    }
    if (entry.killed && !entry.sent) {

      free_ldq_entry(i);
      continue;
    }

    if (entry.waiting_resp || entry.uop.cplt_time == REQ_WAIT_EXEC) {
      if (!entry.tlb_retry) {
        continue;
      }
      uint32_t p_addr = 0;
      auto mmu_ret = mmu->translate(p_addr, entry.uop.result, 1, in.csr_status);
      if (mmu_ret == AbstractMmu::Result::RETRY) {
        continue;
      }
      entry.tlb_retry = false;
      if (mmu_ret == AbstractMmu::Result::FAULT) {
        entry.uop.page_fault_load = true;
        entry.uop.diag_val = entry.uop.result;
        entry.uop.cplt_time = sim_time + 1;
      } else {
        entry.uop.diag_val = p_addr;
        bool is_mmio = is_mmio_addr(p_addr);
        if (ctx != nullptr) {
          ctx->perf.trace_load_set_mmio(ldq_trace_seq[i], is_mmio);
        }
        if (is_mmio && ctx != nullptr) {
          ctx->perf.mmio_inst_count++;
          ctx->perf.mmio_load_count++;
        }
        entry.uop.flush_pipe =
            false; // MMIO no longer triggers flush (non-speculative is enough)
        entry.is_mmio_wait = is_mmio; // 延迟发送：等待到达 ROB 队头后再发出
        auto fwd_res = is_mmio ? StoreForwardResult{}
                               : check_store_forward(p_addr, entry.uop);
        if (fwd_res.state == StoreForwardState::Hit) {
          if (!is_mmio && ctx != nullptr) {
            ctx->perf.trace_load_set_stlf(ldq_trace_seq[i], true);
          }
          entry.uop.result = fwd_res.data;
          entry.uop.cplt_time = sim_time;
        } else if (fwd_res.state == StoreForwardState::NoHit) {
          if (!is_mmio && ctx != nullptr) {
            ctx->perf.trace_load_set_stlf(ldq_trace_seq[i], false);
          }
          entry.uop.cplt_time = REQ_WAIT_SEND;
        } else {
          if (!is_mmio && ctx != nullptr) {
            ctx->perf.trace_load_set_stlf(ldq_trace_seq[i], false);
          }
          entry.uop.cplt_time = REQ_WAIT_RETRY;
        }
      }
      continue;
    }

    if (entry.uop.cplt_time == REQ_WAIT_RETRY) {
      auto fwd_res = check_store_forward(entry.uop.diag_val, entry.uop);
      if (fwd_res.state == StoreForwardState::Hit) {
        if (ctx != nullptr) {
          ctx->perf.trace_load_set_stlf(ldq_trace_seq[i], true);
        }
        entry.uop.result = fwd_res.data;
        entry.uop.cplt_time = sim_time;
      } else if (fwd_res.state == StoreForwardState::NoHit) {
        if (ctx != nullptr) {
          ctx->perf.trace_load_set_stlf(ldq_trace_seq[i], false);
        }
        entry.uop.cplt_time = REQ_WAIT_SEND;
      }
    }

    if (entry.uop.cplt_time <= sim_time) {
      if (!entry.killed) {
        if (is_amo_lr_uop(entry.uop)) {
          reserve_valid = true;
          reserve_addr = entry.uop.diag_val;
        }
        if (ctx != nullptr) {
          ctx->perf.trace_load_on_result(ldq_trace_seq[i], sim_time);
        }
        finished_loads.push_back(entry.uop);
      }
      free_ldq_entry(i);
    }
  }
}

bool RealLsu::finish_store_addr_once(const MicroOp &inst) {
  int idx = inst.stq_idx;
  Assert(idx >= 0 && idx < STQ_SIZE);
  if (!stq_entry_matches_uop(stq[idx], inst)) {
    return true;
  }
  stq[idx].addr = inst.result; // VA

  uint32_t pa = inst.result;
  auto mmu_ret = mmu->translate(pa, inst.result, 2, in.csr_status);
  if (mmu_ret == AbstractMmu::Result::RETRY) {
    return false;
  }

  if (mmu_ret == AbstractMmu::Result::FAULT) {
    MicroOp fault_op = inst;
    fault_op.page_fault_store = true;
    fault_op.cplt_time = sim_time;
    if (is_amo_sc_uop(inst)) {
      reserve_valid = false;
    }
    finished_sta_reqs.push_back(fault_op);
    stq[idx].p_addr = pa;
    stq[idx].addr_valid = false;
    return true;
  }

  MicroOp success_op = inst;
  success_op.cplt_time = sim_time;
  if (is_amo_sc_uop(inst)) {
    bool sc_success = reserve_valid && (reserve_addr == pa);
    // SC clears reservation regardless of success/failure.
    reserve_valid = false;
    success_op.result = sc_success ? 0 : 1;
    success_op.dest_en = true;
    success_op.op =
        UOP_LOAD; // Reuse existing LSU load wb/awake path for SC result
    stq[idx].suppress_write = !sc_success;
    finished_loads.push_back(success_op);
    stq[idx].is_mmio = false; // SC 结果不区分 MMIO，始终走正常内存路径
    stq[idx].p_addr = pa;
    stq[idx].addr_valid = true;
    return true;
  }
  bool is_mmio = is_mmio_addr(pa);
  if (is_mmio && ctx != nullptr) {
    ctx->perf.mmio_inst_count++;
    ctx->perf.mmio_store_count++;
  }
  // MMIO store must not trigger ROB flush at STA writeback. Otherwise ROB may
  // flush globally before LSU consumes rob_commit, dropping the STQ commit.
  success_op.flush_pipe = false;
  stq[idx].is_mmio = is_mmio;
  finished_sta_reqs.push_back(success_op);
  stq[idx].p_addr = pa;
  stq[idx].addr_valid = true;
  return true;
}

void RealLsu::progress_pending_sta_addr() {
  if (pending_sta_addr_reqs.empty()) {
    return;
  }
  size_t n = pending_sta_addr_reqs.size();
  for (size_t i = 0; i < n; i++) {
    MicroOp op = pending_sta_addr_reqs.front();
    pending_sta_addr_reqs.pop_front();
    if (!finish_store_addr_once(op)) {
      pending_sta_addr_reqs.push_back(op);
    }
  }
}

void RealLsu::free_ldq_entry(int idx) {
  Assert(idx >= 0 && idx < LDQ_SIZE);
  if (ldq[idx].valid) {
    if (ctx != nullptr) {
      ctx->perf.trace_load_on_ldq_exit(ldq_trace_seq[idx], sim_time);
    }

    ldq[idx].valid = false;
    ldq[idx].killed = false;
    ldq[idx].sent = false;
    ldq[idx].waiting_resp = false;
    ldq[idx].wait_resp_since = 0;
    ldq[idx].tlb_retry = false;
    ldq[idx].is_mmio_wait = false;
    ldq[idx].uop = {};
    ldq_trace_seq[idx] = 0;
    ldq_cache_wait_replay[idx] = false;
    ldq_count--;
    Assert(ldq_count >= 0);
  }
}

// =========================================================
// 5. Exception: Flush 处理
// =========================================================

void RealLsu::comb_flush() {
  if (in.rob_bcast->flush) {
    // 1. LDQ: 已发请求项标记 killed，未发请求项直接释放
    for (int i = 0; i < LDQ_SIZE; i++) {
      if (!ldq[i].valid) {
        continue;
      }
      if (ldq[i].sent) {

        ldq[i].killed = true;
      } else {

        free_ldq_entry(i);
      }
    }
    finished_loads.clear();
    finished_sta_reqs.clear();
    pending_sta_addr_reqs.clear();
  }
}

// =========================================================
// 6. Sequential Logic: 状态更新与时序模拟
// =========================================================
void RealLsu::seq() {
  bool is_flush = in.rob_bcast->flush;
  bool is_mispred = in.dec_bcast->mispred;
  int push_count = 0;
  int pop_count = 0;

  if (is_flush) {
    mmu->flush();
    handle_global_flush();
    return;
  }

  if (is_mispred) {
    mmu->flush();
    handle_mispred(in.dec_bcast->br_mask);
  }

  // 清除已解析分支的 br_mask bit（在 flush 之后，只影响存活条目）
  mask_t clear = in.dec_bcast->clear_mask;
  if (clear) {
    for (int i = 0; i < LDQ_SIZE; i++) {
      if (ldq[i].valid)
        ldq[i].uop.br_mask &= ~clear;
    }
    for (int i = 0; i < STQ_SIZE; i++) {
      if (stq[i].valid)
        stq[i].br_mask &= ~clear;
    }
    for (auto &e : finished_sta_reqs)
      e.br_mask &= ~clear;
    for (auto &e : finished_loads)
      e.br_mask &= ~clear;
    for (auto &e : pending_sta_addr_reqs)
      e.br_mask &= ~clear;
  }

  if (is_mispred) {
    return;
  }

  if (in.rob_bcast->fence) {
    mmu->flush();
  }

  consume_stq_alloc_reqs(push_count);
  consume_ldq_alloc_reqs();
  commit_stores_from_rob();

  // Make newly allocated stores visible before forwarding checks.
  stq_count = stq_count + push_count;
  if (stq_count > STQ_SIZE) {
    Assert(0 && "STQ Count Overflow! logic bug!");
  }
  progress_ldq_entries();

  // Retire after load progress so same-cycle completed stores can still
  // participate in store-to-load forwarding.
  retire_stq_head_if_ready(pop_count);
  if (pop_count > stq_count) {
    const int scan_active = count_active_stq_entries();
    const int scan_committed = count_committed_stq_prefix();
    LSU_MEM_DBG_PRINTF("[LSU][STQ UNDERFLOW PRECHECK] cyc=%lld pop=%d "
                       "stq_count=%d scan_active=%d scan_committed=%d head=%d "
                       "commit=%d tail=%d head_flag=%d\n",
                       (long long)sim_time, pop_count, stq_count, scan_active,
                       scan_committed, stq_head, stq_commit, stq_tail,
                       static_cast<int>(stq_head_flag));
  }
  stq_count = stq_count - pop_count;
  if (stq_count < 0) {
    Assert(0 && "STQ Count Underflow! logic bug!");
  }

  memcpy(issued_stq_addr, issued_stq_addr_nxt, sizeof(issued_stq_addr));
  memcpy(issued_stq_addr_valid, issued_stq_addr_valid_nxt,
         sizeof(issued_stq_addr_valid));

#if LSU_LIGHT_ASSERT
  if (pop_count == 0) {
    const StqEntry &head = stq[stq_head];
    const bool head_ready_to_retire = head.valid && head.addr_valid &&
                                      head.data_valid && head.committed &&
                                      head.done;
    Assert(!head_ready_to_retire &&
           "STQ invariant: retire-ready head was not popped");
  }
  // Lightweight O(1) ring invariants for STQ pointers/count.
  const int head_to_tail = (stq_tail - stq_head + STQ_SIZE) % STQ_SIZE;
  const int head_to_commit = (stq_commit - stq_head + STQ_SIZE) % STQ_SIZE;
  if (stq_count == 0) {
    Assert(stq_head == stq_tail && stq_tail == stq_commit &&
           "STQ invariant: empty queue pointer mismatch");
  } else if (stq_count == STQ_SIZE) {
    Assert(stq_head == stq_tail &&
           "STQ invariant: full queue requires head == tail");
  } else {
    Assert(head_to_tail == stq_count &&
           "STQ invariant: count != distance(head, tail)");
  }
  Assert(head_to_commit <= stq_count &&
         "STQ invariant: commit pointer is outside active window");
#endif
}

// =========================================================
// 辅助：基于 Tag 查找新的 Tail
// =========================================================
int RealLsu::find_recovery_tail(mask_t br_mask) {
  // 从 Commit 指针（安全点）开始，向 Tail 扫描
  // 我们要找的是“第一个”被误预测影响的指令
  // 因为是顺序分配，一旦找到一个，后面（更年轻）的肯定也都要丢弃

  int ptr = stq_commit;

  // 修正：正确计算未提交指令数，处理队列已满的情况 (Tail == Commit)
  // stq_count 追踪总有效条目 (Head -> Tail)。
  // Head -> Commit 之间的条目已提交。
  // Commit -> Tail 之间的条目未提交。
  int committed_count = count_committed_stq_prefix();
  int active_count = count_active_stq_entries();
  int uncommitted_count = active_count - committed_count;

  // 安全检查
  if (uncommitted_count < 0)
    uncommitted_count = 0; // 不应该发生
  int count = uncommitted_count;

  for (int i = 0; i < count; i++) {
    // 检查当前条目是否依赖于被误预测的分支
    if (stq[ptr].valid && (stq[ptr].br_mask & br_mask)) {
      // 找到了！这个位置就是错误路径的开始
      // 新的 Tail 应该回滚到这里
      return ptr;
    }
    ptr = (ptr + 1) % STQ_SIZE;
  }

  // 扫描完所有未提交指令都没找到相关依赖 -> 不需要回滚
  return -1;
}

int RealLsu::count_active_stq_entries() const {
  int count = 0;
  int ptr = stq_head;
  while (count < STQ_SIZE && stq[ptr].valid) {
    count++;
    ptr = (ptr + 1) % STQ_SIZE;
  }
  return count;
}

int RealLsu::count_committed_stq_prefix() const {
  int count = 0;
  int ptr = stq_head;
  while (count < STQ_SIZE && stq[ptr].valid && stq[ptr].committed) {
    count++;
    ptr = (ptr + 1) % STQ_SIZE;
  }
  return count;
}

int RealLsu::count_stq_entries_until(int stop_idx) const {
  int count = 0;
  int ptr = stq_head;
  while (count < STQ_SIZE && ptr != stop_idx && stq[ptr].valid) {
    count++;
    ptr = (ptr + 1) % STQ_SIZE;
  }
  return count;
}

void RealLsu::clear_stq_entries(int start_idx, int count) {
  int ptr = start_idx;
  for (int i = 0; i < count; i++) {
    stq[ptr].valid = false;
    stq[ptr].addr_valid = false;
    stq[ptr].data_valid = false;
    stq[ptr].committed = false;
    stq[ptr].done = false;
    stq[ptr].is_mmio = false;
    stq[ptr].send = false;
    stq[ptr].replay = 0;
    stq[ptr].addr = 0;
    stq[ptr].data = 0;
    stq[ptr].suppress_write = 0;
    stq[ptr].br_mask = 0;
    stq[ptr].rob_idx = 0;
    stq[ptr].rob_flag = 0;
    stq[ptr].func3 = 0;
    stq_trace_seq[ptr] = 0;
    stq_cache_wait_replay[ptr] = false;
    ptr = (ptr + 1) % STQ_SIZE;
  }
}

bool RealLsu::is_store_older(int s_idx, int s_flag, int l_idx, int l_flag) {
  if (s_flag == l_flag) {
    return s_idx < l_idx;
  } else {
    return s_idx > l_idx;
  }
}

bool RealLsu::has_older_store_pending(const MicroOp &load_uop) const {
  int ptr_idx = stq_head;
  bool ptr_flag = stq_head_flag;
  const int stop_idx = load_uop.stq_idx;
  const bool stop_flag = load_uop.stq_flag;
  int guard = 0;

  while (!(ptr_idx == stop_idx && ptr_flag == stop_flag)) {
    guard++;
    Assert(guard <= STQ_SIZE + 1);

    const StqEntry &entry = stq[ptr_idx];
    if (entry.valid && !entry.suppress_write) {
      return true;
    }

    ptr_idx++;
    if (ptr_idx == STQ_SIZE) {
      ptr_idx = 0;
      ptr_flag = !ptr_flag;
    }
  }

  return false;
}

// =========================================================
// 🛡️ [Nanako Implementation] 完整的 STLF 模拟逻辑
// =========================================================

RealLsu::StoreForwardResult
RealLsu::check_store_forward(uint32_t p_addr, const MicroOp &load_uop) {
  uint32_t current_word = 0;
  bool hit_any = false;
  int ptr_idx = stq_head;
  bool ptr_flag = stq_head_flag;
  const int stop_idx = load_uop.stq_idx;
  const bool stop_flag = load_uop.stq_flag;
  int guard = 0;

  // Scan [head, load.stop) in ring age order, where stop is (idx,flag).
  while (!(ptr_idx == stop_idx && ptr_flag == stop_flag)) {
    guard++;
    Assert(guard <= STQ_SIZE + 1);

    StqEntry &entry = stq[ptr_idx];
    if (entry.valid && !entry.suppress_write) {
      if (!entry.addr_valid) {
        return {StoreForwardState::Retry, 0};
      }

      int store_width = get_mem_width(entry.func3);
      int load_width = get_mem_width(load_uop.func3);
      uint32_t s_start = entry.p_addr;
      uint32_t s_end = s_start + store_width;
      uint32_t l_start = p_addr;
      uint32_t l_end = l_start + load_width;
      uint32_t overlap_start = std::max(s_start, l_start);
      uint32_t overlap_end = std::min(s_end, l_end);

      if (s_start <= l_start && s_end >= l_end) {
        // Store fully covers load bytes; merge by byte-lane so
        // sb/sh at non-zero byte offsets can still forward correctly.
        hit_any = true;
        if (!entry.data_valid) {
          return {StoreForwardState::Retry, 0};
        }
        current_word = merge_data_to_word(current_word, entry.data,
                                          entry.p_addr, entry.func3);
      } else if (overlap_start < overlap_end) {
        hit_any = true;
        // Partial overlap is intentionally conservative: keep the load in
        // retry until the older store fully retires from STQ.
        return {StoreForwardState::Retry, 0};
      }
    }

    ptr_idx++;
    if (ptr_idx == STQ_SIZE) {
      ptr_idx = 0;
      ptr_flag = !ptr_flag;
    }
  }

  if (!hit_any) {
    return {StoreForwardState::NoHit, 0};
  }
  return {StoreForwardState::Hit,
          extract_data(current_word, p_addr, load_uop.func3)};
}
StqEntry RealLsu::get_stq_entry(int stq_idx) {
  Assert(stq_idx >= 0 && stq_idx < STQ_SIZE);
  return stq[stq_idx];
}

uint32_t RealLsu::coherent_read(uint32_t p_addr) {
  // 1. 基准值：读物理内存 (假设 p_addr 已对齐到 4)
  Assert(0 && "coherent_read should not be called in current design!");
  uint32_t data = pmem_read(p_addr);

  // 2. 遍历 STQ 进行覆盖 (Coherent Check)
  int ptr = stq_head;
  int count = stq_count;
  for (int i = 0; i < count; i++) {
    const auto &entry = stq[ptr];
    if (entry.valid && entry.addr_valid && !entry.suppress_write) {
      // 只要 Store 的 Word 地址匹配，就进行 merge (假设 aligned Store 不跨
      // Word)
      if ((entry.p_addr >> 2) == (p_addr >> 2)) {
        data = merge_data_to_word(data, entry.data, entry.p_addr, entry.func3);
      }
    }
    ptr = (ptr + 1) % STQ_SIZE;
  }

  return data;
}

void RealLsu::dump_debug_state() const {
  const bool stq_tail_flag =
      (stq_count == STQ_SIZE || (stq_count > 0 && stq_tail < stq_head))
          ? !stq_head_flag
          : stq_head_flag;
  std::printf(
      "[DEADLOCK][LSU] stq_head=%d stq_head_flag=%d stq_commit=%d stq_tail=%d "
      "stq_tail_flag=%d stq_count=%d ldq_count=%d ldq_alloc_tail=%d "
      "pending_sta_addr=%zu finished_ld=%zu finished_sta=%zu replay_type=%d "
      "replay_ldq=%d replay_stq=%d mshr_replay_ldq=%d mshr_replay_stq=%d\n",
      stq_head, (int)stq_head_flag, stq_commit, stq_tail, (int)stq_tail_flag,
      stq_count, ldq_count, ldq_alloc_tail, pending_sta_addr_reqs.size(),
      finished_loads.size(), finished_sta_reqs.size(), (int)replay_type,
      replay_count_ldq, replay_count_stq, mshr_replay_count_ldq,
      mshr_replay_count_stq);
  for (int i = 0; i < STQ_SIZE; i++) {
    const auto &e = stq[i];
    if (!e.valid) {
      continue;
    }
    std::printf(
        "[DEADLOCK][LSU][STQ %02d] valid=%d addr_v=%d data_v=%d committed=%d "
        "done=%d send=%d suppress_write=%d replay=%u mmio=%d p_addr=0x%08x "
        "data=0x%08x func3=0x%x rob=%u flag=%u br_mask=0x%08x\n",
        i, (int)e.valid, (int)e.addr_valid, (int)e.data_valid, (int)e.committed,
        (int)e.done, (int)e.send, (int)e.suppress_write, (unsigned)e.replay,
        (int)e.is_mmio, e.p_addr, e.data, e.func3, (unsigned)e.rob_idx,
        (unsigned)e.rob_flag, (unsigned)e.br_mask);
  }

  for (int i = 0; i < LDQ_SIZE; i++) {
    const auto &e = ldq[i];
    if (!e.valid) {
      continue;
    }
    const char *state_name = ldq_wait_state_name(e.uop.cplt_time);
    const unsigned long long wait_age =
        (e.waiting_resp && sim_time >= 0 &&
         static_cast<uint64_t>(sim_time) >= e.wait_resp_since)
            ? (unsigned long long)(static_cast<uint64_t>(sim_time) -
                                   e.wait_resp_since)
            : 0ull;
    std::printf(
        "[DEADLOCK][LSU][LDQ %02d] valid=%d killed=%d sent=%d waiting=%d "
        "wait_age=%llu tlb_retry=%d mmio_wait=%d replay_pri=%u cplt_state=%s "
        "cplt_time=%lld rob=%u flag=%u pc=0x%08x paddr=0x%08x result=0x%08x "
        "func3=0x%x stq_idx=%u stq_flag=%u inst_idx=%lld\n",
        i, (int)e.valid, (int)e.killed, (int)e.sent, (int)e.waiting_resp,
        wait_age, (int)e.tlb_retry, (int)e.is_mmio_wait,
        (unsigned)e.replay_priority, state_name, (long long)e.uop.cplt_time,
        (unsigned)e.uop.rob_idx, (unsigned)e.uop.rob_flag, e.uop.dbg.pc,
        e.uop.diag_val, e.uop.result, e.uop.func3, (unsigned)e.uop.stq_idx,
        (unsigned)e.uop.stq_flag, (long long)e.uop.dbg.inst_idx);
  }

  std::printf(
      "[DEADLOCK][LSU][RESP] replay=%u replay_addr=0x%08zx free_slots=%u\n",
      (unsigned)in.dcache2lsu->resp_ports.replay_resp.replay,
      in.dcache2lsu->resp_ports.replay_resp.replay_addr,
      (unsigned)in.dcache2lsu->resp_ports.replay_resp.free_slots);
}
