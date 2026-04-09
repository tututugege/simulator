#ifndef TAGE_TOP_H
#define TAGE_TOP_H

#include "../../frontend.h"
#include "../../wire_types.h"
#include "../BPU_configs.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <random>

// ============================================================================
// 1. 基础结构体定义 (Structures)
// ============================================================================

// TAGE index and tag
struct TageIndex {
  tage_idx_t tage_index[TN_MAX];
  tage_tag_t tag[TN_MAX];
};

// TAGE index-tag and base index
struct TageIndexTag {
  TageIndex index_info;
  tage_base_idx_t base_idx;
};

struct TageTableReadData {
  tage_tag_t tag[TN_MAX];
  tage_cnt_t cnt[TN_MAX];
  tage_useful_t useful[TN_MAX];
  tage_base_cnt_t base_cnt;
};

struct PredResult {
  wire1_t pred;
  wire1_t alt_pred;
  pcpn_t pcpn;
  pcpn_t altpcpn;
  TageIndex index_info;
};

struct UpdateRequest {
  wire1_t cnt_we[TN_MAX];
  tage_cnt_t cnt_wdata[TN_MAX];
  wire1_t useful_we[TN_MAX];
  tage_useful_t useful_wdata[TN_MAX];
  wire1_t tag_we[TN_MAX];
  tage_tag_t tag_wdata[TN_MAX];
  wire1_t base_we;
  tage_base_cnt_t base_wdata;
  wire1_t sc_we;
  tage_sc_ctr_t sc_wdata;
  wire1_t reset_we;
  tage_idx_t reset_row_idx;
  wire1_t reset_msb_only;
  wire1_t use_alt_ctr_we;
  // int8_t use_alt_ctr_wdata;
  tage_use_alt_ctr_t use_alt_ctr_wdata;
};

struct LSFR_Output {
  wire1_t next_state[4];
  tage_lsfr_rand_t random_val;
};

// ============================================================================
// 2. 辅助函数 (Pure Combinational Helpers)
// ============================================================================

struct SatCounterCombIn {
  wire3_t val;
};

struct SatCounterCombOut {
  wire3_t val;
};

static inline void sat_inc_3bit_comb(const SatCounterCombIn &in,
                                     SatCounterCombOut &out) {
  out = SatCounterCombOut{};
  out.val = (in.val >= 7) ? 7 : static_cast<uint8_t>(in.val + 1);
}

static inline void sat_dec_3bit_comb(const SatCounterCombIn &in,
                                     SatCounterCombOut &out) {
  out = SatCounterCombOut{};
  out.val = (in.val == 0) ? 0 : static_cast<uint8_t>(in.val - 1);
}

static inline void sat_inc_2bit_comb(const SatCounterCombIn &in,
                                     SatCounterCombOut &out) {
  out = SatCounterCombOut{};
  out.val = (in.val >= 3) ? 3 : static_cast<uint8_t>(in.val + 1);
}

static inline void sat_dec_2bit_comb(const SatCounterCombIn &in,
                                     SatCounterCombOut &out) {
  out = SatCounterCombOut{};
  out.val = (in.val == 0) ? 0 : static_cast<uint8_t>(in.val - 1);
}

static inline wire3_t sat_inc_3bit_value(wire3_t val) {
  SatCounterCombOut out{};
  sat_inc_3bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline wire3_t sat_dec_3bit_value(wire3_t val) {
  SatCounterCombOut out{};
  sat_dec_3bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline wire3_t sat_inc_2bit_value(wire3_t val) {
  SatCounterCombOut out{};
  sat_inc_2bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline wire3_t sat_dec_2bit_value(wire3_t val) {
  SatCounterCombOut out{};
  sat_dec_2bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline tage_sc_idx_t tage_sc_idx_from_pc(pc_t pc) {
  uint32_t value = pc >> 2;
  value ^= (value >> 7);
  value ^= (value >> 13);
  return static_cast<tage_sc_idx_t>(value & TAGE_SC_IDX_MASK);
}

// ----------------------------------------------------------------------------
// TAGE-SC-L: Strong Statistical Corrector (SC-L) helpers
// ----------------------------------------------------------------------------

static inline tage_use_alt_ctr_t sat_inc_use_alt(tage_use_alt_ctr_t v,
                                                 tage_use_alt_ctr_t max_v) {
  return (v >= max_v) ? max_v : static_cast<tage_use_alt_ctr_t>(v + 1);
}

static inline tage_use_alt_ctr_t sat_dec_use_alt(tage_use_alt_ctr_t v,
                                                 tage_use_alt_ctr_t min_v) {
  return (v <= min_v) ? min_v : static_cast<tage_use_alt_ctr_t>(v - 1);
}

static inline tage_scl_ctr_t sat_inc_scl(tage_scl_ctr_t v,
                                         tage_scl_ctr_t max_v) {
  return (v >= max_v) ? max_v : static_cast<tage_scl_ctr_t>(v + 1);
}

static inline tage_scl_ctr_t sat_dec_scl(tage_scl_ctr_t v,
                                         tage_scl_ctr_t min_v) {
  return (v <= min_v) ? min_v : static_cast<tage_scl_ctr_t>(v - 1);
}

static inline wire16_t abs_diff_u16(wire16_t a, wire16_t b) {
  return (a >= b) ? static_cast<wire16_t>(a - b) : static_cast<wire16_t>(b - a);
}

static inline uint32_t scl_fold_ghr_idx(const wire1_t ghr[GHR_LENGTH],
                                        int hist_len, int idx_bits) {
  uint32_t folded = 0;
  for (int i = 0; i < hist_len && i < GHR_LENGTH; ++i) {
    const uint32_t bit = static_cast<uint32_t>(ghr[i] ? 1 : 0);
    folded ^= (bit << (i % idx_bits));
    folded ^= (bit << ((i * 7) % idx_bits));
  }
  const uint32_t mask = (idx_bits >= 32) ? 0xffffffffu : ((1u << idx_bits) - 1u);
  return folded & mask;
}

static inline uint32_t scl_fold_path_idx(tage_path_hist_t path_hist, int hist_len,
                                         int idx_bits) {
  uint32_t path = static_cast<uint32_t>(path_hist) & TAGE_SC_PATH_MASK;
  const int eff_len = (hist_len < TAGE_SC_PATH_BITS) ? hist_len : TAGE_SC_PATH_BITS;
  if (eff_len < 32) {
    path &= ((1u << eff_len) - 1u);
  }
  uint32_t folded = 0;
  for (int i = 0; i < eff_len; ++i) {
    const uint32_t bit = (path >> i) & 0x1u;
    folded ^= (bit << (i % idx_bits));
    folded ^= (bit << ((i * 5 + 3) % idx_bits));
  }
  const uint32_t mask = (idx_bits >= 32) ? 0xffffffffu : ((1u << idx_bits) - 1u);
  return folded & mask;
}

// ----------------------------------------------------------------------------
// TAGE-SC-L: Loop predictor helpers
// ----------------------------------------------------------------------------

static inline uint32_t loop_idx_from_pc(pc_t pc) {
  const uint32_t mixed = (pc >> 2) ^ (pc >> 11) ^ (pc >> 19);
  return mixed & (TAGE_LOOP_ENTRY_NUM - 1);
}

static inline tage_loop_tag_t loop_tag_from_pc(pc_t pc) {
  const uint32_t value = (pc >> 2) ^ (pc >> (2 + TAGE_LOOP_TAG_BITS));
  return static_cast<tage_loop_tag_t>(value & ((1u << TAGE_LOOP_TAG_BITS) - 1u));
}

static inline tage_loop_iter_t loop_sat_inc(tage_loop_iter_t v,
                                             tage_loop_iter_t max_v) {
  return (v >= max_v) ? max_v : static_cast<tage_loop_iter_t>(v + 1);
}

static inline tage_loop_iter_t loop_sat_dec(tage_loop_iter_t v) {
  return (v == 0) ? 0 : static_cast<tage_loop_iter_t>(v - 1);
}

// ============================================================================
// 2.1 GHR/FH更新组合逻辑函数（提取为公共函数供BPU使用）
// ============================================================================

struct TageGhrUpdateCombIn {
  wire1_t current_GHR[GHR_LENGTH];
  wire1_t real_dir;
};

struct TageGhrUpdateCombOut {
  wire1_t next_GHR[GHR_LENGTH];
};

struct TageFhUpdateCombIn {
  wire32_t current_FH[FH_N_MAX][TN_MAX];
  wire1_t current_GHR[GHR_LENGTH];
  wire1_t new_history;
  uint32_t fh_len[FH_N_MAX][TN_MAX];
  uint32_t ghr_len[TN_MAX];
};

struct TageFhUpdateCombOut {
  wire32_t next_FH[FH_N_MAX][TN_MAX];
};

static inline void tage_ghr_update_comb(const TageGhrUpdateCombIn &in,
                                        TageGhrUpdateCombOut &out) {
  out = TageGhrUpdateCombOut{};
  for (int i = GHR_LENGTH - 1; i > 0; i--) {
    out.next_GHR[i] = in.current_GHR[i - 1];
  }
  out.next_GHR[0] = in.real_dir;
}

static inline void tage_fh_update_comb(const TageFhUpdateCombIn &in,
                                       TageFhUpdateCombOut &out) {
  out = TageFhUpdateCombOut{};
  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      uint32_t val = in.current_FH[k][i];
      uint32_t len = in.fh_len[k][i];
      bool old_highest = ((val >> (len - 1)) & 0x1) != 0;
      val = (val << 1) & ((0x1 << len) - 1);
      val |= static_cast<uint32_t>(in.new_history ^ old_highest);
      uint32_t ghr_idx = in.ghr_len[i];
      val ^= static_cast<uint32_t>(in.current_GHR[ghr_idx - 1]) << (ghr_idx % len);
      out.next_FH[k][i] = val;
    }
  }
}

static inline void tage_ghr_update_apply(const wire1_t current_GHR[GHR_LENGTH],
                                         wire1_t real_dir,
                                         wire1_t next_GHR[GHR_LENGTH]) {
  TageGhrUpdateCombOut out{};
  TageGhrUpdateCombIn in{};
  std::memcpy(in.current_GHR, current_GHR, sizeof(in.current_GHR));
  in.real_dir = real_dir;
  tage_ghr_update_comb(in, out);
  std::memcpy(next_GHR, out.next_GHR, sizeof(out.next_GHR));
}

static inline void tage_fh_update_apply(
    const wire32_t current_FH[FH_N_MAX][TN_MAX], const wire1_t current_GHR[GHR_LENGTH],
    wire1_t new_history, wire32_t next_FH[FH_N_MAX][TN_MAX],
    const uint32_t fh_len[FH_N_MAX][TN_MAX], const uint32_t ghr_len[TN_MAX]) {
  TageFhUpdateCombOut out{};
  TageFhUpdateCombIn in{};
  std::memcpy(in.current_FH, current_FH, sizeof(in.current_FH));
  std::memcpy(in.current_GHR, current_GHR, sizeof(in.current_GHR));
  in.new_history = new_history;
  std::memcpy(in.fh_len, fh_len, sizeof(in.fh_len));
  std::memcpy(in.ghr_len, ghr_len, sizeof(in.ghr_len));
  tage_fh_update_comb(in, out);
  std::memcpy(next_FH, out.next_FH, sizeof(out.next_FH));
}

// ============================================================================
// 3. 核心逻辑类 (TAGE_TOP Class)
// ============================================================================

class TAGE_TOP {
public:
  // ------------------------------------------------------------------------
  // 状态枚举定义（需要在结构体之前定义）
  // ------------------------------------------------------------------------
  enum State {
    S_IDLE = 0,
    S_STAGE2 = 1,
    S_IDLE_WAIT_DATA = 2,
    S_STAGE2_WAIT_DATA = 3
  };

  // ------------------------------------------------------------------------
  // 输入输出接口结构体
  // ------------------------------------------------------------------------
  struct InputPayload {
    wire1_t pred_req;
    pc_t pc_pred_in;
    wire1_t ghr_in[GHR_LENGTH];
    wire32_t fh_in[FH_N_MAX][TN_MAX];
    tage_path_hist_t path_in;
    wire1_t update_en;
    pc_t pc_update_in;
    wire1_t real_dir;
    wire1_t pred_in;
    wire1_t alt_pred_in;
    pcpn_t pcpn_in;
    pcpn_t altpcpn_in;
    tage_tag_t tage_tag_flat_in[TN_MAX];
    tage_idx_t tage_idx_flat_in[TN_MAX];
    wire1_t sc_used_in;
    wire1_t sc_pred_in;
    tage_scl_meta_sum_t sc_sum_in;
    tage_scl_meta_idx_t sc_idx_in[BPU_SCL_META_NTABLE];
    wire1_t loop_used_in;
    wire1_t loop_hit_in;
    wire1_t loop_pred_in;
    tage_loop_meta_idx_t loop_idx_in;
    tage_loop_meta_tag_t loop_tag_in;
  };

  struct OutputPayload {
    wire1_t pred_out;
    wire1_t alt_pred_out;
    pcpn_t pcpn_out;
    pcpn_t altpcpn_out;

    tage_tag_t tage_tag_flat_out[TN_MAX];
    tage_idx_t tage_idx_flat_out[TN_MAX];

    wire1_t sc_used_out;
    wire1_t sc_pred_out;
    tage_scl_meta_sum_t sc_sum_out;
    tage_scl_meta_idx_t sc_idx_out[BPU_SCL_META_NTABLE];
    wire1_t loop_used_out;
    wire1_t loop_hit_out;
    wire1_t loop_pred_out;
    tage_loop_meta_idx_t loop_idx_out;
    tage_loop_meta_tag_t loop_tag_out;

    wire1_t tage_pred_out_valid;
    wire1_t tage_update_done;
    wire1_t busy;
  };

  // 状态输入结构体（包含所有寄存器）
  struct StateInput {
    tage_state_t state;
    wire32_t FH[FH_N_MAX][TN_MAX];
    wire1_t GHR[GHR_LENGTH];
    wire1_t LSFR[4];
    tage_reset_ctr_t reset_cnt_reg;
    // int8_t use_alt_ctr_reg;
    tage_use_alt_ctr_t use_alt_ctr_reg;
#if ENABLE_TAGE_SC_L
    tage_scl_theta_t scl_theta_reg;
#endif
    // input latches
    wire1_t do_pred_latch;
    wire1_t do_upd_latch;
    wire1_t upd_real_dir_latch;
    pc_t upd_pc_latch;
    wire1_t upd_pred_in_latch;
    wire1_t upd_alt_pred_in_latch;
    pcpn_t upd_pcpn_in_latch;
    pcpn_t upd_altpcpn_in_latch;
    tage_tag_t upd_tage_tag_flat_latch[TN_MAX];
    tage_idx_t upd_tage_idx_flat_latch[TN_MAX];
    // pipeline latches
    tage_base_idx_t pred_calc_base_idx_latch;
    tage_idx_t pred_calc_tage_idx_latch[TN_MAX];
    tage_tag_t pred_calc_tage_tag_latch[TN_MAX];
    pc_t pred_pc_latch;
    UpdateRequest upd_calc_winfo_latch;
  };

  // Index生成结果
  struct IndexResult {
    tage_base_idx_t table_base_idx;
    tage_idx_t table_tage_idx[TN_MAX];
    wire1_t table_read_address_valid;
  };

  // 内存读取结果
  struct MemReadResult {
    TageTableReadData table_r;
    wire1_t table_read_data_valid;
  };

  // 三阶段 Read 阶段输出
  struct ReadData {
    StateInput state_in;
    IndexResult idx;
    MemReadResult mem;
    wire1_t useful_reset_row_data_valid;
    tage_useful_t useful_reset_row_data[TN_MAX];

    wire1_t sram_delay_active;
    wire32_t sram_delay_counter;
    TageTableReadData sram_delayed_data;
    wire1_t new_read_valid;
    TageTableReadData new_read_data;
    wire32_t sram_prng_state;

    wire1_t pred_read_valid;
    TageIndexTag pred_idx_tag;
    TageTableReadData pred_read_data;
    tage_sc_ctr_t pred_sc_ctr;
    tage_scl_ctr_t pred_scl_ctr[BPU_SCL_META_NTABLE];
    wire1_t pred_loop_entry_valid;
    tage_loop_meta_tag_t pred_loop_entry_tag;
    tage_loop_iter_t pred_loop_entry_iter_count;
    tage_loop_iter_t pred_loop_entry_iter_limit;
    tage_loop_conf_t pred_loop_entry_conf;
    tage_loop_age_t pred_loop_entry_age;
    wire1_t pred_loop_entry_dir;
    tage_loop_meta_idx_t pred_loop_idx;
    tage_loop_meta_tag_t pred_loop_tag;

    wire1_t upd_read_valid;
    tage_base_idx_t upd_base_idx;
    tage_sc_idx_t upd_sc_idx;
    tage_sc_ctr_t upd_sc_ctr;
    TageTableReadData upd_read_data;
    tage_scl_ctr_t upd_scl_ctr[BPU_SCL_META_NTABLE];
    wire1_t upd_loop_entry_valid;
    tage_loop_meta_tag_t upd_loop_entry_tag;
    tage_loop_iter_t upd_loop_entry_iter_count;
    tage_loop_iter_t upd_loop_entry_iter_limit;
    tage_loop_conf_t upd_loop_entry_conf;
    tage_loop_age_t upd_loop_entry_age;
    wire1_t upd_loop_entry_dir;
    tage_loop_meta_idx_t upd_loop_idx;
    tage_loop_meta_tag_t upd_loop_tag;

    wire1_t upd_reset_row_valid;
    tage_idx_t upd_reset_row_idx;
    tage_useful_t upd_reset_row_data[TN_MAX];
  };

  // 组合逻辑计算结果结构体
  struct CombResult {
    tage_state_t next_state;
    tage_base_idx_t table_base_idx;
    tage_idx_t table_tage_idx[TN_MAX];
    // TageTableReadData table_r;
    TageIndexTag s1_calc;
    LSFR_Output lsfr_out;
    UpdateRequest upd_calc_res;
    PredResult s2_comb_res;
    // GHR/FH更新已迁移到BPU，以下字段保留但不使用（注释掉以避免编译警告）
    // bool next_Spec_GHR[GHR_LENGTH];
    // uint32_t next_Spec_FH[FH_N_MAX][TN_MAX];
    // bool next_Arch_GHR[GHR_LENGTH];
    // uint32_t next_Arch_FH[FH_N_MAX][TN_MAX];
    OutputPayload out_regs;

    wire1_t sram_delay_active_next;
    wire32_t sram_delay_counter_next;
    TageTableReadData sram_delayed_data_next;
    wire32_t sram_prng_state_next;

    wire1_t do_pred_latch_next;
    wire1_t do_upd_latch_next;
    wire1_t upd_real_dir_latch_next;
    pc_t upd_pc_latch_next;
    wire1_t upd_pred_in_latch_next;
    wire1_t upd_alt_pred_in_latch_next;
    pcpn_t upd_pcpn_in_latch_next;
    pcpn_t upd_altpcpn_in_latch_next;
    tage_tag_t upd_tage_tag_flat_latch_next[TN_MAX];
    tage_idx_t upd_tage_idx_flat_latch_next[TN_MAX];
    tage_base_idx_t pred_calc_base_idx_latch_next;
    tage_idx_t pred_calc_tage_idx_latch_next[TN_MAX];
    tage_tag_t pred_calc_tage_tag_latch_next[TN_MAX];
    pc_t pred_pc_latch_next;
    UpdateRequest upd_calc_winfo_latch_next;
    wire1_t reset_cnt_reg_we;
    tage_reset_ctr_t reset_cnt_reg_next;
    wire1_t use_alt_ctr_reg_we;
    // int8_t use_alt_ctr_reg_next;
    tage_use_alt_ctr_t use_alt_ctr_reg_next;
    wire1_t lsfr_we;
    wire1_t LSFR_next[4];
    wire1_t base_we_commit;
    tage_base_idx_t base_wr_idx;
    tage_base_cnt_t base_wdata_commit;
    wire1_t cnt_we_commit[TN_MAX];
    tage_idx_t cnt_wr_idx[TN_MAX];
    tage_cnt_t cnt_wdata_commit[TN_MAX];
    wire1_t useful_we_commit[TN_MAX];
    tage_idx_t useful_wr_idx[TN_MAX];
    tage_useful_t useful_wdata_commit[TN_MAX];
    wire1_t tag_we_commit[TN_MAX];
    tage_idx_t tag_wr_idx[TN_MAX];
    tage_tag_t tag_wdata_commit[TN_MAX];
    wire1_t useful_reset_we_commit[TN_MAX];
    tage_idx_t useful_reset_row_commit[TN_MAX];
    wire1_t useful_reset_msb_only_commit[TN_MAX];
    tage_useful_t useful_reset_wdata_commit[TN_MAX];
    wire1_t sc_we_commit;
    tage_sc_idx_t sc_wr_idx;
    tage_sc_ctr_t sc_wdata_commit;
    wire1_t scl_we_commit[BPU_SCL_META_NTABLE];
    tage_scl_meta_idx_t scl_wr_idx[BPU_SCL_META_NTABLE];
    tage_scl_ctr_t scl_wdata_commit[BPU_SCL_META_NTABLE];
    wire1_t scl_theta_we_commit;
    tage_scl_theta_t scl_theta_wdata_commit;
    wire1_t loop_we_commit;
    tage_loop_meta_idx_t loop_wr_idx;
    wire1_t loop_valid_commit;
    tage_loop_meta_tag_t loop_tag_commit;
    tage_loop_iter_t loop_iter_count_commit;
    tage_loop_iter_t loop_iter_limit_commit;
    tage_loop_conf_t loop_conf_commit;
    tage_loop_age_t loop_age_commit;
    wire1_t loop_dir_commit;
  };

  struct TageGenIndexCombIn {
    InputPayload inp;
    StateInput state_in;
  };

  struct TageGenIndexCombOut {
    IndexResult idx;
  };

  struct TagePredReadReqCombIn {
    InputPayload inp;
  };

  struct TagePredReadReqCombOut {
    wire1_t pred_read_valid;
    TageIndexTag pred_idx_tag;
    tage_sc_idx_t pred_sc_idx;
    tage_scl_meta_idx_t pred_scl_idx[BPU_SCL_META_NTABLE];
    tage_loop_meta_idx_t pred_loop_idx;
    tage_loop_meta_tag_t pred_loop_tag;
  };

  struct TageUpdReadReqCombIn {
    InputPayload inp;
    StateInput state_in;
  };

  struct TageUpdReadReqCombOut {
    wire1_t upd_read_valid;
    tage_base_idx_t upd_base_idx;
    tage_sc_idx_t upd_sc_idx;
    tage_idx_t upd_tage_idx_flat[TN_MAX];
    tage_scl_meta_idx_t upd_scl_idx[BPU_SCL_META_NTABLE];
    tage_loop_meta_idx_t upd_loop_idx;
    tage_loop_meta_tag_t upd_loop_tag;
    wire1_t upd_loop_read_valid;
    wire1_t upd_reset_row_valid;
    tage_idx_t upd_reset_row_idx;
  };

  struct TageUsefulResetReadReqCombIn {
    StateInput state_in;
  };

  struct TageUsefulResetReadReqCombOut {
    wire1_t useful_reset_row_data_valid;
    tage_idx_t useful_reset_row_idx;
  };

  struct TageDataSeqReadIn {
    TagePredReadReqCombOut pred_req;
    TageUpdReadReqCombOut upd_req;
    TageUsefulResetReadReqCombOut useful_reset_req;
    IndexResult idx;
  };

  struct TagePreReadCombIn {
    InputPayload inp;
    StateInput state_in;
  };

  struct TagePreReadCombOut {
    TagePredReadReqCombOut pred_req;
    TageUpdReadReqCombOut upd_req;
    TageUsefulResetReadReqCombOut useful_reset_req;
    IndexResult idx;
  };

  struct TageCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct TageCombOut {
    OutputPayload out_regs;
    CombResult req;
  };

  struct TageXorshift32CombIn {
    wire32_t state;
  };

  struct TageXorshift32CombOut {
    wire32_t next_state;
  };

  struct TagePredIndexCombIn {
    pc_t pc;
    wire32_t fh_in[FH_N_MAX][TN_MAX];
  };

  struct TagePredIndexCombOut {
    TageIndexTag index_tag;
  };

  struct TagePredSelectCombIn {
    TageTableReadData read_data;
    TageIndexTag idx_tag;
    tage_sc_ctr_t sc_ctr;
    // int8_t use_alt_ctr;
    tage_use_alt_ctr_t use_alt_ctr;
  };

  struct TagePredSelectCombOut {
    PredResult pred_res;
  };

  struct TageUpdateCombIn {
    wire1_t real_dir;
    PredResult pred_res;
    TageTableReadData read_vals;
    tage_lsfr_rand_t lsfr_rand;
    tage_sc_ctr_t sc_ctr;
    tage_reset_ctr_t current_reset_cnt;
    wire1_t sc_used;
    wire1_t loop_used;
    // int8_t use_alt_ctr;
    tage_use_alt_ctr_t use_alt_ctr;
  };

  struct TageUpdateCombOut {
    UpdateRequest req;
  };

  struct LsfrUpdateCombIn {
    wire1_t current_lsfr[4];
  };

  struct LsfrUpdateCombOut {
    LSFR_Output lsfr_out;
  };

private:
  // 全局寄存器: GHR/FH已迁移到BPU_TOP统一管理，以下成员已删除
  // uint32_t Arch_FH[FH_N_MAX][TN_MAX];
  // uint32_t Spec_FH[FH_N_MAX][TN_MAX];
  // bool Arch_GHR[GHR_LENGTH];
  // bool Spec_GHR[GHR_LENGTH];

  wire1_t LSFR[4];
  tage_reset_ctr_t reset_cnt_reg;
  // int8_t use_alt_ctr_reg;
  tage_use_alt_ctr_t use_alt_ctr_reg;

  // 表项存储 (Memories)
  tage_base_cnt_t base_counter[BASE_ENTRY_NUM];
  tage_tag_t tag_table[TN_MAX][TN_ENTRY_NUM];
  tage_cnt_t cnt_table[TN_MAX][TN_ENTRY_NUM];
  tage_useful_t useful_table[TN_MAX][TN_ENTRY_NUM];
  tage_sc_ctr_t sc_ctr_table[TAGE_SC_ENTRY_NUM];

#if ENABLE_TAGE_SC_L
  // Strong SC-L tables (multi-table signed counters) + adaptive threshold.
  // int8_t scl_table[BPU_SCL_META_NTABLE][TAGE_SC_L_ENTRY_NUM];
  tage_scl_ctr_t scl_table[BPU_SCL_META_NTABLE][TAGE_SC_L_ENTRY_NUM];
  // int16_t scl_theta;
  tage_scl_theta_t scl_theta;
#endif

#if ENABLE_TAGE_LOOP_PRED
  struct LoopEntry {
    wire1_t valid;
    tage_loop_tag_t tag;
    tage_loop_iter_t iter_count;
    tage_loop_iter_t iter_limit;
    tage_loop_conf_t conf;
    tage_loop_age_t age;
    wire1_t dir;
  };
  LoopEntry loop_table[TAGE_LOOP_ENTRY_NUM];
#endif

  // FH constants
  const uint32_t ghr_length[TN_MAX] = {8, 13, 32, 119};
  const uint32_t fh_length[FH_N_MAX][TN_MAX] = {
      {8, 11, 11, 11}, {8, 8, 8, 8}, {7, 7, 7, 7}};

  // Pipeline Registers
  tage_state_t state;
  wire1_t do_pred_latch;
  wire1_t do_upd_latch;
  wire1_t upd_real_dir_latch;
  pc_t upd_pc_latch;
  wire1_t upd_pred_in_latch;
  wire1_t upd_alt_pred_in_latch;
  pcpn_t upd_pcpn_in_latch;
  pcpn_t upd_altpcpn_in_latch;
  tage_tag_t upd_tage_tag_flat_latch[TN_MAX];
  tage_idx_t upd_tage_idx_flat_latch[TN_MAX];

  // Pipeline Regs
  tage_base_idx_t pred_calc_base_idx_latch;
  tage_idx_t pred_calc_tage_idx_latch[TN_MAX];
  tage_tag_t pred_calc_tage_tag_latch[TN_MAX];
  pc_t pred_pc_latch;

  // For Update Writeback (S1 calc result):
  UpdateRequest upd_calc_winfo_latch; // 包含所有 upd_cnt_we, wdata 等

  // Outputs Registers
  OutputPayload out_regs;

  // SRAM延迟模拟相关变量
  wire1_t sram_delay_active;           // 是否正在进行延迟
  wire32_t sram_delay_counter;            // 剩余延迟周期数
  TageTableReadData sram_delayed_data; // 延迟期间保存的数据
  wire32_t sram_prng_state;          // 固定种子伪随机状态

public:
  // ------------------------------------------------------------------------
  // 构造函数
  // ------------------------------------------------------------------------
  TAGE_TOP() { reset(); }

  void reset() {
    // GHR/FH已迁移到BPU_TOP，不再在此初始化
    // memset(Arch_FH, 0, sizeof(Arch_FH));
    // memset(Spec_FH, 0, sizeof(Spec_FH));
    // memset(Arch_GHR, 0, sizeof(Arch_GHR));
    // memset(Spec_GHR, 0, sizeof(Spec_GHR));
    // Verilog: lsfr_reg <= 4'b0001
    LSFR[0] = 0;
    LSFR[1] = 0;
    LSFR[2] = 0;
    LSFR[3] = 1;
    reset_cnt_reg = 0;
    // use_alt_ctr_reg = static_cast<int8_t>(TAGE_USE_ALT_CTR_INIT);
    use_alt_ctr_reg = static_cast<tage_use_alt_ctr_t>(TAGE_USE_ALT_CTR_INIT);

    memset(base_counter, 0, sizeof(base_counter));
    memset(tag_table, 0, sizeof(tag_table));
    memset(cnt_table, 0, sizeof(cnt_table));
    memset(useful_table, 0, sizeof(useful_table));
    std::memset(sc_ctr_table, 1, sizeof(sc_ctr_table));
#if ENABLE_TAGE_SC_L
    std::memset(scl_table, (1u << (TAGE_SC_L_CTR_BITS - 1)), sizeof(scl_table));
    // scl_theta = static_cast<int16_t>(TAGE_SC_L_THETA_INIT);
    scl_theta = static_cast<tage_scl_theta_t>(TAGE_SC_L_THETA_INIT);
#endif
#if ENABLE_TAGE_LOOP_PRED
    std::memset(loop_table, 0, sizeof(loop_table));
#endif

    state = S_IDLE;
    do_pred_latch = false;
    do_upd_latch = false;
    upd_real_dir_latch = false;
    upd_pc_latch = 0;
    upd_pred_in_latch = false;
    upd_alt_pred_in_latch = false;
    upd_pcpn_in_latch = 0;
    upd_altpcpn_in_latch = 0;
    for (int i = 0; i < TN_MAX; ++i) {
      upd_tage_idx_flat_latch[i] = 0;
      upd_tage_tag_flat_latch[i] = 0;
    }
    pred_calc_base_idx_latch = 0;
    pred_pc_latch = 0;
    for (int i = 0; i < TN_MAX; ++i) {
      pred_calc_tage_idx_latch[i] = 0;
      pred_calc_tage_tag_latch[i] = 0;
    }
    memset(&out_regs, 0, sizeof(OutputPayload));
    // Init pipeline regs to 0
    memset(&upd_calc_winfo_latch, 0, sizeof(UpdateRequest));
    // Init SRAM delay simulation
    sram_delay_active = false;
    sram_delay_counter = 0;
    memset(&sram_delayed_data, 0, sizeof(TageTableReadData));
    sram_prng_state = 0x13579bdfu;
  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 生成Index
  // ------------------------------------------------------------------------
  void tage_gen_index_comb(const TageGenIndexCombIn &in,
                           TageGenIndexCombOut &out) const {
    const InputPayload &inp = in.inp;
    const StateInput &state_in = in.state_in;
    IndexResult &idx = out.idx;
    memset(&idx, 0, sizeof(IndexResult));

    bool read_pred = state_in.state == S_STAGE2 && state_in.do_pred_latch;
    bool read_upd = state_in.state == S_IDLE && inp.update_en;

    // Table Address Mux Logic
    if (read_pred) {
      idx.table_base_idx = state_in.pred_calc_base_idx_latch;
      for (int i = 0; i < TN_MAX; ++i) {
        idx.table_tage_idx[i] = state_in.pred_calc_tage_idx_latch[i];
      }
      idx.table_read_address_valid = true;

    } else if (read_upd) {
      idx.table_base_idx =
          (inp.pc_update_in >> 2) & ((1 << TAGE_BASE_IDX_WIDTH) - 1);
      for (int i = 0; i < TN_MAX; ++i) {
        idx.table_tage_idx[i] = inp.tage_idx_flat_in[i];
      }
      idx.table_read_address_valid = true;

    } else {
      idx.table_read_address_valid = false;
    }

  }

  void tage_pred_read_req_comb(const TagePredReadReqCombIn &in,
                               TagePredReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(TagePredReadReqCombOut));

    if (!in.inp.pred_req) {
      return;
    }

    TagePredIndexCombOut pred_index_out{};
    TagePredIndexCombIn pred_index_in{};
    pred_index_in.pc = in.inp.pc_pred_in;
    std::memcpy(pred_index_in.fh_in, in.inp.fh_in, sizeof(pred_index_in.fh_in));
    tage_pred_index_comb(pred_index_in, pred_index_out);

    out.pred_read_valid = true;
    out.pred_idx_tag = pred_index_out.index_tag;
    out.pred_sc_idx = tage_sc_idx_from_pc(in.inp.pc_pred_in);
#if ENABLE_TAGE_SC_L
    static constexpr int kSclIdxBits = ceil_log2_u32(TAGE_SC_L_ENTRY_NUM);
    static constexpr uint32_t kSclMask = TAGE_SC_L_ENTRY_NUM - 1;
    static constexpr int kHistLen[BPU_SCL_META_NTABLE] = {0, 4, 8, 16, 32, 64, 128, 256};
    static constexpr int kPathHistLen[BPU_SCL_META_NTABLE] = {0, 4, 8, 12, 16, 20, 24, 28};
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      const uint32_t folded =
          scl_fold_ghr_idx(in.inp.ghr_in, kHistLen[t], kSclIdxBits);
      uint32_t mix = (in.inp.pc_pred_in >> 2);
      mix ^= (mix >> 11);
      mix ^= (mix >> 19);
      mix ^= folded;
      mix ^= (folded << 1);
#if ENABLE_TAGE_SC_PATH
      const uint32_t path_folded =
          scl_fold_path_idx(in.inp.path_in, kPathHistLen[t], kSclIdxBits);
      mix ^= path_folded;
      mix ^= (path_folded << 2);
      mix ^= (path_folded >> 1);
#endif
      out.pred_scl_idx[t] = static_cast<tage_scl_meta_idx_t>(mix & kSclMask);
    }
#endif
#if ENABLE_TAGE_LOOP_PRED
    out.pred_loop_idx = static_cast<tage_loop_meta_idx_t>(loop_idx_from_pc(in.inp.pc_pred_in));
    out.pred_loop_tag = static_cast<tage_loop_meta_tag_t>(loop_tag_from_pc(in.inp.pc_pred_in));
#endif
  }

  void tage_upd_read_req_comb(const TageUpdReadReqCombIn &in,
                              TageUpdReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(TageUpdReadReqCombOut));

    if (!in.inp.update_en) {
      return;
    }

    out.upd_read_valid = true;
    out.upd_base_idx = (in.inp.pc_update_in >> 2) & TAGE_BASE_IDX_MASK;
    out.upd_sc_idx = tage_sc_idx_from_pc(in.inp.pc_update_in);
    for (int i = 0; i < TN_MAX; ++i) {
      out.upd_tage_idx_flat[i] = in.inp.tage_idx_flat_in[i];
    }
#if ENABLE_TAGE_SC_L
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      out.upd_scl_idx[t] = in.inp.sc_idx_in[t];
    }
#endif
#if ENABLE_TAGE_LOOP_PRED
    out.upd_loop_read_valid = true;
    out.upd_loop_idx = in.inp.loop_idx_in;
    out.upd_loop_tag = in.inp.loop_tag_in;
#endif

    const uint32_t u_cnt = in.state_in.reset_cnt_reg & 0x7ff;
    if (u_cnt == 0) {
      out.upd_reset_row_valid = true;
      out.upd_reset_row_idx = (in.state_in.reset_cnt_reg >> 11) & TAGE_IDX_MASK;
    }
  }

  void tage_useful_reset_read_req_comb(const TageUsefulResetReadReqCombIn &in,
                                       TageUsefulResetReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(TageUsefulResetReadReqCombOut));

    if (!in.state_in.upd_calc_winfo_latch.reset_we) {
      return;
    }

    out.useful_reset_row_data_valid = true;
    out.useful_reset_row_idx = in.state_in.upd_calc_winfo_latch.reset_row_idx;
  }

  void tage_pre_read_comb(const TagePreReadCombIn &in,
                          TagePreReadCombOut &out) const {
    std::memset(&out, 0, sizeof(TagePreReadCombOut));
    tage_pred_read_req_comb(TagePredReadReqCombIn{in.inp}, out.pred_req);
    tage_upd_read_req_comb(TageUpdReadReqCombIn{in.inp, in.state_in}, out.upd_req);
    tage_useful_reset_read_req_comb(TageUsefulResetReadReqCombIn{in.state_in},
                                    out.useful_reset_req);
    TageGenIndexCombOut gen_index_out{};
    tage_gen_index_comb(TageGenIndexCombIn{in.inp, in.state_in}, gen_index_out);
    out.idx = gen_index_out.idx;
  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 计算部分
  // ------------------------------------------------------------------------
  void tage_comb(const TageCombIn &in, TageCombOut &out_bundle) const {
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;
    OutputPayload &out = out_bundle.out_regs;
    CombResult &req = out_bundle.req;
    std::memset(&out, 0, sizeof(OutputPayload));
    std::memset(&req, 0, sizeof(CombResult));

    req.next_state = S_IDLE;
    req.sram_delay_active_next = false;
    req.sram_delay_counter_next = 0;
    req.sram_delayed_data_next = rd.sram_delayed_data;
    req.sram_prng_state_next = rd.sram_prng_state;
    req.reset_cnt_reg_next = rd.state_in.reset_cnt_reg;
    req.use_alt_ctr_reg_next = rd.state_in.use_alt_ctr_reg;
    for (int i = 0; i < 4; ++i) {
      req.LSFR_next[i] = rd.state_in.LSFR[i];
    }

    if (inp.pred_req && rd.pred_read_valid) {
      TagePredSelectCombOut pred_sel_out{};
      tage_pred_select_comb(
          TagePredSelectCombIn{rd.pred_read_data, rd.pred_idx_tag, rd.pred_sc_ctr,
                               rd.state_in.use_alt_ctr_reg},
          pred_sel_out);

      out.pred_out = pred_sel_out.pred_res.pred;
      out.alt_pred_out = pred_sel_out.pred_res.alt_pred;
      out.pcpn_out = static_cast<uint8_t>(pred_sel_out.pred_res.pcpn);
      out.altpcpn_out = static_cast<uint8_t>(pred_sel_out.pred_res.altpcpn);
      for (int i = 0; i < TN_MAX; ++i) {
        out.tage_tag_flat_out[i] = pred_sel_out.pred_res.index_info.tag[i];
        out.tage_idx_flat_out[i] = pred_sel_out.pred_res.index_info.tage_index[i];
      }
#if ENABLE_TAGE_SC_L
      // Compute SC-L indices and sum; override decision can be tuned later.
      static constexpr int kSclIdxBits = ceil_log2_u32(TAGE_SC_L_ENTRY_NUM);
      static constexpr uint32_t kSclMask = TAGE_SC_L_ENTRY_NUM - 1;
      static constexpr int kHistLen[BPU_SCL_META_NTABLE] = {0, 4, 8, 16, 32, 64, 128, 256};
      static constexpr int kPathHistLen[BPU_SCL_META_NTABLE] = {0, 4, 8, 12, 16, 20, 24, 28};
      // int16_t sum = 0;
      wire16_t sum_raw = 0;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        const uint32_t folded = scl_fold_ghr_idx(rd.state_in.GHR, kHistLen[t], kSclIdxBits);
        uint32_t mix = (inp.pc_pred_in >> 2);
        mix ^= (mix >> 11);
        mix ^= (mix >> 19);
        mix ^= folded;
        mix ^= (folded << 1);
#if ENABLE_TAGE_SC_PATH
        const uint32_t path_folded = scl_fold_path_idx(inp.path_in, kPathHistLen[t], kSclIdxBits);
        mix ^= path_folded;
        mix ^= (path_folded << 2);
        mix ^= (path_folded >> 1);
#endif
        const uint32_t idx = mix & kSclMask;
        out.sc_idx_out[t] = static_cast<tage_scl_meta_idx_t>(idx);
        sum_raw =
            static_cast<wire16_t>(sum_raw + static_cast<wire16_t>(rd.pred_scl_ctr[t]));
      }
      // out.sc_sum_out = sum;
      out.sc_sum_out = sum_raw;
      static constexpr wire16_t kSclCtrBias =
        static_cast<wire16_t>(1u << (TAGE_SC_L_CTR_BITS - 1));
      static constexpr wire16_t kSclSumBias =
        static_cast<wire16_t>(BPU_SCL_META_NTABLE * kSclCtrBias);
      // out.sc_pred_out = (sum >= 0);
      out.sc_pred_out = (sum_raw >= kSclSumBias);
      out.sc_used_out = false;
      if (pred_sel_out.pred_res.pcpn < TN_MAX) {
        const uint8_t provider_cnt = rd.pred_read_data.cnt[pred_sel_out.pred_res.pcpn];
        const bool provider_pred = (provider_cnt >= 4);
        const bool provider_weak =
            (provider_cnt >= TAGE_PROVIDER_WEAK_LOW) &&
            (provider_cnt <= TAGE_PROVIDER_WEAK_HIGH);
        const bool sc_disagree = (out.sc_pred_out != provider_pred);
        // const bool margin_ok = (scl_abs16(sum) >= (scl_theta + TAGE_SC_L_OVERRIDE_MARGIN));
        const wire16_t abs_sum = abs_diff_u16(sum_raw, kSclSumBias);
        const bool margin_ok =
            (abs_sum >= static_cast<wire16_t>(rd.state_in.scl_theta_reg +
                                              TAGE_SC_L_OVERRIDE_MARGIN));
        if (provider_weak && sc_disagree && margin_ok) {
          out.sc_used_out = true;
          out.pred_out = out.sc_pred_out;
        }
      }
#else
      out.sc_used_out = false;
      out.sc_pred_out = false;
      out.sc_sum_out = 0;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        out.sc_idx_out[t] = 0;
      }
#endif

#if ENABLE_TAGE_LOOP_PRED
      {
        const bool hit = rd.pred_loop_entry_valid &&
                         (rd.pred_loop_entry_tag == rd.pred_loop_tag);
        out.loop_hit_out = hit;
        out.loop_idx_out = rd.pred_loop_idx;
        out.loop_tag_out = rd.pred_loop_tag;
        out.loop_used_out = false;
        out.loop_pred_out = false;
        if (hit && rd.pred_loop_entry_conf >= TAGE_LOOP_CONF_THRESHOLD) {
          const bool exit_now = (rd.pred_loop_entry_iter_limit != 0) &&
                                ((rd.pred_loop_entry_iter_count + 1) ==
                                 rd.pred_loop_entry_iter_limit);
          const bool lp = exit_now ? !static_cast<bool>(rd.pred_loop_entry_dir)
                                   : static_cast<bool>(rd.pred_loop_entry_dir);
          out.loop_pred_out = lp;
          // Conservative override: only when TAGE is weak (provider weak).
          if (pred_sel_out.pred_res.pcpn < TN_MAX) {
            const uint8_t provider_cnt = rd.pred_read_data.cnt[pred_sel_out.pred_res.pcpn];
            const bool provider_weak =
                (provider_cnt >= TAGE_PROVIDER_WEAK_LOW) &&
                (provider_cnt <= TAGE_PROVIDER_WEAK_HIGH);
            if (provider_weak) {
              out.loop_used_out = true;
              out.pred_out = lp;
            }
          }
        }
      }
#else
      out.loop_used_out = false;
      out.loop_hit_out = false;
      out.loop_pred_out = false;
      out.loop_idx_out = 0;
      out.loop_tag_out = 0;
#endif
      out.tage_pred_out_valid = true;
    }

    if (inp.update_en && rd.upd_read_valid) {

      PredResult last_pred{};
      last_pred.pred = inp.pred_in;
      last_pred.alt_pred = inp.alt_pred_in;
      last_pred.pcpn = inp.pcpn_in;
      last_pred.altpcpn = inp.altpcpn_in;
      for (int i = 0; i < TN_MAX; ++i) {
        last_pred.index_info.tag[i] = inp.tage_tag_flat_in[i];
        last_pred.index_info.tage_index[i] = inp.tage_idx_flat_in[i] & TAGE_IDX_MASK;
      }

      LsfrUpdateCombOut lsfr_out{};
      LsfrUpdateCombIn lsfr_in{};
      for (int i = 0; i < 4; ++i) {
        lsfr_in.current_lsfr[i] = rd.state_in.LSFR[i];
      }
      lsfr_update_comb(lsfr_in, lsfr_out);
      req.lsfr_we = true;
      for (int i = 0; i < 4; ++i) {
        req.LSFR_next[i] = lsfr_out.lsfr_out.next_state[i];
      }

      TageUpdateCombOut update_out{};
      tage_update_comb(
          TageUpdateCombIn{inp.real_dir, last_pred, rd.upd_read_data,
                           lsfr_out.lsfr_out.random_val, rd.upd_sc_ctr,
                           rd.state_in.reset_cnt_reg, inp.sc_used_in,
                           inp.loop_used_in, rd.state_in.use_alt_ctr_reg},
          update_out);
      const UpdateRequest &upd_req = update_out.req;

      req.reset_cnt_reg_we = true;
      req.reset_cnt_reg_next = rd.state_in.reset_cnt_reg + 1;
      if (upd_req.use_alt_ctr_we) {
        req.use_alt_ctr_reg_we = true;
        req.use_alt_ctr_reg_next = upd_req.use_alt_ctr_wdata;
      }

      if (upd_req.base_we) {
        req.base_we_commit = true;
        req.base_wr_idx = rd.upd_base_idx;
        req.base_wdata_commit = upd_req.base_wdata;
      }
      if (upd_req.sc_we) {
        req.sc_we_commit = true;
        req.sc_wr_idx = rd.upd_sc_idx;
        req.sc_wdata_commit = upd_req.sc_wdata;
      }
      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t mem_idx = inp.tage_idx_flat_in[i] & TAGE_IDX_MASK;
        if (upd_req.cnt_we[i]) {
          req.cnt_we_commit[i] = true;
          req.cnt_wr_idx[i] = mem_idx;
          req.cnt_wdata_commit[i] = upd_req.cnt_wdata[i];
        }
        if (upd_req.useful_we[i]) {
          req.useful_we_commit[i] = true;
          req.useful_wr_idx[i] = mem_idx;
          req.useful_wdata_commit[i] = upd_req.useful_wdata[i];
        }
        if (upd_req.tag_we[i]) {
          req.tag_we_commit[i] = true;
          req.tag_wr_idx[i] = mem_idx;
          req.tag_wdata_commit[i] = upd_req.tag_wdata[i];
        }
      }

      if (upd_req.reset_we) {
        const uint32_t reset_row = upd_req.reset_row_idx & TAGE_IDX_MASK;
        for (int i = 0; i < TN_MAX; ++i) {
          const uint32_t mem_idx = inp.tage_idx_flat_in[i] & TAGE_IDX_MASK;
          const bool useful_update_conflict = upd_req.useful_we[i] && (mem_idx == reset_row);
          if (useful_update_conflict) {
            continue;
          }
          req.useful_reset_we_commit[i] = true;
          req.useful_reset_row_commit[i] = reset_row;
          uint8_t cur = 0;
          if (rd.upd_reset_row_valid && rd.upd_reset_row_idx == reset_row) {
            cur = rd.upd_reset_row_data[i];
          }
          req.useful_reset_wdata_commit[i] = upd_req.reset_msb_only ? (cur & 0x1) : (cur & 0x2);
        }
      }

#if ENABLE_TAGE_SC_L
      if (inp.sc_used_in) {
        static constexpr uint32_t kSclMask = TAGE_SC_L_ENTRY_NUM - 1;
        const tage_scl_ctr_t kCtrMax =
            static_cast<tage_scl_ctr_t>((1u << TAGE_SC_L_CTR_BITS) - 1u);
        const tage_scl_ctr_t kCtrMin = static_cast<tage_scl_ctr_t>(0u);
        const bool real_dir = inp.real_dir;
        const bool sc_pred = inp.sc_pred_in;
        const wire16_t sum_raw = inp.sc_sum_in;
        static constexpr wire16_t kSclCtrBias =
            static_cast<wire16_t>(1u << (TAGE_SC_L_CTR_BITS - 1));
        static constexpr wire16_t kSclSumBias =
            static_cast<wire16_t>(BPU_SCL_META_NTABLE * kSclCtrBias);
        const wire16_t abs_sum = abs_diff_u16(sum_raw, kSclSumBias);
        tage_scl_theta_t next_theta = rd.state_in.scl_theta_reg;
        const bool low_margin = (abs_sum < rd.state_in.scl_theta_reg);
        const bool sc_wrong = (sc_pred != real_dir);
        const bool train = sc_wrong || low_margin;
        if (train) {
          for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
            tage_scl_ctr_t cur = rd.upd_scl_ctr[t];
            cur = real_dir ? sat_inc_scl(cur, kCtrMax) : sat_dec_scl(cur, kCtrMin);
            req.scl_we_commit[t] = true;
            req.scl_wr_idx[t] =
                static_cast<tage_scl_meta_idx_t>(inp.sc_idx_in[t] & kSclMask);
            req.scl_wdata_commit[t] = cur;
          }
        }
        if (sc_wrong && (abs_sum >= rd.state_in.scl_theta_reg)) {
          if (next_theta < TAGE_SC_L_THETA_MAX) {
            next_theta = static_cast<tage_scl_theta_t>(next_theta + 1);
          }
        } else if (!sc_wrong && low_margin) {
          if (next_theta > TAGE_SC_L_THETA_MIN) {
            next_theta = static_cast<tage_scl_theta_t>(next_theta - 1);
          }
        }
        if (next_theta != rd.state_in.scl_theta_reg) {
          req.scl_theta_we_commit = true;
          req.scl_theta_wdata_commit = next_theta;
        }
      }
#endif

#if ENABLE_TAGE_LOOP_PRED
      {
        const tage_loop_tag_t ltag =
            static_cast<tage_loop_tag_t>(rd.upd_loop_tag) &
            static_cast<tage_loop_tag_t>((1u << TAGE_LOOP_TAG_BITS) - 1u);
        LoopEntry next_loop{};
        next_loop.valid = rd.upd_loop_entry_valid;
        next_loop.tag = rd.upd_loop_entry_tag;
        next_loop.iter_count = rd.upd_loop_entry_iter_count;
        next_loop.iter_limit = rd.upd_loop_entry_iter_limit;
        next_loop.conf = rd.upd_loop_entry_conf;
        next_loop.age = rd.upd_loop_entry_age;
        next_loop.dir = rd.upd_loop_entry_dir;
        const tage_loop_conf_t conf_max =
            static_cast<tage_loop_conf_t>((1u << TAGE_LOOP_CONF_BITS) - 1u);
        const tage_loop_age_t age_max =
            static_cast<tage_loop_age_t>((1u << TAGE_LOOP_AGE_BITS) - 1u);
        const tage_loop_iter_t iter_max =
            static_cast<tage_loop_iter_t>((1u << TAGE_LOOP_ITER_BITS) - 1u);

        if (!next_loop.valid || next_loop.tag != ltag) {
          next_loop.valid = true;
          next_loop.tag = ltag;
          next_loop.iter_count = 0;
          next_loop.iter_limit = 0;
          next_loop.conf = 0;
          next_loop.age = age_max;
          next_loop.dir = true;
        }

        if (next_loop.valid && next_loop.tag == ltag) {
          const bool real_dir = inp.real_dir;
          if (real_dir == static_cast<bool>(next_loop.dir)) {
            next_loop.iter_count = loop_sat_inc(next_loop.iter_count, iter_max);
          } else {
            if (next_loop.iter_limit != 0 && next_loop.iter_count == next_loop.iter_limit) {
              next_loop.conf = static_cast<tage_loop_conf_t>(
                  (next_loop.conf >= conf_max) ? conf_max : (next_loop.conf + 1));
              next_loop.age = age_max;
            } else {
              next_loop.conf = static_cast<tage_loop_conf_t>(
                  next_loop.conf ? (next_loop.conf - 1) : 0);
              next_loop.iter_limit =
                  (next_loop.iter_count == 0) ? 0 : next_loop.iter_count;
            }
            next_loop.iter_count = 0;
          }
        }

        req.loop_we_commit = true;
        req.loop_wr_idx = rd.upd_loop_idx;
        req.loop_valid_commit = next_loop.valid;
        req.loop_tag_commit = static_cast<tage_loop_meta_tag_t>(next_loop.tag);
        req.loop_iter_count_commit = next_loop.iter_count;
        req.loop_iter_limit_commit = next_loop.iter_limit;
        req.loop_conf_commit = next_loop.conf;
        req.loop_age_commit = next_loop.age;
        req.loop_dir_commit = next_loop.dir;
      }
#endif
      out.tage_update_done = true;
    }

    out.busy = false;
    req.out_regs = out;
  }

  void tage_seq_read(const InputPayload &inp, ReadData &rd) const {
    std::memset(&rd, 0, sizeof(ReadData));

    rd.state_in.state = state;

    for (int k = 0; k < FH_N_MAX; ++k) {
      for (int i = 0; i < TN_MAX; ++i) {
        rd.state_in.FH[k][i] = inp.fh_in[k][i];
      }
    }
    for (int i = 0; i < GHR_LENGTH; ++i) {
      rd.state_in.GHR[i] = inp.ghr_in[i];
    }

    for (int i = 0; i < 4; ++i) {
      rd.state_in.LSFR[i] = LSFR[i];
    }
    rd.state_in.reset_cnt_reg = reset_cnt_reg;
    rd.state_in.use_alt_ctr_reg = use_alt_ctr_reg;
#if ENABLE_TAGE_SC_L
    rd.state_in.scl_theta_reg = scl_theta;
#endif
    rd.state_in.do_pred_latch = do_pred_latch;
    rd.state_in.do_upd_latch = do_upd_latch;
    rd.state_in.upd_real_dir_latch = upd_real_dir_latch;
    rd.state_in.upd_pc_latch = upd_pc_latch;
    rd.state_in.upd_pred_in_latch = upd_pred_in_latch;
    rd.state_in.upd_alt_pred_in_latch = upd_alt_pred_in_latch;
    rd.state_in.upd_pcpn_in_latch = upd_pcpn_in_latch;
    rd.state_in.upd_altpcpn_in_latch = upd_altpcpn_in_latch;
    for (int i = 0; i < TN_MAX; ++i) {
      rd.state_in.upd_tage_tag_flat_latch[i] = upd_tage_tag_flat_latch[i];
      rd.state_in.upd_tage_idx_flat_latch[i] = upd_tage_idx_flat_latch[i];
    }
    rd.state_in.pred_calc_base_idx_latch = pred_calc_base_idx_latch;
    rd.state_in.pred_pc_latch = pred_pc_latch;
    for (int i = 0; i < TN_MAX; ++i) {
      rd.state_in.pred_calc_tage_idx_latch[i] = pred_calc_tage_idx_latch[i];
      rd.state_in.pred_calc_tage_tag_latch[i] = pred_calc_tage_tag_latch[i];
    }
    rd.state_in.upd_calc_winfo_latch = upd_calc_winfo_latch;

    rd.sram_delay_active = sram_delay_active;
    rd.sram_delay_counter = sram_delay_counter;
    rd.sram_delayed_data = sram_delayed_data;
    rd.sram_prng_state = sram_prng_state;
  }

  void tage_data_seq_read(const TageDataSeqReadIn &in, ReadData &rd) const {
    rd.new_read_valid = false;
    rd.useful_reset_row_data_valid = false;
    std::memset(rd.useful_reset_row_data, 0, sizeof(rd.useful_reset_row_data));
    rd.pred_read_valid = false;
    std::memset(&rd.pred_idx_tag, 0, sizeof(rd.pred_idx_tag));
    std::memset(&rd.pred_read_data, 0, sizeof(rd.pred_read_data));
    rd.pred_sc_ctr = 0;
    std::memset(rd.pred_scl_ctr, 0, sizeof(rd.pred_scl_ctr));
    rd.pred_loop_entry_valid = false;
    rd.pred_loop_entry_tag = 0;
    rd.pred_loop_entry_iter_count = 0;
    rd.pred_loop_entry_iter_limit = 0;
    rd.pred_loop_entry_conf = 0;
    rd.pred_loop_entry_age = 0;
    rd.pred_loop_entry_dir = false;
    rd.pred_loop_idx = 0;
    rd.pred_loop_tag = 0;
    rd.upd_read_valid = false;
    rd.upd_base_idx = 0;
    rd.upd_sc_idx = 0;
    rd.upd_sc_ctr = 0;
    std::memset(&rd.upd_read_data, 0, sizeof(rd.upd_read_data));
    std::memset(rd.upd_scl_ctr, 0, sizeof(rd.upd_scl_ctr));
    rd.upd_loop_entry_valid = false;
    rd.upd_loop_entry_tag = 0;
    rd.upd_loop_entry_iter_count = 0;
    rd.upd_loop_entry_iter_limit = 0;
    rd.upd_loop_entry_conf = 0;
    rd.upd_loop_entry_age = 0;
    rd.upd_loop_entry_dir = false;
    rd.upd_loop_idx = 0;
    rd.upd_loop_tag = 0;
    rd.upd_reset_row_valid = false;
    rd.upd_reset_row_idx = 0;
    std::memset(rd.upd_reset_row_data, 0, sizeof(rd.upd_reset_row_data));

    rd.pred_read_valid = in.pred_req.pred_read_valid;
    rd.pred_idx_tag = in.pred_req.pred_idx_tag;
    if (in.pred_req.pred_read_valid) {
      rd.pred_read_data.base_cnt =
          static_cast<uint8_t>(base_counter[in.pred_req.pred_idx_tag.base_idx]);
      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t mem_idx =
            in.pred_req.pred_idx_tag.index_info.tage_index[i] & TAGE_IDX_MASK;
        rd.pred_read_data.tag[i] = tag_table[i][mem_idx];
        rd.pred_read_data.cnt[i] = cnt_table[i][mem_idx];
        rd.pred_read_data.useful[i] = useful_table[i][mem_idx];
      }
      rd.pred_sc_ctr = sc_ctr_table[in.pred_req.pred_sc_idx];
#if ENABLE_TAGE_SC_L
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        const uint32_t idx = static_cast<uint32_t>(in.pred_req.pred_scl_idx[t]) &
                             (TAGE_SC_L_ENTRY_NUM - 1);
        rd.pred_scl_ctr[t] = scl_table[t][idx];
      }
#endif
#if ENABLE_TAGE_LOOP_PRED
      rd.pred_loop_idx = in.pred_req.pred_loop_idx;
      rd.pred_loop_tag = in.pred_req.pred_loop_tag;
      const LoopEntry &pred_loop_entry = loop_table[rd.pred_loop_idx];
      rd.pred_loop_entry_valid = pred_loop_entry.valid;
      rd.pred_loop_entry_tag = pred_loop_entry.tag;
      rd.pred_loop_entry_iter_count = pred_loop_entry.iter_count;
      rd.pred_loop_entry_iter_limit = pred_loop_entry.iter_limit;
      rd.pred_loop_entry_conf = pred_loop_entry.conf;
      rd.pred_loop_entry_age = pred_loop_entry.age;
      rd.pred_loop_entry_dir = pred_loop_entry.dir;
#endif
    }

    rd.upd_read_valid = in.upd_req.upd_read_valid;
    rd.upd_base_idx = in.upd_req.upd_base_idx;
    rd.upd_sc_idx = in.upd_req.upd_sc_idx;
    if (in.upd_req.upd_read_valid) {
      rd.upd_sc_ctr = sc_ctr_table[rd.upd_sc_idx];
      rd.upd_read_data.base_cnt = static_cast<uint8_t>(base_counter[rd.upd_base_idx]);
      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t mem_idx = in.upd_req.upd_tage_idx_flat[i] & TAGE_IDX_MASK;
        rd.upd_read_data.tag[i] = tag_table[i][mem_idx];
        rd.upd_read_data.cnt[i] = cnt_table[i][mem_idx];
        rd.upd_read_data.useful[i] = useful_table[i][mem_idx];
      }
      rd.upd_reset_row_valid = in.upd_req.upd_reset_row_valid;
      rd.upd_reset_row_idx = in.upd_req.upd_reset_row_idx;
      if (in.upd_req.upd_reset_row_valid) {
        for (int i = 0; i < TN_MAX; ++i) {
          rd.upd_reset_row_data[i] = useful_table[i][rd.upd_reset_row_idx];
        }
      }
#if ENABLE_TAGE_SC_L
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        const uint32_t idx = static_cast<uint32_t>(in.upd_req.upd_scl_idx[t]) &
                             (TAGE_SC_L_ENTRY_NUM - 1);
        rd.upd_scl_ctr[t] = scl_table[t][idx];
      }
#endif
#if ENABLE_TAGE_LOOP_PRED
      if (in.upd_req.upd_loop_read_valid) {
        rd.upd_loop_idx = in.upd_req.upd_loop_idx;
        rd.upd_loop_tag = in.upd_req.upd_loop_tag;
        const LoopEntry &upd_loop_entry = loop_table[rd.upd_loop_idx];
        rd.upd_loop_entry_valid = upd_loop_entry.valid;
        rd.upd_loop_entry_tag = upd_loop_entry.tag;
        rd.upd_loop_entry_iter_count = upd_loop_entry.iter_count;
        rd.upd_loop_entry_iter_limit = upd_loop_entry.iter_limit;
        rd.upd_loop_entry_conf = upd_loop_entry.conf;
        rd.upd_loop_entry_age = upd_loop_entry.age;
        rd.upd_loop_entry_dir = upd_loop_entry.dir;
      }
#endif
    }

    rd.useful_reset_row_data_valid = in.useful_reset_req.useful_reset_row_data_valid;
    if (in.useful_reset_req.useful_reset_row_data_valid) {
      uint32_t row = in.useful_reset_req.useful_reset_row_idx;
      for (int i = 0; i < TN_MAX; i++) {
        rd.useful_reset_row_data[i] = useful_table[i][row];
      }
    }
    rd.idx = in.idx;

    if (rd.sram_delay_active) {
      rd.mem.table_r = rd.sram_delayed_data;
      rd.mem.table_read_data_valid = (rd.sram_delay_counter == 0);
      rd.new_read_valid = false;
      return;
    }

    if (!rd.idx.table_read_address_valid) {
      rd.mem.table_read_data_valid = false;
      rd.new_read_valid = false;
      return;
    }

    rd.mem.table_r.base_cnt = base_counter[rd.idx.table_base_idx];
    for (int i = 0; i < TN_MAX; i++) {
      uint32_t mem_idx = rd.idx.table_tage_idx[i];
      rd.mem.table_r.tag[i] = tag_table[i][mem_idx];
      rd.mem.table_r.cnt[i] = cnt_table[i][mem_idx];
      rd.mem.table_r.useful[i] = useful_table[i][mem_idx];
    }
    rd.new_read_valid = true;
    rd.new_read_data = rd.mem.table_r;

#ifdef SRAM_DELAY_ENABLE
    rd.mem.table_read_data_valid = false;
#else
    rd.mem.table_read_data_valid = true;
#endif
  }

  void tage_comb_calc(const InputPayload &inp, ReadData &rd,
                      OutputPayload &out, CombResult &req) const {
    TagePreReadCombOut pre_read_out{};
    tage_pre_read_comb(TagePreReadCombIn{inp, rd.state_in}, pre_read_out);
    TageCombOut comb_out{};
    rd.pred_read_valid = pre_read_out.pred_req.pred_read_valid;
    rd.pred_idx_tag = pre_read_out.pred_req.pred_idx_tag;
    rd.upd_read_valid = pre_read_out.upd_req.upd_read_valid;
    rd.upd_base_idx = pre_read_out.upd_req.upd_base_idx;
    rd.upd_sc_idx = pre_read_out.upd_req.upd_sc_idx;
    rd.upd_reset_row_valid = pre_read_out.upd_req.upd_reset_row_valid;
    rd.upd_reset_row_idx = pre_read_out.upd_req.upd_reset_row_idx;
    rd.useful_reset_row_data_valid =
        pre_read_out.useful_reset_req.useful_reset_row_data_valid;
    rd.idx = pre_read_out.idx;
    tage_data_seq_read(TageDataSeqReadIn{pre_read_out.pred_req, pre_read_out.upd_req,
                                         pre_read_out.useful_reset_req, pre_read_out.idx},
                       rd);
    tage_comb(TageCombIn{inp, rd}, comb_out);
    out = comb_out.out_regs;
    req = comb_out.req;
  }

  void tage_seq_write(const InputPayload &inp, const CombResult &req, bool reset) {
    if (reset) {
      this->reset();
      return;
    }
    (void)inp;
    do_pred_latch = req.do_pred_latch_next;
    do_upd_latch = req.do_upd_latch_next;
    upd_real_dir_latch = req.upd_real_dir_latch_next;
    upd_pc_latch = req.upd_pc_latch_next;
    upd_pred_in_latch = req.upd_pred_in_latch_next;
    upd_alt_pred_in_latch = req.upd_alt_pred_in_latch_next;
    upd_pcpn_in_latch = req.upd_pcpn_in_latch_next;
    upd_altpcpn_in_latch = req.upd_altpcpn_in_latch_next;
    for (int i = 0; i < TN_MAX; ++i) {
      upd_tage_idx_flat_latch[i] = req.upd_tage_idx_flat_latch_next[i];
      upd_tage_tag_flat_latch[i] = req.upd_tage_tag_flat_latch_next[i];
      pred_calc_tage_idx_latch[i] = req.pred_calc_tage_idx_latch_next[i];
      pred_calc_tage_tag_latch[i] = req.pred_calc_tage_tag_latch_next[i];
    }
    pred_calc_base_idx_latch = req.pred_calc_base_idx_latch_next;
    pred_pc_latch = req.pred_pc_latch_next;
    upd_calc_winfo_latch = req.upd_calc_winfo_latch_next;

    if (req.reset_cnt_reg_we) {
      reset_cnt_reg = req.reset_cnt_reg_next;
    }
    if (req.use_alt_ctr_reg_we) {
      use_alt_ctr_reg = req.use_alt_ctr_reg_next;
    }
    if (req.lsfr_we) {
      for (int i = 0; i < 4; ++i) {
        LSFR[i] = req.LSFR_next[i];
      }
    }
    if (req.base_we_commit) {
      base_counter[req.base_wr_idx] = req.base_wdata_commit;
    }
    if (req.sc_we_commit) {
      sc_ctr_table[req.sc_wr_idx] = req.sc_wdata_commit;
    }
    for (int i = 0; i < TN_MAX; ++i) {
      if (req.cnt_we_commit[i]) {
        cnt_table[i][req.cnt_wr_idx[i]] = req.cnt_wdata_commit[i];
      }
      if (req.useful_we_commit[i]) {
        useful_table[i][req.useful_wr_idx[i]] = req.useful_wdata_commit[i];
      }
      if (req.tag_we_commit[i]) {
        tag_table[i][req.tag_wr_idx[i]] = req.tag_wdata_commit[i];
      }
      if (req.useful_reset_we_commit[i]) {
        uint32_t row = req.useful_reset_row_commit[i];
        useful_table[i][row] = req.useful_reset_wdata_commit[i];
      }
    }

#if ENABLE_TAGE_SC_L
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      if (!req.scl_we_commit[t]) {
        continue;
      }
      const uint32_t idx =
          static_cast<uint32_t>(req.scl_wr_idx[t]) & (TAGE_SC_L_ENTRY_NUM - 1);
      scl_table[t][idx] = req.scl_wdata_commit[t];
    }
    if (req.scl_theta_we_commit) {
      scl_theta = req.scl_theta_wdata_commit;
    }
#endif

#if ENABLE_TAGE_LOOP_PRED
    if (req.loop_we_commit) {
      const uint32_t lidx =
          static_cast<uint32_t>(req.loop_wr_idx) & (TAGE_LOOP_ENTRY_NUM - 1);
      LoopEntry &le = loop_table[lidx];
      le.valid = req.loop_valid_commit;
      le.tag = req.loop_tag_commit;
      le.iter_count = req.loop_iter_count_commit;
      le.iter_limit = req.loop_iter_limit_commit;
      le.conf = req.loop_conf_commit;
      le.age = req.loop_age_commit;
      le.dir = req.loop_dir_commit;
    }
#endif

    sram_delay_active = req.sram_delay_active_next;
    sram_delay_counter = req.sram_delay_counter_next;
    sram_delayed_data = req.sram_delayed_data_next;
    sram_prng_state = req.sram_prng_state_next;

    state = req.next_state;
  }

private:
  // ============================================================
  // 组合逻辑函数实现 (Internal Implementation)
  // ============================================================

  static void tage_xorshift32_comb(const TageXorshift32CombIn &in,
                                   TageXorshift32CombOut &out) {
    out = TageXorshift32CombOut{};
    uint32_t value = (in.state == 0) ? 0x13579bdfu : in.state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    out.next_state = value;
  }

  // [Comb 1]
  static void tage_pred_index_comb(const TagePredIndexCombIn &in,
                                   TagePredIndexCombOut &out) {
    memset(&out, 0, sizeof(TagePredIndexCombOut));

    out.index_tag.base_idx = (in.pc >> 2) & (TAGE_BASE_IDX_MASK);
    for (int i = 0; i < TN_MAX; i++) {
      out.index_tag.index_info.tag[i] =
          (in.fh_in[1][i] ^ in.fh_in[2][i] ^ (in.pc >> 2)) & (TAGE_TAG_MASK);
      out.index_tag.index_info.tage_index[i] =
          (in.fh_in[0][i] ^ (in.pc >> 2)) & (TAGE_IDX_MASK);
    }
  }

  // [Comb 2]
  static void tage_pred_select_comb(const TagePredSelectCombIn &in,
                                    TagePredSelectCombOut &out) {
    memset(&out, 0, sizeof(TagePredSelectCombOut));

    PredResult &res = out.pred_res;
    const TageTableReadData &read_data = in.read_data;
    const TageIndexTag &idx_tag = in.idx_tag;
    res.index_info = idx_tag.index_info;

    bool base_pred = (read_data.base_cnt >= 2);
    int pcpn = TN_MAX;
    int altpcpn = TN_MAX;

    for (int i = TN_MAX - 1; i >= 0; i--) {
      if (read_data.tag[i] == idx_tag.index_info.tag[i]) {
        pcpn = i;
        break;
      }
    }
    for (int i = pcpn - 1; i >= 0; i--) {
      if (read_data.tag[i] == idx_tag.index_info.tag[i]) {
        altpcpn = i;
        break;
      }
    }

    if (altpcpn >= TN_MAX) {
      res.alt_pred = base_pred;
    } else {
      res.alt_pred = (read_data.cnt[altpcpn] >= 4);
    }
    res.pcpn = pcpn;
    res.altpcpn = altpcpn;

    if (pcpn >= TN_MAX) {
      res.pred = base_pred;
    } else {
      const uint8_t provider_cnt = read_data.cnt[pcpn];
      const bool provider_pred = (provider_cnt >= 4);
      res.pred = provider_pred;
      const bool provider_weak_sc =
          (provider_cnt >= TAGE_SC_PROVIDER_WEAK_LOW) &&
          (provider_cnt <= TAGE_SC_PROVIDER_WEAK_HIGH);
#if ENABLE_TAGE_USE_ALT_ON_NA
      const bool provider_weak = (provider_cnt >= TAGE_PROVIDER_WEAK_LOW) &&
                                 (provider_cnt <= TAGE_PROVIDER_WEAK_HIGH);
      const bool useful_low =
          (read_data.useful[pcpn] <= TAGE_USE_ALT_USEFUL_THRESHOLD);
      if (provider_weak && useful_low) {
        // const bool prefer_alt =
        //     (in.use_alt_ctr <= static_cast<int8_t>(TAGE_USE_ALT_CTR_USE_ALT_THRESHOLD));
        const bool prefer_alt =
            (in.use_alt_ctr <= static_cast<tage_use_alt_ctr_t>(TAGE_USE_ALT_CTR_USE_ALT_THRESHOLD));
        res.pred = prefer_alt ? res.alt_pred : provider_pred;
      }
#endif
#if ENABLE_TAGE_SC_LITE
      bool sc_can_override = (provider_pred != res.alt_pred);
#if TAGE_SC_USE_WEAK_ONLY
      sc_can_override = sc_can_override && provider_weak_sc;
#endif
      if (sc_can_override) {
#if TAGE_SC_STRONG_ONLY_OVERRIDE
        if (in.sc_ctr == 0) {
          res.pred = res.alt_pred;
        } else if (in.sc_ctr == 3) {
          res.pred = provider_pred;
        }
#else
        res.pred = (in.sc_ctr >= 2) ? provider_pred : res.alt_pred;
#endif
      }
#endif
    }
  }

  // [Comb Update]
  static void tage_update_comb(const TageUpdateCombIn &in,
                               TageUpdateCombOut &out) {
    std::memset(&out, 0, sizeof(TageUpdateCombOut));
    UpdateRequest &req = out.req;
    const bool real_dir = in.real_dir;
    const PredResult &pred_res = in.pred_res;
    const TageTableReadData &read_vals = in.read_vals;
    const uint8_t lsfr_rand = in.lsfr_rand;
    const uint8_t sc_ctr = in.sc_ctr;
    const uint32_t current_reset_cnt = in.current_reset_cnt;

    bool pred_dir = pred_res.pred;
    int pcpn = pred_res.pcpn;

    // 1. Update Provider / Base
    if (pcpn < TN_MAX) {
      const bool provider_pred_raw = (read_vals.cnt[pcpn] >= 4);
      const bool provider_used = !(in.sc_used || in.loop_used);
      const bool alt_used = provider_used && (pred_dir == pred_res.alt_pred);
      uint8_t new_u = read_vals.useful[pcpn];
      bool should_write_useful = false;
      if (!provider_used) {
        new_u = (provider_pred_raw == real_dir) ? sat_inc_2bit_value(new_u)
                                                : sat_dec_2bit_value(new_u);
        should_write_useful = true;
      } else if (!alt_used) {
        if (pred_dir == real_dir) {
          new_u = sat_inc_2bit_value(new_u);
        } else {
          new_u = sat_dec_2bit_value(new_u);
        }
        should_write_useful = true;
      } else if (provider_pred_raw != real_dir && pred_res.alt_pred == real_dir) {
        new_u = sat_dec_2bit_value(new_u);
        should_write_useful = true;
      }
      if (should_write_useful) {
        req.useful_we[pcpn] = true;
        req.useful_wdata[pcpn] = new_u;
      }
      uint8_t new_cnt = read_vals.cnt[pcpn];
      if (real_dir == true) {
        new_cnt = sat_inc_3bit_value(new_cnt);
      } else {
        new_cnt = sat_dec_3bit_value(new_cnt);
      }
      req.cnt_we[pcpn] = true;
      req.cnt_wdata[pcpn] = new_cnt;

#if ENABLE_TAGE_USE_ALT_ON_NA
      const bool provider_weak = (read_vals.cnt[pcpn] >= TAGE_PROVIDER_WEAK_LOW) &&
                                 (read_vals.cnt[pcpn] <= TAGE_PROVIDER_WEAK_HIGH);
      const bool useful_low =
          (read_vals.useful[pcpn] <= TAGE_USE_ALT_USEFUL_THRESHOLD);
      if (provider_weak && useful_low && (provider_pred_raw != pred_res.alt_pred)) {
        const bool provider_correct = (provider_pred_raw == real_dir);
        const bool alt_correct = (pred_res.alt_pred == real_dir);
        if (provider_correct ^ alt_correct) {
          // const int8_t ctr_max =
          //     static_cast<int8_t>((1 << (TAGE_USE_ALT_CTR_BITS - 1)) - 1);
          // const int8_t ctr_min =
          //     static_cast<int8_t>(-(1 << (TAGE_USE_ALT_CTR_BITS - 1)));
          // int8_t next_ctr = in.use_alt_ctr;
          const tage_use_alt_ctr_t ctr_max =
            static_cast<tage_use_alt_ctr_t>((1u << TAGE_USE_ALT_CTR_BITS) - 1u);
          const tage_use_alt_ctr_t ctr_min = static_cast<tage_use_alt_ctr_t>(0u);
          tage_use_alt_ctr_t next_ctr = in.use_alt_ctr;
          if (alt_correct) {
            next_ctr = sat_dec_use_alt(next_ctr, ctr_min);
          } else {
            next_ctr = sat_inc_use_alt(next_ctr, ctr_max);
          }
          req.use_alt_ctr_we = true;
          req.use_alt_ctr_wdata = next_ctr;
        }
      }
#endif
    } else {
      int new_base = read_vals.base_cnt;
      if (real_dir == true) {
        new_base = sat_inc_2bit_value(new_base);
      } else {
        new_base = sat_dec_2bit_value(new_base);
      }
      req.base_we = true;
      req.base_wdata = new_base;
    }

    // 2. Allocation
    if (pred_dir != real_dir) {
      if (pcpn <= TN_MAX - 2 || pcpn == TN_MAX) {
        bool new_entry_found_j = false;
        int j_i = -1;
        bool new_entry_found_k = false;
        int k_i = -1;
        int start_search = (pcpn == TN_MAX) ? 0 : (pcpn + 1);

        for (int i = start_search; i < TN_MAX; i++) {
          if (read_vals.useful[i] == 0) {
            if (!new_entry_found_j) {
              new_entry_found_j = true;
              j_i = i;
              continue;
            } else {
              new_entry_found_k = true;
              k_i = i;
              break;
            }
          }
        }

        if (!new_entry_found_j) {
          for (int i = start_search; i < TN_MAX; i++) {
            req.useful_we[i] = true;
            req.useful_wdata[i] = sat_dec_2bit_value(read_vals.useful[i]);
          }
        } else {
          int target_i = -1;
          int random_pick = lsfr_rand % 3; // assumption: rand is 2 bits 0-3
          if (new_entry_found_k && random_pick == 0) {
            target_i = k_i;
          } else {
            target_i = j_i;
          }
          req.tag_we[target_i] = true;
          req.tag_wdata[target_i] = pred_res.index_info.tag[target_i];
          req.cnt_we[target_i] = true;
          req.cnt_wdata[target_i] = real_dir ? 4 : 3;
          req.useful_we[target_i] = true;
          req.useful_wdata[target_i] = 0;
        }
      }
    }

#if ENABLE_TAGE_SC_LITE
    if (pcpn < TN_MAX) {
      const uint8_t provider_cnt = read_vals.cnt[pcpn];
      const bool provider_pred_raw = (provider_cnt >= 4);
      const bool alt_pred_raw = pred_res.alt_pred;
      const bool provider_used = !(in.sc_used || in.loop_used);
      bool sc_train_en = provider_used && (provider_pred_raw != alt_pred_raw);
#if TAGE_SC_USE_WEAK_ONLY
      const bool provider_weak =
          (provider_cnt >= TAGE_SC_PROVIDER_WEAK_LOW) &&
          (provider_cnt <= TAGE_SC_PROVIDER_WEAK_HIGH);
      sc_train_en = sc_train_en && provider_weak;
#endif
      if (sc_train_en) {
        const bool provider_correct = (provider_pred_raw == real_dir);
        const bool alt_correct = (alt_pred_raw == real_dir);
        if (provider_correct ^ alt_correct) {
          req.sc_we = true;
          req.sc_wdata = provider_correct ? sat_inc_2bit_value(sc_ctr)
                                          : sat_dec_2bit_value(sc_ctr);
        }
      }
    }
#endif

    // 3. Reset Logic
    uint32_t u_cnt = current_reset_cnt &
                     0x7ff; // we leave these numbers here intentionally...
    uint32_t row_cnt = (current_reset_cnt >> 11) & 0xfff;
    bool u_msb_reset = (current_reset_cnt >> 23) & 0x1;

    if (u_cnt == 0) {
      req.reset_we = true;
      req.reset_row_idx = row_cnt;
      req.reset_msb_only = u_msb_reset;
    }
  }

  // [comb] LSFR Update
  static void lsfr_update_comb(const LsfrUpdateCombIn &in,
                               LsfrUpdateCombOut &out) {
    out = LsfrUpdateCombOut{};
    bool bit0 = in.current_lsfr[0];
    bool bit3 = in.current_lsfr[3];
    bool feedback = bit0 ^ bit3;
    out.lsfr_out.next_state[0] = feedback;
    for (int i = 1; i < 4; i++) {
      out.lsfr_out.next_state[i] = in.current_lsfr[i - 1];
    }
    out.lsfr_out.random_val =
        (static_cast<uint8_t>(out.lsfr_out.next_state[0]) << 1) |
        static_cast<uint8_t>(out.lsfr_out.next_state[1]);
  }

};

#endif // TAGE_TOP_H
