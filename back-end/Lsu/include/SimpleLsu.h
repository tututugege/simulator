#pragma once
#include "AbstractLsu.h"
#include "SimpleCache.h"
#include "SimpleMmu.h" // Added MMU include
#include "config.h"
#include <cstdint>
#include <deque>
#include <list>

class Csr;

class SimpleLsu : public AbstractLsu {
private:
  // MMU Instance (Composition)
  AbstractMmu *mmu;

  // === 内部状态寄存器 (对应 seq 更新) ===

  // 1. Store Queue (简化的环形缓冲区)
  StqEntry stq[STQ_NUM];
  int stq_head;   // deq 指针
  int stq_commit; // commit 指针
  int stq_tail;   // enq 指针
  int stq_count;

  // 2. 正在飞行的 Load 队列 (模拟 Cache 延迟)
  std::list<MicroOp> inflight_loads;

  // 3. 完成的 Load 队列 (等待写回)
  std::deque<MicroOp> finished_loads;

  // 4. 完成的 STA 队列 (等待访存流水线对齐写回)
  std::deque<MicroOp> finished_sta_reqs;

  // 4. 下一周期需要更新的状态 (Latches)
  int next_stq_tail;
  int next_stq_commit;
  int next_stq_count;

  SimpleCache cache;

public:
  SimpleLsu(SimContext *ctx);

  // 组合逻辑实现
  void init() override;
  void comb_lsu2dis_info() override;
  void comb_stq_alloc() override;
  void comb_recv() override;
  void comb_load_res() override;
  void comb_commit() override;
  void comb_flush() override;

  // 时序逻辑实现
  void seq() override;

  StqEntry get_stq_entry(int stq_idx) override;
  uint32_t get_load_addr(int rob_idx) override;

  void set_csr(Csr *c) override { this->csr_module = c; }

  // 一致性访存接口 (供 MMU 使用)
  uint32_t coherent_read(uint32_t p_addr) override;

private:
  Csr *csr_module = nullptr;
  // 内部辅助函数
  void handle_load_req(const MicroOp &uop);
  void handle_store_addr(const MicroOp &uop);
  void handle_store_data(const MicroOp &uop);
  int find_recovery_tail(mask_t br_mask);
  bool is_store_older(int s_idx, int s_flag, int l_idx, int l_flag);

  std::pair<int, uint32_t> check_store_forward(uint32_t p_addr,
                                                const MicroOp &load_uop);
};
