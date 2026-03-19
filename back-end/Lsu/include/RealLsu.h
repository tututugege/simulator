#pragma once
#include "AbstractLsu.h"
#include "SimpleMmu.h" // Added MMU include
#include "config.h"
#include <cstdint>
#include <deque>
#include <memory>

class Csr;
class PtwMemPort;
class PtwWalkPort;


class RealLsu : public AbstractLsu {
private:
  struct LdqEntry {
    bool valid;
    bool killed;
    bool sent;
    bool waiting_resp;
    uint64_t wait_resp_since;
    bool tlb_retry;
    bool is_mmio_wait;  // 地址已翻译为 MMIO，等待到达 ROB 队头后再发送
    uint8_t replay_priority;
    MicroOp uop;
  };

  enum class StoreForwardState : uint8_t {
    NoHit = 0,
    Hit = 1,
    Retry = 2,
  };

  struct StoreForwardResult {
    StoreForwardState state = StoreForwardState::NoHit;
    uint32_t data = 0;
  };

  struct StqAllocTrace {
    bool valid = false;
    uint64_t cycle = 0;
    int stq_idx = -1;
    uint32_t rob_idx = 0;
    uint32_t rob_flag = 0;
    uint32_t func3 = 0;
    mask_t br_mask = 0;
    uint64_t seq = 0;
  };

  // MMU Instance (Composition)
  std::unique_ptr<AbstractMmu> mmu;

  // === 内部状态寄存器 (对应 seq 更新) ===

  // 1. Store Queue (简化的环形缓冲区)
  StqEntry stq[STQ_SIZE];
  int stq_head;   // deq 指针
  int stq_commit; // commit 指针
  int stq_tail;   // enq 指针
  int stq_count;

  // 2. 显式 LDQ（请求发出后即使被 squash 也要等回包释放）
  LdqEntry ldq[LDQ_SIZE];
  int ldq_count;
  int ldq_alloc_tail;
  
  bool reserve_valid;
  int reserve_addr;

  int replay_count_ldq; // 统计重试次数
  int replay_count_stq; // 统计重试次数
  int mshr_replay_count_ldq; // 统计 MSHR 重试次数
  int mshr_replay_count_stq; // 统计 MSHR 重试次数
  uint64_t ldq_seq_counter;
  uint64_t stq_seq_counter;
  uint64_t ldq_trace_seq[LDQ_SIZE];
  uint64_t stq_trace_seq[STQ_SIZE];
  bool ldq_cache_wait_replay[LDQ_SIZE];
  bool stq_cache_wait_replay[STQ_SIZE];

  bool stq_head_flag; // 用于区分环形缓冲区中的两轮

  bool replay_type; // 0 = LDQ, 1 = STQ

  
  uint32_t issued_stq_addr[LSU_STA_COUNT] = {};
  uint32_t issued_stq_addr_nxt[LSU_STA_COUNT] = {}; // 每周期已发出的 Store 地址，用于 Store Forward 检测
  bool issued_stq_addr_valid[LSU_STA_COUNT] = {}; // 标记 issued_stq_addr 中哪些地址是有效的
  bool issued_stq_addr_valid_nxt[LSU_STA_COUNT] = {}; // 下一周期的有效地址标记
  // 3. 完成的 Load 队列 (等待写回)
  std::deque<MicroOp> finished_loads;

  // 4. 完成的 STA 队列 (等待访存流水线对齐写回)
  std::deque<MicroOp> finished_sta_reqs;
  // 5. STA 地址翻译重试队列 (DTLB/PTW miss -> RETRY)
  std::deque<MicroOp> pending_sta_addr_reqs;
  bool pending_mmio_valid = false;
  PeripheralInIO pending_mmio_req{};
  static constexpr int STQ_ALLOC_TRACE_DEPTH = 16;
  StqAllocTrace recent_stq_allocs[STQ_ALLOC_TRACE_DEPTH] = {};
  int recent_stq_alloc_cursor = 0;

public:
  RealLsu(SimContext *ctx);

  // 组合逻辑实现
  void init() override;
  void comb_lsu2dis_info() override;
  void comb_recv() override;
  void comb_load_res() override;
  void comb_flush() override;

  // 时序逻辑实现
  void seq() override;

  StqEntry get_stq_entry(int stq_idx) override;
  void dump_debug_state() const;

  void set_csr(Csr *c) override { this->csr_module = c; }
  void set_ptw_mem_port(PtwMemPort *port) override {
    ptw_mem_port = port;
    mmu->set_ptw_mem_port(port);
  }
  void set_ptw_walk_port(PtwWalkPort *port) override {
    ptw_walk_port = port;
    mmu->set_ptw_walk_port(port);
  }

  // 一致性访存接口 (供 MMU 使用)
  uint32_t coherent_read(uint32_t p_addr) override;
  bool has_committed_store_pending() const override {
    int ptr = stq_head;
    int remain = stq_count;
    while (remain > 0) {
      const StqEntry &e = stq[ptr];
      if (e.valid && e.committed && !e.done) {
        return true;
      }
      ptr = (ptr + 1) % STQ_SIZE;
      remain--;
    }
    return false;
  }

private:
  Csr *csr_module = nullptr;
  // 内部辅助函数
  void handle_load_req(const MicroOp &uop);
  void handle_store_addr(const MicroOp &uop);
  void handle_store_data(const MicroOp &uop);
  int find_recovery_tail(mask_t br_mask);
  bool is_store_older(int s_idx, int s_flag, int l_idx, int l_flag);
  bool reserve_stq_entry(mask_t br_mask, uint32_t rob_idx, uint32_t rob_flag,
                         uint32_t func3);
  void consume_stq_alloc_reqs(int &push_count);
  int count_active_stq_entries() const;
  int count_committed_stq_prefix() const;
  int count_stq_entries_until(int stop_idx) const;
  void clear_stq_entries(int start_idx, int count);
  bool reserve_ldq_entry(int idx, mask_t br_mask, uint32_t rob_idx,
                         uint32_t rob_flag);
  void consume_ldq_alloc_reqs();
  void free_ldq_entry(int idx);
  bool is_mmio_addr(uint32_t paddr) const;
  void change_store_info(StqEntry &entry, int port_idx, int stq_idx);
  void handle_global_flush();
  void handle_mispred(mask_t mask);
  void retire_stq_head_if_ready(int &pop_count);
  void commit_stores_from_rob();
  void progress_ldq_entries();
  void progress_pending_sta_addr();
  bool finish_store_addr_once(const MicroOp &inst);
  void record_stq_alloc_trace(int stq_idx, uint32_t rob_idx, uint32_t rob_flag,
                              uint32_t func3, mask_t br_mask);
  void dump_recent_stq_alloc_traces() const;

  StoreForwardResult check_store_forward(uint32_t p_addr,
                                         const MicroOp &load_uop);
};
