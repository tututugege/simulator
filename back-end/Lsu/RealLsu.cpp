#include "RealLsu.h"
#include "AbstractLsu.h"
#include "DcacheConfig.h"
#include "PhysMemory.h"
#include "RealDcache.h"
#include "SimCpu.h"
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
inline bool stq_entry_matches_uop(const StqEntry &entry, const MicroOp &uop) {
  return entry.valid && entry.rob_idx == uop.rob_idx &&
         entry.rob_flag == uop.rob_flag;
}
} // namespace

RealLsu::RealLsu(SimContext *ctx) : AbstractLsu(ctx) {
  // Initialize MMU
#ifdef CONFIG_TLB_MMU
  mmu = std::make_unique<TlbMmu>(ctx, nullptr, DTLB_ENTRIES);
#else
  mmu = std::make_unique<SimpleMmu>(ctx, this);
#endif
  init();
}

RealLsu::StoreTag RealLsu::make_store_tag(int idx, bool flag) const {
  Assert(idx >= 0 && idx < STQ_SIZE);
  return StoreTag{idx, flag};
}

RealLsu::StoreTag RealLsu::current_stq_head_tag() const {
  return make_store_tag(stq_head, stq_head_flag);
}

bool RealLsu::current_stq_tail_flag() const {
  return (stq_count == STQ_SIZE || (stq_count > 0 && stq_tail < stq_head))
             ? !stq_head_flag
             : stq_head_flag;
}

int RealLsu::encode_store_req_id(const StoreTag &tag) const {
  return tag.idx + (tag.flag ? STQ_SIZE : 0);
}

RealLsu::StoreTag RealLsu::decode_store_req_id(int req_id) const {
  Assert(req_id >= 0 && req_id < 2 * STQ_SIZE);
  return make_store_tag(req_id % STQ_SIZE, req_id >= STQ_SIZE);
}

RealLsu::StoreLocator &RealLsu::store_locator(const StoreTag &tag) {
  return store_loc[tag.flag ? 1 : 0][tag.idx];
}

const RealLsu::StoreLocator &
RealLsu::store_locator(const StoreTag &tag) const {
  return store_loc[tag.flag ? 1 : 0][tag.idx];
}

RealLsu::StoreNode &RealLsu::committed_stq_at(int offset) {
  Assert(offset >= 0 && offset < committed_stq_count);
  return committed_stq[(committed_stq_head + offset) % STQ_SIZE];
}

const RealLsu::StoreNode &RealLsu::committed_stq_at(int offset) const {
  Assert(offset >= 0 && offset < committed_stq_count);
  return committed_stq[(committed_stq_head + offset) % STQ_SIZE];
}

RealLsu::StoreNode &RealLsu::speculative_stq_at(int offset) {
  Assert(offset >= 0 && offset < speculative_stq_count);
  return speculative_stq[(speculative_stq_head + offset) % STQ_SIZE];
}

const RealLsu::StoreNode &RealLsu::speculative_stq_at(int offset) const {
  Assert(offset >= 0 && offset < speculative_stq_count);
  return speculative_stq[(speculative_stq_head + offset) % STQ_SIZE];
}

RealLsu::StoreNode *RealLsu::find_store_node(const StoreTag &tag) {
  const auto &loc = store_locator(tag);
  if (!loc.valid) {
    return nullptr;
  }
  switch (loc.kind) {
  case StoreQueueKind::Committed:
    return &committed_stq[loc.pos];
  case StoreQueueKind::Speculative:
    return &speculative_stq[loc.pos];
  case StoreQueueKind::None:
    break;
  }
  return nullptr;
}

const RealLsu::StoreNode *RealLsu::find_store_node(const StoreTag &tag) const {
  const auto &loc = store_locator(tag);
  if (!loc.valid) {
    return nullptr;
  }
  switch (loc.kind) {
  case StoreQueueKind::Committed:
    return &committed_stq[loc.pos];
  case StoreQueueKind::Speculative:
    return &speculative_stq[loc.pos];
  case StoreQueueKind::None:
    break;
  }
  return nullptr;
}

StqEntry *RealLsu::find_store_entry(const StoreTag &tag) {
  auto *node = find_store_node(tag);
  return node == nullptr ? nullptr : &node->entry;
}

const StqEntry *RealLsu::find_store_entry(const StoreTag &tag) const {
  const auto *node = find_store_node(tag);
  return node == nullptr ? nullptr : &node->entry;
}

void RealLsu::clear_store_node(StoreNode &node) {
  if (node.entry.valid) {
    store_locator(node.tag) = {};
  }
  node.entry.valid = false;
}

void RealLsu::committed_stq_push(const StoreNode &node) {
  Assert(committed_stq_count < STQ_SIZE);
  committed_stq[committed_stq_tail] = node;
  auto &loc = store_locator(node.tag);
  loc.valid = true;
  loc.kind = StoreQueueKind::Committed;
  loc.pos = committed_stq_tail;
  committed_stq_tail = (committed_stq_tail + 1) % STQ_SIZE;
  committed_stq_count++;
}

RealLsu::StoreNode &RealLsu::committed_stq_front() {
  Assert(committed_stq_count > 0);
  return committed_stq[committed_stq_head];
}

const RealLsu::StoreNode &RealLsu::committed_stq_front() const {
  Assert(committed_stq_count > 0);
  return committed_stq[committed_stq_head];
}

void RealLsu::committed_stq_pop() {
  Assert(committed_stq_count > 0);
  auto &node = committed_stq[committed_stq_head];
  clear_store_node(node);
  committed_stq_head = (committed_stq_head + 1) % STQ_SIZE;
  committed_stq_count--;
}

void RealLsu::speculative_stq_push(const StoreNode &node) {
  Assert(speculative_stq_count < STQ_SIZE);
  speculative_stq[speculative_stq_tail] = node;
  auto &loc = store_locator(node.tag);
  loc.valid = true;
  loc.kind = StoreQueueKind::Speculative;
  loc.pos = speculative_stq_tail;
  speculative_stq_tail = (speculative_stq_tail + 1) % STQ_SIZE;
  speculative_stq_count++;
}

RealLsu::StoreNode &RealLsu::speculative_stq_front() {
  Assert(speculative_stq_count > 0);
  return speculative_stq[speculative_stq_head];
}

const RealLsu::StoreNode &RealLsu::speculative_stq_front() const {
  Assert(speculative_stq_count > 0);
  return speculative_stq[speculative_stq_head];
}

void RealLsu::speculative_stq_pop() {
  Assert(speculative_stq_count > 0);
  auto &node = speculative_stq[speculative_stq_head];
  clear_store_node(node);
  speculative_stq_head = (speculative_stq_head + 1) % STQ_SIZE;
  speculative_stq_count--;
}

void RealLsu::move_speculative_front_to_committed() {
  Assert(speculative_stq_count > 0);
  Assert(committed_stq_count < STQ_SIZE);

  auto &src = speculative_stq[speculative_stq_head];
  auto &dst = committed_stq[committed_stq_tail];
  dst = src;
  dst.entry.committed = true;

  auto &loc = store_locator(dst.tag);
  loc.valid = true;
  loc.kind = StoreQueueKind::Committed;
  loc.pos = committed_stq_tail;

  src.entry.valid = false;
  speculative_stq_head = (speculative_stq_head + 1) % STQ_SIZE;
  speculative_stq_count--;
  committed_stq_tail = (committed_stq_tail + 1) % STQ_SIZE;
  committed_stq_count++;
}

void RealLsu::init() {
  stq_head = 0;
  stq_tail = 0;
  stq_count = 0;
  committed_stq_head = 0;
  committed_stq_tail = 0;
  committed_stq_count = 0;
  speculative_stq_head = 0;
  speculative_stq_tail = 0;
  speculative_stq_count = 0;
  ldq_count = 0;
  ldq_alloc_tail = 0;
  stq_head_flag = false;
  finished_loads.clear();
  finished_sta_reqs.clear();
  pending_sta_addr_reqs.clear();
  pending_mmio_valid = false;
  pending_mmio_req = {};
  mmu->flush();
  mmu->seq();

  reserve_valid = false;
  reserve_addr = 0;

  replay_type = 0;
  replay_count_ldq = 0;
  replay_count_stq = 0;

  mshr_replay_count_ldq = 0;
  mshr_replay_count_stq = 0;
  memset(issued_stq_addr, 0, sizeof(issued_stq_addr));
  memset(issued_stq_addr_nxt, 0, sizeof(issued_stq_addr));
  memset(issued_stq_addr_valid, 0, sizeof(issued_stq_addr_valid));
  memset(issued_stq_addr_valid_nxt, 0, sizeof(issued_stq_addr_valid_nxt));

  for (int i = 0; i < STQ_SIZE; i++) {
    committed_stq[i] = {};
    speculative_stq[i] = {};
    store_loc[0][i] = {};
    store_loc[1][i] = {};
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
  const bool stq_tail_flag = current_stq_tail_flag();
  out.lsu2dis->stq_tail = this->stq_tail;
  out.lsu2dis->stq_tail_flag = stq_tail_flag;
  // Leave one full commit row of visible headroom. Without this slack, oracle
  // mode can overfill store-side speculation, which hurts Dhrystone IPC even
  // though the total STQ capacity is technically not exhausted.
  const int visible_stq_free_raw = STQ_SIZE - this->stq_count - COMMIT_WIDTH;
  out.lsu2dis->stq_free = visible_stq_free_raw > 0 ? visible_stq_free_raw : 0;
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
  out.lsu2rob->translation_pending = mmu->translation_pending();
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
  for (int i = 0; i < committed_stq_count; i++) {
    const auto &e = committed_stq_at(i).entry;
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
    for (int i = 0; i < committed_stq_count; i++) {
      const auto &e = committed_stq_at(i).entry;
      if (e.valid && e.is_mmio && e.send && !e.done) {
        return true;
      }
    }
    for (int i = 0; i < speculative_stq_count; i++) {
      const auto &e = speculative_stq_at(i).entry;
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
    for (int i = 0; i < LDQ_SIZE; i++) {
      auto &entry = ldq[i];
      if (!entry.valid || entry.killed || entry.sent || entry.waiting_resp) {
        continue;
      }
      if (fill_wakeup && entry.replay_priority == 2 &&
          cache_line_match(entry.uop.diag_val,
                           in.dcache2lsu->resp_ports.replay_resp.replay_addr)) {
        entry.replay_priority = 5;
      }
      if (mshr_has_free && entry.replay_priority == 1 && replay_type == 0 &&
          !has_replay) {
        entry.replay_priority = 4;
        has_replay = true;
      }
    }
    for (int i = 0; i < committed_stq_count; i++) {
      auto &entry = committed_stq_at(i).entry;
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
        mmio_req_used = true;
        pending_mmio_valid = true;
        pending_mmio_req = mmio_req;
        entry.is_mmio_wait = false; // 已发出请求，重置等待标志
        entry.sent = true;
        entry.waiting_resp = true;
        entry.wait_resp_since = sim_time;
        entry.uop.cplt_time = REQ_WAIT_RESP;
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
      ldq[max_idx].sent = true;
      ldq[max_idx].waiting_resp = true;
      ldq[max_idx].wait_resp_since = sim_time;
      ldq[max_idx].uop.cplt_time = REQ_WAIT_RESP;
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
  for (int i = 0; i < committed_stq_count && issued_sta < LSU_STA_COUNT; i++) {
    auto &node = committed_stq_at(i);
    auto &entry = node.entry;

    // Respect store ordering: younger stores cannot bypass an older
    // store whose addr/data/commit are not ready yet.
    if (!entry.valid || !entry.addr_valid || !entry.data_valid ||
        !entry.committed) {
      break;
    }

    if (entry.suppress_write) {
      continue;
    }
    if (entry.done || entry.send || entry.replay) {
      continue;
    }
    bool continue_flag = false;
    for (int j = 0; j < i; j++) {
      auto &older_entry = committed_stq_at(j).entry;
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
      // MMIO response path uses uop.rob_idx as encoded logical store tag.
      mmio_req.uop.rob_idx = encode_store_req_id(node.tag);
      mmio_req.uop.func3 = entry.func3;
      mmio_req_used = true;
      pending_mmio_valid = true;
      pending_mmio_req = mmio_req;
      entry.send = true;
      issued_sta++;
      continue;
    }
    issued_stq_addr_nxt[issued_sta] = entry.addr;
    issued_stq_addr_valid_nxt[issued_sta] = 1;
    change_store_info(node, issued_sta);
    entry.send = true; // Mark only when the request is truly driven.
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
            continue;
          }
          if (!entry.killed) {
            if (in.dcache2lsu->resp_ports.load_resps[i].replay == 0) {
              uint32_t raw_data = in.dcache2lsu->resp_ports.load_resps[i].data;
              uint32_t extracted =
                  extract_data(raw_data, entry.uop.diag_val, entry.uop.func3);
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
              finished_loads.push_back(entry.uop);
              free_ldq_entry(idx);
            } else {
              // Handle load replay if needed (e.g., due to MSHR eviction)
              entry.replay_priority =
                  in.dcache2lsu->resp_ports.load_resps[i].replay;
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
      const int req_id =
          static_cast<int>(in.dcache2lsu->resp_ports.store_resps[i].req_id);
      if (req_id < 0 || req_id >= 2 * STQ_SIZE) {
        Assert(false && "Invalid STQ tag in store response");
        continue;
      }
      const StoreTag tag = decode_store_req_id(req_id);
      auto *entry = find_store_entry(tag);
      if (entry == nullptr) {
        continue;
      }
      if (entry->valid && !entry->done && entry->send) {
        if (in.dcache2lsu->resp_ports.store_resps[i].replay == 0) {
          entry->done = true;
          entry->replay = 0;
          entry->send = false;
        } else {
          // Handle store replay if needed (e.g., due to MSHR eviction)
          uint8_t replay_code =
              in.dcache2lsu->resp_ports.store_resps[i].replay;
          // replay=3 is bank-conflict: it should be retried directly
          // on the next cycle and must not freeze the STQ head.
          entry->replay = (replay_code == 3) ? 0 : replay_code;
          entry->send = false; // 重置发送标志，等待下次发送
        }
      }
    }
  }

  if (peripheral_io.out.is_mmio && peripheral_io.out.uop.op == UOP_STA) {
    int req_id = peripheral_io.out.uop.rob_idx;
    if (req_id < 0 || req_id >= 2 * STQ_SIZE) {
      Assert(false && "Invalid STQ tag in MMIO store response");
    } else if (auto *entry = find_store_entry(decode_store_req_id(req_id))) {
      if (entry->valid && !entry->done && entry->send) {
        entry->done = true;
        entry->send = false;
      }
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
    // task.flush_pipe = is_mmio;
    ldq[ldq_idx].is_mmio_wait = is_mmio; // 延迟发送：等待到达 ROB 队头后再发出
    auto fwd_res =
        is_mmio ? StoreForwardResult{} : check_store_forward(p_addr, inst);

    if (fwd_res.state == StoreForwardState::Hit) {
      task.result = fwd_res.data;
      task.cplt_time = sim_time + 0; // 这一拍直接完成！
    } else if (fwd_res.state == StoreForwardState::NoHit) {
      task.cplt_time = REQ_WAIT_SEND;
    } else {
      task.cplt_time = REQ_WAIT_RETRY;
    }
  }

  ldq[ldq_idx].tlb_retry = false;
  ldq[ldq_idx].uop = task;
}

void RealLsu::handle_store_addr(const MicroOp &inst) {
  Assert(inst.stq_idx >= 0 && inst.stq_idx < STQ_SIZE);
  if (!finish_store_addr_once(inst)) {
    pending_sta_addr_reqs.push_back(inst);
  }
}

void RealLsu::handle_store_data(const MicroOp &inst) {
  Assert(inst.stq_idx >= 0 && inst.stq_idx < STQ_SIZE);
  auto *entry = find_store_entry(make_store_tag(inst.stq_idx, inst.stq_flag));
  if (entry == nullptr || !stq_entry_matches_uop(*entry, inst)) {
    return;
  }
  entry->data = inst.result;
  entry->data_valid = true;
}

bool RealLsu::reserve_stq_entry(mask_t br_mask, uint32_t rob_idx,
                                uint32_t rob_flag, uint32_t func3,
                                bool slot_flag) {
  if (stq_count >= STQ_SIZE) {
    return false;
  }
  const StoreTag tag = make_store_tag(stq_tail, slot_flag);
  auto &node = speculative_stq[speculative_stq_tail];
  node = {};
  node.tag = tag;
  node.entry.valid = true;
  node.entry.br_mask = br_mask;
  node.entry.rob_idx = rob_idx;
  node.entry.rob_flag = rob_flag;
  node.entry.func3 = func3;

  auto &loc = store_locator(tag);
  loc.valid = true;
  loc.kind = StoreQueueKind::Speculative;
  loc.pos = speculative_stq_tail;
  speculative_stq_tail = (speculative_stq_tail + 1) % STQ_SIZE;
  speculative_stq_count++;
  stq_tail = (stq_tail + 1) % STQ_SIZE;
  return true;
}

void RealLsu::consume_stq_alloc_reqs(int &push_count) {
  for (int i = 0; i < MAX_STQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->alloc_req[i]) {
      continue;
    }
    bool ok = reserve_stq_entry(in.dis2lsu->br_mask[i], in.dis2lsu->rob_idx[i],
                                in.dis2lsu->rob_flag[i],
                                in.dis2lsu->func3[i], in.dis2lsu->stq_flag[i]);
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
         (paddr == OPENSBI_TIMER_LOW_ADDR) ||
         (paddr == OPENSBI_TIMER_HIGH_ADDR);
}
void RealLsu::change_store_info(const StoreNode &node, int port) {
  const auto &head = node.entry;

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
  out.lsu2dcache->req_ports.store_ports[port].req_id =
      encode_store_req_id(node.tag);
}

void RealLsu::handle_global_flush() {
  if (speculative_stq_count > 0) {
    stq_tail = speculative_stq_front().tag.idx;
    for (int i = 0; i < speculative_stq_count; i++) {
      clear_store_node(speculative_stq_at(i));
    }
    stq_count -= speculative_stq_count;
    speculative_stq_head = 0;
    speculative_stq_tail = 0;
    speculative_stq_count = 0;
  }
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

  int kill_pos = -1;
  for (int i = 0; i < speculative_stq_count; i++) {
    const auto &node = speculative_stq_at(i);
    if (node.entry.valid && (node.entry.br_mask & mask)) {
      kill_pos = i;
      break;
    }
  }
  if (kill_pos == -1) {
    return;
  }

  const int new_tail = speculative_stq_at(kill_pos).tag.idx;
  for (int i = kill_pos; i < speculative_stq_count; i++) {
    clear_store_node(speculative_stq_at(i));
  }
  stq_tail = new_tail;
  stq_count -= (speculative_stq_count - kill_pos);
  speculative_stq_tail = (speculative_stq_head + kill_pos) % STQ_SIZE;
  speculative_stq_count = kill_pos;
}
void RealLsu::retire_stq_head_if_ready(int &pop_count) {
  if (committed_stq_count == 0) {
    return;
  }
  auto &head_node = committed_stq_front();
  auto &head = head_node.entry;
  const StoreTag head_tag = current_stq_head_tag();
  Assert(head_node.tag.idx == head_tag.idx && head_node.tag.flag == head_tag.flag);

  if (!head.valid) {
    return;
  }

  if (!head.suppress_write) {
    if (!(head.valid && head.addr_valid && head.data_valid && head.committed)) {
      return;
    }
    // STQ retirement only frees the LSU resource once the store has completed
    // its cache-side handshake. Translation/SFENCE ordering is enforced
    // separately via committed_store_pending; otherwise ordinary stores that
    // are already accepted by the memory hierarchy can pin the STQ head
    // indefinitely.
    if (!head.done) {
      return;
    }
  }

  // Normal store: comb 阶段已完成写握手
  // Suppressed store: 跳过写握手直接 retire
  committed_stq_pop();
  stq_head++;
  if (stq_head == STQ_SIZE) {
    stq_head = 0;
    stq_head_flag = !stq_head_flag;
  }
  pop_count++;
}

void RealLsu::commit_stores_from_rob() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (!in.rob_commit->commit_entry[i].valid) {
      continue;
    }
    const auto &commit_uop = in.rob_commit->commit_entry[i].uop;
    if (ctx != nullptr && is_load(commit_uop)) {
    }
    if (!is_store(commit_uop)) {
      continue;
    }
    Assert(speculative_stq_count > 0 && "Store commit without speculative STQ");
    const auto &node = speculative_stq_front();
    Assert(node.entry.rob_idx == commit_uop.rob_idx &&
               node.entry.rob_flag == commit_uop.rob_flag &&
           "Store commit out of order?");
    move_speculative_front_to_committed();
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
        entry.uop.flush_pipe =
            false; // MMIO no longer triggers flush (non-speculative is enough)
        entry.is_mmio_wait = is_mmio; // 延迟发送：等待到达 ROB 队头后再发出
        auto fwd_res = is_mmio ? StoreForwardResult{}
                               : check_store_forward(p_addr, entry.uop);
        if (fwd_res.state == StoreForwardState::Hit) {
          entry.uop.result = fwd_res.data;
          entry.uop.cplt_time = sim_time;
        } else if (fwd_res.state == StoreForwardState::NoHit) {
          entry.uop.cplt_time = REQ_WAIT_SEND;
        } else {
          entry.uop.cplt_time = REQ_WAIT_RETRY;
        }
      }
      continue;
    }

    if (entry.uop.cplt_time == REQ_WAIT_RETRY) {
      auto fwd_res = check_store_forward(entry.uop.diag_val, entry.uop);
      if (fwd_res.state == StoreForwardState::Hit) {
        entry.uop.result = fwd_res.data;
        entry.uop.cplt_time = sim_time;
      } else if (fwd_res.state == StoreForwardState::NoHit) {
        entry.uop.cplt_time = REQ_WAIT_SEND;
      }
    }

    if (entry.uop.cplt_time <= sim_time) {
      if (!entry.killed) {
        if (is_amo_lr_uop(entry.uop)) {
          reserve_valid = true;
          reserve_addr = entry.uop.diag_val;
        }
        finished_loads.push_back(entry.uop);
      }
      free_ldq_entry(i);
    }
  }
}

bool RealLsu::finish_store_addr_once(const MicroOp &inst) {
  StoreTag tag = make_store_tag(inst.stq_idx, inst.stq_flag);
  auto *entry = find_store_entry(tag);
  if (entry == nullptr || !stq_entry_matches_uop(*entry, inst)) {
    return true;
  }
  entry->addr = inst.result; // VA

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
      fault_op.op = UOP_LOAD;
      fault_op.dest_en = true;
      finished_loads.push_back(fault_op);
    } else {
      finished_sta_reqs.push_back(fault_op);
    }
    entry->p_addr = pa;
    entry->addr_valid = false;
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
    entry->suppress_write = !sc_success;
    finished_loads.push_back(success_op);
    entry->is_mmio = false; // SC 结果不区分 MMIO，始终走正常内存路径
    entry->p_addr = pa;
    entry->addr_valid = true;
    return true;
  }
  bool is_mmio = is_mmio_addr(pa);
  // MMIO store must not trigger ROB flush at STA writeback. Otherwise ROB may
  // flush globally before LSU consumes rob_commit, dropping the STQ commit.
  success_op.flush_pipe = false;
  entry->is_mmio = is_mmio;
  finished_sta_reqs.push_back(success_op);
  entry->p_addr = pa;
  entry->addr_valid = true;
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
    ldq[idx].valid = false;
    ldq[idx].killed = false;
    ldq[idx].sent = false;
    ldq[idx].waiting_resp = false;
    ldq[idx].wait_resp_since = 0;
    ldq[idx].tlb_retry = false;
    ldq[idx].is_mmio_wait = false;
    ldq[idx].uop = {};
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
    mmu->seq();
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
    for (int i = 0; i < committed_stq_count; i++) {
      auto &entry = committed_stq_at(i).entry;
      if (entry.valid)
        entry.br_mask &= ~clear;
    }
    for (int i = 0; i < speculative_stq_count; i++) {
      auto &entry = speculative_stq_at(i).entry;
      if (entry.valid)
        entry.br_mask &= ~clear;
    }
    for (auto &e : finished_sta_reqs)
      e.br_mask &= ~clear;
    for (auto &e : finished_loads)
      e.br_mask &= ~clear;
    for (auto &e : pending_sta_addr_reqs)
      e.br_mask &= ~clear;
  }

  if (is_mispred) {
    mmu->seq();
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
  stq_count = stq_count - pop_count;
  if (stq_count < 0) {
    Assert(0 && "STQ Count Underflow! logic bug!");
  }

  memcpy(issued_stq_addr, issued_stq_addr_nxt, sizeof(issued_stq_addr));
  memcpy(issued_stq_addr_valid, issued_stq_addr_valid_nxt,
         sizeof(issued_stq_addr_valid));

#if LSU_LIGHT_ASSERT
  Assert(committed_stq_count + speculative_stq_count == stq_count &&
         "STQ invariant: queue counts do not add up");
  if (pop_count == 0 && committed_stq_count > 0) {
    const auto &head_node = committed_stq_front();
    const StoreTag head_tag = current_stq_head_tag();
    Assert(head_node.tag.idx == head_tag.idx &&
               head_node.tag.flag == head_tag.flag &&
           "STQ invariant: committed queue front must match stq_head");
    const StqEntry &head = head_node.entry;
    const bool head_ready_to_retire = head.valid && head.addr_valid &&
                                      head.data_valid && head.committed &&
                                      head.done &&
                                      (head.is_mmio ||
                                       !has_translation_store_conflict(
                                           head.p_addr));
    Assert(!head_ready_to_retire &&
           "STQ invariant: retire-ready head was not popped");
  }
  // Lightweight O(1) ring invariants for STQ pointers/count.
  const int head_to_tail = (stq_tail - stq_head + STQ_SIZE) % STQ_SIZE;
  if (stq_count == 0) {
    Assert(stq_head == stq_tail &&
           "STQ invariant: empty queue pointer mismatch");
    Assert(committed_stq_count == 0 && speculative_stq_count == 0 &&
           "STQ invariant: empty queue state mismatch");
  } else if (stq_count == STQ_SIZE) {
    Assert(stq_head == stq_tail &&
           "STQ invariant: full queue requires head == tail");
  } else {
    Assert(head_to_tail == stq_count &&
           "STQ invariant: count != distance(head, tail)");
  }
#endif
  mmu->seq();
}

bool RealLsu::is_store_older(int s_idx, int s_flag, int l_idx,
                             int l_flag) const {
  if (s_flag == l_flag) {
    return s_idx < l_idx;
  } else {
    return s_idx > l_idx;
  }
}

bool RealLsu::has_older_store_pending(const MicroOp &load_uop) const {
  const int stop_idx = load_uop.stq_idx;
  const bool stop_flag = load_uop.stq_flag;
  for (int i = 0; i < committed_stq_count; i++) {
    const StqEntry &entry = committed_stq_at(i).entry;
    if (entry.valid && !entry.suppress_write) {
      return true;
    }
  }
  for (int i = 0; i < speculative_stq_count; i++) {
    const auto &node = speculative_stq_at(i);
    if (!is_store_older(node.tag.idx, node.tag.flag, stop_idx, stop_flag)) {
      break;
    }
    const StqEntry &entry = node.entry;
    if (entry.valid && !entry.suppress_write) {
      return true;
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
  const int stop_idx = load_uop.stq_idx;
  const bool stop_flag = load_uop.stq_flag;

  auto scan_entry = [&](const StqEntry &entry) -> StoreForwardResult {
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
    return {StoreForwardState::NoHit, 0};
  };

  for (int i = 0; i < committed_stq_count; i++) {
    StqEntry &entry = committed_stq_at(i).entry;
    auto res = scan_entry(entry);
    if (res.state == StoreForwardState::Retry) {
      return res;
    }
  }
  for (int i = 0; i < speculative_stq_count; i++) {
    const auto &node = speculative_stq_at(i);
    if (!is_store_older(node.tag.idx, node.tag.flag, stop_idx, stop_flag)) {
      break;
    }
    auto res = scan_entry(node.entry);
    if (res.state == StoreForwardState::Retry) {
      return res;
    }
  }

  if (!hit_any) {
    return {StoreForwardState::NoHit, 0};
  }
  return {StoreForwardState::Hit,
          extract_data(current_word, p_addr, load_uop.func3)};
}
StqEntry RealLsu::get_stq_entry(int stq_idx, bool stq_flag) {
  Assert(stq_idx >= 0 && stq_idx < STQ_SIZE);
  const StoreTag tag = make_store_tag(stq_idx, stq_flag);
  if (const auto *entry = find_store_entry(tag)) {
    return *entry;
  }
  return {};
}

uint32_t RealLsu::coherent_read(uint32_t p_addr) {
  uint32_t data = pmem_read(p_addr);
  overlay_committed_store_word(p_addr, data);
  return data;
}

bool RealLsu::committed_store_conflicts_word(uint32_t word_addr) const {
  if (ctx == nullptr || ctx->cpu == nullptr) {
    return true;
  }

  uint32_t observed = pmem_read(word_addr);
  uint32_t dcache_word = 0;
  const auto q =
      ctx->cpu->mem_subsystem.get_dcache().query_coherent_word(word_addr,
                                                               dcache_word);
  if (q == RealDcache::CoherentQueryResult::Hit) {
    observed = dcache_word;
  }
  uint32_t expected = observed;
  bool has_match = false;

  for (int i = 0; i < committed_stq_count; i++) {
    const auto &entry = committed_stq_at(i).entry;
    if (entry.valid && entry.committed && !entry.suppress_write &&
        !entry.is_mmio &&
        entry.addr_valid && entry.data_valid &&
        ((entry.p_addr & ~0x3u) == word_addr)) {
      has_match = true;
      expected = merge_data_to_word(expected, entry.data, entry.p_addr,
                                    entry.func3);
    }
  }

  if (!has_match) {
    return false;
  }
  if (q == RealDcache::CoherentQueryResult::Retry) {
    return true;
  }
  return expected != observed;
}

bool RealLsu::has_translation_store_conflict(uint32_t p_addr) const {
  if (is_mmio_addr(p_addr)) {
    return false;
  }
  return committed_store_conflicts_word(p_addr & ~0x3u);
}

bool RealLsu::has_committed_store_pending() const {
  for (int i = 0; i < committed_stq_count; i++) {
    const StqEntry &e = committed_stq_at(i).entry;
    if (e.valid && e.committed && !e.suppress_write && !e.is_mmio) {
      if (!e.addr_valid || !e.data_valid || !e.done) {
        return true;
      }
      if (has_translation_store_conflict(e.p_addr)) {
        return true;
      }
    }
  }
  return false;
}

void RealLsu::overlay_committed_store_word(uint32_t p_addr, uint32_t &data) {
  // Only architecturally committed stores are visible to PTW/MMU coherence.
  // Younger speculative stores must not affect translation.
  for (int i = 0; i < committed_stq_count; i++) {
    const auto &entry = committed_stq_at(i).entry;
    if (entry.valid && entry.committed && entry.addr_valid && entry.data_valid &&
        !entry.suppress_write &&
        ((entry.p_addr >> 2) == (p_addr >> 2))) {
      data = merge_data_to_word(data, entry.data, entry.p_addr, entry.func3);
    }
  }
}
