#include "RealLsu.h"
#include "DcacheConfig.h"
#include "MemUtils.h"
#include "PhysMemory.h"
#include "RealLsuPerf.h"
#include "SimCpu.h"
#include "TlbMmu.h"
#include "config.h"
#include "util.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

static uint32_t replay_delay_cycles(ReplayType replay) {
  return replay == ReplayType::MSHR_HIT || replay == ReplayType::MSHR_FULL
             ? LSU_PEPLAY_WAIT_CYCLES
             : 0;
}

static void wake_load_replays_on_mshr_fill(const DcacheLsuIO &dcache_resp,
                                           LsuState &nxt) {
  if (!dcache_resp.mshr_fill || !LSU_WAIT_MSHR_FILL) {
    return;
  }

  uint32_t issue = std::min<uint32_t>(LSU_LOAD_WINDOW_WIDTH, nxt.wait_dcache_ldq_count);
  uint32_t mshr_full_wakeups = 0;
  for (uint32_t i = 0; i < issue; i++) {
    const uint32_t wait_idx = (nxt.wait_dcache_ldq_head + i) % LDQ_SIZE;
    auto &wait_entry = nxt.wait_dcache_ldq[wait_idx];
    if (!wait_entry.valid || wait_entry.replay_wait_cycles == 0) {
      continue;
    }

    auto &ldq_entry = nxt.ldq[wait_entry.ldq_idx];
    if (ldq_entry.load_state != LoadState::ReadyToIssue) {
      continue;
    }

    if (ldq_entry.replay_type == ReplayType::MSHR_HIT &&
        cache_line_match(ldq_entry.p_addr, dcache_resp.mshr_fill_addr)) {
      wait_entry.replay_wait_cycles = 0;
    } else if (ldq_entry.replay_type == ReplayType::MSHR_FULL &&
               mshr_full_wakeups < LSU_LDU_COUNT) {
      wait_entry.replay_wait_cycles = 0;
      mshr_full_wakeups++;
    }
  }
}

static void wake_store_replays_on_mshr_fill(const DcacheLsuIO &dcache_resp,
                                            LsuState &nxt) {
  if (!dcache_resp.mshr_fill || !LSU_WAIT_MSHR_FILL) {
    return;
  }

  uint32_t committed_count = nxt.stq_commit_count;
  if (committed_count > nxt.stq_count) {
    committed_count = nxt.stq_count;
  }

  uint32_t mshr_full_wakeups = 0;
  for (uint32_t i = 0; i < committed_count; i++) {
    const uint32_t stq_idx = (nxt.stq_head + i) % STQ_SIZE;
    auto &stq_entry = nxt.stq[stq_idx];
    if (stq_entry.store_state != StoreState::Committed ||
        stq_entry.replay_wait_cycles == 0) {
      continue;
    }

    if (stq_entry.replay_type == ReplayType::MSHR_HIT &&
        cache_line_match(stq_entry.paddr, dcache_resp.mshr_fill_addr)) {
      stq_entry.replay_wait_cycles = 0;
    } else if (stq_entry.replay_type == ReplayType::MSHR_FULL &&
               mshr_full_wakeups < LSU_STA_COUNT) {
      stq_entry.replay_wait_cycles = 0;
      mshr_full_wakeups++;
    }
  }
}

STLFResult check_stlf(uint32_t load_addr, uint32_t load_func3, uint32_t store_addr, uint32_t store_func3) {
  int store_width = get_mem_width(store_func3);
  int load_width = get_mem_width(load_func3);
  uint32_t s_start = store_addr;
  uint32_t s_end = s_start + store_width;
  uint32_t l_start = load_addr;
  uint32_t l_end = l_start + load_width;
  uint32_t overlap_start = std::max(s_start, l_start);
  uint32_t overlap_end = std::min(s_end, l_end);

  if (s_start <= l_start && s_end >= l_end) {
    return STLFResult::Overlap; // Store completely covers Load
  } else if (l_end <= s_start || l_start >= s_end) {
    return STLFResult::Disjoint; // No overlap
  } else {
    return STLFResult::Retry; // Partial overlap, conservatively treat as conflict to avoid corner cases
  }
};

wire<STQ_IDX_WIDTH> RealLsu::enqueue_wait_mmu_stq(wire<STQ_IDX_WIDTH> stq_idx) {
  uint32_t wait_mmu_idx = (nxt.wait_mmu_stq_head + nxt.wait_mmu_stq_count) % STQ_SIZE;
  nxt.wait_mmu_stq[wait_mmu_idx].valid = true;
  nxt.wait_mmu_stq[wait_mmu_idx].stq_idx = stq_idx;
  nxt.wait_mmu_stq[wait_mmu_idx].reserve = false;
  nxt.wait_mmu_stq_count++;
  return wait_mmu_idx;
}
void RealLsu::dequeue_wait_mmu_stq() {
  nxt.wait_mmu_stq[nxt.wait_mmu_stq_head].valid = false;
  nxt.wait_mmu_stq_head = (nxt.wait_mmu_stq_head + 1) % STQ_SIZE;
  if(nxt.wait_mmu_stq_count>0)
  nxt.wait_mmu_stq_count--;
}
wire<LDQ_IDX_WIDTH> RealLsu::enqueue_wait_mmu_ldq(wire<LDQ_IDX_WIDTH> ldq_idx) {
  uint32_t wait_mmu_idx = (nxt.wait_mmu_ldq_head + nxt.wait_mmu_ldq_count) % LDQ_SIZE;
  nxt.wait_mmu_ldq[wait_mmu_idx].valid = true;
  nxt.wait_mmu_ldq[wait_mmu_idx].ldq_idx = ldq_idx;
  nxt.wait_mmu_ldq[wait_mmu_idx].reserve = false;
  nxt.wait_mmu_ldq_count++;
  return wait_mmu_idx;
}
void RealLsu::requeue_wait_mmu_stq() {
  uint32_t old_head = nxt.wait_mmu_stq_head;
  uint32_t old_tail = (nxt.wait_mmu_stq_head + nxt.wait_mmu_stq_count) % STQ_SIZE;
  if (old_head == old_tail) {
    return; // 队列为空，无需重新排队
  }
  nxt.wait_mmu_stq_head = (nxt.wait_mmu_stq_head + 1) % STQ_SIZE; // 从当前等待TLB的队列中移除
  nxt.wait_mmu_stq[old_tail].valid = true; // 将当前头部条目重新加入到队列尾部
  nxt.wait_mmu_stq[old_tail].stq_idx = nxt.wait_mmu_stq[old_head].stq_idx; // 保持原有的store索引不变
  nxt.wait_mmu_stq[old_tail].reserve = nxt.wait_mmu_stq[old_head].reserve; // 保持原有的reserve位不变
  nxt.wait_mmu_stq[old_head].valid = false; // 将当前头部条目标记为无效
  nxt.wait_mmu_stq[old_head].reserve = 0; // 将当前头部条目的reserve位重置为0，避免后续错误地访问到这个条目
}
void RealLsu::requeue_wait_mmu_ldq() {
  uint32_t old_head = nxt.wait_mmu_ldq_head;
  uint32_t old_tail = (nxt.wait_mmu_ldq_head + nxt.wait_mmu_ldq_count) % LDQ_SIZE;
  if (old_head == old_tail) {
    return; // 队列为空，无需重新排队
  }
  nxt.wait_mmu_ldq_head = (nxt.wait_mmu_ldq_head + 1) % LDQ_SIZE; // 从当前等待TLB的队列中移除
  nxt.wait_mmu_ldq[old_tail].valid = true; // 将当前头部条目重新加入到队列尾部
  nxt.wait_mmu_ldq[old_tail].ldq_idx = nxt.wait_mmu_ldq[old_head].ldq_idx; // 保持原有的load索引不变
  nxt.wait_mmu_ldq[old_tail].reserve = nxt.wait_mmu_ldq[old_head].reserve; // 保持原有的reserve位不变
  nxt.wait_mmu_ldq[old_head].valid = false; // 将当前头部条目标记为无效
  nxt.wait_mmu_ldq[old_head].reserve = 0; // 将当前头部条目的reserve位重置为0，避免后续错误地访问到这个条目
}
void RealLsu::dequeue_wait_mmu_ldq() {
  nxt.wait_mmu_ldq[nxt.wait_mmu_ldq_head].valid = false;
  nxt.wait_mmu_ldq_head = (nxt.wait_mmu_ldq_head + 1) % LDQ_SIZE;
  if(nxt.wait_mmu_ldq_count>0)
  nxt.wait_mmu_ldq_count--;
}
wire<STQ_IDX_WIDTH> RealLsu::enqueue_mmu_done_stq(wire<STQ_IDX_WIDTH> stq_idx) {
  uint32_t mmu_done_idx = (nxt.mmu_done_stq_head + nxt.mmu_done_stq_count) % STQ_SIZE;
  nxt.mmu_done_stq[mmu_done_idx].valid = true;
  nxt.mmu_done_stq[mmu_done_idx].stq_idx = stq_idx;
  nxt.mmu_done_stq[mmu_done_idx].reserve = false;
  nxt.mmu_done_stq_count++;
  return mmu_done_idx;
}
void RealLsu::dequeue_mmu_done_stq() {
  nxt.mmu_done_stq[nxt.mmu_done_stq_head].valid = false;
  nxt.mmu_done_stq_head = (nxt.mmu_done_stq_head + 1) % STQ_SIZE;
  if(nxt.mmu_done_stq_count>0)
  nxt.mmu_done_stq_count--;
}
void RealLsu::enqueue_wait_dcache_ldq(wire<LDQ_IDX_WIDTH> ldq_idx) {
  uint32_t wait_dcache_idx = (nxt.wait_dcache_ldq_head + nxt.wait_dcache_ldq_count) % LDQ_SIZE;
  nxt.wait_dcache_ldq[wait_dcache_idx].valid = true;
  nxt.wait_dcache_ldq[wait_dcache_idx].ldq_idx = ldq_idx;
  nxt.wait_dcache_ldq_count++;
}
void RealLsu::dequeue_wait_dcache_ldq() {
  nxt.wait_dcache_ldq[nxt.wait_dcache_ldq_head].valid = false;
  nxt.wait_dcache_ldq_head = (nxt.wait_dcache_ldq_head + 1) % LDQ_SIZE;
  nxt.wait_dcache_ldq_count--;
}
void RealLsu::enqueue_finish(wire<LDQ_IDX_WIDTH> ldq_idx) {
  uint32_t finish_idx = (nxt.finish_head + nxt.finish_count) % LDQ_SIZE;
  nxt.finish[finish_idx].valid = true;
  nxt.finish[finish_idx].ldq_idx = ldq_idx;
  nxt.finish_count++;
}
void RealLsu::dequeue_finish() {
  nxt.finish[nxt.finish_head].valid = false;
  nxt.finish_head = (nxt.finish_head + 1) % LDQ_SIZE;
  nxt.finish_count--;
}
void RealLsu::enqueue_stlf(wire<LDQ_IDX_WIDTH> ldq_idx) {
  uint32_t stlf_idx = (nxt.stlf_queue_head + nxt.stlf_queue_count) % LDQ_SIZE;
  nxt.stlf_queue[stlf_idx].valid = true;
  nxt.stlf_queue[stlf_idx].ldq_idx = ldq_idx;
  nxt.stlf_queue_count++;
}
void RealLsu::dequeue_stlf() {
  nxt.stlf_queue[nxt.stlf_queue_head].valid = false;
  nxt.stlf_queue_head = (nxt.stlf_queue_head + 1) % LDQ_SIZE;
  nxt.stlf_queue_count--;
}
void RealLsu::activate_wait_mmu_stq(wire<STQ_IDX_WIDTH> stq_idx) {
  nxt.wait_mmu_stq[stq_idx].reserve = true;
}
void RealLsu::activate_wait_mmu_ldq(wire<LDQ_IDX_WIDTH> ldq_idx) {
  nxt.wait_mmu_ldq[ldq_idx].reserve = true;
}
void RealLsu::activate_mmu_done_stq(wire<STQ_IDX_WIDTH> stq_idx) {
  nxt.mmu_done_stq[stq_idx].reserve = true;
}

RealLsu::RealLsu(SimContext *ctx) : cur{}, nxt{}, in{}, out{}, ctx(ctx) {}

void RealLsu::init() {
  cur = {};
  nxt = {};
}
uint32_t ldq_count = 0;
void RealLsu::comb_cal() {
  ldq_count = 0;
  for (int i = 0; i < LDQ_SIZE; i++) {
    if (cur.ldq[i].load_state != LoadState::Empty) {
      ldq_count++;
    }
  }

  lsu_perf::sample_queue_occupancy(ctx, cur, ldq_count);
}

void RealLsu::comb_lsu2rob() {
  *out.lsu2rob = {};

  out.lsu2rob->committed_store_pending = cur.stq_commit_count != 0;
  uint64_t miss_mask = lsu_perf::load_miss_mask(cur);
  out.lsu2rob->tma.miss_mask = miss_mask;
}

void RealLsu::comb_lsu2dis() {
  *out.lsu2dis = {};
  out.lsu2dis->stq_tail = stq_idx_after(cur.stq_head, cur.stq_count);
  out.lsu2dis->stq_tail_flag =
      stq_tail_flag(cur.stq_head, cur.stq_count, cur.stq_head_flag);

  uint32_t stq_free = STQ_SIZE - cur.stq_count;
  uint32_t ldq_free = LDQ_SIZE - ldq_count;
  uint32_t stq_wait_mmu = STQ_SIZE - nxt.wait_mmu_stq_count;
  uint32_t ldq_wait_mmu = LDQ_SIZE - nxt.wait_mmu_ldq_count;
  uint32_t stq_mmu_done = STQ_SIZE - nxt.mmu_done_stq_count;

  if (stq_free > stq_wait_mmu)
    stq_free = stq_wait_mmu;
  if (ldq_free > ldq_wait_mmu)
    ldq_free = ldq_wait_mmu;
  if (stq_free > stq_mmu_done)
    stq_free = stq_mmu_done;
  out.lsu2dis->stq_free = stq_free;
  out.lsu2dis->ldq_free = ldq_free;



  uint32_t index = 0;
  for (int i = 0; i < LDQ_SIZE && index < MAX_LDQ_DISPATCH_WIDTH && index < ldq_free; i++) {
    if (cur.ldq[i].load_state == LoadState::Empty) {
      out.lsu2dis->ldq_alloc_idx[index] = i;
      out.lsu2dis->ldq_alloc_valid[index] = true;
      index++;
    }
  }
}

void RealLsu::comb_dis2lsu() {
  for (int i = 0; i < MAX_LDQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->ldq_alloc_req[i]) {
      continue;
    }
    bool ok = alloc_ldq_entry(in.dis2lsu->ldq_br_mask[i], in.dis2lsu->ldq_rob_idx[i], in.dis2lsu->ldq_rob_flag[i], in.dis2lsu->ldq_idx[i]);
    Assert(ok && "LDQ allocate collision");
  }

  for (int i = 0; i < MAX_STQ_DISPATCH_WIDTH; i++) {
    if (!in.dis2lsu->alloc_req[i]) {
      continue;
    }
    bool ok = alloc_stq_entry(in.dis2lsu->br_mask[i], in.dis2lsu->rob_idx[i], in.dis2lsu->rob_flag[i], in.dis2lsu->func3[i], in.dis2lsu->stq_flag[i]);
    Assert(ok && "STQ allocate overflow");
  }
}

void RealLsu::comb_exe2lsu() {
  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    if (in.exe2lsu->sdu_req[i].valid) {
      handle_store_data(in.exe2lsu->sdu_req[i].uop.to_micro_op());
    }
  }

  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_STA) {
        handle_store_addr(uop.to_micro_op());
      }
    }
  }

  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    if (in.exe2lsu->agu_req[i].valid) {
      const auto &uop = in.exe2lsu->agu_req[i].uop;
      if (uop.op == UOP_LOAD) {
        handle_load_req(uop.to_micro_op());
      }
    }
  }
}

void RealLsu::comb_mmio_out() {
  *out.peripheral_req = {};
  if (in.peripheral_resp->ready) {
    if (nxt.uncached_unit.valid) {
      out.peripheral_req->is_mmio = true;
      out.peripheral_req->wen = !nxt.uncached_unit.is_load;
      out.peripheral_req->mmio_addr = nxt.uncached_unit.addr;
      out.peripheral_req->mmio_wdata = nxt.uncached_unit.wdata;
      out.peripheral_req->mmio_fun3 = nxt.uncached_unit.func3;
    } else {
      out.peripheral_req->is_mmio = false;
    }
  }
}
void RealLsu::comb_mmio_in() {
  if (in.peripheral_resp->is_mmio && cur.uncached_unit.valid) {
    if (cur.uncached_unit.is_load) {
      auto &entry = nxt.ldq[cur.uncached_unit.idx];
      if (nxt.finish_count < LDQ_SIZE) {
        if (entry.load_state == LoadState::WaitMmioResp) {
          entry.result = in.peripheral_resp->mmio_rdata;
          entry.load_state = LoadState::ReadyToWb;
          lsu_perf::finish_mem_inst(ctx, entry);

          enqueue_finish(cur.uncached_unit.idx);
        }
        nxt.uncached_unit.valid = false; // MMIO响应完成后清除uncached unit的valid信号
      }
    } else {
      auto &entry = nxt.stq[cur.uncached_unit.idx];
      if (entry.store_state == StoreState::WaitMmioResp) {
        entry.store_state = StoreState::Done; // MMIO store在收到响应后就可以认为完成了
        lsu_perf::finish_mem_inst(ctx, entry);
      }
      nxt.uncached_unit.valid = false; // MMIO响应完成后清除uncached unit的valid信号
    }
  }
}

void RealLsu::comb_tlb_out() {
  *out.lsu2mmu = {};

  out.lsu2mmu->csr_status = *in.csr_status;

  uint32_t ld_issue = 0;
  uint32_t issue_ldq = std::min<uint32_t>(nxt.wait_mmu_ldq_count, LSU_LOAD_WINDOW_WIDTH);
  for (uint32_t i = 0; i < issue_ldq && ld_issue < LSU_LDU_COUNT; i++) {
    const uint32_t wait_idx = (nxt.wait_mmu_ldq_head + i) % LDQ_SIZE;
    const auto &entry = nxt.wait_mmu_ldq[wait_idx];
    const LdqEntry &ldq_entry = nxt.ldq[entry.ldq_idx];
    if (entry.valid && ldq_entry.load_state == LoadState::WaitTlb && entry.reserve) {
      auto &req = out.lsu2mmu->ldq_req[ld_issue++];
      req.valid = true;
      req.vaddr = ldq_entry.v_addr;
      req.entry_idx = entry.ldq_idx;
      req.wait_idx = wait_idx;
    }
  }

  uint32_t st_issue = 0;
  uint32_t issue_stq = std::min<uint32_t>(nxt.wait_mmu_stq_count, LSU_STORE_WINDOW_WIDTH);
  for (uint32_t i = 0; i < issue_stq && st_issue < LSU_STA_COUNT; i++) {
    const uint32_t wait_idx = (nxt.wait_mmu_stq_head + i) % STQ_SIZE;
    const auto &entry = nxt.wait_mmu_stq[wait_idx];
    const StqEntry &stq_entry = nxt.stq[entry.stq_idx];
    if (entry.valid && stq_entry.store_state == StoreState::WaitTlb && entry.reserve) {
      auto &req = out.lsu2mmu->stq_req[st_issue++];
      req.valid = true;
      req.vaddr = stq_entry.vaddr;
      req.entry_idx = entry.stq_idx;
      req.wait_idx = wait_idx;
    }
  }
}
void RealLsu::comb_tlb_in_ldq() {

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    if (in.mmu2lsu->ldq_resp[i].valid) {
      const auto &resp = in.mmu2lsu->ldq_resp[i];
      const uint32_t wait_idx = resp.wait_idx;
      const uint32_t ldq_idx = resp.entry_idx;
      if (wait_idx >= LDQ_SIZE || ldq_idx >= LDQ_SIZE) {
        continue;
      }
      auto &entry = nxt.wait_mmu_ldq[wait_idx];
      if (!entry.valid || entry.ldq_idx != ldq_idx || !entry.reserve) {
        continue;
      }
      LdqEntry &ldq_entry = nxt.ldq[ldq_idx];
      if (ldq_entry.load_state != LoadState::WaitTlb) {
        nxt.wait_mmu_ldq[wait_idx].valid = false;
        continue;
      }

      if (resp.result == MMUResultType::HIT) {
        ldq_entry.p_addr_valid = true;
        ldq_entry.p_addr = resp.paddr;
        ldq_entry.is_mmio = lsu_is_mmio_addr(resp.paddr);
        ldq_entry.load_state = LoadState::WaitSTLF;
      } else if (resp.result == MMUResultType::PAGE_FAULT) {
        ldq_entry.page_fault = true;
        ldq_entry.diag_val = ldq_entry.v_addr;
        ldq_entry.result = ldq_entry.v_addr;
        ldq_entry.load_state = LoadState::WaitFinish; // 页面错误的load进入WaitFinish状态，由后续逻辑处理异常
        nxt.lrsc_unit.reserve_valid = false;
      } else if (resp.result == MMUResultType::MISS) {
        ldq_entry.load_state = LoadState::WaitTlbRetry;
      }
    }
  }
  uint32_t stlf_issue = 0;
  uint32_t finish_issue = 0;
  uint32_t issue_ldq = std::min<uint32_t>(nxt.wait_mmu_ldq_count, LSU_LOAD_WINDOW_WIDTH);
  uint32_t issue_head = nxt.wait_mmu_ldq_head;
  for (int i = 0; i < issue_ldq; i++) {
    const uint32_t wait_idx = (issue_head + i) % LDQ_SIZE;
    auto &entry = nxt.wait_mmu_ldq[wait_idx];
    LdqEntry &ldq_entry = nxt.ldq[entry.ldq_idx];
    if (entry.valid && entry.reserve) {
      if (ldq_entry.load_state == LoadState::WaitSTLF) {
        if (stlf_issue < LSU_LDU_COUNT && nxt.stlf_queue_count < LDQ_SIZE) {
          stlf_issue++;
          ldq_entry.load_state = LoadState::CheckStlf; // TLB Miss的load在STLF队列中等待检查是否有STLF冲突
          enqueue_stlf(entry.ldq_idx);
          entry.valid = false; // 从等待TLB的队列中移除，进入STLF队列等待STLF检查完成
          entry.reserve = false;
        }
      } else if (ldq_entry.load_state == LoadState::WaitFinish) {
        if (finish_issue < LSU_LDU_COUNT && nxt.finish_count < LDQ_SIZE) {
          finish_issue++;
          ldq_entry.load_state = LoadState::PageFault; // 页面错误的load直接进入完成状态，由后续逻辑处理异常
          enqueue_finish(entry.ldq_idx);
          entry.valid = false; // 从等待TLB的队列中移除，进入完成队列等待异常处理
          entry.reserve = false;
        }
      }
    }
  }

  issue_ldq = std::min<uint32_t>(nxt.wait_mmu_ldq_count, LSU_LOAD_WINDOW_WIDTH);
  for (int i = 0; i < issue_ldq; i++) {
    const uint32_t wait_idx = (issue_head + i) % LDQ_SIZE;
    auto &entry = nxt.wait_mmu_ldq[wait_idx];
    if (!entry.valid && !entry.reserve) {
      dequeue_wait_mmu_ldq();
    } else if(entry.valid && entry.reserve && nxt.ldq[entry.ldq_idx].load_state == LoadState::WaitTlbRetry) {
      requeue_wait_mmu_ldq(); // TLB Miss的load重新进入等待TLB的队列等待重试，需要从当前等待TLB的队列中移除后重新加入等待TLB的队列
      nxt.ldq[entry.ldq_idx].load_state = LoadState::WaitTlb; // 重新设置load状态为等待TLB
    }
    else {
      break;
    }
  }
}
void RealLsu::comb_tlb_in_stq() {
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.mmu2lsu->stq_resp[i].valid) {
      const auto &resp = in.mmu2lsu->stq_resp[i];
      const uint32_t wait_idx = resp.wait_idx;
      const uint32_t stq_idx = resp.entry_idx;
      if (wait_idx >= STQ_SIZE || stq_idx >= STQ_SIZE) {
        continue;
      }
      auto &entry = nxt.wait_mmu_stq[wait_idx];
      if (!entry.valid || entry.stq_idx != stq_idx) {
        continue;
      }
      StqEntry &stq_entry = nxt.stq[stq_idx];

      if (stq_entry.store_state == StoreState::WaitTlb) {
        if (resp.result == MMUResultType::HIT) {
          stq_entry.paddr_valid = true;
          stq_entry.paddr = resp.paddr;
          stq_entry.is_mmio = lsu_is_mmio_addr(resp.paddr);

          if (stq_entry.data_valid) {
            stq_entry.store_state = StoreState::WaitDone;
          } else {
            stq_entry.store_state = StoreState::WaitData;
          }
        } else if (resp.result == MMUResultType::PAGE_FAULT) {
          stq_entry.page_fault = true;
          stq_entry.store_state = StoreState::WaitDone; // 进入页面错误状态，由后续逻辑处理异常
          nxt.lrsc_unit.reserve_valid = false;          // 如果store发生页面错误，取消LRSC保留
        } else if (resp.result == MMUResultType::MISS) {
          stq_entry.store_state = StoreState::WaitTlbRetry; // TLB Miss的store重新进入等待TLB的状态等待重试
        }
      }
    }
  }

  uint32_t issue_stq = std::min<uint32_t>(nxt.wait_mmu_stq_count, LSU_STORE_WINDOW_WIDTH);
  uint32_t issue_head = nxt.wait_mmu_stq_head;
  for (int i = 0; i < issue_stq; i++) {
    const uint32_t wait_idx = (issue_head + i) % STQ_SIZE;
    auto &entry = nxt.wait_mmu_stq[wait_idx];
    StqEntry &stq_entry = nxt.stq[entry.stq_idx];
    if (entry.valid) {
      if (stq_entry.store_state == StoreState::WaitDone) {
        if (nxt.mmu_done_stq_count < STQ_SIZE) {
          if (stq_entry.page_fault) {
            stq_entry.store_state = StoreState::PageFault; // 页面错误的store直接进入页面错误状态，由后续逻辑处理异常
          } else {
            stq_entry.store_state = StoreState::Ready; // TLB Miss的store在完成队列中等待完成
          }
          lsu_perf::finish_mem_inst(ctx, stq_entry);
          activate_mmu_done_stq(stq_entry.done_idx);
          entry.valid = false; // 从等待TLB的队列中移除，进入完成队列等待完成
          entry.reserve = false;
        }
      }else if (stq_entry.store_state == StoreState::WaitData) {
        entry.valid = false; // 从等待TLB的队列中移除，进入数据准备状态等待数据准备完成，数据准备完成后会重新进入等待TLB的队列等待TLB重试
        entry.reserve = false;
      
      }
    }
  }

  issue_stq = std::min<uint32_t>(nxt.wait_mmu_stq_count, LSU_STORE_WINDOW_WIDTH);
  for (int i = 0; i < issue_stq; i++) {
    const uint32_t wait_idx = (issue_head + i) % STQ_SIZE;
    auto &entry = nxt.wait_mmu_stq[wait_idx];
    if (!entry.valid && !entry.reserve) {
      dequeue_wait_mmu_stq();
    } else if(entry.valid && entry.reserve && nxt.stq[entry.stq_idx].store_state == StoreState::WaitTlbRetry) {
      requeue_wait_mmu_stq();
      nxt.stq[entry.stq_idx].store_state = StoreState::WaitTlb; // 重新设置store状态为等待TLB
    }
    else {
      break;
    }
  }
}
void RealLsu::comb_stlf() {

  const uint32_t issue = std::min<uint32_t>(nxt.stlf_queue_count, LSU_LOAD_WINDOW_WIDTH);
  uint32_t issue_deal = 0;

  //TODO 限制issue_deal数量，避免单周期处理过多STLF检查导致时序压力过大
  for (uint32_t i = 0; i < issue; i++) {
    const uint32_t stlf_idx = (nxt.stlf_queue_head+i)%LDQ_SIZE;
    const STLFEntry stlf_entry = nxt.stlf_queue[stlf_idx];

    if (!stlf_entry.valid) {
      continue;
    }

    const uint32_t ldq_idx = stlf_entry.ldq_idx;
    LdqEntry &entry = nxt.ldq[ldq_idx];

    if (entry.load_state == LoadState::Empty) {
      continue;
    }
    if (entry.load_state != LoadState::CheckStlf) {
      continue;
    }

    uint32_t older_store_count = 0;
    const bool boundary_ok = stq_distance_from_head_to_boundary(
        nxt.stq_head,
        nxt.stq_head_flag,
        nxt.stq_count,
        entry.stq_snapshot,
        older_store_count);

    if (!boundary_ok) {
      LSU_NON_BSD_ASSERT(0 && "LDQ STQ snapshot boundary is outside active STQ window");
      continue;
    }

    if (entry.is_mmio) {
      if (older_store_count == 0 &&
          lsu_mmio_is_oldest_unfinished(in.rob_bcast, entry.rob_idx) &&
          !nxt.uncached_unit.valid) {
        entry.load_state = LoadState::WaitMmioResp;
        lsu_perf::start_mem_inst(ctx, entry);
        nxt.uncached_unit.valid = true;
        nxt.uncached_unit.is_load = true;
        nxt.uncached_unit.addr = entry.p_addr;
        nxt.uncached_unit.func3 = entry.func3;
        nxt.uncached_unit.idx = ldq_idx;
        lsu_perf::count_mmio_load_issue(ctx);
      } else {
        entry.load_state = LoadState::CheckStlfRetry;
        lsu_perf::count_mmio_head_block(ctx);
      }
      continue;
    }
    uint32_t check_stlf_num = 0;
    for (int j = older_store_count - 1; j >= 0; j--) {
      const uint32_t stq_idx = (nxt.stq_head + j) % STQ_SIZE;
      const StqEntry &stq_entry = nxt.stq[stq_idx];
      if (stq_entry.store_state == StoreState::Allocated) {
        entry.load_state = LoadState::CheckStlfRetry;
        break;
      }
      if (!stq_entry.paddr_valid) {
        lsu_perf::count_stlf_unknown_store_block(ctx);
        entry.load_state = LoadState::CheckStlfRetry;
        break;
      }
      lsu_perf::count_stlf_check(ctx);
      STLFResult stlf_result = check_stlf(entry.p_addr, entry.func3, stq_entry.paddr, stq_entry.func3);
      if (stlf_result == STLFResult::Overlap) {
        if (!stq_entry.data_valid) {
          entry.load_state = LoadState::CheckStlfRetry;
          break;
        }
        const uint32_t forward_offset = entry.p_addr - stq_entry.paddr;
        entry.result = extract_data(stq_entry.data, forward_offset, entry.func3);
        entry.load_state = LoadState::WaitFinish; // STLF命中且数据已准备好，直接进入完成状态
        break;
      } else if (stlf_result == STLFResult::Retry) {
        entry.load_state = LoadState::CheckStlfRetry;
        break;
      } else {
        check_stlf_num++;
      }
    }

    if (check_stlf_num == older_store_count) {
      entry.load_state = LoadState::WaitDcache;
    }
  }
  for (int i = 0; i < issue; i++) {
    const uint32_t stlf_idx = (nxt.stlf_queue_head + i) % LDQ_SIZE;
    auto &stlf_entry = nxt.stlf_queue[stlf_idx];
    LdqEntry &entry = nxt.ldq[stlf_entry.ldq_idx];
    if (stlf_entry.valid) {
      if (entry.load_state == LoadState::WaitDcache) {
        if (nxt.wait_dcache_ldq_count < LDQ_SIZE) {
          enqueue_wait_dcache_ldq(stlf_entry.ldq_idx); // 通过STLF检查的load进入等待DCACHE的队列等待发出DCACHE请求
          entry.load_state = LoadState::ReadyToIssue;  // 设置load状态为等待DCACHE
          stlf_entry.valid = false;                    // 从STLF队列中移除，等待进入等待DCACHE的队列时重新设置entry内容
        }
      } else if (entry.load_state == LoadState::WaitFinish) {
        if (nxt.finish_count < LDQ_SIZE) {
          enqueue_finish(stlf_entry.ldq_idx);      // 通过STLF检查且数据已准备好的load进入完成队列等待完成
          entry.load_state = LoadState::ReadyToWb; // 设置load状态为等待完成
          stlf_entry.valid = false;                // 从STLF队列中移除，等待进入完成队列时重新设置entry内容
        }
      } else if(entry.load_state == LoadState::WaitMmioResp) {
        // MMIO load在STLF检查阶段直接进入等待MMIO响应状态，不需要重试也不需要进入等待DCACHE的队列
        stlf_entry.valid = false; // 从STLF队列中移除，等待MMIO响应时不需要保留STLF entry了
      }
    }
  }

  uint32_t stlf_head = nxt.stlf_queue_head;
  for (int i = 0; i < issue; i++) {
    const uint32_t stlf_idx = (stlf_head + i) % LDQ_SIZE;
    if(nxt.stlf_queue[stlf_idx].valid&&nxt.ldq[nxt.stlf_queue[stlf_idx].ldq_idx].load_state == LoadState::CheckStlfRetry) {
      dequeue_stlf(); // 需要重试的load已经在上面处理过了，从队列中移除
      nxt.stlf_queue[stlf_idx].valid = false; // 当前entry无论如何都不应该再保留在STLF队列中了
      enqueue_stlf(nxt.stlf_queue[stlf_idx].ldq_idx); // 重新加入STLF队列等待下一轮检查
      nxt.ldq[nxt.stlf_queue[stlf_idx].ldq_idx].load_state = LoadState::CheckStlf; // 重新设置load状态为等待STLF检查
    }
    else if(!nxt.stlf_queue[stlf_idx].valid) {
      dequeue_stlf(); // 已经在上面处理过的entry无论如何都不应该再保留在STLF队列中了，从队列中移除
    }
    else if(nxt.stlf_queue[stlf_idx].valid){
      break; // 还未处理的entry后面的一定也还未处理，直接跳出循环等待下一轮处理
    }
  }
}
void RealLsu::comb_lsu2dcache_ldq() {
  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    out.lsu2dcache->req_ports.load_ports[i].valid = false;
    lsu_perf::clear_replay(out.lsu2dcache->req_ports.load_ports[i]);
  }

  lsu_perf::retry_load_resp_timeouts(ctx, cur, nxt);

  uint32_t issued = 0;
  uint32_t scan_windows = nxt.wait_dcache_ldq_count > LSU_LOAD_WINDOW_WIDTH
                              ? LSU_LOAD_WINDOW_WIDTH
                              : nxt.wait_dcache_ldq_count;

  for (uint32_t scan = 0; scan < scan_windows && issued < LSU_LDU_COUNT; scan++) {
    const uint32_t wait_idx =
        (nxt.wait_dcache_ldq_head + scan) % LDQ_SIZE;
    auto &entry = nxt.wait_dcache_ldq[wait_idx];

    if (!entry.valid) {
      continue;
    }

    auto &nxt_ldq_entry = nxt.ldq[entry.ldq_idx];

    if (nxt_ldq_entry.load_state != LoadState::ReadyToIssue) {
      continue;
    }

    if (entry.replay_wait_cycles > 0) {
      entry.replay_wait_cycles--;
      if (entry.replay_wait_cycles > 0) {
        continue;
      }
    }

    LSU_NON_BSD_ASSERT(!nxt_ldq_entry.is_mmio &&
                       "MMIO load must not be issued to DCache");

    out.lsu2dcache->req_ports.load_ports[issued].valid = true;
    out.lsu2dcache->req_ports.load_ports[issued].addr = nxt_ldq_entry.p_addr;

    const uint32_t gen = normalize_lsu_req_gen(cur.req_gen + issued);
    const bool is_replay = lsu_perf::is_load_replay(nxt_ldq_entry);

    out.lsu2dcache->req_ports.load_ports[issued].req_id =
        make_lsu_load_req_id(wait_idx, gen);
    lsu_perf::set_replay(out.lsu2dcache->req_ports.load_ports[issued],
                         is_replay);
    lsu_perf::count_load_dcache_issue(ctx, is_replay);

    nxt.wait_dcache_ldq[wait_idx].req_gen = gen;
    lsu_perf::mark_wait_start(ctx, nxt.wait_dcache_ldq[wait_idx]);

    nxt_ldq_entry.load_state = LoadState::WaitDcacheResp;
    lsu_perf::start_mem_inst_if_needed(ctx, nxt_ldq_entry);

    issued++;
  }

  nxt.req_gen = normalize_lsu_req_gen(cur.req_gen + issued);
}

void RealLsu::comb_dcache2lsu_ldq() {

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    if (in.dcache2lsu->resp_ports.load_resps[i].valid) {
      uint32_t req_id = in.dcache2lsu->resp_ports.load_resps[i].req_id;

      if (req_id & (1u << 31)) {
        LSU_NON_BSD_ASSERT(0 && "Invalid load response ID from DCache");
        continue; // 理论上 MemRouteBlock 应该过滤掉，LSU 不处理非 LSU response
      }

      uint32_t entry_idx = lsu_req_id_wait_idx(req_id);
      uint32_t resp_gen = lsu_req_id_gen(req_id);

      const auto &wait_entry = nxt.wait_dcache_ldq[entry_idx];

      if (!wait_entry.valid) {
        lsu_perf::count_stale_load_resp(ctx);
        continue; // stale response after flush/replay queue movement
      }

      if (wait_entry.req_gen != resp_gen) {
        lsu_perf::count_stale_load_resp(ctx);
        lsu_report_load_req_gen_mismatch(
            i, req_id, entry_idx, resp_gen,
            static_cast<unsigned>(wait_entry.req_gen),
            static_cast<unsigned>(wait_entry.ldq_idx),
            static_cast<unsigned>(cur.wait_dcache_ldq_head),
            static_cast<unsigned>(cur.wait_dcache_ldq_count));
        LSU_NON_BSD_ASSERT(0 && "LSU load response req_gen mismatch");
        continue;
      }

      uint32_t ldq_idx = wait_entry.ldq_idx;
      if (wait_entry.valid) {
        LdqEntry &entry = nxt.ldq[ldq_idx];
        if (entry.load_state == LoadState::WaitDcacheResp) {
          if (in.dcache2lsu->resp_ports.load_resps[i].replay == ReplayType::HIT) {
            entry.result = extract_data(in.dcache2lsu->resp_ports.load_resps[i].data, entry.p_addr, entry.func3);
            entry.load_state = LoadState::ReadyToWb;
            lsu_perf::mark_load_hit(entry);
            lsu_perf::finish_mem_inst(ctx, entry);

          } else {
            const ReplayType replay =
                in.dcache2lsu->resp_ports.load_resps[i].replay;
            nxt.ldq[ldq_idx].load_state = LoadState::ReadyToIssue;
            lsu_perf::mark_load_replay(entry, replay);
            nxt.wait_dcache_ldq[entry_idx].replay_wait_cycles =
                replay_delay_cycles(replay);
          }
        }
      }
    }
  }
  wake_load_replays_on_mshr_fill(*in.dcache2lsu, nxt);

  uint32_t wait_count = std::min<uint32_t>(nxt.wait_dcache_ldq_count, LSU_LOAD_WINDOW_WIDTH);
  for (int i = 0; i < wait_count; i++) {
    const uint32_t wait_idx = (nxt.wait_dcache_ldq_head + i) % LDQ_SIZE;
    auto &entry = nxt.wait_dcache_ldq[wait_idx];
    auto &ldq_entry = nxt.ldq[entry.ldq_idx];
    if (entry.valid) {
      if (ldq_entry.load_state == LoadState::ReadyToWb && nxt.finish_count < LDQ_SIZE) {
        enqueue_finish(entry.ldq_idx);
        entry.valid = false;
      }
    }
  }

  uint32_t wait_head = nxt.wait_dcache_ldq_head;
  for (int i = 0; i < wait_count; i++) {
    const uint32_t wait_idx = (wait_head + i) % LDQ_SIZE;
    if (nxt.wait_dcache_ldq[wait_idx].valid) {
      break;
    } else {
      dequeue_wait_dcache_ldq(); // 已经在上面处理过的entry无论如何都不应该再保留在等待DCACHE的队列中了，从队列中移除
    }
  }
}

void RealLsu::comb_dcache2lsu_stq() {

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.dcache2lsu->resp_ports.store_resps[i].valid) {
      uint32_t stq_idx = in.dcache2lsu->resp_ports.store_resps[i].req_id;
      if (stq_idx >= STQ_SIZE) {
        LSU_NON_BSD_ASSERT(0 && "Store response idx out of bound");
        continue;
      }
      StqEntry &entry = nxt.stq[stq_idx];
      if (entry.store_state == StoreState::WaitDcacheResp) {
        if (in.dcache2lsu->resp_ports.store_resps[i].replay == ReplayType::HIT) {
          entry.store_state = StoreState::Done; // store完成，可以提交了
          lsu_perf::count_store_dcache_hit(ctx);
          lsu_perf::mark_store_hit(entry);
          entry.replay_wait_cycles = 0;
          lsu_perf::finish_mem_inst(ctx, entry);
        } else {
          const ReplayType replay =
              in.dcache2lsu->resp_ports.store_resps[i].replay;
          entry.store_state = StoreState::Committed; // store重放，进入重放状态等待dcache fill
          lsu_perf::count_store_replay_resp(ctx, replay);
          lsu_perf::mark_store_replay(entry, replay);
          entry.replay_wait_cycles = replay_delay_cycles(replay);
        }
      }
    }
  }
  wake_store_replays_on_mshr_fill(*in.dcache2lsu, nxt);
}

void RealLsu::comb_lsu2dcache_stq() {
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    out.lsu2dcache->req_ports.store_ports[i].valid = false; // 默认不发出store请求，后续根据STQ条目状态决定是否发出请求
    lsu_perf::clear_replay(out.lsu2dcache->req_ports.store_ports[i]);
  }

  uint32_t commit_count = std::min<uint32_t>(nxt.stq_commit_count, LSU_STORE_WINDOW_WIDTH);
  lsu_perf::sample_stq_issue_window(ctx, nxt, commit_count);

  int32_t issued_stq = 0;
  for (uint32_t i = 0; i < commit_count && issued_stq < LSU_STA_COUNT; i++) {
    const uint32_t stq_idx = (nxt.stq_head + i) % STQ_SIZE;
    const auto &cur_entry = cur.stq[stq_idx];
    auto &nxt_entry = nxt.stq[stq_idx];
    uint8_t entry_strb = get_store_strb(nxt_entry.paddr, nxt_entry.func3);

    if (nxt_entry.store_state == StoreState::Committed &&
        cur_entry.store_state != StoreState::WaitDcacheResp) {
      if (nxt_entry.replay_wait_cycles > 0) {
        nxt_entry.replay_wait_cycles--;
        if (nxt_entry.replay_wait_cycles > 0) {
          continue;
        }
      }

      bool has_older_unfinished_store = false;
      for (uint32_t j = 0; j < i; j++) {
        const uint32_t older_stq_idx = (nxt.stq_head + j) % STQ_SIZE;
        uint8_t older_entry_strb = get_store_strb(nxt.stq[older_stq_idx].paddr, nxt.stq[older_stq_idx].func3);
        if (CheckAddr(nxt.stq[older_stq_idx].paddr, older_entry_strb, nxt_entry.paddr, entry_strb) && nxt.stq[older_stq_idx].store_state != StoreState::Done) {
          nxt_entry.store_state = StoreState::Committed; // 还有更老的store没有完成，当前store继续保持在提交状态等待更老的store完成
          has_older_unfinished_store = true;
          lsu_perf::count_same_addr_block(
              ctx, nxt.stq[older_stq_idx].store_state, i - j);
          break;
        }
      }

      if (has_older_unfinished_store) {
        continue;
      }

      if (nxt_entry.is_mmio == true) {
        if (i == 0 && !nxt.uncached_unit.valid) {
          nxt.uncached_unit.valid = true;
          nxt.uncached_unit.is_load = false;
          nxt.uncached_unit.addr = nxt_entry.paddr;
          nxt.uncached_unit.wdata = nxt_entry.data;
          nxt.uncached_unit.func3 = nxt_entry.func3;
          nxt.uncached_unit.idx = stq_idx;                  // 将准备好发出请求的store的STQ索引传递给uncached unit，方便后续处理响应时找到对应的STQ条目
          nxt_entry.store_state = StoreState::WaitMmioResp; // MMIO store发出后进入等待MMIO响应状态
          lsu_perf::start_mem_inst(ctx, nxt_entry);
          lsu_perf::count_mmio_store_issue(ctx);
        } else {
          lsu_perf::count_mmio_head_block(ctx);
        }
      } else {
        if (nxt_entry.suppress_write) {
          nxt_entry.store_state = StoreState::Done;
          nxt_entry.replay_wait_cycles = 0;
          lsu_perf::count_suppress_store_done(ctx);
          continue;
        }
        out.lsu2dcache->req_ports.store_ports[issued_stq].valid = true;
        out.lsu2dcache->req_ports.store_ports[issued_stq].addr = nxt_entry.paddr;
        out.lsu2dcache->req_ports.store_ports[issued_stq].data =
            align_store_data(nxt_entry.data, nxt_entry.paddr, nxt_entry.func3);
        out.lsu2dcache->req_ports.store_ports[issued_stq].strb = entry_strb;
        out.lsu2dcache->req_ports.store_ports[issued_stq].req_id = stq_idx;
        const bool is_replay = lsu_perf::is_store_replay(nxt_entry);
        lsu_perf::set_replay(
            out.lsu2dcache->req_ports.store_ports[issued_stq], is_replay);
        lsu_perf::count_store_dcache_issue(ctx, is_replay);
        nxt_entry.store_state = StoreState::WaitDcacheResp; // 发出store请求后，进入等待dcache响应状态
        lsu_perf::start_mem_inst_if_needed(ctx, cur_entry, nxt_entry);
        issued_stq++;
      }
    } else {
      // break;
    }
  }
  lsu_perf::count_store_issue_none(ctx, commit_count, issued_stq);
}

void RealLsu::comb_lsu2exe() {
  *out.lsu2exe = {};

  uint32_t issue = std::min<int32_t>(nxt.mmu_done_stq_count, LSU_STORE_WINDOW_WIDTH);
  uint32_t issue_st = 0;
  uint32_t issue_ld = 0;

  for (int i = 0; i < issue; i++) {
    const uint32_t done_idx = (nxt.mmu_done_stq_head + i) % STQ_SIZE;
    const auto entry = nxt.mmu_done_stq[done_idx];
    if (entry.valid && entry.reserve) {
      auto &stq_entry = nxt.stq[entry.stq_idx];
      if (stq_entry.store_state == StoreState::Ready || stq_entry.store_state == StoreState::PageFault) {
        if (stq_entry.is_lrsc) {
          if (issue_ld >= LSU_LDU_COUNT) {
            continue;
          }
          MicroOp wb_uop;
          wb_uop.op = UOP_LOAD; // 关键：让 ROB 看到 G0
          wb_uop.rob_idx = stq_entry.rob_idx;
          wb_uop.rob_flag = stq_entry.rob_flag;
          wb_uop.br_mask = stq_entry.br_mask;
          wb_uop.diag_val = stq_entry.vaddr;
          wb_uop.page_fault_store = stq_entry.page_fault;
          wb_uop.dest_en = false;

          if (stq_entry.page_fault) {
            wb_uop.result = stq_entry.vaddr;
          } else {
            const bool sc_pass =
                cur.lrsc_unit.reserve_valid &&
                cur.lrsc_unit.reserve_addr == stq_entry.paddr;

            stq_entry.sc_pass = sc_pass;
            stq_entry.suppress_write = !sc_pass;
            nxt.lrsc_unit.reserve_valid = false;

            wb_uop.result = sc_pass ? 0 : 1;
            wb_uop.dest_preg = stq_entry.dest_preg;
            wb_uop.dest_en = true;
          }

          out.lsu2exe->wb_req[issue_ld].valid = true;
          out.lsu2exe->wb_req[issue_ld].uop =
              LsuExeIO::LsuExeRespUop::from_micro_op(wb_uop);
          issue_ld++;
          stq_entry.store_state = StoreState::WaitCommit; // 已经发出写回请求，进入提交状态等待提交

          nxt.mmu_done_stq[done_idx].valid = false; // 无论如何都需要将MMU完成的STQ条目标记为无效
          nxt.mmu_done_stq[done_idx].reserve = false; // 相关的load如果还在完成队列中，直接标记为无效让它出列，避免后续错误地发出写回请求
          
        } else {
          if (issue_st >= LSU_STA_COUNT) {
            continue;
          }
          out.lsu2exe->sta_wb_req[issue_st].valid = true;
          MicroOp wb_uop;
          wb_uop.op = UOP_STA;
          wb_uop.rob_idx = stq_entry.rob_idx;
          wb_uop.rob_flag = stq_entry.rob_flag;
          wb_uop.br_mask = stq_entry.br_mask;
          wb_uop.diag_val = stq_entry.vaddr;
          wb_uop.result = stq_entry.vaddr;
          wb_uop.page_fault_store = stq_entry.page_fault;
          wb_uop.dest_en = false;

          out.lsu2exe->sta_wb_req[issue_st].uop =
              LsuExeIO::LsuExeRespUop::from_micro_op(wb_uop);
          stq_entry.store_state = StoreState::WaitCommit; // 已经发出写回请求，进入提交状态等待提交
          issue_st++;
          nxt.mmu_done_stq[done_idx].valid = false; // 无论如何都需要将MMU完成的STQ条目标记为无效
          nxt.mmu_done_stq[done_idx].reserve = false; // 相关的load如果还在完成队列中，直接标记为无效让它出列，避免后续错误地发出写回请求
        }
      }
    }
  }

  uint32_t mmu_done_head = nxt.mmu_done_stq_head;
  issue = std::min<int32_t>(nxt.mmu_done_stq_count, LSU_STORE_WINDOW_WIDTH);
  for (int i = 0; i < issue; i++) {
    const uint32_t done_idx = (mmu_done_head + i) % STQ_SIZE;
    if (nxt.mmu_done_stq[done_idx].valid) {
      break;
    } else {
      if (!nxt.mmu_done_stq[done_idx].reserve) {
        dequeue_mmu_done_stq();
      }
    }
  }

  issue = std::min<int32_t>(nxt.finish_count, LSU_LOAD_WINDOW_WIDTH);
  for (int i = 0; i < issue && issue_ld < LSU_LDU_COUNT; i++) {
    const uint32_t finish_idx = (nxt.finish_head + i) % LDQ_SIZE;
    const auto entry = nxt.finish[finish_idx];
    if (entry.valid) {
      auto &ldq_entry = nxt.ldq[entry.ldq_idx];
      if (ldq_entry.load_state == LoadState::ReadyToWb || ldq_entry.load_state == LoadState::PageFault) {
        out.lsu2exe->wb_req[issue_ld].valid = true;
        MicroOp wb_uop;
        wb_uop.op = UOP_LOAD;
        wb_uop.rob_idx = ldq_entry.rob_idx;
        wb_uop.rob_flag = ldq_entry.rob_flag;
        wb_uop.br_mask = ldq_entry.br_mask;
        wb_uop.diag_val =
            ldq_entry.page_fault ? ldq_entry.diag_val : ldq_entry.p_addr;
        wb_uop.result =
            ldq_entry.page_fault ? ldq_entry.diag_val : ldq_entry.result;
        wb_uop.dest_preg = ldq_entry.dest_preg;
        wb_uop.page_fault_load = ldq_entry.page_fault;
        wb_uop.dest_en = true;

        wb_uop.dbg.difftest_skip = !ldq_entry.page_fault && lsu_is_timer_addr(ldq_entry.p_addr);
        out.lsu2exe->wb_req[issue_ld].uop =
            LsuExeIO::LsuExeRespUop::from_micro_op(wb_uop);

        ldq_entry.load_state = LoadState::Empty; // load完成，进入完成状态等待提交
        issue_ld++;
        nxt.finish[finish_idx].valid = false; // 无论如何都需要将完成的条目标记为无效

        if (ldq_entry.is_lrsc && !ldq_entry.page_fault) {
          nxt.lrsc_unit.reserve_valid = true;
          nxt.lrsc_unit.reserve_addr = ldq_entry.p_addr;
          nxt.lrsc_unit.reserve_rob_idx = ldq_entry.rob_idx;
          nxt.lrsc_unit.reserve_rob_flag = ldq_entry.rob_flag;
          nxt.lrsc_unit.reserve_br_mask = ldq_entry.br_mask;
        }
      }
    }
  }
  uint32_t finish_head = nxt.finish_head;
  for (int i = 0; i < issue; i++) {
    const uint32_t finish_idx = (finish_head + i) % LDQ_SIZE;
    if (nxt.finish[finish_idx].valid) {
      break;
    } else {
      dequeue_finish(); // 已经在上面处理过的entry无论如何都不应该再保留在完成的队列中了，从队列中移除
    }
  }
}
void RealLsu::comb_stq_commit() {
  auto commit_stq_entry = [&]() {
    advance_ring_ptr(nxt.stq_commit, STQ_SIZE);
    if (nxt.stq_commit_count < STQ_SIZE) {
      nxt.stq_commit_count++;
    }
  };

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (!in.rob_commit->commit_entry[i].valid) {
      continue;
    }
    const auto &commit_uop = in.rob_commit->commit_entry[i].uop;
    if (!is_store(commit_uop)) {
      continue;
    }
    int idx = commit_uop.stq_idx;
    if (idx == nxt.stq_commit && nxt.stq[idx].store_state == StoreState::WaitCommit) {
      StqEntry &nxt_entry = nxt.stq[idx];

      if (nxt_entry.page_fault) {
        nxt_entry.store_state = StoreState::Done;
        nxt_entry.suppress_write = true;
        commit_stq_entry();
      } else if (nxt_entry.is_lrsc && !nxt_entry.sc_pass) {
        nxt_entry.suppress_write = true;
        nxt_entry.store_state = StoreState::Done;
        commit_stq_entry();
      } else {
        nxt_entry.store_state = StoreState::Committed;
        commit_stq_entry();
      }

    } else {
      LSU_NON_BSD_ASSERT(0 && "Store commit out of order?");
    }
  }
}

void RealLsu::comb_check() {

  uint32_t committed_count = nxt.stq_commit_count;
  if (committed_count > nxt.stq_count) {
    committed_count = nxt.stq_count;
  }
  uint32_t issue = std::min<uint32_t>(committed_count, LSU_STORE_WINDOW_WIDTH);
  uint32_t retired_stq = 0;
  for (int i = 0; i < issue; i++) {
    const uint32_t stq_idx = nxt.stq_head;
    if (nxt.stq[stq_idx].store_state == StoreState::Done) {
      nxt.stq_count--;
      if (nxt.stq_commit_count > 0) {
        nxt.stq_commit_count--;
      }
      advance_ring_ptr(nxt.stq_head, nxt.stq_head_flag, STQ_SIZE);
      retired_stq++;
    } else {
      break;
    }
  }
  lsu_perf::count_store_retire(ctx, committed_count, retired_stq);
}
void RealLsu::comb_flush() {
  if (in.rob_bcast->flush) {
    for (int i = 0; i < LDQ_SIZE; i++) {
      nxt.ldq[i].load_state = LoadState::Empty;
    }
    const uint32_t keep_committed =
        std::min<uint32_t>(nxt.stq_commit_count, nxt.stq_count);

    nxt.stq_count = keep_committed;
    nxt.stq_commit_count = keep_committed;
    nxt.stq_commit = stq_idx_after(nxt.stq_head, keep_committed);

    for (int i = 0; i < LDQ_SIZE; i++) {
      nxt.stlf_queue[i].valid = false;
      nxt.wait_dcache_ldq[i].valid = false;
      nxt.wait_mmu_ldq[i].valid = false;
    }
    for (int i = 0; i < STQ_SIZE; i++) {
      nxt.wait_mmu_stq[i].valid = false;
      nxt.mmu_done_stq[i].valid = false;
    }
    for (int i = 0; i < LDQ_SIZE; i++) {
      nxt.finish[i].valid = false;
    }

    nxt.wait_mmu_ldq_count = 0;
    nxt.wait_mmu_stq_count = 0;
    nxt.mmu_done_stq_count = 0;
    nxt.finish_count = 0;
    nxt.wait_dcache_ldq_count = 0;
    nxt.stlf_queue_count = 0;


    const bool keep_uncached_store =
        nxt.uncached_unit.valid && !nxt.uncached_unit.is_load &&
        stq_idx_alive_after_flush(nxt.uncached_unit.idx, nxt.stq_head,
                                  keep_committed) &&
        nxt.stq[nxt.uncached_unit.idx].store_state ==
            StoreState::WaitMmioResp;
    if (!keep_uncached_store) {
      nxt.uncached_unit.valid = false;
    }
    // nxt.lrsc_unit.reserve_valid = false;
  }

  if (in.dec_bcast->mispred) {
    for (int i = 0; i < LDQ_SIZE; i++) {
      auto &entry = nxt.ldq[i];
      if (entry.load_state != LoadState::Empty && (entry.br_mask & in.dec_bcast->br_mask) != 0) {
        entry.load_state = LoadState::Empty; // 将第一个需要清除的条目之前的条目保留，之后的条目全部清除
      }
    }

    uint32_t old_stq_count = nxt.stq_count;
    uint32_t keep_stq_count = old_stq_count;
    for (uint32_t i = 0; i < old_stq_count; i++) {
      if ((nxt.stq[(nxt.stq_head + i) % STQ_SIZE].br_mask & in.dec_bcast->br_mask) != 0) {
        keep_stq_count = i; // 将第一个需要清除的条目之前的条目保留，之后的条目全部清除
        break;
      }
    }
    nxt.stq_count = keep_stq_count;

    for (int i = 0; i < nxt.wait_mmu_ldq_count; i++) {
      if (nxt.ldq[nxt.wait_mmu_ldq[(nxt.wait_mmu_ldq_head + i) % LDQ_SIZE].ldq_idx].load_state == LoadState::Empty) { // wait MMU的条目索引如果超过了新的LDQ count，说明这个条目需要被清除
        nxt.wait_mmu_ldq[(nxt.wait_mmu_ldq_head + i) % LDQ_SIZE].valid = 0;                                           // 将需要清除的条目无效化
        nxt.wait_mmu_ldq[(nxt.wait_mmu_ldq_head + i) % LDQ_SIZE].reserve = 0;   
      }
    }
    for (int i = 0; i < nxt.wait_mmu_stq_count; i++) {
      if (!stq_idx_alive_after_flush(nxt.wait_mmu_stq[(nxt.wait_mmu_stq_head + i) % STQ_SIZE].stq_idx, nxt.stq_head, nxt.stq_count)) { // wait MMU的条目索引如果超过了新的STQ count，说明这个条目需要被清除
        nxt.wait_mmu_stq[(nxt.wait_mmu_stq_head + i) % STQ_SIZE].valid = 0; 
        nxt.wait_mmu_stq[(nxt.wait_mmu_stq_head + i) % STQ_SIZE].reserve = 0;      
      }
    }
    for (int i = 0; i < nxt.mmu_done_stq_count; i++) {
      if (!stq_idx_alive_after_flush(nxt.mmu_done_stq[(nxt.mmu_done_stq_head + i) % STQ_SIZE].stq_idx, nxt.stq_head, nxt.stq_count)) { // MMU完成队列的条目索引如果超过了新的STQ count，说明这个条目需要被清除
        nxt.mmu_done_stq[(nxt.mmu_done_stq_head + i) % STQ_SIZE].valid = 0;                                                            // 将需要清除的条目无效化
        nxt.mmu_done_stq[(nxt.mmu_done_stq_head + i) % STQ_SIZE].reserve = 0;                                                               // 将需要清除的条目索引重置为0，避免后续错误地访问到这个条目
      }
    }
    for (int i = 0; i < nxt.stlf_queue_count; i++) {
      if (nxt.ldq[nxt.stlf_queue[(nxt.stlf_queue_head + i) % LDQ_SIZE].ldq_idx].load_state == LoadState::Empty) {
        nxt.stlf_queue[(nxt.stlf_queue_head + i) % LDQ_SIZE].valid = 0;
      }
    }
    for (int i = 0; i < nxt.finish_count; i++) {
      if (nxt.ldq[nxt.finish[(nxt.finish_head + i) % LDQ_SIZE].ldq_idx].load_state == LoadState::Empty) { // 完成队列中的load条目如果已经是空的了，说明这个条目需要被清除
        nxt.finish[(nxt.finish_head + i) % LDQ_SIZE].valid = 0;                                           // 将需要清除的条目无效化
      }
    }
    for (int i = 0; i < nxt.wait_dcache_ldq_count; i++) {
      if (nxt.ldq[nxt.wait_dcache_ldq[(nxt.wait_dcache_ldq_head + i) % LDQ_SIZE].ldq_idx].load_state == LoadState::Empty) { // wait dcache的条目索引如果超过了新的LDQ count，说明这个条目需要被清除
        nxt.wait_dcache_ldq[(nxt.wait_dcache_ldq_head + i) % LDQ_SIZE].valid = 0;                                           // 将需要清除的条目无效化
      }
    }
    if (nxt.uncached_unit.valid) {
      if (nxt.uncached_unit.is_load && nxt.ldq[nxt.uncached_unit.idx].load_state == LoadState::Empty) { // uncached unit中如果有load条目，并且索引超过了新的LDQ count，说明这个条目需要被清除
        nxt.uncached_unit.valid = false;                                                                // 将uncached unit无效化
      }
      if (!nxt.uncached_unit.is_load && !stq_idx_alive_after_flush(nxt.uncached_unit.idx, nxt.stq_head, nxt.stq_count)) { // uncached unit中如果有store条目，并且索引超过了新的STQ count，说明这个条目需要被清除
        nxt.uncached_unit.valid = false;                                                                                  // 将uncached unit无效化
      }
    }
    if (nxt.lrsc_unit.reserve_valid &&
        (nxt.lrsc_unit.reserve_br_mask & in.dec_bcast->br_mask) != 0) {
      nxt.lrsc_unit.reserve_valid = false;
    }

    nxt.stq_commit_count = std::min(nxt.stq_commit_count, nxt.stq_count);
    nxt.stq_commit = stq_idx_after(nxt.stq_head, nxt.stq_commit_count);
  }

  if (in.dec_bcast->clear_mask) {
    for (int i = 0; i < LDQ_SIZE; i++) {
      if (cur.ldq[i].load_state != LoadState::Empty) {   // 如果这个条目的br mask和需要清除的mask有交集，说明这个条目需要被清除
        nxt.ldq[i].br_mask &= ~in.dec_bcast->clear_mask; // 将需要清除的分支对应的br mask位清0
      }
    }
    for (int i = 0; i < cur.stq_count; i++) {
      nxt.stq[(cur.stq_head + i) % STQ_SIZE].br_mask &= ~in.dec_bcast->clear_mask; // 将需要清除的分支对应的br mask位清0
    }
    if (in.dec_bcast->clear_mask && nxt.lrsc_unit.reserve_valid) {
      nxt.lrsc_unit.reserve_br_mask &= ~in.dec_bcast->clear_mask;
    }
  }
}

void RealLsu::seq() {
  cur = nxt;
}

void RealLsu::dump_debug_state(FILE *out) const {
  if (out == nullptr) {
    out = stderr;
  }

  std::fprintf(out, "RealLsu State:\n");
  std::fprintf(out,
               "  LDQ: wait_mmu_head=%u wait_mmu_count=%u "
               "stlf_head=%u stlf_count=%u "
               "wait_dcache_head=%u wait_dcache_count=%u\n",
               static_cast<unsigned>(cur.wait_mmu_ldq_head),
               static_cast<unsigned>(cur.wait_mmu_ldq_count),
               static_cast<unsigned>(cur.stlf_queue_head),
               static_cast<unsigned>(cur.stlf_queue_count),
               static_cast<unsigned>(cur.wait_dcache_ldq_head),
               static_cast<unsigned>(cur.wait_dcache_ldq_count));
  std::fprintf(out,
               "  STQ: head=%u commit=%u head_flag=%u count=%u "
               "commit_count=%u "
               "wait_mmu_head=%u wait_mmu_count=%u mmu_done_head=%u "
               "mmu_done_count=%u\n",
               static_cast<unsigned>(cur.stq_head),
               static_cast<unsigned>(cur.stq_commit),
               static_cast<unsigned>(cur.stq_head_flag),
               static_cast<unsigned>(cur.stq_count),
               static_cast<unsigned>(cur.stq_commit_count),
               static_cast<unsigned>(cur.wait_mmu_stq_head),
               static_cast<unsigned>(cur.wait_mmu_stq_count),
               static_cast<unsigned>(cur.mmu_done_stq_head),
               static_cast<unsigned>(cur.mmu_done_stq_count));
  std::fprintf(out,
               "  Finish: head=%u count=%u\n",
               static_cast<unsigned>(cur.finish_head),
               static_cast<unsigned>(cur.finish_count));
  std::fprintf(out,
               "  Uncached: valid=%u is_load=%u addr=0x%08x idx=%u\n",
               static_cast<unsigned>(cur.uncached_unit.valid),
               static_cast<unsigned>(cur.uncached_unit.is_load),
               static_cast<unsigned>(cur.uncached_unit.addr),
               static_cast<unsigned>(cur.uncached_unit.idx));
  std::fprintf(out,
               "  LRSC: reserve_valid=%u reserve_addr=0x%08x\n",
               static_cast<unsigned>(cur.lrsc_unit.reserve_valid),
               static_cast<unsigned>(cur.lrsc_unit.reserve_addr));

  std::fprintf(out, "  LDQ active count=%u\n",
               static_cast<unsigned>(ldq_count));
  const uint32_t ldq_dump_count = ldq_count < 16 ? ldq_count : 16;
  std::fprintf(out, "  LDQ active entries (first %u):\n",
               static_cast<unsigned>(ldq_dump_count));
  uint32_t dumped_ldq_count = 0;
  for (uint32_t idx = 0; idx < LDQ_SIZE && dumped_ldq_count < ldq_dump_count;
       idx++) {
    const auto &entry = cur.ldq[idx];
    if (entry.load_state == LoadState::Empty) {
      continue;
    }
    std::fprintf(out,
                 "    [%u] state=%u rob=%u/%u vaddr=%u:0x%08x "
                 "paddr=%u:0x%08x stq_snapshot=%u/%u mmio=%u "
                 "page_fault=%u lrsc=%u\n",
                 static_cast<unsigned>(idx),
                 static_cast<unsigned>(entry.load_state),
                 static_cast<unsigned>(entry.rob_idx),
                 static_cast<unsigned>(entry.rob_flag),
                 static_cast<unsigned>(entry.v_addr_valid),
                 static_cast<unsigned>(entry.v_addr),
                 static_cast<unsigned>(entry.p_addr_valid),
                 static_cast<unsigned>(entry.p_addr),
                 static_cast<unsigned>(entry.stq_snapshot.idx),
                 static_cast<unsigned>(entry.stq_snapshot.flag),
                 static_cast<unsigned>(entry.is_mmio),
                 static_cast<unsigned>(entry.page_fault),
                 static_cast<unsigned>(entry.is_lrsc));
    dumped_ldq_count++;
  }

  const uint32_t stlf_dump_count =
      cur.stlf_queue_count < 8 ? cur.stlf_queue_count : 8;
  std::fprintf(out, "  STLF queue entries (first %u):\n",
               static_cast<unsigned>(stlf_dump_count));
  for (uint32_t i = 0; i < stlf_dump_count; i++) {
    const uint32_t idx = (cur.stlf_queue_head + i) % LDQ_SIZE;
    const auto &entry = cur.stlf_queue[idx];
    std::fprintf(out, "    [%u] valid=%u ldq_idx=%u\n",
                 static_cast<unsigned>(idx),
                 static_cast<unsigned>(entry.valid),
                 static_cast<unsigned>(entry.ldq_idx));
  }

  const uint32_t stq_dump_count = cur.stq_count < 8 ? cur.stq_count : 8;
  std::fprintf(out, "  STQ active entries (first %u):\n",
               static_cast<unsigned>(stq_dump_count));
  for (uint32_t i = 0; i < stq_dump_count; i++) {
    const uint32_t idx = (cur.stq_head + i) % STQ_SIZE;
    const auto &entry = cur.stq[idx];
    std::fprintf(out,
                 "    [%u] state=%u rob=%u/%u data=%u:0x%08x "
                 "vaddr=%u:0x%08x paddr=%u:0x%08x mmio=%u "
                 "page_fault=%u lrsc=%u sc_pass=%u\n",
                 static_cast<unsigned>(idx),
                 static_cast<unsigned>(entry.store_state),
                 static_cast<unsigned>(entry.rob_idx),
                 static_cast<unsigned>(entry.rob_flag),
                 static_cast<unsigned>(entry.data_valid),
                 static_cast<unsigned>(entry.data),
                 static_cast<unsigned>(entry.vaddr_valid),
                 static_cast<unsigned>(entry.vaddr),
                 static_cast<unsigned>(entry.paddr_valid),
                 static_cast<unsigned>(entry.paddr),
                 static_cast<unsigned>(entry.is_mmio),
                 static_cast<unsigned>(entry.page_fault),
                 static_cast<unsigned>(entry.is_lrsc),
                 static_cast<unsigned>(entry.sc_pass));
  }
}

void RealLsu::handle_store_data(const MicroOp &inst) {
  if (inst.stq_idx >= STQ_SIZE) {
    return;
  }
  StqEntry &entry = nxt.stq[inst.stq_idx];

  if (entry.rob_flag != inst.rob_flag || entry.rob_idx != inst.rob_idx) {
    LSU_NON_BSD_ASSERT(0 && "Store data mismatch");
    return;
  }
  entry.data = inst.result;
  entry.data_valid = true;
  if (entry.paddr_valid && entry.store_state == StoreState::WaitData) {
    entry.store_state = StoreState::Ready;
    activate_mmu_done_stq(entry.done_idx);
  }
}

void RealLsu::handle_store_addr(const MicroOp &inst) {
  if (inst.stq_idx >= STQ_SIZE) {
    return;
  }
  StqEntry &entry = nxt.stq[inst.stq_idx];

  if (entry.rob_flag != inst.rob_flag || entry.rob_idx != inst.rob_idx) {
    LSU_NON_BSD_ASSERT(0 && "Store addr mismatch");
    return;
  }
  entry.vaddr_valid = true;
  entry.vaddr = inst.result;

  entry.paddr_valid = false;
  entry.paddr = 0;

  entry.func3 = inst.func3;
  entry.dest_preg = inst.dest_preg;
  entry.store_state = StoreState::WaitTlb;

  entry.is_lrsc = inst.is_atomic && ((inst.func7 >> 2) == AmoOp::SC);

  activate_wait_mmu_stq(entry.mmu_idx);
}

void RealLsu::handle_load_req(const MicroOp &inst) {
  if (inst.ldq_idx >= LDQ_SIZE) {
    return;
  }
  LdqEntry &entry = nxt.ldq[inst.ldq_idx];

  if (entry.rob_flag != inst.rob_flag || entry.rob_idx != inst.rob_idx) {
    LSU_NON_BSD_ASSERT(entry.load_state == LoadState::Allocated &&
                       "Load req mismatch with non-empty entry");
    LSU_NON_BSD_ASSERT(0 && "Load req mismatch");
    return;
  }
  entry.v_addr_valid = true;
  entry.v_addr = inst.result;
  entry.dest_preg = inst.dest_preg;

  entry.p_addr_valid = false;
  entry.p_addr = 0;

  entry.func3 = inst.func3;
  entry.load_state = LoadState::WaitTlb;

  entry.stq_snapshot.idx = inst.stq_idx;
  entry.stq_snapshot.flag = inst.stq_flag;

  entry.is_lrsc = is_amo_lr_uop(inst);

  activate_wait_mmu_ldq(entry.mmu_idx);
}

StqEntry RealLsu::get_stq_entry(int idx, bool flag) {
  if (idx < 0 || idx >= STQ_SIZE) {
    return {};
  }

  StqEntry &entry = cur.stq[idx];
  int32_t count = (idx + STQ_SIZE - cur.stq_head) % STQ_SIZE;
  if (entry.stq_flag != flag || count >= cur.stq_count) {
    LSU_NON_BSD_ASSERT(0 && "STQ entry flag mismatch");
  } else {
    return entry;
  }

  return {};
}

bool RealLsu::alloc_stq_entry(mask_t br_mask, uint32_t rob_idx, uint32_t rob_flag, uint32_t func3, bool slot_flag) {
  if (nxt.stq_count >= STQ_SIZE) {
    return false;
  }
  bool expected_tail_flag =
      stq_tail_flag(nxt.stq_head, nxt.stq_count, nxt.stq_head_flag);
  if (slot_flag != expected_tail_flag) {
    LSU_NON_BSD_ASSERT(0 && "STQ slot flag mismatch");
    return false;
  }
  StqEntry &entry = nxt.stq[stq_idx_after(nxt.stq_head, nxt.stq_count)];
  entry = {};
  entry.rob_idx = rob_idx;
  entry.rob_flag = rob_flag;
  entry.stq_flag = slot_flag;
  entry.br_mask = br_mask;
  entry.func3 = func3;
  entry.store_state = StoreState::Allocated;

  entry.mmu_idx = enqueue_wait_mmu_stq(stq_idx_after(nxt.stq_head, nxt.stq_count));
  entry.done_idx = enqueue_mmu_done_stq(stq_idx_after(nxt.stq_head, nxt.stq_count));

  nxt.stq_count++;
  return true;
}
bool RealLsu::alloc_ldq_entry(mask_t br_mask, uint32_t rob_idx, uint32_t rob_flag, uint32_t ldq_idx) {
  if (nxt.ldq[ldq_idx].load_state != LoadState::Empty) {
    return false;
  }
  LdqEntry &entry = nxt.ldq[ldq_idx];
  entry = {};
  entry.rob_idx = rob_idx;
  entry.rob_flag = rob_flag;
  entry.br_mask = br_mask;

  entry.load_state = LoadState::Allocated;
  entry.mmu_idx = enqueue_wait_mmu_ldq(ldq_idx);
  return true;
}
