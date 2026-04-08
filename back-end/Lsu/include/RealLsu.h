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
  enum class LoadState : uint8_t {
    WaitExec = 0,
    WaitSend = 1,
    WaitResp = 2,
    WaitRetry = 3,
    Ready = 4,
  };

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

  struct StoreTag {
    int idx = 0;
    bool flag = false;
  };

  struct StoreNode {
    StoreTag tag;
    StqEntry entry;
  };

  enum class StoreQueueKind : uint8_t {
    None = 0,
    Committed = 1,
    Speculative = 2,
  };

  struct StoreLocator {
    bool valid = false;
    StoreQueueKind kind = StoreQueueKind::None;
    int pos = 0;
  };

  // MMU Instance (Composition)
  std::unique_ptr<AbstractMmu> mmu;

  // === 内部状态寄存器 (对应 seq 更新) ===

  // 1. Logical STQ age ring + dual internal entity queues.
  int stq_head; // oldest active logical tag / next tag to retire
  int stq_tail; // next logical tag to allocate
  int stq_count;
  StoreNode committed_stq[STQ_SIZE];
  int committed_stq_head;
  int committed_stq_tail;
  int committed_stq_count;
  StoreNode speculative_stq[STQ_SIZE];
  int speculative_stq_head;
  int speculative_stq_tail;
  int speculative_stq_count;
  StoreLocator store_loc[2][STQ_SIZE];

  // 2. 显式 LDQ（请求发出后即使被 squash 也要等回包释放）
  LdqEntry ldq[LDQ_SIZE];
  int ldq_count;
  int ldq_alloc_tail;
  
  bool reserve_valid;
  uint32_t reserve_addr;

  int replay_count_ldq; // 统计重试次数
  int replay_count_stq; // 统计重试次数
  int mshr_replay_count_ldq; // 统计 MSHR 重试次数
  int mshr_replay_count_stq; // 统计 MSHR 重试次数
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

  StqEntry get_stq_entry(int stq_idx, bool stq_flag) override;

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
  void overlay_committed_store_word(uint32_t p_addr,
                                    uint32_t &data) override;
  bool has_translation_store_conflict(uint32_t p_addr) const override;
  bool has_committed_store_pending() const override;

private:
  Csr *csr_module = nullptr;
  // 内部辅助函数
  void handle_load_req(const MicroOp &uop);
  void handle_store_addr(const MicroOp &uop);
  void handle_store_data(const MicroOp &uop);
  StoreTag make_store_tag(int idx, bool flag) const;
  StoreTag current_stq_head_tag() const;
  bool current_stq_tail_flag() const;
  int encode_store_req_id(const StoreTag &tag) const;
  StoreTag decode_store_req_id(int req_id) const;
  StoreLocator &store_locator(const StoreTag &tag);
  const StoreLocator &store_locator(const StoreTag &tag) const;
  StoreNode &committed_stq_at(int offset);
  const StoreNode &committed_stq_at(int offset) const;
  StoreNode &speculative_stq_at(int offset);
  const StoreNode &speculative_stq_at(int offset) const;
  StoreNode *find_store_node(const StoreTag &tag);
  const StoreNode *find_store_node(const StoreTag &tag) const;
  StqEntry *find_store_entry(const StoreTag &tag);
  const StqEntry *find_store_entry(const StoreTag &tag) const;
  bool is_store_older(int s_idx, int s_flag, int l_idx, int l_flag) const;
  void clear_store_node(StoreNode &node);
  void committed_stq_push(const StoreNode &node);
  StoreNode &committed_stq_front();
  const StoreNode &committed_stq_front() const;
  void committed_stq_pop();
  void speculative_stq_push(const StoreNode &node);
  StoreNode &speculative_stq_front();
  const StoreNode &speculative_stq_front() const;
  void speculative_stq_pop();
  void move_speculative_front_to_committed();
  bool reserve_stq_entry(mask_t br_mask, uint32_t rob_idx, uint32_t rob_flag,
                         uint32_t func3, bool stq_flag);
  void consume_stq_alloc_reqs(int &push_count);
  bool reserve_ldq_entry(int idx, mask_t br_mask, uint32_t rob_idx,
                         uint32_t rob_flag);
  void consume_ldq_alloc_reqs();
  void free_ldq_entry(int idx);
  bool is_mmio_addr(uint32_t paddr) const;
  void change_store_info(const StoreNode &node, int port_idx);
  void handle_global_flush();
  void handle_mispred(mask_t mask);
  void retire_stq_head_if_ready(int &pop_count);
  void commit_stores_from_rob();
  void progress_ldq_entries();
  void progress_pending_sta_addr();
  bool finish_store_addr_once(const MicroOp &inst);
  bool committed_store_conflicts_word(uint32_t word_addr) const;

  bool has_older_store_pending(const MicroOp &load_uop) const;
  StoreForwardResult check_store_forward(uint32_t p_addr,
                                         const MicroOp &load_uop);
};
