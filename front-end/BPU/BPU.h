#ifndef BPU_TOP_H
#define BPU_TOP_H

#include "../frontend.h"
#include "../host_profile.h"
#include "../wire_types.h"
#include "./dir_predictor/TAGE_top.h"
#include "./type_predictor/TypePredictor.h"
#include "./target_predictor/BTB_top.h"
#include "BPU_configs.h"
#include <SimCpu.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

class BPU_TOP;             // 前向声明类
extern BPU_TOP *g_bpu_top; // 再声明全局指针

struct BankSelCombIn {
  pc_t pc;
};

struct BankSelCombOut {
  bpu_bank_sel_t bank_sel;
};

inline void bank_sel_comb(const BankSelCombIn &in, BankSelCombOut &out) {
  out = BankSelCombOut{};
  out.bank_sel = static_cast<bpu_bank_sel_t>((in.pc >> 2) % BPU_BANK_NUM);
}

struct BankPcCombIn {
  pc_t pc;
};

struct BankPcCombOut {
  pc_t bank_pc;
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

struct NlpIndexCombIn {
  pc_t pc;
};

struct NlpIndexCombOut {
  nlp_index_t index;
};

inline void nlp_index_comb(const NlpIndexCombIn &in, NlpIndexCombOut &out) {
  out = NlpIndexCombOut{};
  uint32_t mixed = (in.pc >> 2) ^ (in.pc >> 11) ^ (in.pc >> 19);
  if ((NLP_TABLE_SIZE & (NLP_TABLE_SIZE - 1)) == 0) {
    out.index = static_cast<nlp_index_t>(mixed & (NLP_TABLE_SIZE - 1));
  } else {
    out.index = static_cast<nlp_index_t>(mixed % NLP_TABLE_SIZE);
  }
}

struct NlpTagCombIn {
  pc_t pc;
};

struct NlpTagCombOut {
  nlp_tag_t tag;
};

inline void nlp_tag_comb(const NlpTagCombIn &in, NlpTagCombOut &out) {
  out = NlpTagCombOut{};
  out.tag = static_cast<nlp_tag_t>(in.pc >> 2);
}

inline fetch_addr_t nlp_fallback_next_pc(fetch_addr_t base_pc) {
  const uint32_t cache_mask = ~(ICACHE_LINE_SIZE - 1);
  const uint32_t pc_plus_width = base_pc + (FETCH_WIDTH * 4);
  return static_cast<fetch_addr_t>(((base_pc & cache_mask) != (pc_plus_width & cache_mask))
                                       ? (pc_plus_width & cache_mask)
                                       : pc_plus_width);
}

inline tage_path_hist_t tage_path_update_value(tage_path_hist_t current_path, pc_t pc,
                                               wire1_t dir) {
  uint32_t path = static_cast<uint32_t>(current_path) & TAGE_SC_PATH_MASK;
  uint32_t pc_mix = (pc >> 2);
  pc_mix ^= (pc_mix >> 5);
  pc_mix ^= (pc_mix >> 11);
  pc_mix ^= dir ? 0x9e3779b9u : 0x7f4a7c15u;
  path = ((path << 1) ^ pc_mix) & TAGE_SC_PATH_MASK;
  return static_cast<tage_path_hist_t>(path);
}


class BPU_TOP {
public:
  struct InputPayload {
    // I-Cache & Backend Control
    wire1_t refetch;
    fetch_addr_t refetch_address;
    wire1_t icache_read_ready;

    // Update Interface
    pc_t in_update_base_pc[COMMIT_WIDTH];
    wire1_t in_upd_valid[COMMIT_WIDTH];
    wire1_t in_actual_dir[COMMIT_WIDTH];
    br_type_t in_actual_br_type[COMMIT_WIDTH]; // 3-bit each
    target_addr_t in_actual_targets[COMMIT_WIDTH];

    wire1_t in_pred_dir[COMMIT_WIDTH];
    wire1_t in_alt_pred[COMMIT_WIDTH];
    pcpn_t in_pcpn[COMMIT_WIDTH];    // 3-bit each
    pcpn_t in_altpcpn[COMMIT_WIDTH]; // 3-bit each
    tage_tag_t in_tage_tags[COMMIT_WIDTH][TN_MAX];
    tage_idx_t in_tage_idxs[COMMIT_WIDTH][TN_MAX];
    wire1_t in_sc_used[COMMIT_WIDTH];
    wire1_t in_sc_pred[COMMIT_WIDTH];
    tage_scl_meta_sum_t in_sc_sum[COMMIT_WIDTH];
    tage_scl_meta_idx_t in_sc_idx[COMMIT_WIDTH][BPU_SCL_META_NTABLE];
    wire1_t in_loop_used[COMMIT_WIDTH];
    wire1_t in_loop_hit[COMMIT_WIDTH];
    wire1_t in_loop_pred[COMMIT_WIDTH];
    tage_loop_meta_idx_t in_loop_idx[COMMIT_WIDTH];
    tage_loop_meta_tag_t in_loop_tag[COMMIT_WIDTH];
  };

  struct OutputPayload {
    fetch_addr_t fetch_address;
    wire1_t icache_read_valid;
    fetch_addr_t predict_next_fetch_address;
    wire1_t PTAB_write_enable;
    wire1_t out_pred_dir[FETCH_WIDTH];
    wire1_t out_alt_pred[FETCH_WIDTH];
    pcpn_t out_pcpn[FETCH_WIDTH];
    pcpn_t out_altpcpn[FETCH_WIDTH];
    tage_tag_t out_tage_tags[FETCH_WIDTH][TN_MAX];
    tage_idx_t out_tage_idxs[FETCH_WIDTH][TN_MAX];
    wire1_t out_sc_used[FETCH_WIDTH];
    wire1_t out_sc_pred[FETCH_WIDTH];
    tage_scl_meta_sum_t out_sc_sum[FETCH_WIDTH];
    tage_scl_meta_idx_t out_sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
    wire1_t out_loop_used[FETCH_WIDTH];
    wire1_t out_loop_hit[FETCH_WIDTH];
    wire1_t out_loop_pred[FETCH_WIDTH];
    tage_loop_meta_idx_t out_loop_idx[FETCH_WIDTH];
    tage_loop_meta_tag_t out_loop_tag[FETCH_WIDTH];
    pc_t out_pred_base_pc;
    wire1_t update_queue_full;
    // 2-Ahead Predictor outputs
    // 下下行取指地址
    wire1_t two_ahead_valid;
    fetch_addr_t two_ahead_target;
    // 指示要不要多消耗inst FIFO
    wire1_t mini_flush_req;
    // 指示要不要多写一次fetch_address_fifo
    wire1_t mini_flush_correct;
    // 如果要多写，多写的地址
    fetch_addr_t mini_flush_target;
  };

  // 顶层三阶段兼容接口的数据容器
  struct ReadData {
    struct QueueEntrySnapshot {
      pc_t base_pc;
      wire1_t valid_mask;
      wire1_t actual_dir;
      br_type_t br_type;
      target_addr_t targets;
      wire1_t pred_dir;
      wire1_t alt_pred;
      pcpn_t pcpn;
      pcpn_t altpcpn;
      tage_tag_t tage_tags[TN_MAX];
      tage_idx_t tage_idxs[TN_MAX];
      wire1_t sc_used;
      wire1_t sc_pred;
      tage_scl_meta_sum_t sc_sum;
      tage_scl_meta_idx_t sc_idx[BPU_SCL_META_NTABLE];
      wire1_t loop_used;
      wire1_t loop_hit;
      wire1_t loop_pred;
      tage_loop_meta_idx_t loop_idx;
      tage_loop_meta_tag_t loop_tag;
    };
    struct NlpEntrySnapshot {
      wire1_t entry_valid;
      nlp_tag_t entry_tag;
      target_addr_t entry_target;
      nlp_conf_t entry_conf;
    };

    bpu_state_t state_snapshot;
    pc_t pc_reg_snapshot;
    wire1_t pc_can_send_to_icache_snapshot;
    pc_t pred_base_pc_fired_snapshot;
    wire1_t do_pred_latch_snapshot;
    wire1_t do_upd_latch_snapshot[BPU_BANK_NUM];

    wire1_t tage_calc_pred_dir_latch_snapshot[FETCH_WIDTH];
    wire1_t tage_calc_altpred_latch_snapshot[FETCH_WIDTH];
    pcpn_t tage_calc_pcpn_latch_snapshot[FETCH_WIDTH];
    pcpn_t tage_calc_altpcpn_latch_snapshot[FETCH_WIDTH];
    tage_tag_t tage_pred_calc_tags_latch_snapshot[FETCH_WIDTH][TN_MAX];
    tage_idx_t tage_pred_calc_idxs_latch_snapshot[FETCH_WIDTH][TN_MAX];
    wire1_t tage_result_valid_latch_snapshot[FETCH_WIDTH];
    target_addr_t btb_pred_target_latch_snapshot[FETCH_WIDTH];
    wire1_t btb_result_valid_latch_snapshot[FETCH_WIDTH];
    wire1_t tage_done_snapshot[BPU_BANK_NUM];
    wire1_t btb_done_snapshot[BPU_BANK_NUM];

    queue_ptr_t q_wr_ptr_snapshot[BPU_BANK_NUM];
    queue_ptr_t q_rd_ptr_snapshot[BPU_BANK_NUM];
    queue_count_t q_count_snapshot[BPU_BANK_NUM];

    fetch_addr_t saved_2ahead_prediction_snapshot;
    wire1_t saved_2ahead_pred_valid_snapshot;
    wire1_t saved_mini_flush_req_snapshot;
    wire1_t saved_mini_flush_correct_snapshot;
    fetch_addr_t saved_mini_flush_target_snapshot;
    wire1_t ras_has_entry_snapshot;
    target_addr_t ras_top_snapshot;
    ras_count_t ras_count_snapshot;
    wire1_t Arch_GHR_snapshot[GHR_LENGTH];
    wire1_t Spec_GHR_snapshot[GHR_LENGTH];
    wire32_t Arch_FH_snapshot[FH_N_MAX][TN_MAX];
    wire32_t Spec_FH_snapshot[FH_N_MAX][TN_MAX];
    tage_path_hist_t Arch_PATH_snapshot;
    tage_path_hist_t Spec_PATH_snapshot;
    target_addr_t Arch_ras_stack_snapshot[RAS_DEPTH];
    ras_count_t Arch_ras_count_snapshot;
    target_addr_t Spec_ras_stack_snapshot[RAS_DEPTH];
    ras_count_t Spec_ras_count_snapshot;

    pc_t pred_base_pc;
    fetch_addr_t boundary_addr;
    wire1_t do_pred_on_this_pc[FETCH_WIDTH];
    bpu_bank_sel_ext_t this_pc_bank_sel[FETCH_WIDTH];
    pc_t do_pred_for_this_pc[FETCH_WIDTH];
    br_type_t pred_inst_type_snapshot[FETCH_WIDTH];

    wire1_t q_full[BPU_BANK_NUM];
    wire1_t q_empty[BPU_BANK_NUM];
    QueueEntrySnapshot q_data[BPU_BANK_NUM];

    wire1_t going_to_do_pred;
    wire1_t going_to_do_upd[BPU_BANK_NUM];
    wire1_t going_to_do_upd_any;
    wire1_t trans_ready_to_fire;
    wire1_t set_submodule_input;

    TAGE_TOP::InputPayload tage_in[BPU_BANK_NUM];
    BTB_TOP::InputPayload btb_in[BPU_BANK_NUM];
    TypePredictor::ReadData type_rd;
    TAGE_TOP::ReadData tage_rd[BPU_BANK_NUM];
    BTB_TOP::ReadData btb_rd[BPU_BANK_NUM];
    NlpEntrySnapshot nlp_pred_base_entry_snapshot;
    fetch_addr_t nlp_s1_req_pc_snapshot;
    NlpEntrySnapshot nlp_s1_entry_snapshot;
    NlpEntrySnapshot nlp_train_entry_snapshot;
  };

  struct UpdateRequest {
    OutputPayload out_regs;
    bpu_state_t next_state;
    pc_t pc_reg_next;
    wire1_t pc_can_send_to_icache_next;
    wire1_t do_pred_latch_next;
    wire1_t do_upd_latch_next[BPU_BANK_NUM];
    pc_t pred_base_pc_fired_next;
    fetch_addr_t saved_2ahead_prediction_next;
    wire1_t saved_2ahead_pred_valid_next;
    wire1_t saved_mini_flush_req_next;
    wire1_t saved_mini_flush_correct_next;
    fetch_addr_t saved_mini_flush_target_next;
    wire1_t nlp_s1_valid_next;
    fetch_addr_t nlp_s1_req_pc_next;
    fetch_addr_t nlp_s1_pred_next_pc_next;
    wire1_t nlp_s1_hit_next;
    nlp_conf_t nlp_s1_conf_next;
    wire1_t nlp_s2_valid_next;
    fetch_addr_t nlp_s2_req_pc_next;
    fetch_addr_t nlp_s2_pred_2ahead_pc_next;
    wire1_t nlp_s2_hit_next;
    nlp_conf_t nlp_s2_conf_next;
    wire1_t nlp_entry_we;
    nlp_index_t nlp_entry_idx;
    wire1_t nlp_entry_valid_next;
    nlp_tag_t nlp_entry_tag_next;
    target_addr_t nlp_entry_target_next;
    nlp_conf_t nlp_entry_conf_next;
    wire1_t Spec_GHR_next[GHR_LENGTH];
    wire1_t Arch_GHR_next[GHR_LENGTH];
    wire32_t Spec_FH_next[FH_N_MAX][TN_MAX];
    wire32_t Arch_FH_next[FH_N_MAX][TN_MAX];
    tage_path_hist_t Spec_PATH_next;
    tage_path_hist_t Arch_PATH_next;
    target_addr_t Arch_ras_stack_next[RAS_DEPTH];
    ras_count_t Arch_ras_count_next;
    target_addr_t Spec_ras_stack_next[RAS_DEPTH];
    ras_count_t Spec_ras_count_next;
    queue_ptr_t q_wr_ptr_next[BPU_BANK_NUM];
    queue_ptr_t q_rd_ptr_next[BPU_BANK_NUM];
    queue_count_t q_count_next[BPU_BANK_NUM];
    wire1_t q_entry_we[COMMIT_WIDTH];
    bpu_bank_sel_t q_entry_bank[COMMIT_WIDTH];
    queue_ptr_t q_entry_slot[COMMIT_WIDTH];
    ReadData::QueueEntrySnapshot q_entry_data[COMMIT_WIDTH];
    pc_t pred_base_pc;
    wire1_t do_pred_on_this_pc[FETCH_WIDTH];
    bpu_bank_sel_ext_t this_pc_bank_sel[FETCH_WIDTH];
    pc_t do_pred_for_this_pc[FETCH_WIDTH];

    wire1_t going_to_do_pred;
    wire1_t going_to_do_upd[BPU_BANK_NUM];

    wire1_t q_push_en[BPU_BANK_NUM];
    wire1_t q_pop_en[BPU_BANK_NUM];

    wire1_t final_pred_dir[FETCH_WIDTH];
    fetch_addr_t next_fetch_addr_calc;
    fetch_addr_t final_2_ahead_address;
    wire1_t should_update_spec_hist;

    TAGE_TOP::InputPayload tage_in[BPU_BANK_NUM];
    BTB_TOP::InputPayload btb_in[BPU_BANK_NUM];
    TypePredictor::InputPayload type_in;
    TypePredictor::CombResult type_req;
    TAGE_TOP::CombResult tage_req[BPU_BANK_NUM];
    BTB_TOP::CombResult btb_req[BPU_BANK_NUM];

    wire1_t tage_done_next[BPU_BANK_NUM];
    wire1_t btb_done_next[BPU_BANK_NUM];
    wire1_t tage_calc_pred_dir_latch_next[FETCH_WIDTH];
    wire1_t tage_calc_altpred_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_pcpn_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_altpcpn_latch_next[FETCH_WIDTH];
    tage_tag_t tage_pred_calc_tags_latch_next[FETCH_WIDTH][TN_MAX];
    tage_idx_t tage_pred_calc_idxs_latch_next[FETCH_WIDTH][TN_MAX];
    wire1_t tage_result_valid_latch_next[FETCH_WIDTH];
    target_addr_t btb_pred_target_latch_next[FETCH_WIDTH];
    wire1_t btb_result_valid_latch_next[FETCH_WIDTH];
  };

  struct BpuPreReadReqCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BpuPreReadReqCombOut {
    wire1_t use_arch_ras_snapshot;
    ras_count_t ras_count_snapshot;
    wire1_t ras_has_entry_snapshot;
    ras_index_t ras_top_index;
    pc_t pred_base_pc;
    fetch_addr_t boundary_addr;
    wire1_t do_pred_on_this_pc[FETCH_WIDTH];
    bpu_bank_sel_ext_t this_pc_bank_sel[FETCH_WIDTH];
    pc_t do_pred_for_this_pc[FETCH_WIDTH];
    wire1_t pred_inst_type_re[FETCH_WIDTH];
    bpu_type_idx_t pred_inst_type_idx[FETCH_WIDTH];
    wire1_t q_full[BPU_BANK_NUM];
    wire1_t q_empty[BPU_BANK_NUM];
    queue_ptr_t q_read_slot[BPU_BANK_NUM];
    wire1_t going_to_do_pred;
    wire1_t going_to_do_upd[BPU_BANK_NUM];
    wire1_t going_to_do_upd_any;
    wire1_t trans_ready_to_fire;
    wire1_t set_submodule_input;
    wire1_t nlp_pred_base_re;
    nlp_index_t nlp_pred_base_idx;
    wire1_t nlp_train_re;
    nlp_index_t nlp_train_idx;
  };

  struct BpuPostReadReqCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BpuPostReadReqCombOut {
    wire1_t nlp_s1_re;
    nlp_index_t nlp_s1_idx;
    fetch_addr_t nlp_s1_req_pc;
    TypePredictor::InputPayload type_in;
    TAGE_TOP::InputPayload tage_in[BPU_BANK_NUM];
    BTB_TOP::InputPayload btb_in[BPU_BANK_NUM];
  };

  struct BpuCombIn {
    InputPayload inp;
    ReadData rd;
    BpuPostReadReqCombOut post_req;
  };

  struct BpuCombOut {
    OutputPayload out_regs;
    UpdateRequest update_req;
  };

  struct BpuSubmoduleBindCombIn {
    ReadData rd;
    BpuPostReadReqCombOut post_req;
    TypePredictor::OutputPayload type_out;
  };

  struct BpuSubmoduleBindCombOut {
    BTB_TOP::InputPayload btb_in_with_type[BPU_BANK_NUM];
  };

  struct BpuPredictMainCombIn {
    InputPayload inp;
    ReadData rd;
    TypePredictor::OutputPayload type_out;
    TAGE_TOP::OutputPayload tage_out[BPU_BANK_NUM];
    BTB_TOP::OutputPayload btb_out[BPU_BANK_NUM];
  };

  struct BpuPredictMainCombOut {
    OutputPayload out;
    wire1_t final_pred_dir[FETCH_WIDTH];
    fetch_addr_t next_fetch_addr_calc;
    fetch_addr_t final_2_ahead_address;
    wire1_t tage_calc_pred_dir_latch_next[FETCH_WIDTH];
    wire1_t tage_calc_altpred_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_pcpn_latch_next[FETCH_WIDTH];
    pcpn_t tage_calc_altpcpn_latch_next[FETCH_WIDTH];
    tage_tag_t tage_pred_calc_tags_latch_next[FETCH_WIDTH][TN_MAX];
    tage_idx_t tage_pred_calc_idxs_latch_next[FETCH_WIDTH][TN_MAX];
    wire1_t tage_result_valid_latch_next[FETCH_WIDTH];
    target_addr_t btb_pred_target_latch_next[FETCH_WIDTH];
    wire1_t btb_result_valid_latch_next[FETCH_WIDTH];
  };

  struct BpuNlpCombIn {
    InputPayload inp;
    ReadData rd;
    fetch_addr_t next_fetch_addr_calc;
    fetch_addr_t final_2_ahead_address_seed;
    OutputPayload out_seed;
  };

  struct BpuNlpCombOut {
    OutputPayload out_regs;
    fetch_addr_t final_2_ahead_address;
    fetch_addr_t saved_2ahead_prediction_next;
    wire1_t saved_2ahead_pred_valid_next;
    wire1_t saved_mini_flush_req_next;
    wire1_t saved_mini_flush_correct_next;
    fetch_addr_t saved_mini_flush_target_next;
    wire1_t nlp_entry_we;
    nlp_index_t nlp_entry_idx;
    wire1_t nlp_entry_valid_next;
    nlp_tag_t nlp_entry_tag_next;
    target_addr_t nlp_entry_target_next;
    nlp_conf_t nlp_entry_conf_next;
  };

  struct BpuHistCombIn {
    InputPayload inp;
    ReadData rd;
    TypePredictor::OutputPayload type_out;
    wire1_t final_pred_dir[FETCH_WIDTH];
  };

  struct BpuHistCombOut {
    wire1_t should_update_spec_hist;
    wire1_t Spec_GHR_next[GHR_LENGTH];
    wire32_t Spec_FH_next[FH_N_MAX][TN_MAX];
    wire1_t Arch_GHR_next[GHR_LENGTH];
    wire32_t Arch_FH_next[FH_N_MAX][TN_MAX];
    tage_path_hist_t Spec_PATH_next;
    tage_path_hist_t Arch_PATH_next;
    target_addr_t Arch_ras_stack_next[RAS_DEPTH];
    ras_count_t Arch_ras_count_next;
    target_addr_t Spec_ras_stack_next[RAS_DEPTH];
    ras_count_t Spec_ras_count_next;
  };

  struct BpuQueueCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BpuQueueCombOut {
    wire1_t q_push_en[BPU_BANK_NUM];
    wire1_t q_pop_en[BPU_BANK_NUM];
    queue_ptr_t q_wr_ptr_next[BPU_BANK_NUM];
    queue_ptr_t q_rd_ptr_next[BPU_BANK_NUM];
    queue_count_t q_count_next[BPU_BANK_NUM];
    wire1_t q_entry_we[COMMIT_WIDTH];
    bpu_bank_sel_t q_entry_bank[COMMIT_WIDTH];
    queue_ptr_t q_entry_slot[COMMIT_WIDTH];
    ReadData::QueueEntrySnapshot q_entry_data[COMMIT_WIDTH];
    wire1_t update_queue_full;
  };

private:
  // ========================================================================
  // 内部数据结构 (Internal Structures)
  // ========================================================================

  // Update Queue Entry Structure --- for one bank slot!
  struct QueueEntry {
    pc_t base_pc;
    wire1_t valid_mask;
    wire1_t actual_dir;
    br_type_t br_type;
    target_addr_t targets;
    wire1_t pred_dir;
    wire1_t alt_pred;
    pcpn_t pcpn;
    pcpn_t altpcpn;
    tage_tag_t tage_tags[TN_MAX];
    tage_idx_t tage_idxs[TN_MAX];
    wire1_t sc_used;
    wire1_t sc_pred;
    tage_scl_meta_sum_t sc_sum;
    tage_scl_meta_idx_t sc_idx[BPU_SCL_META_NTABLE];
    wire1_t loop_used;
    wire1_t loop_hit;
    wire1_t loop_pred;
    tage_loop_meta_idx_t loop_idx;
    tage_loop_meta_tag_t loop_tag;
  };

  struct NLPEntry {
    wire1_t valid;
    nlp_tag_t tag;
    target_addr_t target;
    nlp_conf_t conf;
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
  wire1_t Arch_GHR[GHR_LENGTH];
  wire1_t Spec_GHR[GHR_LENGTH];
  wire32_t Arch_FH[FH_N_MAX][TN_MAX];
  wire32_t Spec_FH[FH_N_MAX][TN_MAX];
  tage_path_hist_t Arch_PATH;
  tage_path_hist_t Spec_PATH;
  target_addr_t Arch_ras_stack[RAS_DEPTH];
  ras_count_t Arch_ras_count;
  target_addr_t Spec_ras_stack[RAS_DEPTH];
  ras_count_t Spec_ras_count;

  // FH constants (从TAGE复制，用于调用FH_update函数)
  const uint32_t ghr_length[TN_MAX] = {8, 13, 32, 119};
  const uint32_t fh_length[FH_N_MAX][TN_MAX] = {
      {8, 11, 11, 11}, {8, 8, 8, 8}, {7, 7, 7, 7}};

  // PC & Memory
  pc_t pc_reg;
  State state;
  // Transaction Flags (Latched in IDLE) // doing prediction and updating
  wire1_t do_pred_latch;
  wire1_t do_upd_latch[BPU_BANK_NUM]; // indicate whether to update this bank

  wire1_t pc_can_send_to_icache;            // 当前pc是不是可以发送给icache
  pc_t pred_base_pc_fired; // 当前预测流程中正在处理的pc基地址

  // 存储TAGE和BTB的预测结果（缓存先到的结果）对于FETCH_WIDTH建立！
  wire1_t tage_calc_pred_dir_latch[FETCH_WIDTH];
  wire1_t tage_calc_altpred_latch[FETCH_WIDTH];
  pcpn_t tage_calc_pcpn_latch[FETCH_WIDTH];
  pcpn_t tage_calc_altpcpn_latch[FETCH_WIDTH];
  tage_tag_t tage_pred_calc_tags_latch[FETCH_WIDTH][TN_MAX];
  tage_idx_t tage_pred_calc_idxs_latch[FETCH_WIDTH][TN_MAX];
  wire1_t tage_result_valid_latch[FETCH_WIDTH]; // 标记TAGE预测结果是否已缓存
  target_addr_t btb_pred_target_latch[FETCH_WIDTH];
  wire1_t btb_result_valid_latch[FETCH_WIDTH]; // 标记BTB预测结果是否已缓存

  // Done信号：标记每个bank的TAGE/BTB是否完成
  wire1_t tage_done[BPU_BANK_NUM];  // TAGE完成信号
  wire1_t btb_done[BPU_BANK_NUM];   // BTB完成信号

  // Queue Registers
  QueueEntry update_queue[Q_DEPTH][BPU_BANK_NUM];
  queue_ptr_t q_wr_ptr[BPU_BANK_NUM];
  queue_ptr_t q_rd_ptr[BPU_BANK_NUM];
  queue_count_t q_count[BPU_BANK_NUM];

  // bool out_pred_dir_latch[FETCH_WIDTH];
  // bool out_alt_pred_latch[FETCH_WIDTH];
  // uint8_t out_pcpn_latch[FETCH_WIDTH];
  // uint8_t out_altpcpn_latch[FETCH_WIDTH];
  // uint8_t out_tage_pred_calc_tags_latch_latch[FETCH_WIDTH][TN_MAX];
  // uint32_t out_tage_pred_calc_idxs_latch_latch[FETCH_WIDTH][TN_MAX];

  // Sub-modules
  TAGE_TOP *tage_inst[BPU_BANK_NUM];
  BTB_TOP *btb_inst[BPU_BANK_NUM];
  TypePredictor *type_pred_inst;

  NLPEntry nlp_table[NLP_TABLE_SIZE];
  
  // 2-Ahead Predictor Registers
  // 类似pc_reg的2-ahead reg,跟pc_reg保持同步
  fetch_addr_t saved_2ahead_prediction;
  // 指示的是上一个的2-ahead预测器是否有效
  wire1_t saved_2ahead_pred_valid; // may not used
  // goes to PTAB, 需要跟PTAB相关信号同步
  wire1_t saved_mini_flush_req;
  // goes to fetch_address_FIFO, 需要跟fetch_address_FIFO相关信号同步
  wire1_t saved_mini_flush_correct;
  fetch_addr_t saved_mini_flush_target; // may not used
  wire1_t nlp_s1_valid;
  fetch_addr_t nlp_s1_req_pc;
  fetch_addr_t nlp_s1_pred_next_pc;
  wire1_t nlp_s1_hit;
  nlp_conf_t nlp_s1_conf;
  wire1_t nlp_s2_valid;
  fetch_addr_t nlp_s2_req_pc;
  fetch_addr_t nlp_s2_pred_2ahead_pc;
  wire1_t nlp_s2_hit;
  nlp_conf_t nlp_s2_conf;

  void ras_push(target_addr_t *stack, ras_count_t &count, target_addr_t value) const {
    if (count < RAS_DEPTH) {
      stack[count++] = value;
      return;
    }
    for (int i = 1; i < RAS_DEPTH; ++i) {
      stack[i - 1] = stack[i];
    }
    stack[RAS_DEPTH - 1] = value;
  }

  void ras_pop(ras_count_t &count) const {
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
    type_pred_inst = new TypePredictor();

    reset_internal_all();
  }

  void bpu_seq_read(const InputPayload &inp, ReadData &rd) const {
    FRONTEND_HOST_PROFILE_SCOPE(BpuSeqRead);
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
    std::memcpy(rd.Arch_GHR_snapshot, Arch_GHR, sizeof(rd.Arch_GHR_snapshot));
    std::memcpy(rd.Spec_GHR_snapshot, Spec_GHR, sizeof(rd.Spec_GHR_snapshot));
    std::memcpy(rd.Arch_FH_snapshot, Arch_FH, sizeof(rd.Arch_FH_snapshot));
    std::memcpy(rd.Spec_FH_snapshot, Spec_FH, sizeof(rd.Spec_FH_snapshot));
    rd.Arch_PATH_snapshot = Arch_PATH;
    rd.Spec_PATH_snapshot = Spec_PATH;
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
  }

  void bpu_pre_read_req_comb(const BpuPreReadReqCombIn &in,
                             BpuPreReadReqCombOut &out) const {
    FRONTEND_HOST_PROFILE_SCOPE(BpuPreReadReq);
    out = BpuPreReadReqCombOut{};
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;

#ifdef ENABLE_BPU_RAS
    out.use_arch_ras_snapshot = inp.refetch;
    out.ras_count_snapshot =
        out.use_arch_ras_snapshot ? rd.Arch_ras_count_snapshot : rd.Spec_ras_count_snapshot;
    out.ras_has_entry_snapshot = (out.ras_count_snapshot > 0);
    out.ras_top_index = out.ras_has_entry_snapshot ? (out.ras_count_snapshot - 1) : 0;
#endif

    out.pred_base_pc = inp.refetch ? inp.refetch_address : rd.pc_reg_snapshot;

    const uint32_t cache_mask = ~(ICACHE_LINE_SIZE - 1);
    const uint32_t pc_plus_width = out.pred_base_pc + (FETCH_WIDTH * 4);
    out.boundary_addr =
        ((out.pred_base_pc & cache_mask) != (pc_plus_width & cache_mask))
            ? (pc_plus_width & cache_mask)
            : pc_plus_width;

    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.do_pred_for_this_pc[i] = out.pred_base_pc + (i * 4);
      if (out.do_pred_for_this_pc[i] < out.boundary_addr) {
        BankSelCombOut bank_sel_out{};
        BankPcCombOut bank_pc_out{};
        bank_sel_comb(BankSelCombIn{out.do_pred_for_this_pc[i]}, bank_sel_out);
        bank_pc_comb(BankPcCombIn{out.do_pred_for_this_pc[i]}, bank_pc_out);
        out.this_pc_bank_sel[i] = bank_sel_out.bank_sel;
        out.do_pred_on_this_pc[i] = true;
        out.pred_inst_type_re[i] = true;
        out.pred_inst_type_idx[i] = bank_pc_out.bank_pc & BPU_TYPE_IDX_MASK;
      } else {
        out.this_pc_bank_sel[i] = BPU_BANK_SEL_INVALID;
        out.do_pred_on_this_pc[i] = false;
        out.pred_inst_type_re[i] = false;
        out.pred_inst_type_idx[i] = 0;
      }
    }

    out.going_to_do_pred =
        rd.pc_can_send_to_icache_snapshot && (inp.icache_read_ready || inp.refetch);
    out.going_to_do_upd_any = false;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      out.q_full[i] = (rd.q_count_snapshot[i] == Q_DEPTH);
      out.q_empty[i] = (rd.q_count_snapshot[i] == 0);
      out.q_read_slot[i] = rd.q_rd_ptr_snapshot[i];
      out.going_to_do_upd[i] = !out.q_empty[i];
      out.going_to_do_upd_any |= out.going_to_do_upd[i];
    }
    out.trans_ready_to_fire = out.going_to_do_pred || out.going_to_do_upd_any;
    out.set_submodule_input = out.trans_ready_to_fire;

    NlpIndexCombOut nlp_idx_out{};
    nlp_index_comb(NlpIndexCombIn{out.pred_base_pc}, nlp_idx_out);
    out.nlp_pred_base_re = true;
    out.nlp_pred_base_idx = nlp_idx_out.index;
    out.nlp_train_re = true;
    out.nlp_train_idx = nlp_idx_out.index;
  }

  void bpu_data_seq_read(const BpuPreReadReqCombOut &in, ReadData &rd) const {
    FRONTEND_HOST_PROFILE_SCOPE(BpuDataSeqRead);
#ifdef ENABLE_BPU_RAS
    rd.ras_count_snapshot = in.ras_count_snapshot;
    rd.ras_has_entry_snapshot = in.ras_has_entry_snapshot;
    if (in.ras_has_entry_snapshot) {
      rd.ras_top_snapshot = in.use_arch_ras_snapshot
                                ? rd.Arch_ras_stack_snapshot[in.ras_top_index]
                                : rd.Spec_ras_stack_snapshot[in.ras_top_index];
    } else {
      rd.ras_top_snapshot = 0;
    }
#else
    rd.ras_count_snapshot = 0;
    rd.ras_has_entry_snapshot = false;
    rd.ras_top_snapshot = 0;
#endif

    rd.pred_base_pc = in.pred_base_pc;
    rd.boundary_addr = in.boundary_addr;
    rd.going_to_do_pred = in.going_to_do_pred;
    rd.going_to_do_upd_any = in.going_to_do_upd_any;
    rd.trans_ready_to_fire = in.trans_ready_to_fire;
    rd.set_submodule_input = in.set_submodule_input;

    for (int i = 0; i < FETCH_WIDTH; i++) {
      rd.do_pred_on_this_pc[i] = in.do_pred_on_this_pc[i];
      rd.this_pc_bank_sel[i] = in.this_pc_bank_sel[i];
      rd.do_pred_for_this_pc[i] = in.do_pred_for_this_pc[i];
      rd.pred_inst_type_snapshot[i] = BR_NONCTL;
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      rd.q_full[i] = in.q_full[i];
      rd.q_empty[i] = in.q_empty[i];
      rd.going_to_do_upd[i] = in.going_to_do_upd[i];
      if (!in.q_empty[i]) {
        const QueueEntry &entry = update_queue[in.q_read_slot[i]][i];
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
        rd.q_data[i].sc_used = entry.sc_used;
        rd.q_data[i].sc_pred = entry.sc_pred;
        rd.q_data[i].sc_sum = entry.sc_sum;
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          rd.q_data[i].sc_idx[t] = entry.sc_idx[t];
        }
        rd.q_data[i].loop_used = entry.loop_used;
        rd.q_data[i].loop_hit = entry.loop_hit;
        rd.q_data[i].loop_pred = entry.loop_pred;
        rd.q_data[i].loop_idx = entry.loop_idx;
        rd.q_data[i].loop_tag = entry.loop_tag;
      } else {
        std::memset(&rd.q_data[i], 0, sizeof(rd.q_data[i]));
      }
    }

    std::memset(&rd.nlp_pred_base_entry_snapshot, 0, sizeof(rd.nlp_pred_base_entry_snapshot));
    if (in.nlp_pred_base_re) {
      const NLPEntry &entry = nlp_table[in.nlp_pred_base_idx];
      rd.nlp_pred_base_entry_snapshot.entry_valid = entry.valid;
      rd.nlp_pred_base_entry_snapshot.entry_tag = entry.tag;
      rd.nlp_pred_base_entry_snapshot.entry_target = entry.target;
      rd.nlp_pred_base_entry_snapshot.entry_conf = entry.conf;
    }

    std::memset(&rd.nlp_train_entry_snapshot, 0, sizeof(rd.nlp_train_entry_snapshot));
    if (in.nlp_train_re) {
      const NLPEntry &entry = nlp_table[in.nlp_train_idx];
      rd.nlp_train_entry_snapshot.entry_valid = entry.valid;
      rd.nlp_train_entry_snapshot.entry_tag = entry.tag;
      rd.nlp_train_entry_snapshot.entry_target = entry.target;
      rd.nlp_train_entry_snapshot.entry_conf = entry.conf;
    }
  }

  void bpu_post_read_req_comb(const BpuPostReadReqCombIn &in,
                              BpuPostReadReqCombOut &out) const {
    FRONTEND_HOST_PROFILE_SCOPE(BpuPostReadReq);
    out = BpuPostReadReqCombOut{};
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;

#ifdef SPECULATIVE_ON
    const bool *ghr_src = rd.Spec_GHR_snapshot;
    const uint32_t (*fh_src)[TN_MAX] = rd.Spec_FH_snapshot;
    const tage_path_hist_t path_src = rd.Spec_PATH_snapshot;
#else
    const bool *ghr_src = rd.Arch_GHR_snapshot;
    const uint32_t (*fh_src)[TN_MAX] = rd.Arch_FH_snapshot;
    const tage_path_hist_t path_src = rd.Arch_PATH_snapshot;
#endif
    for (int b = 0; b < BPU_BANK_NUM; b++) {
      for (int k = 0; k < FH_N_MAX; k++) {
        for (int i = 0; i < TN_MAX; i++) {
          out.tage_in[b].fh_in[k][i] = fh_src[k][i];
        }
      }
      for (int i = 0; i < GHR_LENGTH; i++) {
        out.tage_in[b].ghr_in[i] = ghr_src[i];
      }
      out.tage_in[b].path_in = path_src;
    }

#ifdef ENABLE_2AHEAD
    if (rd.going_to_do_pred && !inp.refetch) {
      NlpTagCombOut stage1_tag_out{};
      nlp_tag_comb(NlpTagCombIn{rd.pred_base_pc}, stage1_tag_out);
      const bool stage1_hit =
          rd.nlp_pred_base_entry_snapshot.entry_valid &&
          (rd.nlp_pred_base_entry_snapshot.entry_tag == stage1_tag_out.tag);
      const uint8_t stage1_conf =
          stage1_hit ? rd.nlp_pred_base_entry_snapshot.entry_conf : 0;
      out.nlp_s1_req_pc =
          (stage1_hit && stage1_conf >= NLP_CONF_THRESHOLD)
              ? rd.nlp_pred_base_entry_snapshot.entry_target
              : nlp_fallback_next_pc(rd.pred_base_pc);
      out.nlp_s1_re = true;
      NlpIndexCombOut stage1_idx_out{};
      nlp_index_comb(NlpIndexCombIn{out.nlp_s1_req_pc}, stage1_idx_out);
      out.nlp_s1_idx = stage1_idx_out.index;
    }
#endif

    if (rd.set_submodule_input) {
      if (rd.going_to_do_pred) {
        bool pred_req_sent[BPU_BANK_NUM];
        std::memset(pred_req_sent, 0, sizeof(pred_req_sent));
        for (int i = 0; i < FETCH_WIDTH; i++) {
          if (!rd.do_pred_on_this_pc[i]) {
            continue;
          }
          out.type_in.pred_valid[i] = true;
          out.type_in.pred_pc[i] = rd.do_pred_for_this_pc[i];
          const bpu_bank_sel_ext_t bank_sel = rd.this_pc_bank_sel[i];
          if (bank_sel < BPU_BANK_NUM && !pred_req_sent[bank_sel]) {
            BankPcCombOut bank_pc_out{};
            bank_pc_comb(BankPcCombIn{rd.do_pred_for_this_pc[i]}, bank_pc_out);
            out.tage_in[bank_sel].pred_req = true;
            out.tage_in[bank_sel].pc_pred_in = bank_pc_out.bank_pc;
            out.btb_in[bank_sel].pred_req = true;
            out.btb_in[bank_sel].pred_pc = bank_pc_out.bank_pc;
            out.btb_in[bank_sel].pred_type_in = BR_NONCTL;
            pred_req_sent[bank_sel] = true;
          }
        }
      }

      for (int i = 0; i < BPU_BANK_NUM; i++) {
        if (!rd.going_to_do_upd[i]) {
          continue;
        }
        BankPcCombOut bank_pc_out{};
        bank_pc_comb(BankPcCombIn{rd.q_data[i].base_pc}, bank_pc_out);
        if (rd.q_data[i].br_type == BR_DIRECT) {
          out.tage_in[i].update_en = rd.q_data[i].valid_mask;
          out.tage_in[i].pc_update_in = bank_pc_out.bank_pc;
          out.tage_in[i].real_dir = rd.q_data[i].actual_dir;
          out.tage_in[i].pred_in = rd.q_data[i].pred_dir;
          out.tage_in[i].alt_pred_in = rd.q_data[i].alt_pred;
          out.tage_in[i].pcpn_in = rd.q_data[i].pcpn;
          out.tage_in[i].altpcpn_in = rd.q_data[i].altpcpn;
          out.tage_in[i].sc_used_in = rd.q_data[i].sc_used;
          out.tage_in[i].sc_pred_in = rd.q_data[i].sc_pred;
          out.tage_in[i].sc_sum_in = rd.q_data[i].sc_sum;
          for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            out.tage_in[i].sc_idx_in[t] = rd.q_data[i].sc_idx[t];
          }
          out.tage_in[i].loop_used_in = rd.q_data[i].loop_used;
          out.tage_in[i].loop_hit_in = rd.q_data[i].loop_hit;
          out.tage_in[i].loop_pred_in = rd.q_data[i].loop_pred;
          out.tage_in[i].loop_idx_in = rd.q_data[i].loop_idx;
          out.tage_in[i].loop_tag_in = rd.q_data[i].loop_tag;
          for (int k = 0; k < TN_MAX; k++) {
            out.tage_in[i].tage_tag_flat_in[k] = rd.q_data[i].tage_tags[k];
            out.tage_in[i].tage_idx_flat_in[k] = rd.q_data[i].tage_idxs[k];
          }
        }

        out.btb_in[i].upd_valid = rd.q_data[i].valid_mask;
        out.btb_in[i].upd_pc = bank_pc_out.bank_pc;
        out.btb_in[i].upd_actual_addr = rd.q_data[i].targets;
        out.btb_in[i].upd_actual_dir = rd.q_data[i].actual_dir;
        out.btb_in[i].upd_br_type_in = rd.q_data[i].br_type;
      }
    }

    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      out.type_in.upd_valid[i] = inp.in_upd_valid[i];
      out.type_in.upd_pc[i] = inp.in_update_base_pc[i];
      out.type_in.upd_br_type[i] = inp.in_actual_br_type[i];
    }
  }

  void bpu_submodule_bind_comb(const BpuSubmoduleBindCombIn &in,
                               BpuSubmoduleBindCombOut &out) const {
    std::memset(&out, 0, sizeof(out));
    const ReadData &rd = in.rd;
    const BpuPostReadReqCombOut &post_req = in.post_req;
    const TypePredictor::OutputPayload &type_out = in.type_out;
    for (int i = 0; i < BPU_BANK_NUM; ++i) {
      out.btb_in_with_type[i] = post_req.btb_in[i];
    }
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      if (!rd.do_pred_on_this_pc[i]) {
        continue;
      }
      const bpu_bank_sel_ext_t bank_sel = rd.this_pc_bank_sel[i];
      if (bank_sel >= BPU_BANK_NUM) {
        continue;
      }
      if (out.btb_in_with_type[bank_sel].pred_req) {
        out.btb_in_with_type[bank_sel].pred_type_in = type_out.pred_type[i];
      }
    }
  }

  void bpu_predict_main_comb(const BpuPredictMainCombIn &in,
                             BpuPredictMainCombOut &out) const {
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;
    const TypePredictor::OutputPayload &type_out = in.type_out;
    const TAGE_TOP::OutputPayload (&tage_out)[BPU_BANK_NUM] = in.tage_out;
    const BTB_TOP::OutputPayload (&btb_out)[BPU_BANK_NUM] = in.btb_out;
    std::memset(&out, 0, sizeof(out));
    out.out.icache_read_valid = rd.pc_can_send_to_icache_snapshot && !inp.refetch;
    out.out.fetch_address = rd.pred_base_pc;
    out.out.out_pred_base_pc = out.out.fetch_address;
    out.next_fetch_addr_calc = rd.boundary_addr;

    bool found_taken_branch = false;
    if (rd.going_to_do_pred) {
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        const bpu_bank_sel_ext_t bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        const br_type_t p_type = type_out.pred_type[i];
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
        out.final_pred_dir[i] = pred_taken;
        out.tage_calc_pred_dir_latch_next[i] = pred_taken;
        out.tage_calc_altpred_latch_next[i] =
            tage_valid ? tage_out[bank_sel].alt_pred_out : false;
        out.tage_calc_pcpn_latch_next[i] =
            tage_valid ? tage_out[bank_sel].pcpn_out : static_cast<pcpn_t>(0);
        out.tage_calc_altpcpn_latch_next[i] =
            tage_valid ? tage_out[bank_sel].altpcpn_out : static_cast<pcpn_t>(0);
        out.tage_result_valid_latch_next[i] = tage_valid;
        out.btb_pred_target_latch_next[i] =
            btb_valid ? btb_out[bank_sel].pred_target : rd.do_pred_for_this_pc[i] + 4;
        out.btb_result_valid_latch_next[i] = btb_valid;
        for (int k = 0; k < TN_MAX; ++k) {
          out.tage_pred_calc_tags_latch_next[i][k] =
              tage_out[bank_sel].tage_tag_flat_out[k];
          out.tage_pred_calc_idxs_latch_next[i][k] =
              tage_out[bank_sel].tage_idx_flat_out[k];
        }
        out.out.out_sc_used[i] = tage_valid ? tage_out[bank_sel].sc_used_out : false;
        out.out.out_sc_pred[i] = tage_valid ? tage_out[bank_sel].sc_pred_out : false;
        out.out.out_sc_sum[i] = tage_valid ? tage_out[bank_sel].sc_sum_out : 0;
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          out.out.out_sc_idx[i][t] =
              tage_valid ? tage_out[bank_sel].sc_idx_out[t] : 0;
        }
        out.out.out_loop_used[i] = tage_valid ? tage_out[bank_sel].loop_used_out : false;
        out.out.out_loop_hit[i] = tage_valid ? tage_out[bank_sel].loop_hit_out : false;
        out.out.out_loop_pred[i] = tage_valid ? tage_out[bank_sel].loop_pred_out : false;
        out.out.out_loop_idx[i] = tage_valid ? tage_out[bank_sel].loop_idx_out : 0;
        out.out.out_loop_tag[i] = tage_valid ? tage_out[bank_sel].loop_tag_out : 0;

        if (pred_taken && !found_taken_branch && btb_valid) {
          uint32_t chosen_target = btb_out[bank_sel].pred_target;
          if (p_type == BR_RET && rd.ras_has_entry_snapshot) {
            chosen_target = rd.ras_top_snapshot;
          }
          out.next_fetch_addr_calc = chosen_target;
          found_taken_branch = true;
        }
      }
    }

    out.out.predict_next_fetch_address = out.next_fetch_addr_calc;
    out.out.PTAB_write_enable = rd.going_to_do_pred && !inp.refetch;
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      out.out.out_pred_dir[i] = out.final_pred_dir[i];
      out.out.out_alt_pred[i] = out.tage_calc_altpred_latch_next[i];
      out.out.out_pcpn[i] = out.tage_calc_pcpn_latch_next[i];
      out.out.out_altpcpn[i] = out.tage_calc_altpcpn_latch_next[i];
      for (int k = 0; k < TN_MAX; ++k) {
        out.out.out_tage_tags[i][k] = out.tage_pred_calc_tags_latch_next[i][k];
        out.out.out_tage_idxs[i][k] = out.tage_pred_calc_idxs_latch_next[i][k];
      }
    }

    out.final_2_ahead_address = out.out.fetch_address + (FETCH_WIDTH * 4);
    const uint32_t refetch_twoahead_target = inp.refetch_address + (FETCH_WIDTH * 4);
    const uint32_t fallback_twoahead_target = out.out.fetch_address + (FETCH_WIDTH * 4);
    out.out.two_ahead_valid =
        inp.refetch ? false : rd.saved_2ahead_pred_valid_snapshot;
    out.out.two_ahead_target =
        inp.refetch ? refetch_twoahead_target
                       : (out.out.two_ahead_valid ? rd.saved_2ahead_prediction_snapshot
                                                  : fallback_twoahead_target);
    out.out.mini_flush_req = false;
    out.out.mini_flush_correct =
        rd.saved_mini_flush_correct_snapshot && !inp.refetch;
    out.out.mini_flush_target = rd.saved_mini_flush_target_snapshot;
  }

  void bpu_nlp_comb(const BpuNlpCombIn &in, BpuNlpCombOut &out) const {
    const ReadData &rd = in.rd;
    const fetch_addr_t final_2_ahead_address_seed = in.final_2_ahead_address_seed;
    std::memset(&out, 0, sizeof(out));
    out.out_regs = in.out_seed;
    out.final_2_ahead_address = final_2_ahead_address_seed;
    out.saved_2ahead_prediction_next = rd.saved_2ahead_prediction_snapshot;
    out.saved_2ahead_pred_valid_next = rd.saved_2ahead_pred_valid_snapshot;
    out.saved_mini_flush_req_next = rd.saved_mini_flush_req_snapshot;
    out.saved_mini_flush_correct_next = rd.saved_mini_flush_correct_snapshot;
    out.saved_mini_flush_target_next = rd.saved_mini_flush_target_snapshot;

#ifdef ENABLE_2AHEAD
    const InputPayload &inp = in.inp;
    const fetch_addr_t next_fetch_addr_calc = in.next_fetch_addr_calc;
    if (rd.going_to_do_pred && !inp.refetch) {
      NlpTagCombOut stage1_tag_out{};
      nlp_tag_comb(NlpTagCombIn{rd.pred_base_pc}, stage1_tag_out);
      const bool stage1_hit = rd.nlp_pred_base_entry_snapshot.entry_valid &&
                              (rd.nlp_pred_base_entry_snapshot.entry_tag ==
                               stage1_tag_out.tag);
      const uint8_t stage1_conf =
          stage1_hit ? rd.nlp_pred_base_entry_snapshot.entry_conf : 0;
      const uint32_t stage1_next_pc =
          (stage1_hit && stage1_conf >= NLP_CONF_THRESHOLD)
              ? rd.nlp_pred_base_entry_snapshot.entry_target
              : nlp_fallback_next_pc(rd.pred_base_pc);

      NlpTagCombOut stage2_tag_out{};
      nlp_tag_comb(NlpTagCombIn{stage1_next_pc}, stage2_tag_out);
      const bool stage2_hit = rd.nlp_s1_entry_snapshot.entry_valid &&
                              (rd.nlp_s1_entry_snapshot.entry_tag == stage2_tag_out.tag);
      const uint8_t stage2_conf =
          stage2_hit ? rd.nlp_s1_entry_snapshot.entry_conf : 0;
      const uint32_t stage2_target =
          stage2_hit ? rd.nlp_s1_entry_snapshot.entry_target
                     : nlp_fallback_next_pc(stage1_next_pc);
      const bool s1_match = (stage1_next_pc == next_fetch_addr_calc);
      const bool s2_usable = stage2_hit && (stage2_conf >= NLP_CONF_THRESHOLD);
      const bool emit_two_ahead = s1_match && s2_usable;
      out.final_2_ahead_address =
          emit_two_ahead ? stage2_target : (next_fetch_addr_calc + (FETCH_WIDTH * 4));

      const bool need_mini_flush =
          (rd.saved_2ahead_prediction_snapshot != next_fetch_addr_calc);
      out.out_regs.mini_flush_req =
          need_mini_flush && out.out_regs.PTAB_write_enable;
      out.out_regs.mini_flush_target = rd.saved_mini_flush_target_snapshot;
      out.saved_2ahead_prediction_next = out.final_2_ahead_address;
      out.saved_2ahead_pred_valid_next = emit_two_ahead;
      out.saved_mini_flush_req_next = need_mini_flush;
      out.saved_mini_flush_correct_next = !need_mini_flush;
      out.saved_mini_flush_target_next = rd.saved_2ahead_prediction_snapshot;

      const uint32_t train_pc = rd.pred_base_pc;
      const uint32_t train_target = next_fetch_addr_calc;
      NlpIndexCombOut train_idx_out{};
      NlpTagCombOut train_tag_out{};
      nlp_index_comb(NlpIndexCombIn{train_pc}, train_idx_out);
      nlp_tag_comb(NlpTagCombIn{train_pc}, train_tag_out);
      const ReadData::NlpEntrySnapshot &old_entry = rd.nlp_train_entry_snapshot;
      const bool train_hit = old_entry.entry_valid && (old_entry.entry_tag == train_tag_out.tag);
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
      out.nlp_entry_we = true;
      out.nlp_entry_idx = train_idx_out.index;
      out.nlp_entry_valid_next = true;
      out.nlp_entry_tag_next = train_tag_out.tag;
      out.nlp_entry_target_next = train_target;
      out.nlp_entry_conf_next = next_conf;
    }
#endif

#ifndef ENABLE_2AHEAD
    out.out_regs.two_ahead_valid = false;
    out.out_regs.mini_flush_req = false;
    out.out_regs.mini_flush_correct = false;
#endif
  }

  void bpu_hist_comb(const BpuHistCombIn &in, BpuHistCombOut &out) const {
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;
    const TypePredictor::OutputPayload &type_out = in.type_out;
    const wire1_t (&final_pred_dir)[FETCH_WIDTH] = in.final_pred_dir;
    std::memset(&out, 0, sizeof(out));
    std::memcpy(out.Spec_GHR_next, rd.Spec_GHR_snapshot, sizeof(out.Spec_GHR_next));
    std::memcpy(out.Arch_GHR_next, rd.Arch_GHR_snapshot, sizeof(out.Arch_GHR_next));
    std::memcpy(out.Spec_FH_next, rd.Spec_FH_snapshot, sizeof(out.Spec_FH_next));
    std::memcpy(out.Arch_FH_next, rd.Arch_FH_snapshot, sizeof(out.Arch_FH_next));
    out.Spec_PATH_next = rd.Spec_PATH_snapshot;
    out.Arch_PATH_next = rd.Arch_PATH_snapshot;
    std::memcpy(out.Arch_ras_stack_next, rd.Arch_ras_stack_snapshot,
                sizeof(out.Arch_ras_stack_next));
    out.Arch_ras_count_next = rd.Arch_ras_count_snapshot;
    std::memcpy(out.Spec_ras_stack_next, rd.Spec_ras_stack_snapshot,
                sizeof(out.Spec_ras_stack_next));
    out.Spec_ras_count_next = rd.Spec_ras_count_snapshot;
    out.should_update_spec_hist = rd.going_to_do_pred && !inp.refetch;

    if (out.should_update_spec_hist) {
      bool spec_ghr_tmp[GHR_LENGTH];
      uint32_t spec_fh_tmp[FH_N_MAX][TN_MAX];
      tage_path_hist_t spec_path_tmp = out.Spec_PATH_next;
      std::memcpy(spec_ghr_tmp, out.Spec_GHR_next, sizeof(spec_ghr_tmp));
      std::memcpy(spec_fh_tmp, out.Spec_FH_next, sizeof(spec_fh_tmp));
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        const bpu_bank_sel_ext_t bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        br_type_t p_type = type_out.pred_type[i];
        const bool pred_taken = final_pred_dir[i];
        if (p_type == BR_DIRECT) {
          bool next_ghr[GHR_LENGTH];
          uint32_t next_fh[FH_N_MAX][TN_MAX];
          tage_ghr_update_apply(spec_ghr_tmp, pred_taken, next_ghr);
          tage_fh_update_apply(spec_fh_tmp, spec_ghr_tmp, pred_taken, next_fh,
                               fh_length, ghr_length);
          std::memcpy(spec_ghr_tmp, next_ghr, sizeof(next_ghr));
          std::memcpy(spec_fh_tmp, next_fh, sizeof(next_fh));
          spec_path_tmp =
              tage_path_update_value(spec_path_tmp, rd.do_pred_for_this_pc[i], pred_taken);
        }
        if (pred_taken) {
          break;
        }
      }
      std::memcpy(out.Spec_GHR_next, spec_ghr_tmp, sizeof(out.Spec_GHR_next));
      std::memcpy(out.Spec_FH_next, spec_fh_tmp, sizeof(out.Spec_FH_next));
      out.Spec_PATH_next = spec_path_tmp;

#ifdef ENABLE_BPU_RAS
      target_addr_t spec_ras_stack_tmp[RAS_DEPTH];
      ras_count_t spec_ras_count_tmp = out.Spec_ras_count_next;
      std::memcpy(spec_ras_stack_tmp, out.Spec_ras_stack_next, sizeof(spec_ras_stack_tmp));
      for (int i = 0; i < FETCH_WIDTH; ++i) {
        if (!rd.do_pred_on_this_pc[i]) {
          continue;
        }
        const bpu_bank_sel_ext_t bank_sel = rd.this_pc_bank_sel[i];
        if (bank_sel >= BPU_BANK_NUM) {
          continue;
        }
        pc_t pc = rd.do_pred_for_this_pc[i];
        br_type_t p_type = type_out.pred_type[i];
        bool pred_taken = final_pred_dir[i];
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
      std::memcpy(out.Spec_ras_stack_next, spec_ras_stack_tmp, sizeof(out.Spec_ras_stack_next));
      out.Spec_ras_count_next = spec_ras_count_tmp;
#endif
    }

    {
      wire1_t arch_ghr_tmp[GHR_LENGTH];
      wire32_t arch_fh_tmp[FH_N_MAX][TN_MAX];
      tage_path_hist_t arch_path_tmp = out.Arch_PATH_next;
      std::memcpy(arch_ghr_tmp, out.Arch_GHR_next, sizeof(arch_ghr_tmp));
      std::memcpy(arch_fh_tmp, out.Arch_FH_next, sizeof(arch_fh_tmp));
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
        tage_path_hist_t next_path =
            tage_path_update_value(arch_path_tmp, inp.in_update_base_pc[i], real_dir);
        if (inp.in_pred_dir[i] != real_dir) {
          std::memcpy(out.Spec_GHR_next, next_ghr, sizeof(out.Spec_GHR_next));
          std::memcpy(out.Spec_FH_next, next_fh, sizeof(out.Spec_FH_next));
          out.Spec_PATH_next = next_path;
        }
        std::memcpy(arch_ghr_tmp, next_ghr, sizeof(arch_ghr_tmp));
        std::memcpy(arch_fh_tmp, next_fh, sizeof(arch_fh_tmp));
        arch_path_tmp = next_path;
        arch_need_write = true;
      }
      if (arch_need_write) {
        std::memcpy(out.Arch_GHR_next, arch_ghr_tmp, sizeof(out.Arch_GHR_next));
        std::memcpy(out.Arch_FH_next, arch_fh_tmp, sizeof(out.Arch_FH_next));
        out.Arch_PATH_next = arch_path_tmp;
      }
    }

#ifdef ENABLE_BPU_RAS
    {
      target_addr_t arch_ras_stack_tmp[RAS_DEPTH];
      ras_count_t arch_ras_count_tmp = out.Arch_ras_count_next;
      std::memcpy(arch_ras_stack_tmp, out.Arch_ras_stack_next, sizeof(arch_ras_stack_tmp));
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
      std::memcpy(out.Arch_ras_stack_next, arch_ras_stack_tmp,
                  sizeof(out.Arch_ras_stack_next));
      out.Arch_ras_count_next = arch_ras_count_tmp;
    }
#endif
  }

  void bpu_queue_comb(const BpuQueueCombIn &in, BpuQueueCombOut &out) const {
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;
    std::memset(&out, 0, sizeof(out));
    for (int i = 0; i < BPU_BANK_NUM; ++i) {
      out.q_wr_ptr_next[i] = rd.q_wr_ptr_snapshot[i];
      out.q_rd_ptr_next[i] = rd.q_rd_ptr_snapshot[i];
      out.q_count_next[i] = rd.q_count_snapshot[i];
      if (rd.going_to_do_upd[i] && out.q_count_next[i] > 0) {
        out.q_pop_en[i] = true;
        out.q_rd_ptr_next[i] = (out.q_rd_ptr_next[i] + 1) % Q_DEPTH;
        out.q_count_next[i]--;
      }
    }
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      if (!inp.in_upd_valid[i]) {
        continue;
      }
      BankSelCombOut bank_sel_out{};
      bank_sel_comb(BankSelCombIn{inp.in_update_base_pc[i]}, bank_sel_out);
      int bank_sel = bank_sel_out.bank_sel;
      if (out.q_count_next[bank_sel] < Q_DEPTH) {
        out.q_push_en[bank_sel] = true;
        out.q_entry_we[i] = true;
        out.q_entry_bank[i] = bank_sel;
        out.q_entry_slot[i] = out.q_wr_ptr_next[bank_sel];
        out.q_entry_data[i].base_pc = inp.in_update_base_pc[i];
        out.q_entry_data[i].valid_mask = inp.in_upd_valid[i];
        out.q_entry_data[i].actual_dir = inp.in_actual_dir[i];
        out.q_entry_data[i].pred_dir = inp.in_pred_dir[i];
        out.q_entry_data[i].alt_pred = inp.in_alt_pred[i];
        out.q_entry_data[i].br_type = inp.in_actual_br_type[i];
        out.q_entry_data[i].targets = inp.in_actual_targets[i];
        out.q_entry_data[i].pcpn = inp.in_pcpn[i];
        out.q_entry_data[i].altpcpn = inp.in_altpcpn[i];
        for (int k = 0; k < TN_MAX; k++) {
          out.q_entry_data[i].tage_tags[k] = inp.in_tage_tags[i][k];
          out.q_entry_data[i].tage_idxs[k] = inp.in_tage_idxs[i][k];
        }
        out.q_entry_data[i].sc_used = inp.in_sc_used[i];
        out.q_entry_data[i].sc_pred = inp.in_sc_pred[i];
        out.q_entry_data[i].sc_sum = inp.in_sc_sum[i];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          out.q_entry_data[i].sc_idx[t] = inp.in_sc_idx[i][t];
        }
        out.q_entry_data[i].loop_used = inp.in_loop_used[i];
        out.q_entry_data[i].loop_hit = inp.in_loop_hit[i];
        out.q_entry_data[i].loop_pred = inp.in_loop_pred[i];
        out.q_entry_data[i].loop_idx = inp.in_loop_idx[i];
        out.q_entry_data[i].loop_tag = inp.in_loop_tag[i];
        out.q_wr_ptr_next[bank_sel] = (out.q_wr_ptr_next[bank_sel] + 1) % Q_DEPTH;
        out.q_count_next[bank_sel]++;
      } else {
        std::cerr << "[BPU] update_queue overflow: commit_slot=" << i
                  << " bank=" << bank_sel
                  << " base_pc=0x" << std::hex << inp.in_update_base_pc[i]
                  << std::dec << " q_count=" << out.q_count_next[bank_sel]
                  << " depth=" << Q_DEPTH << std::endl;
        assert(false && "BPU update_queue overflow");
        std::exit(EXIT_FAILURE);
      }
    }

    bool q_full_any = false;
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      q_full_any |= (out.q_count_next[i] == Q_DEPTH);
    }
    out.update_queue_full = q_full_any;
  }

  void bpu_submodule_seq_read(const BpuPostReadReqCombOut &in, ReadData &rd) const {
    FRONTEND_HOST_PROFILE_SCOPE(BpuSubmoduleSeqRead);
    rd.nlp_s1_req_pc_snapshot = in.nlp_s1_req_pc;
    std::memset(&rd.nlp_s1_entry_snapshot, 0, sizeof(rd.nlp_s1_entry_snapshot));
    if (in.nlp_s1_re) {
      const NLPEntry &entry = nlp_table[in.nlp_s1_idx];
      rd.nlp_s1_entry_snapshot.entry_valid = entry.valid;
      rd.nlp_s1_entry_snapshot.entry_tag = entry.tag;
      rd.nlp_s1_entry_snapshot.entry_target = entry.target;
      rd.nlp_s1_entry_snapshot.entry_conf = entry.conf;
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
      tage_inst[i]->tage_seq_read(in.tage_in[i], rd.tage_rd[i]);
      btb_inst[i]->btb_seq_read(in.btb_in[i], rd.btb_rd[i]);
    }
    type_pred_inst->type_pred_seq_read(in.type_in, rd.type_rd);
  }

  void bpu_core_comb_calc(const InputPayload &inp, ReadData &rd,
                          const BpuPostReadReqCombOut &post_req,
                          BpuCombOut &comb_out) {
    FRONTEND_HOST_PROFILE_SCOPE(BpuCoreComb);
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
    req.Spec_PATH_next = rd.Spec_PATH_snapshot;
    req.Arch_PATH_next = rd.Arch_PATH_snapshot;
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
      req.tage_done_next[i] = false;
      req.btb_done_next[i] = false;
      req.tage_in[i] = post_req.tage_in[i];
      req.btb_in[i] = post_req.btb_in[i];
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
    }

    req.type_in = post_req.type_in;
    TypePredictor::OutputPayload type_out{};
    {
      FRONTEND_HOST_PROFILE_SCOPE(BpuTypeComb);
      type_pred_inst->type_pred_comb_calc(post_req.type_in, rd.type_rd, type_out,
                                          req.type_req);
    }

    TAGE_TOP::OutputPayload tage_out[BPU_BANK_NUM];
    BTB_TOP::OutputPayload btb_out[BPU_BANK_NUM];
    BpuSubmoduleBindCombOut bind_out{};
    BpuSubmoduleBindCombIn bind_in{};
    bind_in.rd = rd;
    bind_in.post_req = post_req;
    bind_in.type_out = type_out;
    bpu_submodule_bind_comb(bind_in, bind_out);
    {
      FRONTEND_HOST_PROFILE_SCOPE(BpuTageComb);
      for (int i = 0; i < BPU_BANK_NUM; i++) {
        tage_inst[i]->tage_comb_calc(post_req.tage_in[i], rd.tage_rd[i], tage_out[i],
                                     req.tage_req[i]);
      }
    }
    {
      FRONTEND_HOST_PROFILE_SCOPE(BpuBtbComb);
      for (int i = 0; i < BPU_BANK_NUM; i++) {
        btb_inst[i]->btb_comb_calc(bind_out.btb_in_with_type[i], rd.btb_rd[i],
                                   btb_out[i], req.btb_req[i]);
        req.btb_in[i] = bind_out.btb_in_with_type[i];
      }
    }
    BpuPredictMainCombIn predict_main_in{};
    predict_main_in.inp = inp;
    predict_main_in.rd = rd;
    predict_main_in.type_out = type_out;
    for (int i = 0; i < BPU_BANK_NUM; ++i) {
      predict_main_in.tage_out[i] = tage_out[i];
      predict_main_in.btb_out[i] = btb_out[i];
    }
    BpuPredictMainCombOut predict_main_out{};
    bpu_predict_main_comb(predict_main_in, predict_main_out);
    out = predict_main_out.out;
    req.next_fetch_addr_calc = predict_main_out.next_fetch_addr_calc;
    req.final_2_ahead_address = predict_main_out.final_2_ahead_address;
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      req.final_pred_dir[i] = predict_main_out.final_pred_dir[i];
      req.tage_calc_pred_dir_latch_next[i] =
          predict_main_out.tage_calc_pred_dir_latch_next[i];
      req.tage_calc_altpred_latch_next[i] =
          predict_main_out.tage_calc_altpred_latch_next[i];
      req.tage_calc_pcpn_latch_next[i] = predict_main_out.tage_calc_pcpn_latch_next[i];
      req.tage_calc_altpcpn_latch_next[i] =
          predict_main_out.tage_calc_altpcpn_latch_next[i];
      req.tage_result_valid_latch_next[i] =
          predict_main_out.tage_result_valid_latch_next[i];
      req.btb_pred_target_latch_next[i] = predict_main_out.btb_pred_target_latch_next[i];
      req.btb_result_valid_latch_next[i] =
          predict_main_out.btb_result_valid_latch_next[i];
      for (int k = 0; k < TN_MAX; ++k) {
        req.tage_pred_calc_tags_latch_next[i][k] =
            predict_main_out.tage_pred_calc_tags_latch_next[i][k];
        req.tage_pred_calc_idxs_latch_next[i][k] =
            predict_main_out.tage_pred_calc_idxs_latch_next[i][k];
      }
    }

    BpuNlpCombIn nlp_in{};
    nlp_in.inp = inp;
    nlp_in.rd = rd;
    nlp_in.next_fetch_addr_calc = req.next_fetch_addr_calc;
    nlp_in.final_2_ahead_address_seed = req.final_2_ahead_address;
    nlp_in.out_seed = out;
    BpuNlpCombOut nlp_out{};
    bpu_nlp_comb(nlp_in, nlp_out);
    out = nlp_out.out_regs;
    req.final_2_ahead_address = nlp_out.final_2_ahead_address;
    req.saved_2ahead_prediction_next = nlp_out.saved_2ahead_prediction_next;
    req.saved_2ahead_pred_valid_next = nlp_out.saved_2ahead_pred_valid_next;
    req.saved_mini_flush_req_next = nlp_out.saved_mini_flush_req_next;
    req.saved_mini_flush_correct_next = nlp_out.saved_mini_flush_correct_next;
    req.saved_mini_flush_target_next = nlp_out.saved_mini_flush_target_next;
    req.nlp_entry_we = nlp_out.nlp_entry_we;
    req.nlp_entry_idx = nlp_out.nlp_entry_idx;
    req.nlp_entry_valid_next = nlp_out.nlp_entry_valid_next;
    req.nlp_entry_tag_next = nlp_out.nlp_entry_tag_next;
    req.nlp_entry_target_next = nlp_out.nlp_entry_target_next;
    req.nlp_entry_conf_next = nlp_out.nlp_entry_conf_next;

    BpuHistCombIn hist_in{};
    hist_in.inp = inp;
    hist_in.rd = rd;
    hist_in.type_out = type_out;
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      hist_in.final_pred_dir[i] = req.final_pred_dir[i];
    }
    BpuHistCombOut hist_out{};
    bpu_hist_comb(hist_in, hist_out);
    req.should_update_spec_hist = hist_out.should_update_spec_hist;
    std::memcpy(req.Spec_GHR_next, hist_out.Spec_GHR_next, sizeof(req.Spec_GHR_next));
    std::memcpy(req.Spec_FH_next, hist_out.Spec_FH_next, sizeof(req.Spec_FH_next));
    std::memcpy(req.Arch_GHR_next, hist_out.Arch_GHR_next, sizeof(req.Arch_GHR_next));
    std::memcpy(req.Arch_FH_next, hist_out.Arch_FH_next, sizeof(req.Arch_FH_next));
    req.Spec_PATH_next = hist_out.Spec_PATH_next;
    req.Arch_PATH_next = hist_out.Arch_PATH_next;
    std::memcpy(req.Arch_ras_stack_next, hist_out.Arch_ras_stack_next,
                sizeof(req.Arch_ras_stack_next));
    req.Arch_ras_count_next = hist_out.Arch_ras_count_next;
    std::memcpy(req.Spec_ras_stack_next, hist_out.Spec_ras_stack_next,
                sizeof(req.Spec_ras_stack_next));
    req.Spec_ras_count_next = hist_out.Spec_ras_count_next;

    BpuQueueCombIn queue_in{};
    queue_in.inp = inp;
    queue_in.rd = rd;
    BpuQueueCombOut queue_out{};
    bpu_queue_comb(queue_in, queue_out);
    for (int i = 0; i < BPU_BANK_NUM; ++i) {
      req.q_push_en[i] = queue_out.q_push_en[i];
      req.q_pop_en[i] = queue_out.q_pop_en[i];
      req.q_wr_ptr_next[i] = queue_out.q_wr_ptr_next[i];
      req.q_rd_ptr_next[i] = queue_out.q_rd_ptr_next[i];
      req.q_count_next[i] = queue_out.q_count_next[i];
    }
    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      req.q_entry_we[i] = queue_out.q_entry_we[i];
      req.q_entry_bank[i] = queue_out.q_entry_bank[i];
      req.q_entry_slot[i] = queue_out.q_entry_slot[i];
      req.q_entry_data[i] = queue_out.q_entry_data[i];
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
      req.Spec_PATH_next = req.Arch_PATH_next;
#ifdef ENABLE_BPU_RAS
      std::memcpy(req.Spec_ras_stack_next, req.Arch_ras_stack_next,
                  sizeof(req.Spec_ras_stack_next));
      req.Spec_ras_count_next = req.Arch_ras_count_next;
#endif
    } else if (req.going_to_do_pred) {
      req.pc_reg_next = req.next_fetch_addr_calc;
      req.pc_can_send_to_icache_next = true;
    }

    out.update_queue_full = queue_out.update_queue_full;

    assert(out.out_pred_base_pc == out.fetch_address);
    req.out_regs = out;
  }

  void bpu_seq_write(const InputPayload &inp, const UpdateRequest &req,
                     bool reset) {
    if (reset) {
      type_pred_inst->reset();
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
    Spec_PATH = req.Spec_PATH_next;
    Arch_PATH = req.Arch_PATH_next;
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
      entry.sc_used = req.q_entry_data[i].sc_used;
      entry.sc_pred = req.q_entry_data[i].sc_pred;
      entry.sc_sum = req.q_entry_data[i].sc_sum;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        entry.sc_idx[t] = req.q_entry_data[i].sc_idx[t];
      }
      entry.loop_used = req.q_entry_data[i].loop_used;
      entry.loop_hit = req.q_entry_data[i].loop_hit;
      entry.loop_pred = req.q_entry_data[i].loop_pred;
      entry.loop_idx = req.q_entry_data[i].loop_idx;
      entry.loop_tag = req.q_entry_data[i].loop_tag;
    }

    for (int i = 0; i < BPU_BANK_NUM; i++) {
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

    type_pred_inst->type_pred_seq_write(req.type_in, req.type_req, false);
    state = static_cast<State>(req.next_state);
  }

  ~BPU_TOP() {
    for (int i = 0; i < BPU_BANK_NUM; i++) {
      delete tage_inst[i];
      delete btb_inst[i];
    }
    delete type_pred_inst;
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
    Arch_PATH = 0;
    Spec_PATH = 0;
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

  void bpu_comb_calc(const InputPayload &inp, ReadData &rd,
                     OutputPayload &out, UpdateRequest &req) {
    BpuPreReadReqCombOut pre_read_req{};
    BpuPostReadReqCombOut post_read_req{};
    BpuCombOut comb_out{};
    bpu_pre_read_req_comb(BpuPreReadReqCombIn{inp, rd}, pre_read_req);
    bpu_data_seq_read(pre_read_req, rd);
    bpu_post_read_req_comb(BpuPostReadReqCombIn{inp, rd}, post_read_req);
    bpu_submodule_seq_read(post_read_req, rd);
    bpu_core_comb_calc(inp, rd, post_read_req, comb_out);
    out = comb_out.out_regs;
    req = comb_out.update_req;
  }

};

#endif // BPU_TOP_H
