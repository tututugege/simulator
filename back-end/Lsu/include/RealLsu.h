#pragma once

#include "AbstractLsu.h"
#include "SimpleMmu.h"
#include "config.h"
#include <cstdint>
#include <memory>

class Csr;
class PtwMemPort;
class PtwWalkPort;

class RealLsu : public AbstractLsu {
private:
#ifndef CONFIG_REAL_LSU_RANDOM_TEST
#define CONFIG_REAL_LSU_RANDOM_TEST 0
#endif
  static constexpr int FINISHED_LOADS_QUEUE_SIZE = ROB_NUM;
  static constexpr int FINISHED_STA_QUEUE_SIZE = ROB_NUM;
  static constexpr int PENDING_STA_ADDR_QUEUE_SIZE = STQ_SIZE;
  static constexpr int LOAD_STATE_COUNT = 5;
  static constexpr int LOAD_RESP_WAIT_CYCLES_LIMIT = 150;
  static constexpr int STQ_COUNT_WIDTH = bit_width_for_count(STQ_SIZE + 1);
  static constexpr int LDQ_COUNT_WIDTH = bit_width_for_count(LDQ_SIZE + 1);
  static constexpr int FINISHED_LOADS_QUEUE_IDX_WIDTH =
      bit_width_for_count(FINISHED_LOADS_QUEUE_SIZE);
  static constexpr int FINISHED_LOADS_QUEUE_COUNT_WIDTH =
      bit_width_for_count(FINISHED_LOADS_QUEUE_SIZE + 1);
  static constexpr int FINISHED_STA_QUEUE_IDX_WIDTH =
      bit_width_for_count(FINISHED_STA_QUEUE_SIZE);
  static constexpr int FINISHED_STA_QUEUE_COUNT_WIDTH =
      bit_width_for_count(FINISHED_STA_QUEUE_SIZE + 1);
  static constexpr int PENDING_STA_ADDR_QUEUE_IDX_WIDTH =
      bit_width_for_count(PENDING_STA_ADDR_QUEUE_SIZE);
  static constexpr int PENDING_STA_ADDR_QUEUE_COUNT_WIDTH =
      bit_width_for_count(PENDING_STA_ADDR_QUEUE_SIZE + 1);
  static constexpr int LOAD_REPLAY_PRIORITY_WIDTH = bit_width_for_count(6);
  static constexpr int LOAD_RESP_WAIT_CYCLES_WIDTH =
      bit_width_for_count(LOAD_RESP_WAIT_CYCLES_LIMIT + 1);

  enum class LoadState : uint8_t {
    WaitExec = 0,
    WaitSend = 1,
    WaitResp = 2,
    WaitRetry = 3,
    Ready = 4,
  };

  struct LdqEntry {
    reg<1> valid;
    reg<1> killed;
    reg<1> sent;
    reg<1> waiting_resp;
    LoadState load_state;
    reg<1> ready_delay;
    reg<LOAD_RESP_WAIT_CYCLES_WIDTH> resp_wait_cycles;
    reg<1> tlb_retry;
    reg<1> is_mmio_wait;
    reg<LOAD_REPLAY_PRIORITY_WIDTH> replay_priority;
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
    reg<STQ_IDX_WIDTH> idx = 0;
    reg<1> flag = false;
  };

  struct StoreNode {
    StoreTag tag;
    StqEntry entry;
  };

  struct LsuState {
    StoreTag empty_stq_tag{};

    StoreNode committed_stq[STQ_SIZE];
    reg<STQ_IDX_WIDTH> committed_stq_head = 0;
    reg<STQ_COUNT_WIDTH> committed_stq_count = 0;
    StoreNode speculative_stq[STQ_SIZE];
    reg<STQ_IDX_WIDTH> speculative_stq_head = 0;
    reg<STQ_COUNT_WIDTH> speculative_stq_count = 0;

    LdqEntry ldq[LDQ_SIZE];
    reg<1> reserve_valid = false;
    reg<32> reserve_addr = 0;

    reg<1> replay_type = false;

    MicroOp finished_loads[FINISHED_LOADS_QUEUE_SIZE];
    reg<FINISHED_LOADS_QUEUE_IDX_WIDTH> finished_loads_head = 0;
    reg<FINISHED_LOADS_QUEUE_COUNT_WIDTH> finished_loads_count = 0;
    MicroOp finished_sta_reqs[FINISHED_STA_QUEUE_SIZE];
    reg<FINISHED_STA_QUEUE_IDX_WIDTH> finished_sta_reqs_head = 0;
    reg<FINISHED_STA_QUEUE_COUNT_WIDTH> finished_sta_reqs_count = 0;
    MicroOp pending_sta_addr_reqs[PENDING_STA_ADDR_QUEUE_SIZE];
    reg<PENDING_STA_ADDR_QUEUE_IDX_WIDTH> pending_sta_addr_reqs_head = 0;
    reg<PENDING_STA_ADDR_QUEUE_COUNT_WIDTH> pending_sta_addr_reqs_count = 0;
    reg<1> pending_mmio_valid = false;
    PeripheralReqIO pending_mmio_req{};
  };

  std::unique_ptr<AbstractMmu> mmu;
  LsuState cur;
  LsuState nxt;

public:
  RealLsu(SimContext *ctx);

  void init() override;
  void comb_cal() override;
  void comb_lsu2dis_info() override;
  void comb_recv() override;
  void comb_load_res() override;
  void comb_flush() override;
  void seq() override;

  StqEntry get_stq_entry(int stq_idx, bool stq_flag) override;

  void set_ptw_mem_port(PtwMemPort *port)override {
    mmu->set_ptw_mem_port(port);
  }
  void set_ptw_walk_port(PtwWalkPort *port) override {
    mmu->set_ptw_walk_port(port);
  }

  uint32_t coherent_read(uint32_t p_addr) override;
  void overlay_committed_store_word(uint32_t p_addr,
                                    uint32_t &data) override;
  bool has_translation_store_conflict(uint32_t p_addr) const override;
  bool has_committed_store_pending() const override;

private:

  void handle_load_req(const MicroOp &uop);
  void handle_store_addr(const MicroOp &uop);
  void handle_store_data(const MicroOp &uop);
  StoreTag make_store_tag(int idx, bool flag) const;
  StoreTag next_store_tag(const StoreTag &tag) const;
  StoreTag current_stq_head_tag(const LsuState &state) const;
  StoreTag current_stq_tail_tag(const LsuState &state) const;
  int total_stq_count(const LsuState &state) const;
  int encode_store_req_id(const StoreTag &tag) const;
  StoreTag decode_store_req_id(int req_id) const;
  StoreNode &committed_stq_at(LsuState &state, int offset);
  const StoreNode &committed_stq_at(const LsuState &state, int offset) const;
  StoreNode &speculative_stq_at(LsuState &state, int offset);
  const StoreNode &speculative_stq_at(const LsuState &state, int offset) const;
  StoreNode *find_store_node(LsuState &state, const StoreTag &tag);
  const StoreNode *find_store_node(const LsuState &state,
                                   const StoreTag &tag) const;
  StqEntry *find_store_entry(LsuState &state, const StoreTag &tag);
  const StqEntry *find_store_entry(const LsuState &state,
                                   const StoreTag &tag) const;
  bool is_store_older(int s_idx, int s_flag, int l_idx, int l_flag) const;
  void clear_store_node(StoreNode &node);
  void committed_stq_push(LsuState &state, const StoreNode &node);
  StoreNode &committed_stq_front(LsuState &state);
  const StoreNode &committed_stq_front(const LsuState &state) const;
  void committed_stq_pop(LsuState &state);
  void speculative_stq_push(LsuState &state, const StoreNode &node);
  StoreNode &speculative_stq_front(LsuState &state);
  const StoreNode &speculative_stq_front(const LsuState &state) const;
  void speculative_stq_pop(LsuState &state);
  void move_speculative_front_to_committed(LsuState &state);
  bool reserve_stq_entry(LsuState &state, mask_t br_mask, uint32_t rob_idx,
                         uint32_t rob_flag, uint32_t func3, bool stq_flag);
  void consume_stq_alloc_reqs(LsuState &state);
  bool reserve_ldq_entry(LsuState &state, int idx, mask_t br_mask,
                         uint32_t rob_idx, uint32_t rob_flag);
  void consume_ldq_alloc_reqs(LsuState &state);
  void free_ldq_entry(LsuState &state, int idx);
  bool is_mmio_addr(uint32_t paddr) const;
  void change_store_info(const StoreNode &node, int port_idx);
  void handle_global_flush(LsuState &state);
  void handle_mispred(LsuState &state, mask_t mask);
  void retire_stq_head_if_ready(LsuState &state, int &pop_count);
  void commit_stores_from_rob(LsuState &state);
  void progress_ldq_entries(LsuState &state);
  void progress_pending_sta_addr(LsuState &state);
  bool finish_store_addr_once(LsuState &state, const MicroOp &inst);
  bool committed_store_conflicts_word(const LsuState &state,
                                      uint32_t word_addr) const;
  void set_load_state(LdqEntry &entry, LoadState state);
  void set_load_ready(LdqEntry &entry, uint8_t delay);
  void begin_load_response_wait(LdqEntry &entry);
  void sanitize_state_for_random_test(LsuState &state);
  void prepare_runtime_state(LsuState &state);

  bool has_older_store_pending(const LsuState &state,
                               const MicroOp &load_uop) const;
  StoreForwardResult check_store_forward(const LsuState &state, uint32_t p_addr,
                                         const MicroOp &load_uop);
};
