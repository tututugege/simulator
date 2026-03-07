#ifndef BPU_TOP_H
#define BPU_TOP_H

#include "../frontend.h"
#include "../wire_types.h"
#include "./dir_predictor/TAGE_top.h"
#include "./target_predictor/BTB_top.h"
#include "BPU_configs.h"
#include <SimCpu.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

static_assert(BPU_BANK_NUM > 0, "BPU_BANK_NUM must be positive");
static_assert(FETCH_WIDTH > 0, "FETCH_WIDTH must be positive");
static_assert(COMMIT_WIDTH > 0, "COMMIT_WIDTH must be positive");
static_assert(BPU_BANK_NUM == FETCH_WIDTH,
              "BPU currently assumes one bank per fetch lane");
static_assert(TN_MAX == 4,
              "BPU/front-end IO currently assumes TN_MAX == 4 lanes");
static_assert(Q_DEPTH > 0, "Q_DEPTH must be positive");

class BPU_TOP;             // 前向声明类
extern BPU_TOP *g_bpu_top; // 再声明全局指针

uint32_t bpu_sim_time = 0; // only for debug

struct BankSelCombIn {
  uint32_t pc;
};

struct BankSelCombOut {
  int bank_sel;
};

inline void bank_sel_comb(const BankSelCombIn &in, BankSelCombOut &out) {
  out = BankSelCombOut{};
  out.bank_sel = static_cast<int>((in.pc >> 2) % BPU_BANK_NUM);
}

inline int bank_sel_from_pc(uint32_t pc) {
  BankSelCombOut out{};
  bank_sel_comb(BankSelCombIn{pc}, out);
  return out.bank_sel;
}

struct BankPcCombIn {
  uint32_t pc;
};

struct BankPcCombOut {
  uint32_t bank_pc;
};

inline void bank_pc_comb(const BankPcCombIn &in, BankPcCombOut &out) {
  out = BankPcCombOut{};
  if ((BPU_BANK_NUM & (BPU_BANK_NUM - 1)) != 0) {
    uint32_t bank_pc = in.pc >> 2;
    bank_pc = bank_pc / BPU_BANK_NUM;
    bank_pc = bank_pc << 2;
    out.bank_pc = bank_pc;
    return;
  }

  uint32_t n = BPU_BANK_NUM;
  int highest_bit_pos = 0;
  while (n > 1) {
    n >>= 1;
    highest_bit_pos++;
  }
  out.bank_pc = in.pc >> highest_bit_pos;
}

inline uint32_t bank_pc_from_pc(uint32_t pc) {
  BankPcCombOut out{};
  bank_pc_comb(BankPcCombIn{pc}, out);
  return out.bank_pc;
}

struct AheadSlot1PredictCombIn {
  bool valid;
  bool taken;
  uint8_t conf;
  uint32_t target;
  uint32_t fallback_target;
};

struct AheadSlot1PredictCombOut {
  bool hit;
  bool low_conf_fallback;
  uint32_t target;
};

inline void ahead_slot1_predict_comb(const AheadSlot1PredictCombIn &in,
                                     AheadSlot1PredictCombOut &out) {
  out = AheadSlot1PredictCombOut{};
  const bool conf_ready = (in.conf >= AHEAD_SLOT1_CONF_THRESHOLD);
  out.hit = in.valid && in.taken && conf_ready;
  out.low_conf_fallback = in.valid && in.taken && !conf_ready;
  out.target = out.hit ? in.target : in.fallback_target;
}

struct AheadSlot1ConfUpdateCombIn {
  uint8_t current_conf;
  bool prediction_correct;
};

struct AheadSlot1ConfUpdateCombOut {
  uint8_t next_conf;
};

inline void ahead_slot1_conf_update_comb(const AheadSlot1ConfUpdateCombIn &in,
                                         AheadSlot1ConfUpdateCombOut &out) {
  out = AheadSlot1ConfUpdateCombOut{};
  if (in.prediction_correct) {
    out.next_conf = (in.current_conf >= AHEAD_SLOT1_CONF_MAX)
                        ? static_cast<uint8_t>(AHEAD_SLOT1_CONF_MAX)
                        : static_cast<uint8_t>(in.current_conf + 1);
  } else {
    out.next_conf = 0;
  }
}

struct AheadGateUpdateCombIn {
  bool gate_enable;
  uint32_t sample_count;
  uint32_t success_count;
  bool sample_valid;
  bool sample_success;
};

struct AheadGateUpdateCombOut {
  bool gate_enable_next;
  uint32_t sample_count_next;
  uint32_t success_count_next;
  bool toggled_disable;
  bool toggled_enable;
};

inline void ahead_gate_update_comb(const AheadGateUpdateCombIn &in,
                                   AheadGateUpdateCombOut &out) {
  out = AheadGateUpdateCombOut{};
  out.gate_enable_next = in.gate_enable;
  out.sample_count_next = in.sample_count;
  out.success_count_next = in.success_count;
  out.toggled_disable = false;
  out.toggled_enable = false;

  if (!in.sample_valid) {
    return;
  }

  uint32_t next_samples = in.sample_count + 1;
  uint32_t next_success = in.success_count + (in.sample_success ? 1U : 0U);
  out.sample_count_next = next_samples;
  out.success_count_next = next_success;

  if (next_samples < static_cast<uint32_t>(AHEAD_GATE_WINDOW)) {
    return;
  }

  const uint32_t success_rate = (next_success * 100U) / next_samples;
  bool gate_next = in.gate_enable;
  if (in.gate_enable &&
      success_rate < static_cast<uint32_t>(AHEAD_GATE_DISABLE_THRESHOLD)) {
    gate_next = false;
    out.toggled_disable = true;
  } else if (!in.gate_enable &&
             success_rate > static_cast<uint32_t>(AHEAD_GATE_ENABLE_THRESHOLD)) {
    gate_next = true;
    out.toggled_enable = true;
  }
  out.gate_enable_next = gate_next;
  out.sample_count_next = 0;
  out.success_count_next = 0;
}

struct NlpIndexCombIn {
  uint32_t pc;
};

struct NlpIndexCombOut {
  uint32_t index;
};

inline void nlp_index_comb(const NlpIndexCombIn &in, NlpIndexCombOut &out) {
  out = NlpIndexCombOut{};
  uint32_t mixed = (in.pc >> 2) ^ (in.pc >> 11) ^ (in.pc >> 19);
  if ((NLP_TABLE_SIZE & (NLP_TABLE_SIZE - 1)) == 0) {
    out.index = mixed & (NLP_TABLE_SIZE - 1);
  } else {
    out.index = mixed % NLP_TABLE_SIZE;
  }
}

inline uint32_t nlp_index_from_pc(uint32_t pc) {
  NlpIndexCombOut out{};
  nlp_index_comb(NlpIndexCombIn{pc}, out);
  return out.index;
}

struct NlpTagCombIn {
  uint32_t pc;
};

struct NlpTagCombOut {
  uint32_t tag;
};

inline void nlp_tag_comb(const NlpTagCombIn &in, NlpTagCombOut &out) {
  out = NlpTagCombOut{};
  out.tag = in.pc >> 2;
}

inline uint32_t nlp_tag_from_pc(uint32_t pc) {
  NlpTagCombOut out{};
  nlp_tag_comb(NlpTagCombIn{pc}, out);
  return out.tag;
}

inline uint32_t nlp_fallback_next_pc(uint32_t base_pc) {
  const uint32_t cache_mask = ~(ICACHE_LINE_SIZE - 1);
  const uint32_t pc_plus_width = base_pc + (FETCH_WIDTH * 4);
  return ((base_pc & cache_mask) != (pc_plus_width & cache_mask))
             ? (pc_plus_width & cache_mask)
             : pc_plus_width;
}


class BPU_TOP {
  friend const bool *BPU_get_Arch_GHR();
  friend const bool *BPU_get_Spec_GHR();
  friend const uint32_t (*BPU_get_Arch_FH())[TN_MAX];
  friend const uint32_t (*BPU_get_Spec_FH())[TN_MAX];

public:
  struct InputPayload {
    // I-Cache & Backend Control
    bool refetch;
    uint32_t refetch_address;
    bool icache_read_ready;

    // Update Interface
    uint32_t in_update_base_pc[COMMIT_WIDTH];
    bool in_upd_valid[COMMIT_WIDTH];
    bool in_actual_dir[COMMIT_WIDTH];
    br_type_t in_actual_br_type[COMMIT_WIDTH]; // 3-bit each
    uint32_t in_actual_targets[COMMIT_WIDTH];

    bool in_pred_dir[COMMIT_WIDTH];
    bool in_alt_pred[COMMIT_WIDTH];
    pcpn_t in_pcpn[COMMIT_WIDTH];    // 3-bit each
    pcpn_t in_altpcpn[COMMIT_WIDTH]; // 3-bit each
    tage_tag_t in_tage_tags[COMMIT_WIDTH][TN_MAX];
    tage_idx_t in_tage_idxs[COMMIT_WIDTH][TN_MAX];
  };

  struct OutputPayload {
    uint32_t fetch_address;
    bool icache_read_valid;
    uint32_t predict_next_fetch_address;
    bool PTAB_write_enable;
    bool out_pred_dir[FETCH_WIDTH];
    bool out_alt_pred[FETCH_WIDTH];
    pcpn_t out_pcpn[FETCH_WIDTH];
    pcpn_t out_altpcpn[FETCH_WIDTH];
    tage_tag_t out_tage_tags[FETCH_WIDTH][TN_MAX];
    tage_idx_t out_tage_idxs[FETCH_WIDTH][TN_MAX];
    uint32_t out_pred_base_pc; // used for predecode
    bool update_queue_full;
    // 2-Ahead Predictor outputs
    // 下下行取指地址
    bool two_ahead_valid;
    uint32_t two_ahead_target;
    // 指示要不要多消耗inst FIFO
    bool mini_flush_req;
    // 指示要不要多写一次fetch_address_fifo
    bool mini_flush_correct;
    // 如果要多写，多写的地址
    uint32_t mini_flush_target;
  };

  // 顶层三阶段兼容接口的数据容器
  struct ReadData {
    struct QueueEntrySnapshot {
      uint32_t base_pc;
      bool valid_mask;
      bool actual_dir;
      br_type_t br_type;
      uint32_t targets;
      bool pred_dir;
      bool alt_pred;
      pcpn_t pcpn;
      pcpn_t altpcpn;
      tage_tag_t tage_tags[TN_MAX];
      tage_idx_t tage_idxs[TN_MAX];
    };
    struct NlpEntrySnapshot {
      bool entry_valid;
      uint32_t entry_tag;
      uint32_t entry_target;
      uint8_t entry_conf;
    };

    int state_snapshot;
    uint32_t pc_reg_snapshot;
    bool pc_can_send_to_icache_snapshot;
    uint32_t pred_base_pc_fired_snapshot;
    bool do_pred_latch_snapshot;
    bool do_upd_latch_snapshot[BPU_BANK_NUM];

    bool tage_calc_pred_dir_latch_snapshot[FETCH_WIDTH];
    bool tage_calc_altpred_latch_snapshot[FETCH_WIDTH];
    pcpn_t tage_calc_pcpn_latch_snapshot[FETCH_WIDTH];
    pcpn_t tage_calc_altpcpn_latch_snapshot[FETCH_WIDTH];
    tage_tag_t tage_pred_calc_tags_latch_snapshot[FETCH_WIDTH][TN_MAX];
    tage_idx_t tage_pred_calc_idxs_latch_snapshot[FETCH_WIDTH][TN_MAX];
    bool tage_result_valid_latch_snapshot[FETCH_WIDTH];
    uint32_t btb_pred_target_latch_snapshot[FETCH_WIDTH];
    bool btb_result_valid_latch_snapshot[FETCH_WIDTH];
    bool tage_done_snapshot[BPU_BANK_NUM];
    bool btb_done_snapshot[BPU_BANK_NUM];

    uint32_t q_wr_ptr_snapshot[BPU_BANK_NUM];
    uint32_t q_rd_ptr_snapshot[BPU_BANK_NUM];
    uint32_t q_count_snapshot[BPU_BANK_NUM];

    uint32_t saved_2ahead_prediction_snapshot;
    bool saved_2ahead_pred_valid_snapshot;
    bool saved_mini_flush_req_snapshot;
    bool saved_mini_flush_correct_snapshot;
    uint32_t saved_mini_flush_target_snapshot;
    bool ahead_gate_enable_snapshot;
    uint32_t ahead_gate_sample_count_snapshot;
    uint32_t ahead_gate_success_count_snapshot;
    uint32_t ahead_gate_disable_count_snapshot;
    uint32_t ahead_gate_enable_count_snapshot;
    bool ras_has_entry_snapshot;
    uint32_t ras_top_snapshot;
    uint32_t ras_count_snapshot;
    bool Arch_GHR_snapshot[GHR_LENGTH];
    bool Spec_GHR_snapshot[GHR_LENGTH];
    uint32_t Arch_FH_snapshot[FH_N_MAX][TN_MAX];
    uint32_t Spec_FH_snapshot[FH_N_MAX][TN_MAX];
    uint32_t Arch_ras_stack_snapshot[RAS_DEPTH];
    uint32_t Arch_ras_count_snapshot;
    uint32_t Spec_ras_stack_snapshot[RAS_DEPTH];
    uint32_t Spec_ras_count_snapshot;

    uint32_t pred_base_pc;
    uint32_t boundary_addr;
    bool do_pred_on_this_pc[FETCH_WIDTH];
    int this_pc_bank_sel[FETCH_WIDTH];
    uint32_t do_pred_for_this_pc[FETCH_WIDTH];
    br_type_t pred_inst_type_snapshot[FETCH_WIDTH];
    bool ahead_entry_valid_snapshot[FETCH_WIDTH];
    bool ahead_entry_taken_snapshot[FETCH_WIDTH];
    uint32_t ahead_entry_target_snapshot[FETCH_WIDTH];
    uint8_t ahead_entry_conf_snapshot[FETCH_WIDTH];
    bool last_block_valid_snapshot[BPU_BANK_NUM];
    uint32_t last_block_pc_snapshot[BPU_BANK_NUM];
    bool last_block_entry_valid_snapshot[BPU_BANK_NUM];
    bool last_block_entry_taken_snapshot[BPU_BANK_NUM];
    uint32_t last_block_entry_target_snapshot[BPU_BANK_NUM];
    uint8_t last_block_entry_conf_snapshot[BPU_BANK_NUM];

    bool q_full[BPU_BANK_NUM];
    bool q_empty[BPU_BANK_NUM];
    QueueEntrySnapshot q_data[BPU_BANK_NUM];

    bool going_to_do_pred;
    bool going_to_do_upd[BPU_BANK_NUM];
    bool going_to_do_upd_any;
    bool trans_ready_to_fire;
    bool set_submodule_input;

    TAGE_TOP::InputPayload tage_in[BPU_BANK_NUM];
    BTB_TOP::InputPayload btb_in[BPU_BANK_NUM];
    TAGE_TOP::ReadData tage_rd[BPU_BANK_NUM];
    BTB_TOP::ReadData btb_rd[BPU_BANK_NUM];
    NlpEntrySnapshot nlp_pred_base_entry_snapshot;
    uint32_t nlp_s1_req_pc_snapshot;
    NlpEntrySnapshot nlp_s1_entry_snapshot;
    NlpEntrySnapshot nlp_train_entry_snapshot;
  };

  struct UpdateRequest {
    OutputPayload out_regs;
    int next_state;
    uint32_t pc_reg_next;
    bool pc_can_send_to_icache_next;
    bool do_pred_latch_next;
    bool do_upd_latch_next[BPU_BANK_NUM];
    uint32_t pred_base_pc_fired_next;
    uint32_t saved_2ahead_prediction_next;
    bool saved_2ahead_pred_valid_next;
    bool saved_mini_flush_req_next;
    bool saved_mini_flush_correct_next;
    uint32_t saved_mini_flush_target_next;
    bool ahead_gate_enable_next;
    uint32_t ahead_gate_sample_count_next;
    uint32_t ahead_gate_success_count_next;
    uint32_t ahead_gate_disable_count_next;
    uint32_t ahead_gate_enable_count_next;
    bool nlp_s1_valid_next;
    uint32_t nlp_s1_req_pc_next;
    uint32_t nlp_s1_pred_next_pc_next;
    bool nlp_s1_hit_next;
    uint8_t nlp_s1_conf_next;
    bool nlp_s2_valid_next;
    uint32_t nlp_s2_req_pc_next;
    uint32_t nlp_s2_pred_2ahead_pc_next;
    bool nlp_s2_hit_next;
    uint8_t nlp_s2_conf_next;
    bool nlp_entry_we;
    uint32_t nlp_entry_idx;
    bool nlp_entry_valid_next;
    uint32_t nlp_entry_tag_next;
    uint32_t nlp_entry_target_next;
    uint8_t nlp_entry_conf_next;
    bool Spec_GHR_next[GHR_LENGTH];
    bool Arch_GHR_next[GHR_LENGTH];
    uint32_t Spec_FH_next[FH_N_MAX][TN_MAX];
    uint32_t Arch_FH_next[FH_N_MAX][TN_MAX];
    uint32_t Arch_ras_stack_next[RAS_DEPTH];
    uint32_t Arch_ras_count_next;
    uint32_t Spec_ras_stack_next[RAS_DEPTH];
    uint32_t Spec_ras_count_next;
    uint32_t q_wr_ptr_next[BPU_BANK_NUM];
    uint32_t q_rd_ptr_next[BPU_BANK_NUM];
    uint32_t q_count_next[BPU_BANK_NUM];
    bool q_entry_we[COMMIT_WIDTH];
    int q_entry_bank[COMMIT_WIDTH];
    uint32_t q_entry_slot[COMMIT_WIDTH];
    ReadData::QueueEntrySnapshot q_entry_data[COMMIT_WIDTH];
    bool inst_type_we[COMMIT_WIDTH];
    int inst_type_bank[COMMIT_WIDTH];
    uint32_t inst_type_idx[COMMIT_WIDTH];
    br_type_t inst_type_data[COMMIT_WIDTH];
    bool ahead_entry_we[BPU_BANK_NUM];
    uint32_t ahead_entry_idx[BPU_BANK_NUM];
    bool ahead_entry_valid_next[BPU_BANK_NUM];
    bool ahead_entry_taken_next[BPU_BANK_NUM];
    uint32_t ahead_entry_target_next[BPU_BANK_NUM];
    uint8_t ahead_entry_conf_next[BPU_BANK_NUM];
    bool last_block_valid_next[BPU_BANK_NUM];
    uint32_t last_block_pc_next[BPU_BANK_NUM];

    uint32_t pred_base_pc;
    bool do_pred_on_this_pc[FETCH_WIDTH];
    int this_pc_bank_sel[FETCH_WIDTH];
    uint32_t do_pred_for_this_pc[FETCH_WIDTH];

    bool going_to_do_pred;
    bool going_to_do_upd[BPU_BANK_NUM];

    bool q_push_en[BPU_BANK_NUM];
    bool q_pop_en[BPU_BANK_NUM];

    bool final_pred_dir[FETCH_WIDTH];
    uint32_t next_fetch_addr_calc;
    uint32_t final_2_ahead_address;
    bool should_update_spec_hist;

    TAGE_TOP::InputPayload tage_in[BPU_BANK_NUM];
    BTB_TOP::InputPayload btb_in[BPU_BANK_NUM];
    TAGE_TOP::CombResult tage_req[BPU_BANK_NUM];
    BTB_TOP::CombResult btb_req[BPU_BANK_NUM];

    bool tage_done_next[BPU_BANK_NUM];
    bool btb_done_next[BPU_BANK_NUM];
    bool tage_calc_pred_dir_latch_next[FETCH_WIDTH];
    bool tage_calc_altpred_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_pcpn_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_altpcpn_latch_next[FETCH_WIDTH];
    tage_tag_t tage_pred_calc_tags_latch_next[FETCH_WIDTH][TN_MAX];
    tage_idx_t tage_pred_calc_idxs_latch_next[FETCH_WIDTH][TN_MAX];
    bool tage_result_valid_latch_next[FETCH_WIDTH];
    uint32_t btb_pred_target_latch_next[FETCH_WIDTH];
    bool btb_result_valid_latch_next[FETCH_WIDTH];
  };

  struct BpuCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BpuCombOut {
    OutputPayload out_regs;
    UpdateRequest update_req;
  };

private:
  // ========================================================================
  // 内部数据结构 (Internal Structures)
  // ========================================================================

  // Update Queue Entry Structure --- for one bank slot!
  struct QueueEntry {
    uint32_t base_pc;
    bool valid_mask;
    bool actual_dir;
    br_type_t br_type;
    uint32_t targets;
    bool pred_dir;
    bool alt_pred;
    pcpn_t pcpn;
    pcpn_t altpcpn;
    tage_tag_t tage_tags[TN_MAX];
    tage_idx_t tage_idxs[TN_MAX];
  };

  // 2-Ahead Predictor Structures
  struct AheadSlot1Entry {
    bool valid;
    bool taken;
    uint32_t target;
    uint8_t conf;
  };

  struct LastBlockEntry {
    bool valid;
    uint32_t last_pc;
  };

  struct NLPEntry {
    bool valid;
    uint32_t tag;
    uint32_t target;
    uint8_t conf;
  };

  enum State {
    S_IDLE = 0,
    S_WORKING = 1,  // TAGE和BTB并行执行
    S_REFEATCH = 2  // refetch时的并行更新
  };

  // ========================================================================
  // Registers & Memory
  // ========================================================================

  // Global History Registers & Folded Histories (Arch + Spec)
  // GHR/FH从TAGE迁移到BPU统一管理
  bool Arch_GHR[GHR_LENGTH];
  bool Spec_GHR[GHR_LENGTH];
  uint32_t Arch_FH[FH_N_MAX][TN_MAX];
  uint32_t Spec_FH[FH_N_MAX][TN_MAX];
  uint32_t Arch_ras_stack[RAS_DEPTH];
  uint32_t Arch_ras_count;
  uint32_t Spec_ras_stack[RAS_DEPTH];
  uint32_t Spec_ras_count;

  // FH constants (从TAGE复制，用于调用FH_update函数)
  const uint32_t ghr_length[TN_MAX] = {8, 13, 32, 119};
  const uint32_t fh_length[FH_N_MAX][TN_MAX] = {
      {8, 11, 11, 11}, {8, 8, 8, 8}, {7, 7, 7, 7}};

  // PC & Memory
  uint32_t pc_reg;
  br_type_t inst_type_mem[BPU_TYPE_ENTRY_NUM]
                       [BPU_BANK_NUM]; // 3-bit stored in uint8

  State state;
  // Transaction Flags (Latched in IDLE) // doing prediction and updating
  bool do_pred_latch;
  bool do_upd_latch[BPU_BANK_NUM]; // indicate whether to update this bank

  bool pc_can_send_to_icache;            // 当前pc是不是可以发送给icache
  uint32_t pred_base_pc_fired; // 当前预测流程中正在处理的pc基地址

  // 存储TAGE和BTB的预测结果（缓存先到的结果）对于FETCH_WIDTH建立！
  bool tage_calc_pred_dir_latch[FETCH_WIDTH];
  bool tage_calc_altpred_latch[FETCH_WIDTH];
  pcpn_t tage_calc_pcpn_latch[FETCH_WIDTH];
  pcpn_t tage_calc_altpcpn_latch[FETCH_WIDTH];
  tage_tag_t tage_pred_calc_tags_latch[FETCH_WIDTH][TN_MAX];
  tage_idx_t tage_pred_calc_idxs_latch[FETCH_WIDTH][TN_MAX];
  bool tage_result_valid_latch[FETCH_WIDTH]; // 标记TAGE预测结果是否已缓存
  uint32_t btb_pred_target_latch[FETCH_WIDTH];
  bool btb_result_valid_latch[FETCH_WIDTH]; // 标记BTB预测结果是否已缓存

  // Done信号：标记每个bank的TAGE/BTB是否完成
  bool tage_done[BPU_BANK_NUM];  // TAGE完成信号
  bool btb_done[BPU_BANK_NUM];   // BTB完成信号

  // Queue Registers
  QueueEntry update_queue[Q_DEPTH][BPU_BANK_NUM];
  uint32_t q_wr_ptr[BPU_BANK_NUM];
  uint32_t q_rd_ptr[BPU_BANK_NUM];
  uint32_t q_count[BPU_BANK_NUM];

  // bool out_pred_dir_latch[FETCH_WIDTH];
  // bool out_alt_pred_latch[FETCH_WIDTH];
  // uint8_t out_pcpn_latch[FETCH_WIDTH];
  // uint8_t out_altpcpn_latch[FETCH_WIDTH];
  // uint8_t out_tage_pred_calc_tags_latch_latch[FETCH_WIDTH][TN_MAX];
  // uint32_t out_tage_pred_calc_idxs_latch_latch[FETCH_WIDTH][TN_MAX];

  // Sub-modules
  TAGE_TOP *tage_inst[BPU_BANK_NUM];
  BTB_TOP *btb_inst[BPU_BANK_NUM];

  // 2-Ahead slot1 predictor table
  AheadSlot1Entry ahead_slot1_table[BPU_BANK_NUM][AHEAD_SLOT1_TABLE_SIZE];
  LastBlockEntry last_block_table[BPU_BANK_NUM];
  NLPEntry nlp_table[NLP_TABLE_SIZE];
  
  // 2-Ahead Predictor Registers
  // 类似pc_reg的2-ahead reg,跟pc_reg保持同步
  uint32_t saved_2ahead_prediction;
  // 指示的是上一个的2-ahead预测器是否有效
  bool saved_2ahead_pred_valid; // may not used
  // goes to PTAB, 需要跟PTAB相关信号同步
  bool saved_mini_flush_req;
  // goes to fetch_address_FIFO, 需要跟fetch_address_FIFO相关信号同步
  bool saved_mini_flush_correct;
  uint32_t saved_mini_flush_target; // may not used
  bool ahead_gate_enable;
  uint32_t ahead_gate_sample_count;
  uint32_t ahead_gate_success_count;
  uint32_t ahead_gate_disable_count;
  uint32_t ahead_gate_enable_count;
  bool nlp_s1_valid;
  uint32_t nlp_s1_req_pc;
  uint32_t nlp_s1_pred_next_pc;
  bool nlp_s1_hit;
  uint8_t nlp_s1_conf;
  bool nlp_s2_valid;
  uint32_t nlp_s2_req_pc;
  uint32_t nlp_s2_pred_2ahead_pc;
  bool nlp_s2_hit;
  uint8_t nlp_s2_conf;

  void ras_push(uint32_t *stack, uint32_t &count, uint32_t value) {
    if (count < RAS_DEPTH) {
      stack[count++] = value;
      return;
    }
    for (int i = 1; i < RAS_DEPTH; ++i) {
      stack[i - 1] = stack[i];
    }
    stack[RAS_DEPTH - 1] = value;
  }

  void ras_pop(uint32_t &count) {
    if (count > 0) {
      count--;
    }
  }


public:
  // ========================================================================
  // 构造与析构
  // ========================================================================
  BPU_TOP() {
    // 注册全局指针，让TAGE可以访问BPU的GHR/FH
    g_bpu_top = this;
    
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      tage_inst[i] = new TAGE_TOP();
      btb_inst[i] = new BTB_TOP();
    }

    // Initialize Memory
    std::memset(inst_type_mem, 0, sizeof(inst_type_mem));
    reset_internal_all();
  }

  void bpu_seq_read(const InputPayload &inp, ReadData &rd) const {
    std::memset(&rd, 0, sizeof(ReadData));

    rd.state_snapshot = state;
    rd.pc_reg_snapshot = pc_reg;
    rd.pc_can_send_to_icache_snapshot = pc_can_send_to_icache;
    rd.pred_base_pc_fired_snapshot = pred_base_pc_fired;
    rd.do_pred_latch_snapshot = do_pred_latch;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      rd.do_upd_latch_snapshot[i] = do_upd_latch[i];
      rd.tage_done_snapshot[i] = tage_done[i];
      rd.btb_done_snapshot[i] = btb_done[i];
      rd.q_wr_ptr_snapshot[i] = q_wr_ptr[i];
      rd.q_rd_ptr_snapshot[i] = q_rd_ptr[i];
      rd.q_count_snapshot[i] = q_count[i];
    }

    for (int i = 0; i < FETCH_WIDTH; i++) {
      rd.tage_calc_pred_dir_latch_snapshot[i] = tage_calc_pred_dir_latch[i];
      rd.tage_calc_altpred_latch_snapshot[i] = tage_calc_altpred_latch[i];
      rd.tage_calc_pcpn_latch_snapshot[i] = tage_calc_pcpn_latch[i];
      rd.tage_calc_altpcpn_latch_snapshot[i] = tage_calc_altpcpn_latch[i];
      rd.tage_result_valid_latch_snapshot[i] = tage_result_valid_latch[i];
      rd.btb_pred_target_latch_snapshot[i] = btb_pred_target_latch[i];
      rd.btb_result_valid_latch_snapshot[i] = btb_result_valid_latch[i];
      for (int k = 0; k < TN_MAX; k++) {
        rd.tage_pred_calc_tags_latch_snapshot[i][k] = tage_pred_calc_tags_latch[i][k];
        rd.tage_pred_calc_idxs_latch_snapshot[i][k] = tage_pred_calc_idxs_latch[i][k];
      }
    }

    rd.saved_2ahead_prediction_snapshot = saved_2ahead_prediction;
    rd.saved_2ahead_pred_valid_snapshot = saved_2ahead_pred_valid;
    rd.saved_mini_flush_req_snapshot = saved_mini_flush_req;
    rd.saved_mini_flush_correct_snapshot = saved_mini_flush_correct;
    rd.saved_mini_flush_target_snapshot = saved_mini_flush_target;
    rd.ahead_gate_enable_snapshot = ahead_gate_enable;
    rd.ahead_gate_sample_count_snapshot = ahead_gate_sample_count;
    rd.ahead_gate_success_count_snapshot = ahead_gate_success_count;
    rd.ahead_gate_disable_count_snapshot = ahead_gate_disable_count;
    rd.ahead_gate_enable_count_snapshot = ahead_gate_enable_count;
    std::memcpy(rd.Arch_GHR_snapshot, Arch_GHR, sizeof(rd.Arch_GHR_snapshot));
    std::memcpy(rd.Spec_GHR_snapshot, Spec_GHR, sizeof(rd.Spec_GHR_snapshot));
    std::memcpy(rd.Arch_FH_snapshot, Arch_FH, sizeof(rd.Arch_FH_snapshot));
    std::memcpy(rd.Spec_FH_snapshot, Spec_FH, sizeof(rd.Spec_FH_snapshot));
    std::memcpy(rd.Arch_ras_stack_snapshot, Arch_ras_stack, sizeof(rd.Arch_ras_stack_snapshot));
    rd.Arch_ras_count_snapshot = Arch_ras_count;
    std::memcpy(rd.Spec_ras_stack_snapshot, Spec_ras_stack, sizeof(rd.Spec_ras_stack_snapshot));
    rd.Spec_ras_count_snapshot = Spec_ras_count;
#ifdef ENABLE_BPU_RAS
    rd.ras_count_snapshot = 0;
    rd.ras_has_entry_snapshot = false;
    rd.ras_top_snapshot = 0;
#else
    rd.ras_count_snapshot = 0;
    rd.ras_has_entry_snapshot = false;
    rd.ras_top_snapshot = 0;
#endif
    bpu_prepare_comb_read(inp, rd);
  }

  void bpu_prepare_comb_read(const InputPayload &inp, ReadData &rd) const {
#ifdef ENABLE_BPU_RAS
    const bool use_arch_ras_snapshot = inp.refetch;
    const uint32_t ras_count_snapshot =
        use_arch_ras_snapshot ? rd.Arch_ras_count_snapshot : rd.Spec_ras_count_snapshot;
    rd.ras_count_snapshot = ras_count_snapshot;
    rd.ras_has_entry_snapshot = (ras_count_snapshot > 0);
    if (rd.ras_has_entry_snapshot) {
      rd.ras_top_snapshot = use_arch_ras_snapshot
                                ? rd.Arch_ras_stack_snapshot[ras_count_snapshot - 1]
                                : rd.Spec_ras_stack_snapshot[ras_count_snapshot - 1];
    } else {
      rd.ras_top_snapshot = 0;
    }
#endif

    rd.pred_base_pc = inp.refetch ? inp.refetch_address : rd.pc_reg_snapshot;

    const uint32_t CACHE_MASK = ~(ICACHE_LINE_SIZE - 1);
    uint32_t pc_plus_width = rd.pred_base_pc + (FETCH_WIDTH * 4);
    rd.boundary_addr =
        ((rd.pred_base_pc & CACHE_MASK) != (pc_plus_width & CACHE_MASK))
            ? (pc_plus_width & CACHE_MASK)
            : pc_plus_width;

    for (int i = 0; i < FETCH_WIDTH; i++) {
      rd.do_pred_for_this_pc[i] = rd.pred_base_pc + (i * 4);
      if (rd.do_pred_for_this_pc[i] < rd.boundary_addr) {
        rd.this_pc_bank_sel[i] = bank_sel_from_pc(rd.do_pred_for_this_pc[i]);
        rd.do_pred_on_this_pc[i] = true;
        int type_idx = bank_pc_from_pc(rd.do_pred_for_this_pc[i]) & BPU_TYPE_IDX_MASK;
        int bank_sel = rd.this_pc_bank_sel[i];
        rd.pred_inst_type_snapshot[i] = inst_type_mem[type_idx][bank_sel];
        uint32_t ahead_idx =
            bank_pc_from_pc(rd.do_pred_for_this_pc[i]) % AHEAD_SLOT1_TABLE_SIZE;
        const AheadSlot1Entry &ahead_entry = ahead_slot1_table[bank_sel][ahead_idx];
        rd.ahead_entry_valid_snapshot[i] = ahead_entry.valid;
        rd.ahead_entry_taken_snapshot[i] = ahead_entry.taken;
        rd.ahead_entry_target_snapshot[i] = ahead_entry.target;
        rd.ahead_entry_conf_snapshot[i] = ahead_entry.conf;
      } else {
        rd.this_pc_bank_sel[i] = -1;
        rd.do_pred_on_this_pc[i] = false;
        rd.pred_inst_type_snapshot[i] = BR_NONCTL;
        rd.ahead_entry_valid_snapshot[i] = false;
        rd.ahead_entry_taken_snapshot[i] = false;
        rd.ahead_entry_target_snapshot[i] = 0;
        rd.ahead_entry_conf_snapshot[i] = 0;
      }
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      rd.q_full[i] = (rd.q_count_snapshot[i] == Q_DEPTH);
      rd.q_empty[i] = (rd.q_count_snapshot[i] == 0);
      rd.last_block_valid_snapshot[i] = last_block_table[i].valid;
      rd.last_block_pc_snapshot[i] = last_block_table[i].last_pc;
      if (rd.last_block_valid_snapshot[i]) {
        uint32_t slot1_idx =
            bank_pc_from_pc(rd.last_block_pc_snapshot[i]) % AHEAD_SLOT1_TABLE_SIZE;
        const AheadSlot1Entry &slot1_entry = ahead_slot1_table[i][slot1_idx];
        rd.last_block_entry_valid_snapshot[i] = slot1_entry.valid;
        rd.last_block_entry_taken_snapshot[i] = slot1_entry.taken;
        rd.last_block_entry_target_snapshot[i] = slot1_entry.target;
        rd.last_block_entry_conf_snapshot[i] = slot1_entry.conf;
      } else {
        rd.last_block_entry_valid_snapshot[i] = false;
        rd.last_block_entry_taken_snapshot[i] = false;
        rd.last_block_entry_target_snapshot[i] = 0;
        rd.last_block_entry_conf_snapshot[i] = 0;
      }
      const QueueEntry &entry = update_queue[rd.q_rd_ptr_snapshot[i]][i];
      rd.q_data[i].base_pc = entry.base_pc;
      rd.q_data[i].valid_mask = entry.valid_mask;
      rd.q_data[i].actual_dir = entry.actual_dir;
      rd.q_data[i].br_type = entry.br_type;
      rd.q_data[i].targets = entry.targets;
      rd.q_data[i].pred_dir = entry.pred_dir;
      rd.q_data[i].alt_pred = entry.alt_pred;
      rd.q_data[i].pcpn = entry.pcpn;
      rd.q_data[i].altpcpn = entry.altpcpn;
      for (int k = 0; k < TN_MAX; k++) {
        rd.q_data[i].tage_tags[k] = entry.tage_tags[k];
        rd.q_data[i].tage_idxs[k] = entry.tage_idxs[k];
      }
    }

    rd.going_to_do_pred =
        rd.pc_can_send_to_icache_snapshot && (inp.icache_read_ready || inp.refetch);
    rd.going_to_do_upd_any = false;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      rd.going_to_do_upd[i] = !rd.q_empty[i];
      rd.going_to_do_upd_any |= rd.going_to_do_upd[i];
    }
    rd.trans_ready_to_fire = rd.going_to_do_pred || rd.going_to_do_upd_any;
    rd.set_submodule_input = rd.trans_ready_to_fire;

    auto read_nlp_entry = [&](uint32_t pc, ReadData::NlpEntrySnapshot &snapshot) {
      const uint32_t idx = nlp_index_from_pc(pc);
      const NLPEntry &entry = nlp_table[idx];
      snapshot.entry_valid = entry.valid;
      snapshot.entry_tag = entry.tag;
      snapshot.entry_target = entry.target;
      snapshot.entry_conf = entry.conf;
    };
    read_nlp_entry(rd.pred_base_pc, rd.nlp_pred_base_entry_snapshot);
    read_nlp_entry(rd.pred_base_pc, rd.nlp_train_entry_snapshot);
    rd.nlp_s1_req_pc_snapshot = 0;
    std::memset(&rd.nlp_s1_entry_snapshot, 0, sizeof(rd.nlp_s1_entry_snapshot));
#ifdef ENABLE_2AHEAD
    if (rd.going_to_do_pred && !inp.refetch) {
      const uint32_t stage1_tag = nlp_tag_from_pc(rd.pred_base_pc);
      const bool stage1_hit = rd.nlp_pred_base_entry_snapshot.entry_valid &&
                              (rd.nlp_pred_base_entry_snapshot.entry_tag == stage1_tag);
      const uint8_t stage1_conf = stage1_hit ? rd.nlp_pred_base_entry_snapshot.entry_conf : 0;
      const uint32_t stage1_next_pc =
          (stage1_hit && stage1_conf >= NLP_CONF_THRESHOLD)
              ? rd.nlp_pred_base_entry_snapshot.entry_target
              : nlp_fallback_next_pc(rd.pred_base_pc);
      rd.nlp_s1_req_pc_snapshot = stage1_next_pc;
      read_nlp_entry(stage1_next_pc, rd.nlp_s1_entry_snapshot);
    }
#endif

    std::memset(rd.tage_in, 0, sizeof(rd.tage_in));
    std::memset(rd.btb_in, 0, sizeof(rd.btb_in));
#ifdef SPECULATIVE_ON
    const bool *ghr_src = rd.Spec_GHR_snapshot;
    const uint32_t (*fh_src)[TN_MAX] = rd.Spec_FH_snapshot;
#else
    const bool *ghr_src = rd.Arch_GHR_snapshot;
    const uint32_t (*fh_src)[TN_MAX] = rd.Arch_FH_snapshot;
#endif
    for (int b = 0; b < BPU_BANK_NUM; b++) {
      for (int k = 0; k < FH_N_MAX; k++) {
        for (int i = 0; i < TN_MAX; i++) {
          rd.tage_in[b].fh_in[k][i] = fh_src[k][i];
        }
      }
      for (int i = 0; i < GHR_LENGTH; i++) {
        rd.tage_in[b].ghr_in[i] = ghr_src[i];
      }
    }

    if (rd.set_submodule_input) {
      if (rd.going_to_do_pred) {
        bool pred_req_sent[BPU_BANK_NUM];
        std::memset(pred_req_sent, 0, sizeof(pred_req_sent));
        for (int i = 0; i < FETCH_WIDTH; i++) {
          if (!rd.do_pred_on_this_pc[i]) {
            continue;
          }
          int bank_sel = rd.this_pc_bank_sel[i];
          if (bank_sel >= 0 && bank_sel < BPU_BANK_NUM && !pred_req_sent[bank_sel]) {
            rd.tage_in[bank_sel].pred_req = true;
            rd.tage_in[bank_sel].pc_pred_in = bank_pc_from_pc(rd.do_pred_for_this_pc[i]);
            rd.btb_in[bank_sel].pred_req = true;
            rd.btb_in[bank_sel].pred_pc = bank_pc_from_pc(rd.do_pred_for_this_pc[i]);
            pred_req_sent[bank_sel] = true;
          }
        }
      }

      for (int i = 0; i < BPU_BANK_NUM; i++) {
        if (rd.going_to_do_upd[i]) {
          uint32_t u_pc = rd.q_data[i].base_pc;
          bool is_cond_upd = (rd.q_data[i].br_type == BR_DIRECT);
          if (is_cond_upd) {
            rd.tage_in[i].update_en = rd.q_data[i].valid_mask;
            rd.tage_in[i].pc_update_in = bank_pc_from_pc(u_pc);
            rd.tage_in[i].real_dir = rd.q_data[i].actual_dir;
            rd.tage_in[i].pred_in = rd.q_data[i].pred_dir;
            rd.tage_in[i].alt_pred_in = rd.q_data[i].alt_pred;
            rd.tage_in[i].pcpn_in = rd.q_data[i].pcpn;
            rd.tage_in[i].altpcpn_in = rd.q_data[i].altpcpn;
            for (int k = 0; k < TN_MAX; k++) {
              rd.tage_in[i].tage_tag_flat_in[k] = rd.q_data[i].tage_tags[k];
              rd.tage_in[i].tage_idx_flat_in[k] = rd.q_data[i].tage_idxs[k];
            }
          }

          rd.btb_in[i].upd_valid = rd.q_data[i].valid_mask;
          rd.btb_in[i].upd_pc = bank_pc_from_pc(rd.q_data[i].base_pc);
          rd.btb_in[i].upd_actual_addr = rd.q_data[i].targets;
          rd.btb_in[i].upd_actual_dir = rd.q_data[i].actual_dir;
          rd.btb_in[i].upd_br_type_in = rd.q_data[i].br_type;
        }
      }
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      tage_inst[i]->tage_seq_read(rd.tage_in[i], rd.tage_rd[i]);
      btb_inst[i]->btb_seq_read(rd.btb_in[i], rd.btb_rd[i]);
    }
  }

  void bpu_comb(const BpuCombIn &comb_in, BpuCombOut &comb_out) {
    const InputPayload &inp = comb_in.inp;
    const ReadData &rd = comb_in.rd;
    OutputPayload &out = comb_out.out_regs;
    UpdateRequest &req = comb_out.update_req;
    std::memset(&out, 0, sizeof(OutputPayload));
    std::memset(&req, 0, sizeof(UpdateRequest));
    req.next_state = S_IDLE;
    req.pred_base_pc = rd.pred_base_pc;
    req.going_to_do_pred = rd.going_to_do_pred;
    req.pc_reg_next = rd.pc_reg_snapshot;
    req.pc_can_send_to_icache_next = rd.pc_can_send_to_icache_snapshot;
    req.do_pred_latch_next = false;
    req.pred_base_pc_fired_next = rd.pred_base_pc;
    req.saved_2ahead_prediction_next = rd.saved_2ahead_prediction_snapshot;
    req.saved_2ahead_pred_valid_next = rd.saved_2ahead_pred_valid_snapshot;
    req.saved_mini_flush_req_next = rd.saved_mini_flush_req_snapshot;
    req.saved_mini_flush_correct_next = rd.saved_mini_flush_correct_snapshot;
    req.saved_mini_flush_target_next = rd.saved_mini_flush_target_snapshot;
    req.ahead_gate_enable_next = rd.ahead_gate_enable_snapshot;
    req.ahead_gate_sample_count_next = rd.ahead_gate_sample_count_snapshot;
    req.ahead_gate_success_count_next = rd.ahead_gate_success_count_snapshot;
    req.ahead_gate_disable_count_next = rd.ahead_gate_disable_count_snapshot;
    req.ahead_gate_enable_count_next = rd.ahead_gate_enable_count_snapshot;
    req.nlp_s1_valid_next = false;
    req.nlp_s1_req_pc_next = 0;
    req.nlp_s1_pred_next_pc_next = 0;
    req.nlp_s1_hit_next = false;
    req.nlp_s1_conf_next = 0;
    req.nlp_s2_valid_next = false;
    req.nlp_s2_req_pc_next = 0;
    req.nlp_s2_pred_2ahead_pc_next = 0;
    req.nlp_s2_hit_next = false;
    req.nlp_s2_conf_next = 0;
    req.nlp_entry_we = false;
    req.nlp_entry_idx = 0;
    req.nlp_entry_valid_next = false;
    req.nlp_entry_tag_next = 0;
    req.nlp_entry_target_next = 0;
    req.nlp_entry_conf_next = 0;
    std::memcpy(req.Spec_GHR_next, rd.Spec_GHR_snapshot, sizeof(req.Spec_GHR_next));
    std::memcpy(req.Arch_GHR_next, rd.Arch_GHR_snapshot, sizeof(req.Arch_GHR_next));
    std::memcpy(req.Spec_FH_next, rd.Spec_FH_snapshot, sizeof(req.Spec_FH_next));
    std::memcpy(req.Arch_FH_next, rd.Arch_FH_snapshot, sizeof(req.Arch_FH_next));
    std::memcpy(req.Arch_ras_stack_next, rd.Arch_ras_stack_snapshot,
                sizeof(req.Arch_ras_stack_next));
    req.Arch_ras_count_next = rd.Arch_ras_count_snapshot;
    std::memcpy(req.Spec_ras_stack_next, rd.Spec_ras_stack_snapshot,
                sizeof(req.Spec_ras_stack_next));
    req.Spec_ras_count_next = rd.Spec_ras_count_snapshot;

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      req.going_to_do_upd[i] = rd.going_to_do_upd[i];
      req.do_upd_latch_next[i] = false;
      req.q_push_en[i] = false;
      req.q_pop_en[i] = false;
      req.q_wr_ptr_next[i] = rd.q_wr_ptr_snapshot[i];
      req.q_rd_ptr_next[i] = rd.q_rd_ptr_snapshot[i];
      req.q_count_next[i] = rd.q_count_snapshot[i];
      req.ahead_entry_we[i] = false;
      req.ahead_entry_idx[i] = 0;
      req.ahead_entry_valid_next[i] = false;
      req.ahead_entry_taken_next[i] = false;
      req.ahead_entry_target_next[i] = 0;
      req.ahead_entry_conf_next[i] = 0;
      req.last_block_valid_next[i] = rd.last_block_valid_snapshot[i];
      req.last_block_pc_next[i] = rd.last_block_pc_snapshot[i];
      req.tage_done_next[i] = false;
      req.btb_done_next[i] = false;
      req.tage_in[i] = rd.tage_in[i];
      req.btb_in[i] = rd.btb_in[i];
    }
    for (int i = 0; i < FETCH_WIDTH; i++) {
      req.do_pred_on_this_pc[i] = rd.do_pred_on_this_pc[i];
      req.this_pc_bank_sel[i] = rd.this_pc_bank_sel[i];
      req.do_pred_for_this_pc[i] = rd.do_pred_for_this_pc[i];
      req.final_pred_dir[i] = false;
      req.tage_calc_pred_dir_latch_next[i] = false;
      req.tage_calc_altpred_latch_next[i] = false;
      req.tage_calc_pcpn_latch_next[i] = 0;
      req.tage_calc_altpcpn_latch_next[i] = 0;
      req.tage_result_valid_latch_next[i] = false;
      req.btb_pred_target_latch_next[i] = 0;
      req.btb_result_valid_latch_next[i] = false;
      for (int k = 0; k < TN_MAX; ++k) {
        req.tage_pred_calc_tags_latch_next[i][k] = 0;
        req.tage_pred_calc_idxs_latch_next[i][k] = 0;
      }
    }
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      req.q_entry_we[i] = false;
      req.q_entry_bank[i] = 0;
      req.q_entry_slot[i] = 0;
      std::memset(&req.q_entry_data[i], 0, sizeof(req.q_entry_data[i]));
      req.inst_type_we[i] = false;
      req.inst_type_bank[i] = 0;
      req.inst_type_idx[i] = 0;
      req.inst_type_data[i] = BR_NONCTL;
    }

    TAGE_TOP::OutputPayload tage_out[BPU_BANK_NUM];
    BTB_TOP::OutputPayload btb_out[BPU_BANK_NUM];
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      TAGE_TOP::TageCombOut tage_comb_out{};
      tage_inst[i]->tage_comb(TAGE_TOP::TageCombIn{rd.tage_in[i], rd.tage_rd[i]},
                              tage_comb_out);
      tage_out[i] = tage_comb_out.out_regs;
      req.tage_req[i] = tage_comb_out.req;

      BTB_TOP::BtbCombOut btb_comb_out{};
      btb_inst[i]->btb_comb(BTB_TOP::BtbCombIn{rd.btb_in[i], rd.btb_rd[i]},
                            btb_comb_out);
      btb_out[i] = btb_comb_out.out_regs;
      req.btb_req[i] = btb_comb_out.req;
    }

    out.icache_read_valid = rd.pc_can_send_to_icache_snapshot && !inp.refetch;
    out.fetch_address = rd.pred_base_pc;
    out.out_pred_base_pc = out.fetch_address;
    req.next_fetch_addr_calc = rd.boundary_addr;
    bool found_taken_branch = false;
    if (req.going_to_do_pred) {
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        int bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel < 0 || bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        const br_type_t p_type = rd.pred_inst_type_snapshot[i];
        const bool tage_valid = tage_out[bank_sel].tage_pred_out_valid;
        const bool btb_valid = btb_out[bank_sel].btb_pred_out_valid;
        bool pred_taken = false;
        if (p_type == BR_NONCTL) {
          pred_taken = false;
        } else if (p_type == BR_RET || p_type == BR_CALL || p_type == BR_IDIRECT ||
                   p_type == BR_JAL) {
          pred_taken = true;
        } else {
          pred_taken = tage_valid ? tage_out[bank_sel].pred_out : false;
        }
        req.final_pred_dir[i] = pred_taken;
        req.tage_calc_pred_dir_latch_next[i] = pred_taken;
        req.tage_calc_altpred_latch_next[i] =
            tage_valid ? tage_out[bank_sel].alt_pred_out : false;
        req.tage_calc_pcpn_latch_next[i] =
            tage_valid ? tage_out[bank_sel].pcpn_out : static_cast<pcpn_t>(0);
        req.tage_calc_altpcpn_latch_next[i] =
            tage_valid ? tage_out[bank_sel].altpcpn_out : static_cast<pcpn_t>(0);
        req.tage_result_valid_latch_next[i] = tage_valid;
        req.btb_pred_target_latch_next[i] =
            btb_valid ? btb_out[bank_sel].pred_target : rd.do_pred_for_this_pc[i] + 4;
        req.btb_result_valid_latch_next[i] = btb_valid;
        for (int k = 0; k < TN_MAX; ++k) {
          req.tage_pred_calc_tags_latch_next[i][k] = tage_out[bank_sel].tage_tag_flat_out[k];
          req.tage_pred_calc_idxs_latch_next[i][k] = tage_out[bank_sel].tage_idx_flat_out[k];
        }

        if (pred_taken && !found_taken_branch && btb_valid) {
          uint32_t chosen_target = btb_out[bank_sel].pred_target;
          if (p_type == BR_RET && rd.ras_has_entry_snapshot) {
            chosen_target = rd.ras_top_snapshot;
          }
          req.next_fetch_addr_calc = chosen_target;
          found_taken_branch = true;
        }
      }
    }

    out.predict_next_fetch_address = req.next_fetch_addr_calc;
    out.PTAB_write_enable = req.going_to_do_pred && !inp.refetch;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.out_pred_dir[i] = req.final_pred_dir[i];
      out.out_alt_pred[i] = req.tage_calc_altpred_latch_next[i];
      out.out_pcpn[i] = req.tage_calc_pcpn_latch_next[i];
      out.out_altpcpn[i] = req.tage_calc_altpcpn_latch_next[i];
      for (int k = 0; k < TN_MAX; k++) {
        out.out_tage_tags[i][k] = req.tage_pred_calc_tags_latch_next[i][k];
        out.out_tage_idxs[i][k] = req.tage_pred_calc_idxs_latch_next[i][k];
      }
    }

    req.final_2_ahead_address = out.fetch_address + (FETCH_WIDTH * 4);
    const uint32_t refetch_twoahead_target = inp.refetch_address + (FETCH_WIDTH * 4);
    const uint32_t fallback_twoahead_target = out.fetch_address + (FETCH_WIDTH * 4);
    out.two_ahead_valid = inp.refetch ? false : rd.saved_2ahead_pred_valid_snapshot;
    out.two_ahead_target =
        inp.refetch ? refetch_twoahead_target
                    : (out.two_ahead_valid ? rd.saved_2ahead_prediction_snapshot
                                           : fallback_twoahead_target);
    out.mini_flush_req = false;
    out.mini_flush_correct = rd.saved_mini_flush_correct_snapshot && !inp.refetch;
    out.mini_flush_target = rd.saved_mini_flush_target_snapshot;

#ifdef ENABLE_2AHEAD
    if (req.going_to_do_pred && !inp.refetch) {
      const uint32_t stage1_tag = nlp_tag_from_pc(rd.pred_base_pc);
      const bool stage1_hit = rd.nlp_pred_base_entry_snapshot.entry_valid &&
                              (rd.nlp_pred_base_entry_snapshot.entry_tag == stage1_tag);
      const uint8_t stage1_conf = stage1_hit ? rd.nlp_pred_base_entry_snapshot.entry_conf : 0;
      const uint32_t stage1_next_pc =
          (stage1_hit && stage1_conf >= NLP_CONF_THRESHOLD)
              ? rd.nlp_pred_base_entry_snapshot.entry_target
              : nlp_fallback_next_pc(rd.pred_base_pc);

      const uint32_t stage2_tag = nlp_tag_from_pc(stage1_next_pc);
      const bool stage2_hit = rd.nlp_s1_entry_snapshot.entry_valid &&
                              (rd.nlp_s1_entry_snapshot.entry_tag == stage2_tag);
      const uint8_t stage2_conf = stage2_hit ? rd.nlp_s1_entry_snapshot.entry_conf : 0;
      const uint32_t stage2_target =
          stage2_hit ? rd.nlp_s1_entry_snapshot.entry_target
                     : nlp_fallback_next_pc(stage1_next_pc);
      const bool s1_match = (stage1_next_pc == req.next_fetch_addr_calc);
      const bool s2_usable = stage2_hit && (stage2_conf >= NLP_CONF_THRESHOLD);
      const bool emit_two_ahead = s1_match && s2_usable;
      req.final_2_ahead_address =
          emit_two_ahead ? stage2_target : (req.next_fetch_addr_calc + (FETCH_WIDTH * 4));

      const bool need_mini_flush =
          (rd.saved_2ahead_prediction_snapshot != req.next_fetch_addr_calc);
      out.mini_flush_req = need_mini_flush && out.PTAB_write_enable;
      out.mini_flush_target = rd.saved_mini_flush_target_snapshot;
      req.saved_2ahead_prediction_next = req.final_2_ahead_address;
      req.saved_2ahead_pred_valid_next = emit_two_ahead;
      req.saved_mini_flush_req_next = need_mini_flush;
      req.saved_mini_flush_correct_next = !need_mini_flush;
      req.saved_mini_flush_target_next = rd.saved_2ahead_prediction_snapshot;

      const uint32_t train_pc = rd.pred_base_pc;
      const uint32_t train_target = req.next_fetch_addr_calc;
      const uint32_t train_idx = nlp_index_from_pc(train_pc);
      const uint32_t train_tag = nlp_tag_from_pc(train_pc);
      const ReadData::NlpEntrySnapshot &old_entry = rd.nlp_train_entry_snapshot;
      const bool train_hit = old_entry.entry_valid && (old_entry.entry_tag == train_tag);
      uint8_t next_conf = NLP_CONF_INIT;
      if (train_hit) {
        if (old_entry.entry_target == train_target) {
          next_conf = (old_entry.entry_conf >= NLP_CONF_MAX)
                          ? static_cast<uint8_t>(NLP_CONF_MAX)
                          : static_cast<uint8_t>(old_entry.entry_conf + 1);
        } else {
          next_conf = 0;
        }
      }
      req.nlp_entry_we = true;
      req.nlp_entry_idx = train_idx;
      req.nlp_entry_valid_next = true;
      req.nlp_entry_tag_next = train_tag;
      req.nlp_entry_target_next = train_target;
      req.nlp_entry_conf_next = next_conf;
    }
#endif

#ifndef ENABLE_2AHEAD
    out.two_ahead_valid = false;
    out.mini_flush_req = false;
    out.mini_flush_correct = false;
#endif

    req.should_update_spec_hist = req.going_to_do_pred && !inp.refetch;
    bool saw_low_conf_fallback = false;
    (void)saw_low_conf_fallback;
#if defined(ENABLE_2AHEAD) && ENABLE_2AHEAD_SLOT1_PRED && \
    ENABLE_2AHEAD_SLOT1_ADAPTIVE_GATING
    const bool has_gate_sample = req.going_to_do_pred && !inp.refetch;
    bool gate_sample_success = false;
    if (has_gate_sample) {
      gate_sample_success =
          (rd.saved_2ahead_prediction_snapshot == req.next_fetch_addr_calc) &&
          !saw_low_conf_fallback;
    }
    AheadGateUpdateCombOut gate_update_out{};
    ahead_gate_update_comb(
        AheadGateUpdateCombIn{rd.ahead_gate_enable_snapshot,
                              rd.ahead_gate_sample_count_snapshot,
                              rd.ahead_gate_success_count_snapshot,
                              has_gate_sample,
                              gate_sample_success},
        gate_update_out);
    req.ahead_gate_enable_next = gate_update_out.gate_enable_next;
    req.ahead_gate_sample_count_next = gate_update_out.sample_count_next;
    req.ahead_gate_success_count_next = gate_update_out.success_count_next;
    req.ahead_gate_disable_count_next =
        rd.ahead_gate_disable_count_snapshot + (gate_update_out.toggled_disable ? 1U : 0U);
    req.ahead_gate_enable_count_next =
        rd.ahead_gate_enable_count_snapshot + (gate_update_out.toggled_enable ? 1U : 0U);
#endif

    if (req.should_update_spec_hist) {
      bool spec_ghr_tmp[GHR_LENGTH];
      uint32_t spec_fh_tmp[FH_N_MAX][TN_MAX];
      std::memcpy(spec_ghr_tmp, req.Spec_GHR_next, sizeof(spec_ghr_tmp));
      std::memcpy(spec_fh_tmp, req.Spec_FH_next, sizeof(spec_fh_tmp));
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        int bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel < 0 || bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        br_type_t p_type = rd.pred_inst_type_snapshot[i];
        const bool pred_taken = req.final_pred_dir[i];
        if (p_type == BR_DIRECT) {
          bool next_ghr[GHR_LENGTH];
          uint32_t next_fh[FH_N_MAX][TN_MAX];
          tage_ghr_update_apply(spec_ghr_tmp, pred_taken, next_ghr);
          tage_fh_update_apply(spec_fh_tmp, spec_ghr_tmp, pred_taken, next_fh,
                               fh_length, ghr_length);
          std::memcpy(spec_ghr_tmp, next_ghr, sizeof(next_ghr));
          std::memcpy(spec_fh_tmp, next_fh, sizeof(next_fh));
        }
        if (pred_taken) {
          break;
        }
      }
      std::memcpy(req.Spec_GHR_next, spec_ghr_tmp, sizeof(req.Spec_GHR_next));
      std::memcpy(req.Spec_FH_next, spec_fh_tmp, sizeof(req.Spec_FH_next));

#ifdef ENABLE_BPU_RAS
      uint32_t spec_ras_stack_tmp[RAS_DEPTH];
      uint32_t spec_ras_count_tmp = req.Spec_ras_count_next;
      std::memcpy(spec_ras_stack_tmp, req.Spec_ras_stack_next, sizeof(spec_ras_stack_tmp));
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        int bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel < 0 || bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        uint32_t pc = rd.do_pred_for_this_pc[i];
        br_type_t p_type = rd.pred_inst_type_snapshot[i];
        bool pred_taken = req.final_pred_dir[i];
        if (!pred_taken) {
          continue;
        }
        if (p_type == BR_CALL) {
          ras_push(spec_ras_stack_tmp, spec_ras_count_tmp, pc + 4);
        } else if (p_type == BR_RET) {
          ras_pop(spec_ras_count_tmp);
        }
        break;
      }
      std::memcpy(req.Spec_ras_stack_next, spec_ras_stack_tmp, sizeof(req.Spec_ras_stack_next));
      req.Spec_ras_count_next = spec_ras_count_tmp;
#endif
    }

    {
      bool arch_ghr_tmp[GHR_LENGTH];
      uint32_t arch_fh_tmp[FH_N_MAX][TN_MAX];
      std::memcpy(arch_ghr_tmp, req.Arch_GHR_next, sizeof(arch_ghr_tmp));
      std::memcpy(arch_fh_tmp, req.Arch_FH_next, sizeof(arch_fh_tmp));
      bool arch_need_write = false;
      for (int i = 0; i < COMMIT_WIDTH; ++i) {
        if (!inp.in_upd_valid[i]) {
          continue;
        }
        bool is_cond_upd = (inp.in_actual_br_type[i] == BR_DIRECT);
        if (!is_cond_upd) {
          continue;
        }
        bool real_dir = inp.in_actual_dir[i];
        bool next_ghr[GHR_LENGTH];
        uint32_t next_fh[FH_N_MAX][TN_MAX];
        tage_ghr_update_apply(arch_ghr_tmp, real_dir, next_ghr);
        tage_fh_update_apply(arch_fh_tmp, arch_ghr_tmp, real_dir, next_fh, fh_length,
                             ghr_length);
        if (inp.in_pred_dir[i] != real_dir) {
          std::memcpy(req.Spec_GHR_next, next_ghr, sizeof(req.Spec_GHR_next));
          std::memcpy(req.Spec_FH_next, next_fh, sizeof(req.Spec_FH_next));
        }
        std::memcpy(arch_ghr_tmp, next_ghr, sizeof(arch_ghr_tmp));
        std::memcpy(arch_fh_tmp, next_fh, sizeof(arch_fh_tmp));
        arch_need_write = true;
      }
      if (arch_need_write) {
        std::memcpy(req.Arch_GHR_next, arch_ghr_tmp, sizeof(req.Arch_GHR_next));
        std::memcpy(req.Arch_FH_next, arch_fh_tmp, sizeof(req.Arch_FH_next));
      }
    }

#ifdef ENABLE_BPU_RAS
    {
      uint32_t arch_ras_stack_tmp[RAS_DEPTH];
      uint32_t arch_ras_count_tmp = req.Arch_ras_count_next;
      std::memcpy(arch_ras_stack_tmp, req.Arch_ras_stack_next, sizeof(arch_ras_stack_tmp));
      for (int i = 0; i < COMMIT_WIDTH; ++i) {
        if (!inp.in_upd_valid[i]) {
          continue;
        }
        br_type_t br_type = inp.in_actual_br_type[i];
        if (br_type == BR_CALL) {
          ras_push(arch_ras_stack_tmp, arch_ras_count_tmp, inp.in_update_base_pc[i] + 4);
        } else if (br_type == BR_RET) {
          ras_pop(arch_ras_count_tmp);
        }
      }
      std::memcpy(req.Arch_ras_stack_next, arch_ras_stack_tmp, sizeof(req.Arch_ras_stack_next));
      req.Arch_ras_count_next = arch_ras_count_tmp;
    }
#endif

    for (int i = 0; i < BPU_BANK_NUM; ++i) {
      if (rd.going_to_do_upd[i] && req.q_count_next[i] > 0) {
        req.q_pop_en[i] = true;
        req.q_rd_ptr_next[i] = (req.q_rd_ptr_next[i] + 1) % Q_DEPTH;
        req.q_count_next[i]--;
      }
    }
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      if (!inp.in_upd_valid[i]) {
        continue;
      }
      int bank_sel = bank_sel_from_pc(inp.in_update_base_pc[i]);
      if (req.q_count_next[bank_sel] < Q_DEPTH) {
        req.q_push_en[bank_sel] = true;
        req.q_entry_we[i] = true;
        req.q_entry_bank[i] = bank_sel;
        req.q_entry_slot[i] = req.q_wr_ptr_next[bank_sel];
        req.q_entry_data[i].base_pc = inp.in_update_base_pc[i];
        req.q_entry_data[i].valid_mask = inp.in_upd_valid[i];
        req.q_entry_data[i].actual_dir = inp.in_actual_dir[i];
        req.q_entry_data[i].pred_dir = inp.in_pred_dir[i];
        req.q_entry_data[i].alt_pred = inp.in_alt_pred[i];
        req.q_entry_data[i].br_type = inp.in_actual_br_type[i];
        req.q_entry_data[i].targets = inp.in_actual_targets[i];
        req.q_entry_data[i].pcpn = inp.in_pcpn[i];
        req.q_entry_data[i].altpcpn = inp.in_altpcpn[i];
        for (int k = 0; k < TN_MAX; k++) {
          req.q_entry_data[i].tage_tags[k] = inp.in_tage_tags[i][k];
          req.q_entry_data[i].tage_idxs[k] = inp.in_tage_idxs[i][k];
        }
        req.q_wr_ptr_next[bank_sel] = (req.q_wr_ptr_next[bank_sel] + 1) % Q_DEPTH;
        req.q_count_next[bank_sel]++;
      }

      uint32_t addr = inp.in_update_base_pc[i];
      req.inst_type_we[i] = true;
      req.inst_type_bank[i] = bank_sel_from_pc(addr);
      req.inst_type_idx[i] = bank_pc_from_pc(addr) & BPU_TYPE_IDX_MASK;
      req.inst_type_data[i] = inp.in_actual_br_type[i];
    }

    if (inp.refetch) {
      req.pc_reg_next = inp.refetch_address;
      req.pc_can_send_to_icache_next = true;
      req.saved_2ahead_prediction_next = inp.refetch_address + (FETCH_WIDTH * 4);
      req.saved_2ahead_pred_valid_next = false;
      req.saved_mini_flush_req_next = false;
      req.saved_mini_flush_correct_next = false;
      req.saved_mini_flush_target_next = 0;
      std::memcpy(req.Spec_GHR_next, req.Arch_GHR_next, sizeof(req.Spec_GHR_next));
      std::memcpy(req.Spec_FH_next, req.Arch_FH_next, sizeof(req.Spec_FH_next));
#ifdef ENABLE_BPU_RAS
      std::memcpy(req.Spec_ras_stack_next, req.Arch_ras_stack_next,
                  sizeof(req.Spec_ras_stack_next));
      req.Spec_ras_count_next = req.Arch_ras_count_next;
#endif
    } else if (req.going_to_do_pred) {
      req.pc_reg_next = req.next_fetch_addr_calc;
      req.pc_can_send_to_icache_next = true;
    }

    bool q_full_any = false;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      q_full_any |= (req.q_count_next[i] == Q_DEPTH);
    }
    out.update_queue_full = q_full_any;

    assert(out.out_pred_base_pc == out.fetch_address);
    req.out_regs = out;
  }

  void bpu_seq_write(const InputPayload &inp, const UpdateRequest &req,
                     bool reset) {
    if (reset) {
      reset_internal_all();
      return;
    }
    (void)inp;

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      tage_inst[i]->tage_seq_write(req.tage_in[i], req.tage_req[i], false);
      btb_inst[i]->btb_seq_write(req.btb_in[i], req.btb_req[i], false);
      tage_done[i] = req.tage_done_next[i];
      btb_done[i] = req.btb_done_next[i];
    }
    for (int i = 0; i < FETCH_WIDTH; i++) {
      tage_calc_pred_dir_latch[i] = req.tage_calc_pred_dir_latch_next[i];
      tage_calc_altpred_latch[i] = req.tage_calc_altpred_latch_next[i];
      tage_calc_pcpn_latch[i] = req.tage_calc_pcpn_latch_next[i];
      tage_calc_altpcpn_latch[i] = req.tage_calc_altpcpn_latch_next[i];
      tage_result_valid_latch[i] = req.tage_result_valid_latch_next[i];
      btb_pred_target_latch[i] = req.btb_pred_target_latch_next[i];
      btb_result_valid_latch[i] = req.btb_result_valid_latch_next[i];
      for (int k = 0; k < TN_MAX; k++) {
        tage_pred_calc_tags_latch[i][k] = req.tage_pred_calc_tags_latch_next[i][k];
        tage_pred_calc_idxs_latch[i][k] = req.tage_pred_calc_idxs_latch_next[i][k];
      }
    }

    do_pred_latch = req.do_pred_latch_next;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      do_upd_latch[i] = req.do_upd_latch_next[i];
    }
    pred_base_pc_fired = req.pred_base_pc_fired_next;
    pc_reg = req.pc_reg_next;
    pc_can_send_to_icache = req.pc_can_send_to_icache_next;
    saved_2ahead_prediction = req.saved_2ahead_prediction_next;
    saved_2ahead_pred_valid = req.saved_2ahead_pred_valid_next;
    saved_mini_flush_req = req.saved_mini_flush_req_next;
    saved_mini_flush_correct = req.saved_mini_flush_correct_next;
    saved_mini_flush_target = req.saved_mini_flush_target_next;
    ahead_gate_enable = req.ahead_gate_enable_next;
    ahead_gate_sample_count = req.ahead_gate_sample_count_next;
    ahead_gate_success_count = req.ahead_gate_success_count_next;
    ahead_gate_disable_count = req.ahead_gate_disable_count_next;
    ahead_gate_enable_count = req.ahead_gate_enable_count_next;
    nlp_s1_valid = req.nlp_s1_valid_next;
    nlp_s1_req_pc = req.nlp_s1_req_pc_next;
    nlp_s1_pred_next_pc = req.nlp_s1_pred_next_pc_next;
    nlp_s1_hit = req.nlp_s1_hit_next;
    nlp_s1_conf = req.nlp_s1_conf_next;
    nlp_s2_valid = req.nlp_s2_valid_next;
    nlp_s2_req_pc = req.nlp_s2_req_pc_next;
    nlp_s2_pred_2ahead_pc = req.nlp_s2_pred_2ahead_pc_next;
    nlp_s2_hit = req.nlp_s2_hit_next;
    nlp_s2_conf = req.nlp_s2_conf_next;

    std::memcpy(Spec_GHR, req.Spec_GHR_next, sizeof(Spec_GHR));
    std::memcpy(Arch_GHR, req.Arch_GHR_next, sizeof(Arch_GHR));
    std::memcpy(Spec_FH, req.Spec_FH_next, sizeof(Spec_FH));
    std::memcpy(Arch_FH, req.Arch_FH_next, sizeof(Arch_FH));
    std::memcpy(Arch_ras_stack, req.Arch_ras_stack_next, sizeof(Arch_ras_stack));
    Arch_ras_count = req.Arch_ras_count_next;
    std::memcpy(Spec_ras_stack, req.Spec_ras_stack_next, sizeof(Spec_ras_stack));
    Spec_ras_count = req.Spec_ras_count_next;

    for (int i = 0; i < COMMIT_WIDTH; i++) {
      if (!req.q_entry_we[i]) {
        continue;
      }
      int bank_sel = req.q_entry_bank[i];
      uint32_t slot = req.q_entry_slot[i];
      QueueEntry &entry = update_queue[slot][bank_sel];
      entry.base_pc = req.q_entry_data[i].base_pc;
      entry.valid_mask = req.q_entry_data[i].valid_mask;
      entry.actual_dir = req.q_entry_data[i].actual_dir;
      entry.br_type = req.q_entry_data[i].br_type;
      entry.targets = req.q_entry_data[i].targets;
      entry.pred_dir = req.q_entry_data[i].pred_dir;
      entry.alt_pred = req.q_entry_data[i].alt_pred;
      entry.pcpn = req.q_entry_data[i].pcpn;
      entry.altpcpn = req.q_entry_data[i].altpcpn;
      for (int k = 0; k < TN_MAX; k++) {
        entry.tage_tags[k] = req.q_entry_data[i].tage_tags[k];
        entry.tage_idxs[k] = req.q_entry_data[i].tage_idxs[k];
      }
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      if (req.ahead_entry_we[i]) {
        AheadSlot1Entry &entry = ahead_slot1_table[i][req.ahead_entry_idx[i]];
        entry.valid = req.ahead_entry_valid_next[i];
        entry.taken = req.ahead_entry_taken_next[i];
        entry.target = req.ahead_entry_target_next[i];
        entry.conf = req.ahead_entry_conf_next[i];
      }
      last_block_table[i].valid = req.last_block_valid_next[i];
      last_block_table[i].last_pc = req.last_block_pc_next[i];
      q_wr_ptr[i] = req.q_wr_ptr_next[i];
      q_rd_ptr[i] = req.q_rd_ptr_next[i];
      q_count[i] = req.q_count_next[i];
    }

    if (req.nlp_entry_we) {
      NLPEntry &entry = nlp_table[req.nlp_entry_idx];
      entry.valid = req.nlp_entry_valid_next;
      entry.tag = req.nlp_entry_tag_next;
      entry.target = req.nlp_entry_target_next;
      entry.conf = req.nlp_entry_conf_next;
    }

    for (int i = 0; i < COMMIT_WIDTH; i++) {
      if (req.inst_type_we[i]) {
        inst_type_mem[req.inst_type_idx[i]][req.inst_type_bank[i]] =
            req.inst_type_data[i];
      }
    }

    state = static_cast<State>(req.next_state);
  }

  ~BPU_TOP() {
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      delete tage_inst[i];
      delete btb_inst[i];
    }
  }

  void reset_internal_all() {
    DEBUG_LOG_SMALL_4("reset_internal_all\n");
    pc_reg = RESET_PC;
    pc_can_send_to_icache = true;
    pred_base_pc_fired = 0;

    state = S_IDLE;
    do_pred_latch = false;
    for (int i = 0; i < BPU_BANK_NUM; i++)
      do_upd_latch[i] = false;

    // 初始化全局GHR/FH（从TAGE迁移过来）
    std::memset(Arch_GHR, 0, sizeof(Arch_GHR));
    std::memset(Spec_GHR, 0, sizeof(Spec_GHR));
    std::memset(Arch_FH, 0, sizeof(Arch_FH));
    std::memset(Spec_FH, 0, sizeof(Spec_FH));
    std::memset(Arch_ras_stack, 0, sizeof(Arch_ras_stack));
    std::memset(Spec_ras_stack, 0, sizeof(Spec_ras_stack));
    Arch_ras_count = 0;
    Spec_ras_count = 0;

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      q_wr_ptr[i] = 0;
      q_rd_ptr[i] = 0;
      q_count[i] = 0;
      tage_done[i] = false;
      btb_done[i] = false;
    }

    // 初始化预测结果数组
    for (int i = 0; i < FETCH_WIDTH; i++) {
      tage_calc_pred_dir_latch[i] = false;
      tage_calc_altpred_latch[i] = false;
      tage_calc_pcpn_latch[i] = 0;
      tage_calc_altpcpn_latch[i] = 0;
      tage_result_valid_latch[i] = false;
      btb_pred_target_latch[i] = 0;
      btb_result_valid_latch[i] = false;
      for (int k = 0; k < TN_MAX; k++) {
        tage_pred_calc_tags_latch[i][k] = 0;
        tage_pred_calc_idxs_latch[i][k] = 0;
      }
    }
    
    // 初始化done信号
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      tage_done[i] = false;
      btb_done[i] = false;
    }

    // 初始化2-Ahead预测器
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      for (int j = 0; j < AHEAD_SLOT1_TABLE_SIZE; j++) {
        ahead_slot1_table[i][j].valid = false;
        ahead_slot1_table[i][j].taken = false;
        ahead_slot1_table[i][j].target = 0;
        ahead_slot1_table[i][j].conf = 0;
      }
      last_block_table[i].valid = false;
      last_block_table[i].last_pc = 0;
    }
    for (int i = 0; i < NLP_TABLE_SIZE; i++) {
      nlp_table[i].valid = false;
      nlp_table[i].tag = 0;
      nlp_table[i].target = 0;
      nlp_table[i].conf = 0;
    }
    // 初始化2-ahead预测器为下一个cache line的地址
    DEBUG_LOG_SMALL_4("reset_internal_all,pc_reg: %x\n", pc_reg);
    saved_2ahead_prediction = pc_reg + (FETCH_WIDTH * 4);
    DEBUG_LOG_SMALL_4("reset_internal_all,saved_2ahead_prediction: %x\n", saved_2ahead_prediction);
    saved_2ahead_pred_valid = false;
    saved_mini_flush_req = false;
    saved_mini_flush_correct = false;
    saved_mini_flush_target = 0;
    ahead_gate_enable = true;
    ahead_gate_sample_count = 0;
    ahead_gate_success_count = 0;
    ahead_gate_disable_count = 0;
    ahead_gate_enable_count = 0;
    nlp_s1_valid = false;
    nlp_s1_req_pc = 0;
    nlp_s1_pred_next_pc = 0;
    nlp_s1_hit = false;
    nlp_s1_conf = 0;
    nlp_s2_valid = false;
    nlp_s2_req_pc = 0;
    nlp_s2_pred_2ahead_pc = 0;
    nlp_s2_hit = false;
    nlp_s2_conf = 0;
  }

  void bpu_comb_calc(const InputPayload &inp, const ReadData &rd,
                     OutputPayload &out, UpdateRequest &req) {
    BpuCombOut comb_out{};
    bpu_comb(BpuCombIn{inp, rd}, comb_out);
    out = comb_out.out_regs;
    req = comb_out.update_req;
  }

  void bpu_seq(const InputPayload &inp, const UpdateRequest &req, bool reset) {
    bpu_seq_write(inp, req, reset);
  }

  // ========================================================================
  // 主 Step 函数
  // ========================================================================
  OutputPayload step(bool clk, bool rst_n, const InputPayload &inp) {
    (void)clk;
    DEBUG_LOG_SMALL_4("BPU_TOP step,saved_2ahead_prediction: %x\n",
                      saved_2ahead_prediction);

    bpu_sim_time++;

    OutputPayload out_reg;
    std::memset(&out_reg, 0, sizeof(OutputPayload));
    if (rst_n) {
      bpu_seq_write(inp, UpdateRequest{}, true);
      DEBUG_LOG("[BPU_TOP] reset\n");
      extern SimCpu cpu;
      out_reg.fetch_address = cpu.back.number_PC;
      out_reg.two_ahead_valid = false;
      out_reg.two_ahead_target = out_reg.fetch_address + (FETCH_WIDTH * 4);
      return out_reg;
    }

    ReadData rd;
    UpdateRequest req;
    bpu_seq_read(inp, rd);
    bpu_comb_calc(inp, rd, out_reg, req);
    bpu_seq(inp, req, false);

    DEBUG_LOG_SMALL("[BPU_TOP] sim_time: %u, refetch: %d, refetch_addr: 0x%x, pc_reg: 0x%x\n",
                    bpu_sim_time, inp.refetch, inp.refetch_address, pc_reg);
    return out_reg;
  }
};

#endif // BPU_TOP_H
