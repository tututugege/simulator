#include "SimpleLsu.h"
#include "PhysMemory.h"
#include "AbstractLsu.h"
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


SimpleLsu::SimpleLsu(SimContext *ctx) : AbstractLsu(ctx) {
  // Initialize MMU
#ifdef CONFIG_TLB_MMU
  mmu = std::make_unique<TlbMmu>(ctx, nullptr, DTLB_ENTRIES);
#else
  mmu = std::make_unique<SimpleMmu>(ctx, this);
#endif

  init();
}

void SimpleLsu::init() {
  stq_head = 0;
  stq_tail = 0;
  stq_commit = 0;
  stq_count = 0;
  ldq_count = 0;
  ldq_alloc_tail = 0;
  finished_loads.clear();
  finished_sta_reqs.clear();
  pending_sta_addr_reqs.clear();
  mmu->flush();

  // 初始化所有 STQ LDQ 条目，防止未初始化内存导致的破坏
  for (int i = 0; i < STQ_SIZE; i++) {
    stq[i].valid = false;
    stq[i].addr_valid = false;
    stq[i].data_valid = false;
    stq[i].committed = false;
    stq[i].addr = 0;
    stq[i].data = 0;
    stq[i].br_mask = 0;
    stq[i].rob_idx = 0;
    stq[i].rob_flag = 0;
    stq[i].func3 = 0;
  }

  for (int i = 0; i < LDQ_SIZE; i++) {
    ldq[i].valid = false;
    ldq[i].killed = false;
    ldq[i].sent = false;
    ldq[i].waiting_resp = false;
    ldq[i].tlb_retry = false;
    ldq[i].uop = {};
  }
}

// =========================================================
// 1. Dispatch 阶段: STQ 分配反馈
// =========================================================

void SimpleLsu::comb_lsu2dis_info() {
  out.lsu2dis->stq_tail = this->stq_tail;
  out.lsu2dis->stq_free = STQ_SIZE - this->stq_count;
  out.lsu2dis->ldq_free = LDQ_SIZE - this->ldq_count;

  for (auto &v : out.lsu2dis->ldq_alloc_idx) {
    v = -1;
  }
  int scan_pos = ldq_alloc_tail;
  int produced = 0;
  for (int n = 0; n < LDQ_SIZE && produced < MAX_LDQ_DISPATCH_WIDTH;
       n++) {
    if (!ldq[scan_pos].valid) {
      out.lsu2dis->ldq_alloc_idx[produced++] = scan_pos;
    }
    scan_pos = (scan_pos + 1) % LDQ_SIZE;
  }

  // Populate miss_mask (Phase 4)
  uint64_t mask = 0;
  for (int i = 0; i < LDQ_SIZE; i++) {
    const auto &entry = ldq[i];
    if (entry.valid && !entry.killed && entry.uop.is_cache_miss) {
      mask |= (1ULL << entry.uop.rob_idx);
    }
  }
  out.lsu2rob->miss_mask = mask;
  out.lsu2rob->committed_store_pending = has_committed_store_pending();
}

// =========================================================
// 2. Execute 阶段: 接收 AGU/SDU 请求 (多端口轮询)
// =========================================================
void SimpleLsu::comb_recv() {
  // 顶层当前采用直接变量赋值连线；这里每拍将端口硬连到 MMU。
  mmu->set_ptw_mem_port(ptw_mem_port);
  mmu->set_ptw_walk_port(ptw_walk_port);

  Assert(out.lsu2dcache != nullptr && "out.lsu2dcache is not connected");
  Assert(in.dcache2lsu  != nullptr && "in.dcache2lsu is not connected");
  out.lsu2dcache->req_ports.clear();

  // Retry STA address translations that previously returned MMU::RETRY.
  progress_pending_sta_addr();

  // 1. 优先级：Store Data (来自 SDU)
  // 确保在消费者检查之前数据就绪
  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    if (in.exe2lsu->sdu_req[i].valid) {
      handle_store_data(in.exe2lsu->sdu_req[i].uop);
    }
  }

  // 2. 优先级：Store Addr (来自 AGU)
  // 确保地址对于别名检查有效
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_STA) {
        handle_store_addr(uop);
      }
    }
  }

  // 3. 优先级：Loads (来自 AGU)
  // 最后处理 Load，使其能看到本周期最新的 Store (STLF)
  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_LOAD) {
        handle_load_req(uop);
      }
    }
  }

  for (int i = 0; i < LDQ_SIZE; i++) {
    auto &entry = ldq[i];
    if (!entry.valid || entry.killed || entry.sent || entry.waiting_resp) {
      continue;
    }
    if (entry.uop.cplt_time == REQ_WAIT_SEND) {
      MicroOp req_uop = entry.uop;
      req_uop.rob_idx = i; // Local token: LDQ index
      auto &lp = out.lsu2dcache->req_ports.load_ports[0];
      lp.valid = true;
      lp.addr  = entry.uop.diag_val;
      lp.uop   = req_uop;
      entry.sent = true;
      entry.waiting_resp = true;
      entry.uop.cplt_time = REQ_WAIT_RESP;

      break;
    }
  }

  drive_store_write_req();
}

// =========================================================
// 3. Writeback 阶段: 输出 Load 结果 (多端口写回)
// =========================================================
void SimpleLsu::comb_load_res() {
  // 1. 先清空所有写回端口
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.lsu2exe->wb_req[i].valid = false;
  }

  const auto &lr = in.dcache2lsu->resp_ports.load_resps[0];
  if (lr.valid) {
    int idx = lr.uop.rob_idx;
    if (idx >= 0 && idx < LDQ_SIZE) {
      auto &entry = ldq[idx];
      if (entry.valid && entry.sent && entry.waiting_resp) {
        if (!entry.killed) {
          uint32_t raw = lr.data;
          uint32_t res =
              extract_data(raw, entry.uop.diag_val, entry.uop.func3);
          entry.uop.result = res;
          entry.uop.difftest_skip = lr.uop.difftest_skip;
          entry.uop.cplt_time = sim_time;
          entry.uop.is_cache_miss = lr.uop.is_cache_miss;
          finished_loads.push_back(entry.uop);
        } else {

        }
        free_ldq_entry(idx);
      }
    }
  }

  // 2. 从完成队列填充端口 (Load)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (!finished_loads.empty()) {
      out.lsu2exe->wb_req[i].valid = true;
      out.lsu2exe->wb_req[i].uop = finished_loads.front();

      finished_loads.pop_front();
    } else {
      break;
    }
  }

  // 3. 从完成队列填充端口 (STA)
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (!finished_sta_reqs.empty()) {
      out.lsu2exe->sta_wb_req[i].valid = true;
      out.lsu2exe->sta_wb_req[i].uop = finished_sta_reqs.front();
      finished_sta_reqs.pop_front();
    } else {
      out.lsu2exe->sta_wb_req[i].valid = false;
    }
  }
}

void SimpleLsu::handle_load_req(const MicroOp &inst) {
  int ldq_idx = inst.ldq_idx;
  Assert(ldq_idx >= 0 && ldq_idx < LDQ_SIZE);
  if (!ldq[ldq_idx].valid || ldq[ldq_idx].killed) {
    return;
  }

  MicroOp task = inst;
  task.is_cache_miss = false; // Initialize to false
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
    task.flush_pipe = is_mmio;
    auto fwd_res = is_mmio ? StoreForwardResult{} : check_store_forward(p_addr, inst);

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

void SimpleLsu::handle_store_addr(const MicroOp &inst) {
  if (!finish_store_addr_once(inst)) {
    pending_sta_addr_reqs.push_back(inst);
  }
}

void SimpleLsu::handle_store_data(const MicroOp &inst) {
  Assert(inst.stq_idx >= 0 && inst.stq_idx < STQ_SIZE);
  stq[inst.stq_idx].data = inst.result;
  stq[inst.stq_idx].data_valid = true;
}

bool SimpleLsu::reserve_stq_entry(mask_t br_mask, uint32_t rob_idx,
                                  uint32_t rob_flag, uint32_t func3) {
  if (stq_count >= STQ_SIZE) {
    return false;
  }
  stq[stq_tail].valid = true;
  stq[stq_tail].addr_valid = false;
  stq[stq_tail].data_valid = false;
  stq[stq_tail].committed = false;
  stq[stq_tail].br_mask = br_mask;
  stq[stq_tail].rob_idx = rob_idx;
  stq[stq_tail].rob_flag = rob_flag;
  stq[stq_tail].func3 = func3;
  stq_tail = (stq_tail + 1) % STQ_SIZE;
  return true;
}

void SimpleLsu::consume_stq_alloc_reqs(int &push_count) {
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

bool SimpleLsu::reserve_ldq_entry(int idx, mask_t br_mask, uint32_t rob_idx,
                                  uint32_t rob_flag) {
  Assert(idx >= 0 && idx < LDQ_SIZE);
  if (ldq[idx].valid) {
    return false;
  }
  ldq[idx].valid = true;
  ldq[idx].killed = false;
  ldq[idx].sent = false;
  ldq[idx].waiting_resp = false;
  ldq[idx].tlb_retry = false;
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

void SimpleLsu::consume_ldq_alloc_reqs() {
  for (int i = 0; i < MAX_LDQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->ldq_alloc_req[i]) {
      continue;
    }
    bool ok = reserve_ldq_entry(in.dis2lsu->ldq_idx[i], in.dis2lsu->ldq_br_mask[i],
                                in.dis2lsu->ldq_rob_idx[i], in.dis2lsu->ldq_rob_flag[i]);
    Assert(ok && "LDQ allocate collision");
  }
}

bool SimpleLsu::is_mmio_addr(uint32_t paddr) const {
  return ((paddr & UART_ADDR_MASK) == UART_ADDR_BASE) ||
         ((paddr & PLIC_ADDR_MASK) == PLIC_ADDR_BASE);
}

void SimpleLsu::drive_store_write_req() {
  if (stq_head == stq_commit) {
    return;
  }
  StqEntry &head = stq[stq_head];
  if (!(head.valid && head.addr_valid && head.data_valid)) {
    return;
  }

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

  auto &sp = out.lsu2dcache->req_ports.store_ports[0];
  sp.valid = true;
  sp.addr  = head.p_addr;
  sp.data  = wdata;
  sp.strb  = wstrb;
}

void SimpleLsu::handle_global_flush() {
  int old_tail = stq_tail;
  stq_tail = stq_commit;
  stq_count = (stq_tail - stq_head + STQ_SIZE) % STQ_SIZE;

  int ptr = stq_tail;
  while (ptr != old_tail) {
    stq[ptr].valid = false;
    stq[ptr].addr_valid = false;
    stq[ptr].data_valid = false;
    ptr = (ptr + 1) % STQ_SIZE;
  }
  pending_sta_addr_reqs.clear();
}

void SimpleLsu::handle_mispred(mask_t mask) {
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

  int recovery_tail = find_recovery_tail(mask);
  if (recovery_tail == -1) {
    return;
  }

  int old_tail = stq_tail;
  stq_tail = recovery_tail;
  stq_count = (stq_tail - stq_head + STQ_SIZE) % STQ_SIZE;
  int ptr = stq_tail;

  if (old_tail == stq_tail) {
    do {
      stq[ptr].valid = false;
      stq[ptr].addr_valid = false;
      stq[ptr].data_valid = false;
      ptr = (ptr + 1) % STQ_SIZE;
    } while (ptr != old_tail);
  } else {
    while (ptr != old_tail) {
      stq[ptr].valid = false;
      stq[ptr].addr_valid = false;
      stq[ptr].data_valid = false;
      ptr = (ptr + 1) % STQ_SIZE;
    }
  }
}

void SimpleLsu::retire_stq_head_if_ready(bool write_fire, int &pop_count) {
  if (!write_fire) {
    return;
  }
  if (stq_head == stq_commit) {
    return;
  }
  StqEntry &head = stq[stq_head];
  if (!(head.valid && head.addr_valid && head.data_valid)) {
    return;
  }

  // Store write handshake succeeded in comb stage.
  head.valid = false;
  head.committed = false;
  head.addr_valid = false;
  head.data_valid = false;
  head.addr = 0;
  head.data = 0;
  head.br_mask = 0;

  stq_head = (stq_head + 1) % STQ_SIZE;
  pop_count++;
}

void SimpleLsu::commit_stores_from_rob() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (!(in.rob_commit->commit_entry[i].valid &&
          is_store(in.rob_commit->commit_entry[i].uop))) {
      continue;
    }
    int idx = in.rob_commit->commit_entry[i].uop.stq_idx;
    Assert(idx >= 0 && idx < STQ_SIZE);
    if (idx == stq_commit) {
      stq[idx].committed = true;
      stq_commit = (stq_commit + 1) % STQ_SIZE;
    } else {
      Assert(0 && "Store commit out of order?");
    }
  }
}

void SimpleLsu::progress_ldq_entries() {
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
        entry.uop.flush_pipe = is_mmio;
        auto fwd_res =
            is_mmio ? StoreForwardResult{} : check_store_forward(p_addr, entry.uop);
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
        finished_loads.push_back(entry.uop);
      }
      free_ldq_entry(i);
    }
  }
}

bool SimpleLsu::finish_store_addr_once(const MicroOp &inst) {
  int idx = inst.stq_idx;
  Assert(idx >= 0 && idx < STQ_SIZE);
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
    finished_sta_reqs.push_back(fault_op);
    stq[idx].p_addr = pa;
    stq[idx].addr_valid = false;
    return true;
  }

  MicroOp success_op = inst;
  success_op.cplt_time = sim_time;
  bool is_mmio = is_mmio_addr(pa);
  success_op.flush_pipe = is_mmio;
  finished_sta_reqs.push_back(success_op);
  stq[idx].p_addr = pa;
  stq[idx].addr_valid = true;
  return true;
}

void SimpleLsu::progress_pending_sta_addr() {
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

void SimpleLsu::free_ldq_entry(int idx) {
  Assert(idx >= 0 && idx < LDQ_SIZE);
  if (ldq[idx].valid) {

    ldq[idx].valid = false;
    ldq[idx].killed = false;
    ldq[idx].sent = false;
    ldq[idx].waiting_resp = false;
    ldq[idx].tlb_retry = false;
    ldq[idx].uop = {};
    ldq_count--;
    Assert(ldq_count >= 0);
  }
}

// =========================================================
// 5. Exception: Flush 处理
// =========================================================

void SimpleLsu::comb_flush() {
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
void SimpleLsu::seq() {
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
      if (ldq[i].valid) ldq[i].uop.br_mask &= ~clear;
    }
    for (int i = 0; i < STQ_SIZE; i++) {
      if (stq[i].valid) stq[i].br_mask &= ~clear;
    }
    for (auto &e : finished_sta_reqs) e.br_mask &= ~clear;
    for (auto &e : finished_loads)    e.br_mask &= ~clear;
    for (auto &e : pending_sta_addr_reqs) e.br_mask &= ~clear;
  }

  if (is_mispred) {
    return;
  }

  if (in.rob_bcast->fence) {
    mmu->flush();
  }

  consume_stq_alloc_reqs(push_count);
  consume_ldq_alloc_reqs();
  bool write_fire = out.lsu2dcache->req_ports.store_ports[0].valid;
  retire_stq_head_if_ready(write_fire, pop_count);
  commit_stores_from_rob();

  stq_count = stq_count + push_count - pop_count;
  if (stq_count < 0) {
    Assert(0 && "STQ Count Underflow! logic bug!");
  }
  if (stq_count > STQ_SIZE) {
    Assert(0 && "STQ Count Overflow! logic bug!");
  }
  progress_ldq_entries();
}

// =========================================================
// 辅助：基于 Tag 查找新的 Tail
// =========================================================
int SimpleLsu::find_recovery_tail(mask_t br_mask) {
  // 从 Commit 指针（安全点）开始，向 Tail 扫描
  // 我们要找的是“第一个”被误预测影响的指令
  // 因为是顺序分配，一旦找到一个，后面（更年轻）的肯定也都要丢弃

  int ptr = stq_commit;

  // 修正：正确计算未提交指令数，处理队列已满的情况 (Tail == Commit)
  // stq_count 追踪总有效条目 (Head -> Tail)。
  // Head -> Commit 之间的条目已提交。
  // Commit -> Tail 之间的条目未提交。
  int committed_count = (stq_commit - stq_head + STQ_SIZE) % STQ_SIZE;
  int uncommitted_count = stq_count - committed_count;

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

bool SimpleLsu::is_store_older(int s_idx, int s_flag, int l_idx, int l_flag) {
  if (s_flag == l_flag) {
    return s_idx < l_idx;
  } else {
    return s_idx > l_idx;
  }
}

// =========================================================
// 🛡️ [Nanako Implementation] 完整的 STLF 模拟逻辑
// =========================================================
SimpleLsu::StoreForwardResult
SimpleLsu::check_store_forward(uint32_t p_addr, const MicroOp &load_uop) {

  uint32_t current_word = pmem_read(p_addr);
  bool hit_any = false;

  int ptr = this->stq_head;
  // The load remembers the tail at dispatch time.
  // We check all stores from Head up to (but not including) that tail snapshot.
  // Wait, if it's a circular buffer, and we stop at stq_idx, we need to be
  // careful. Dispatch: stq_idx = (tail + alloc) % STQ_SIZE. The load sees
  // everything BEFORE this stq_idx. So we iterate until ptr ==
  // load_uop.stq_idx.

  int stop_idx = load_uop.stq_idx;

  while (ptr != stop_idx) {
    StqEntry &entry = stq[ptr];

    // Important: We only care if the entry is valid.
    // If it's valid, it's an older store that this load must respect.
    if (entry.valid) {
      if (!entry.addr_valid)
        return {StoreForwardState::Retry, 0}; // Unknown address -> Stall (Retry)

      // Address is valid, check overlap
      int store_width = get_mem_width(entry.func3);
      int load_width = get_mem_width(load_uop.func3);
      uint32_t s_start = entry.p_addr;
      uint32_t s_end = s_start + store_width;
      uint32_t l_start = p_addr;
      uint32_t l_end = l_start + load_width;

      uint32_t overlap_start = std::max(s_start, l_start);
      uint32_t overlap_end = std::min(s_end, l_end);

      if (overlap_start < overlap_end) {
        hit_any = true;
        if (!entry.data_valid)
          return {StoreForwardState::Retry, 0}; // Data unknown -> Stall (Retry)
        current_word = merge_data_to_word(current_word, entry.data,
                                          entry.p_addr, entry.func3);
      }
    }
    ptr = (ptr + 1) % STQ_SIZE;
  }

  if (!hit_any)
    return {StoreForwardState::NoHit, 0};

  uint32_t final_data = extract_data(current_word, p_addr, load_uop.func3);
  return {StoreForwardState::Hit, final_data};
}

StqEntry SimpleLsu::get_stq_entry(int stq_idx) {
  Assert(stq_idx >= 0 && stq_idx < STQ_SIZE);
  return stq[stq_idx];
}

uint32_t SimpleLsu::coherent_read(uint32_t p_addr) {
  // 1. 基准值：读物理内存 (假设 p_addr 已对齐到 4)
  uint32_t data = pmem_read(p_addr);

  // 2. 遍历 STQ 进行覆盖 (Coherent Check)
  int ptr = stq_head;
  int count = stq_count;
  for (int i = 0; i < count; i++) {
    const auto &entry = stq[ptr];
    if (entry.valid && entry.addr_valid) {
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
