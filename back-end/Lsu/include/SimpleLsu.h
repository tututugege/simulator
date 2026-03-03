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

class SimpleLsu : public AbstractLsu {
private:
  struct LdqEntry {
    bool valid;
    bool killed;
    bool sent;
    bool waiting_resp;
    bool tlb_retry;
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

  // 3. 完成的 Load 队列 (等待写回)
  std::deque<MicroOp> finished_loads;

  // 4. 完成的 STA 队列 (等待访存流水线对齐写回)
  std::deque<MicroOp> finished_sta_reqs;
  // 5. STA 地址翻译重试队列 (DTLB/PTW miss -> RETRY)
  std::deque<MicroOp> pending_sta_addr_reqs;

public:
  SimpleLsu(SimContext *ctx);

  // 组合逻辑实现
  void init() override;
  void comb_lsu2dis_info() override;
  void comb_recv() override;
  void comb_load_res() override;
  void comb_flush() override;

  // 时序逻辑实现
  void seq() override;

  StqEntry get_stq_entry(int stq_idx) override;

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
    return stq_head != stq_commit;
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
  bool reserve_ldq_entry(int idx, mask_t br_mask, uint32_t rob_idx,
                         uint32_t rob_flag);
  void consume_ldq_alloc_reqs();
  void free_ldq_entry(int idx);
  bool is_mmio_addr(uint32_t paddr) const;
  void drive_store_write_req();
  void handle_global_flush();
  void handle_mispred(mask_t mask);
  void retire_stq_head_if_ready(bool write_fire, int &pop_count);
  void commit_stores_from_rob();
  void progress_ldq_entries();
  void progress_pending_sta_addr();
  bool finish_store_addr_once(const MicroOp &inst);

  StoreForwardResult check_store_forward(uint32_t p_addr,
                                         const MicroOp &load_uop);
};
