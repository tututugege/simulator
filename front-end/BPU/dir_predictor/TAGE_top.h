#ifndef TAGE_TOP_H
#define TAGE_TOP_H

#include "../../frontend.h"
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
  uint32_t tage_index[TN_MAX];
  uint8_t tag[TN_MAX];
};

// TAGE index-tag and base index
struct TageIndexTag {
  TageIndex index_info;
  uint32_t base_idx;
};

struct TageTableReadData {
  uint8_t tag[TN_MAX];    // 8-bit tag
  uint8_t cnt[TN_MAX];    // 3-bit cnt
  uint8_t useful[TN_MAX]; // 2-bit useful
  uint8_t base_cnt;       // 2-bit base counter
};

struct PredResult {
  bool pred;
  bool alt_pred;
  int pcpn;    // 3-bit pcpn(0123 and miss)
  int altpcpn; // 3-bit altpcpn(0123 and miss)
  TageIndex index_info;
};

struct UpdateRequest {
  bool cnt_we[TN_MAX];
  uint8_t cnt_wdata[TN_MAX];
  bool useful_we[TN_MAX];
  uint8_t useful_wdata[TN_MAX];
  bool tag_we[TN_MAX];
  uint8_t tag_wdata[TN_MAX];
  bool base_we;
  int base_wdata;
  bool sc_we;
  uint8_t sc_wdata;
  bool reset_we;
  uint32_t reset_row_idx;
  bool reset_msb_only;
};

struct LSFR_Output {
  bool next_state[4];
  uint8_t random_val;
};

// ============================================================================
// 2. 辅助函数 (Pure Combinational Helpers)
// ============================================================================

struct SatCounterCombIn {
  uint8_t val;
};

struct SatCounterCombOut {
  uint8_t val;
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

static inline uint8_t sat_inc_3bit_value(uint8_t val) {
  SatCounterCombOut out{};
  sat_inc_3bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline uint8_t sat_dec_3bit_value(uint8_t val) {
  SatCounterCombOut out{};
  sat_dec_3bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline uint8_t sat_inc_2bit_value(uint8_t val) {
  SatCounterCombOut out{};
  sat_inc_2bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline uint8_t sat_dec_2bit_value(uint8_t val) {
  SatCounterCombOut out{};
  sat_dec_2bit_comb(SatCounterCombIn{val}, out);
  return out.val;
}

static inline uint32_t tage_sc_idx_from_pc(uint32_t pc) {
  uint32_t value = pc >> 2;
  value ^= (value >> 7);
  value ^= (value >> 13);
  return value & TAGE_SC_IDX_MASK;
}

// ============================================================================
// 2.1 GHR/FH更新组合逻辑函数（提取为公共函数供BPU使用）
// ============================================================================

struct TageGhrUpdateCombIn {
  bool current_GHR[GHR_LENGTH];
  bool real_dir;
};

struct TageGhrUpdateCombOut {
  bool next_GHR[GHR_LENGTH];
};

struct TageFhUpdateCombIn {
  uint32_t current_FH[FH_N_MAX][TN_MAX];
  bool current_GHR[GHR_LENGTH];
  bool new_history;
  uint32_t fh_len[FH_N_MAX][TN_MAX];
  uint32_t ghr_len[TN_MAX];
};

struct TageFhUpdateCombOut {
  uint32_t next_FH[FH_N_MAX][TN_MAX];
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

static inline void tage_ghr_update_apply(const bool current_GHR[GHR_LENGTH],
                                         bool real_dir,
                                         bool next_GHR[GHR_LENGTH]) {
  TageGhrUpdateCombOut out{};
  TageGhrUpdateCombIn in{};
  std::memcpy(in.current_GHR, current_GHR, sizeof(in.current_GHR));
  in.real_dir = real_dir;
  tage_ghr_update_comb(in, out);
  std::memcpy(next_GHR, out.next_GHR, sizeof(out.next_GHR));
}

static inline void tage_fh_update_apply(
    const uint32_t current_FH[FH_N_MAX][TN_MAX], const bool current_GHR[GHR_LENGTH],
    bool new_history, uint32_t next_FH[FH_N_MAX][TN_MAX],
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
    bool pred_req;
    uint32_t pc_pred_in;
    bool ghr_in[GHR_LENGTH];
    uint32_t fh_in[FH_N_MAX][TN_MAX];
    bool update_en;
    uint32_t pc_update_in;
    bool real_dir;
    bool pred_in;       // 1-bit
    bool alt_pred_in;   // 1-bit
    uint8_t pcpn_in;    // 3-bit
    uint8_t altpcpn_in; // 3-bit
    uint8_t tage_tag_flat_in[TN_MAX];
    uint32_t tage_idx_flat_in[TN_MAX];
  };

  struct OutputPayload {
    bool pred_out;
    bool alt_pred_out;
    uint8_t pcpn_out;
    uint8_t altpcpn_out;

    uint8_t tage_tag_flat_out[TN_MAX];
    uint32_t tage_idx_flat_out[TN_MAX];

    bool tage_pred_out_valid;
    bool tage_update_done;
    bool busy;
  };

  // 状态输入结构体（包含所有寄存器）
  struct StateInput {
    State state;
    uint32_t FH[FH_N_MAX][TN_MAX];
    bool GHR[GHR_LENGTH];
    bool LSFR[4];
    uint32_t reset_cnt_reg;
    // input latches
    bool do_pred_latch;
    bool do_upd_latch;
    bool upd_real_dir_latch;
    uint32_t upd_pc_latch;
    bool upd_pred_in_latch;
    bool upd_alt_pred_in_latch;
    uint8_t upd_pcpn_in_latch;
    uint8_t upd_altpcpn_in_latch;
    uint8_t upd_tage_tag_flat_latch[TN_MAX];
    uint32_t upd_tage_idx_flat_latch[TN_MAX];
    // pipeline latches
    uint32_t pred_calc_base_idx_latch;
    uint32_t pred_calc_tage_idx_latch[TN_MAX];
    uint8_t pred_calc_tage_tag_latch[TN_MAX];
    uint32_t pred_pc_latch;
    UpdateRequest upd_calc_winfo_latch;
  };

  // Index生成结果
  struct IndexResult {
    uint32_t table_base_idx;
    uint32_t table_tage_idx[TN_MAX];
    bool table_read_address_valid;
  };

  // 内存读取结果
  struct MemReadResult {
    TageTableReadData table_r;
    bool table_read_data_valid;
  };

  // 三阶段 Read 阶段输出
  struct ReadData {
    StateInput state_in;
    IndexResult idx;
    MemReadResult mem;
    bool useful_reset_row_data_valid;
    uint8_t useful_reset_row_data[TN_MAX];

    bool sram_delay_active;
    int sram_delay_counter;
    TageTableReadData sram_delayed_data;
    bool new_read_valid;
    TageTableReadData new_read_data;
    uint32_t sram_prng_state;

    bool pred_read_valid;
    TageIndexTag pred_idx_tag;
    TageTableReadData pred_read_data;
    uint8_t pred_sc_ctr;

    bool upd_read_valid;
    uint32_t upd_base_idx;
    uint32_t upd_sc_idx;
    uint8_t upd_sc_ctr;
    TageTableReadData upd_read_data;

    bool upd_reset_row_valid;
    uint32_t upd_reset_row_idx;
    uint8_t upd_reset_row_data[TN_MAX];
  };

  // 组合逻辑计算结果结构体
  struct CombResult {
    State next_state;
    uint32_t table_base_idx;
    uint32_t table_tage_idx[TN_MAX];
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

    bool sram_delay_active_next;
    int sram_delay_counter_next;
    TageTableReadData sram_delayed_data_next;
    uint32_t sram_prng_state_next;

    bool do_pred_latch_next;
    bool do_upd_latch_next;
    bool upd_real_dir_latch_next;
    uint32_t upd_pc_latch_next;
    bool upd_pred_in_latch_next;
    bool upd_alt_pred_in_latch_next;
    uint8_t upd_pcpn_in_latch_next;
    uint8_t upd_altpcpn_in_latch_next;
    uint8_t upd_tage_tag_flat_latch_next[TN_MAX];
    uint32_t upd_tage_idx_flat_latch_next[TN_MAX];
    uint32_t pred_calc_base_idx_latch_next;
    uint32_t pred_calc_tage_idx_latch_next[TN_MAX];
    uint8_t pred_calc_tage_tag_latch_next[TN_MAX];
    uint32_t pred_pc_latch_next;
    UpdateRequest upd_calc_winfo_latch_next;
    bool reset_cnt_reg_we;
    uint32_t reset_cnt_reg_next;
    bool lsfr_we;
    bool LSFR_next[4];
    bool base_we_commit;
    uint32_t base_wr_idx;
    int base_wdata_commit;
    bool cnt_we_commit[TN_MAX];
    uint32_t cnt_wr_idx[TN_MAX];
    uint8_t cnt_wdata_commit[TN_MAX];
    bool useful_we_commit[TN_MAX];
    uint32_t useful_wr_idx[TN_MAX];
    uint8_t useful_wdata_commit[TN_MAX];
    bool tag_we_commit[TN_MAX];
    uint32_t tag_wr_idx[TN_MAX];
    uint8_t tag_wdata_commit[TN_MAX];
    bool useful_reset_we_commit[TN_MAX];
    uint32_t useful_reset_row_commit[TN_MAX];
    bool useful_reset_msb_only_commit[TN_MAX];
    uint8_t useful_reset_wdata_commit[TN_MAX];
    bool sc_we_commit;
    uint32_t sc_wr_idx;
    uint8_t sc_wdata_commit;
  };

  struct TageGenIndexCombIn {
    InputPayload inp;
    StateInput state_in;
  };

  struct TageGenIndexCombOut {
    IndexResult idx;
  };

  struct TageCoreCombIn {
    InputPayload inp;
    StateInput state_in;
    IndexResult idx;
    MemReadResult mem;
  };

  struct TageCoreCombOut {
    CombResult result;
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
    uint32_t state;
  };

  struct TageXorshift32CombOut {
    uint32_t next_state;
  };

  struct TagePredIndexCombIn {
    uint32_t pc;
    uint32_t fh_in[FH_N_MAX][TN_MAX];
  };

  struct TagePredIndexCombOut {
    TageIndexTag index_tag;
  };

  struct TagePredSelectCombIn {
    TageTableReadData read_data;
    TageIndexTag idx_tag;
    uint8_t sc_ctr;
  };

  struct TagePredSelectCombOut {
    PredResult pred_res;
  };

  struct TageUpdateCombIn {
    bool real_dir;
    PredResult pred_res;
    TageTableReadData read_vals;
    uint8_t lsfr_rand;
    uint8_t sc_ctr;
    uint32_t current_reset_cnt;
  };

  struct TageUpdateCombOut {
    UpdateRequest req;
  };

  struct LsfrUpdateCombIn {
    bool current_lsfr[4];
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

  bool LSFR[4];
  uint32_t reset_cnt_reg;

  // 表项存储 (Memories)
  int base_counter[BASE_ENTRY_NUM];
  uint8_t tag_table[TN_MAX][TN_ENTRY_NUM];
  uint8_t cnt_table[TN_MAX][TN_ENTRY_NUM];
  uint8_t useful_table[TN_MAX][TN_ENTRY_NUM];
  uint8_t sc_ctr_table[TAGE_SC_ENTRY_NUM];

  // FH constants
  const uint32_t ghr_length[TN_MAX] = {8, 13, 32, 119};
  const uint32_t fh_length[FH_N_MAX][TN_MAX] = {
      {8, 11, 11, 11}, {8, 8, 8, 8}, {7, 7, 7, 7}};

  // Pipeline Registers
  State state;
  bool do_pred_latch;
  bool do_upd_latch;
  bool upd_real_dir_latch;
  uint32_t upd_pc_latch;
  bool upd_pred_in_latch;
  bool upd_alt_pred_in_latch;
  uint8_t upd_pcpn_in_latch;
  uint8_t upd_altpcpn_in_latch;
  uint8_t upd_tage_tag_flat_latch[TN_MAX];
  uint32_t upd_tage_idx_flat_latch[TN_MAX];

  // Pipeline Regs
  uint32_t pred_calc_base_idx_latch;
  uint32_t pred_calc_tage_idx_latch[TN_MAX];
  uint8_t pred_calc_tage_tag_latch[TN_MAX];
  uint32_t pred_pc_latch;

  // For Update Writeback (S1 calc result):
  UpdateRequest upd_calc_winfo_latch; // 包含所有 upd_cnt_we, wdata 等

  // Outputs Registers
  OutputPayload out_regs;

  // SRAM延迟模拟相关变量
  bool sram_delay_active;           // 是否正在进行延迟
  int sram_delay_counter;            // 剩余延迟周期数
  TageTableReadData sram_delayed_data; // 延迟期间保存的数据
  bool sram_new_req_this_cycle;      // 本周期是否有新的读请求（在step_pipeline中设置，step_seq中使用）
  uint32_t sram_prng_state;          // 固定种子伪随机状态

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

    memset(base_counter, 0, sizeof(base_counter));
    memset(tag_table, 0, sizeof(tag_table));
    memset(cnt_table, 0, sizeof(cnt_table));
    memset(useful_table, 0, sizeof(useful_table));
    std::memset(sc_ctr_table, 1, sizeof(sc_ctr_table));

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
    sram_new_req_this_cycle = false;
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

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 计算部分
  // ------------------------------------------------------------------------
  void tage_core_comb(const TageCoreCombIn &in, TageCoreCombOut &out) const {
    const InputPayload &inp = in.inp;
    const StateInput &state_in = in.state_in;
    (void)in.idx;
    const MemReadResult &mem = in.mem;
    CombResult &comb = out.result;
    memset(&comb, 0, sizeof(CombResult));
    comb.do_pred_latch_next = state_in.do_pred_latch;
    comb.do_upd_latch_next = state_in.do_upd_latch;
    comb.upd_real_dir_latch_next = state_in.upd_real_dir_latch;
    comb.upd_pc_latch_next = state_in.upd_pc_latch;
    comb.upd_pred_in_latch_next = state_in.upd_pred_in_latch;
    comb.upd_alt_pred_in_latch_next = state_in.upd_alt_pred_in_latch;
    comb.upd_pcpn_in_latch_next = state_in.upd_pcpn_in_latch;
    comb.upd_altpcpn_in_latch_next = state_in.upd_altpcpn_in_latch;
    for (int i = 0; i < TN_MAX; ++i) {
      comb.upd_tage_tag_flat_latch_next[i] = state_in.upd_tage_tag_flat_latch[i];
      comb.upd_tage_idx_flat_latch_next[i] = state_in.upd_tage_idx_flat_latch[i];
      comb.pred_calc_tage_idx_latch_next[i] = state_in.pred_calc_tage_idx_latch[i];
      comb.pred_calc_tage_tag_latch_next[i] = state_in.pred_calc_tage_tag_latch[i];
    }
    for (int i = 0; i < 4; ++i) {
      comb.LSFR_next[i] = state_in.LSFR[i];
    }
    comb.pred_calc_base_idx_latch_next = state_in.pred_calc_base_idx_latch;
    comb.pred_pc_latch_next = state_in.pred_pc_latch;
    comb.upd_calc_winfo_latch_next = state_in.upd_calc_winfo_latch;

    // 复制index结果到输出
    // comb.table_base_idx = idx.table_base_idx;
    // for (int i = 0; i < TN_MAX; ++i) {
    //   comb.table_tage_idx[i] = idx.table_tage_idx[i];
    // }

    DEBUG_LOG_SMALL("[TAGE_TOP] state=%d, inp.pred_req=%d, inp.update_en=%d, pred_latch=%d, upd_latch=%d\n", 
      state_in.state, inp.pred_req, inp.update_en, state_in.do_pred_latch, state_in.do_upd_latch);
    // 1.1 Next State Logic
    switch (state_in.state) {
      case S_IDLE:
        if (inp.pred_req || inp.update_en) {
          if (!inp.update_en)
            comb.next_state = S_STAGE2; // no update req, go straight to stage 2
          else if (mem.table_read_data_valid)
            comb.next_state = S_STAGE2; // data is ready, go straight to stage 2
          else
            comb.next_state =
                S_IDLE_WAIT_DATA; // data is not ready, wait for data
        } else {
          comb.next_state = S_IDLE;
        }
        break;
      case S_STAGE2:
        if (!state_in.do_pred_latch)
          comb.next_state = S_IDLE; // no pred req, go straight to idle
        else if (mem.table_read_data_valid)
          comb.next_state = S_IDLE; // data is ready, go straight to idle
        else
          comb.next_state =
              S_STAGE2_WAIT_DATA; // data is not ready, wait for data
        break;
      case S_IDLE_WAIT_DATA:
        if (mem.table_read_data_valid)
          comb.next_state = S_STAGE2;
        else
          comb.next_state = S_IDLE_WAIT_DATA;
        break;
      case S_STAGE2_WAIT_DATA:
        if (mem.table_read_data_valid)
          comb.next_state = S_IDLE;
        else
          comb.next_state = S_STAGE2_WAIT_DATA;
        break;
      default:
        printf("[TAGE_TOP] ERROR!!: state = %d\n", state_in.state);
        exit(1); // unknown state
        comb.next_state = state_in.state;
        break;
    }

    // 1.4 Stage 1 Calculation
    if (state_in.state == S_IDLE && inp.pred_req) {
      TagePredIndexCombOut pred_index_out{};
      TagePredIndexCombIn pred_index_in{};
      pred_index_in.pc = inp.pc_pred_in;
      std::memcpy(pred_index_in.fh_in, state_in.FH, sizeof(pred_index_in.fh_in));
      tage_pred_index_comb(pred_index_in, pred_index_out);
      comb.s1_calc = pred_index_out.index_tag;
    }

    // 1.5 Stage 1 Calculation (计算更新写入值)
    LsfrUpdateCombIn lsfr_in{};
    for (int i = 0; i < 4; ++i) {
      lsfr_in.current_lsfr[i] = state_in.LSFR[i];
    }
    LsfrUpdateCombOut lsfr_out{};
    lsfr_update_comb(lsfr_in, lsfr_out);
    comb.lsfr_out = lsfr_out.lsfr_out;
    memset(&comb.upd_calc_res, 0, sizeof(UpdateRequest));

    if (comb.next_state == S_STAGE2) { // upd data read ready
      TageTableReadData old_data;
      old_data.base_cnt = mem.table_r.base_cnt;
      for (int i = 0; i < TN_MAX; ++i) {
        old_data.tag[i] = mem.table_r.tag[i];
        old_data.cnt[i] = mem.table_r.cnt[i];
        old_data.useful[i] = mem.table_r.useful[i];
      }

      PredResult last_pred;
      bool upd_real_dir;
      uint32_t upd_pc;
      if(state_in.state == S_IDLE){ // from input
        upd_real_dir = inp.real_dir;
        upd_pc = inp.pc_update_in;
        last_pred.pred = inp.pred_in; 
        last_pred.alt_pred = inp.alt_pred_in;
        last_pred.pcpn = inp.pcpn_in;
        last_pred.altpcpn = inp.altpcpn_in;
        for (int i = 0; i < TN_MAX; ++i) {
          last_pred.index_info.tag[i] = inp.tage_tag_flat_in[i];
          last_pred.index_info.tage_index[i] = inp.tage_idx_flat_in[i]; // not used
        }
      } else if(state_in.state == S_IDLE_WAIT_DATA){ // from latch
        upd_real_dir = state_in.upd_real_dir_latch;
        upd_pc = state_in.upd_pc_latch;
        last_pred.pred = state_in.upd_pred_in_latch;
        last_pred.alt_pred = state_in.upd_alt_pred_in_latch;
        last_pred.pcpn = state_in.upd_pcpn_in_latch;
        last_pred.altpcpn = state_in.upd_altpcpn_in_latch;
        for (int i = 0; i < TN_MAX; ++i) {
          last_pred.index_info.tag[i] = state_in.upd_tage_tag_flat_latch[i];
          last_pred.index_info.tage_index[i] = state_in.upd_tage_idx_flat_latch[i];
        }
      }
      const uint32_t upd_sc_idx = tage_sc_idx_from_pc(upd_pc);
      const uint8_t upd_sc_ctr = sc_ctr_table[upd_sc_idx];
      TageUpdateCombOut update_out{};
      tage_update_comb(
          TageUpdateCombIn{upd_real_dir, last_pred, old_data,
                           comb.lsfr_out.random_val, upd_sc_ctr,
                           state_in.reset_cnt_reg},
          update_out);
      comb.upd_calc_res = update_out.req;
    }

    // 1.6 Stage 2 Calculation (计算预测值)
    if ((state_in.state == S_STAGE2 || state_in.state == S_STAGE2_WAIT_DATA) &&
        comb.next_state == S_IDLE) { // pred data read ready
      TageTableReadData s2_r_data;
      s2_r_data.base_cnt = mem.table_r.base_cnt;
      for (int i = 0; i < TN_MAX; ++i) {
        s2_r_data.tag[i] = mem.table_r.tag[i];
        s2_r_data.cnt[i] = mem.table_r.cnt[i];
        s2_r_data.useful[i] = mem.table_r.useful[i]; // not used
      }
      TageIndexTag s2_idx_tag;
      s2_idx_tag.base_idx = state_in.pred_calc_base_idx_latch;
      for (int i = 0; i < TN_MAX; ++i) {
        s2_idx_tag.index_info.tag[i] =
            state_in.pred_calc_tage_tag_latch[i]; // only tag is used
        s2_idx_tag.index_info.tage_index[i] = state_in.pred_calc_tage_idx_latch[i];
      }
      const uint32_t pred_sc_idx = tage_sc_idx_from_pc(state_in.pred_pc_latch);
      const uint8_t pred_sc_ctr = sc_ctr_table[pred_sc_idx];
      TagePredSelectCombOut pred_select_out{};
      tage_pred_select_comb(TagePredSelectCombIn{s2_r_data, s2_idx_tag, pred_sc_ctr},
                            pred_select_out);
      comb.s2_comb_res = pred_select_out.pred_res;
    }

    // 1.7 Next Latch and Commit Request Calculation
    const bool enter_pipeline = (state_in.state == S_IDLE && comb.next_state != S_IDLE);
    if (enter_pipeline) {
      comb.do_pred_latch_next = inp.pred_req;
      comb.do_upd_latch_next = inp.update_en;
      comb.upd_real_dir_latch_next = inp.real_dir;
      comb.upd_pc_latch_next = inp.pc_update_in;
      comb.upd_pred_in_latch_next = inp.pred_in;
      comb.upd_alt_pred_in_latch_next = inp.alt_pred_in;
      comb.upd_pcpn_in_latch_next = inp.pcpn_in;
      comb.upd_altpcpn_in_latch_next = inp.altpcpn_in;
      for (int i = 0; i < TN_MAX; ++i) {
        comb.upd_tage_idx_flat_latch_next[i] = inp.tage_idx_flat_in[i];
        comb.upd_tage_tag_flat_latch_next[i] = inp.tage_tag_flat_in[i];
      }
      comb.pred_calc_base_idx_latch_next = comb.s1_calc.base_idx;
      if (inp.pred_req) {
        comb.pred_pc_latch_next = inp.pc_pred_in;
      }
      for (int i = 0; i < TN_MAX; ++i) {
        comb.pred_calc_tage_idx_latch_next[i] = comb.s1_calc.index_info.tage_index[i];
        comb.pred_calc_tage_tag_latch_next[i] = comb.s1_calc.index_info.tag[i];
      }
    }

    if (comb.next_state == S_STAGE2) {
      comb.upd_calc_winfo_latch_next = comb.upd_calc_res;
    }

    const bool do_commit_update =
        (state_in.state != S_IDLE && comb.next_state == S_IDLE &&
         state_in.do_upd_latch);
    if (do_commit_update) {
      comb.reset_cnt_reg_we = true;
      comb.reset_cnt_reg_next = state_in.reset_cnt_reg + 1;
      comb.lsfr_we = true;
      for (int i = 0; i < 4; ++i) {
        comb.LSFR_next[i] = comb.lsfr_out.next_state[i];
      }

      comb.base_we_commit = state_in.upd_calc_winfo_latch.base_we;
      comb.base_wr_idx = (state_in.upd_pc_latch >> 2) & TAGE_BASE_IDX_MASK;
      comb.base_wdata_commit = state_in.upd_calc_winfo_latch.base_wdata;
      comb.sc_we_commit = state_in.upd_calc_winfo_latch.sc_we;
      comb.sc_wr_idx = tage_sc_idx_from_pc(state_in.upd_pc_latch);
      comb.sc_wdata_commit = state_in.upd_calc_winfo_latch.sc_wdata;

      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t wr_idx = state_in.upd_tage_idx_flat_latch[i];
        comb.cnt_we_commit[i] = state_in.upd_calc_winfo_latch.cnt_we[i];
        comb.cnt_wr_idx[i] = wr_idx;
        comb.cnt_wdata_commit[i] = state_in.upd_calc_winfo_latch.cnt_wdata[i];
        comb.useful_we_commit[i] = state_in.upd_calc_winfo_latch.useful_we[i];
        comb.useful_wr_idx[i] = wr_idx;
        comb.useful_wdata_commit[i] = state_in.upd_calc_winfo_latch.useful_wdata[i];
        comb.tag_we_commit[i] = state_in.upd_calc_winfo_latch.tag_we[i];
        comb.tag_wr_idx[i] = wr_idx;
        comb.tag_wdata_commit[i] = state_in.upd_calc_winfo_latch.tag_wdata[i];
        comb.useful_reset_we_commit[i] = state_in.upd_calc_winfo_latch.reset_we;
        comb.useful_reset_row_commit[i] = state_in.upd_calc_winfo_latch.reset_row_idx;
        comb.useful_reset_msb_only_commit[i] =
            state_in.upd_calc_winfo_latch.reset_msb_only;
      }
    }

    // 1.8 Output Logic
    // comb.out_regs.busy = (state_in.state != S_IDLE);

    if (state_in.state != S_IDLE &&
        comb.next_state == S_IDLE) { // moving to idle
      if (state_in.do_upd_latch) {
        comb.out_regs.tage_update_done = true;
      }
      if (state_in.do_pred_latch) {
        comb.out_regs.pred_out = comb.s2_comb_res.pred;
        comb.out_regs.alt_pred_out = comb.s2_comb_res.alt_pred;
        comb.out_regs.pcpn_out = comb.s2_comb_res.pcpn;
        comb.out_regs.altpcpn_out = comb.s2_comb_res.altpcpn;

        for (int i = 0; i < TN_MAX; ++i) {
          comb.out_regs.tage_tag_flat_out[i] = state_in.pred_calc_tage_tag_latch[i];
          comb.out_regs.tage_idx_flat_out[i] = state_in.pred_calc_tage_idx_latch[i];
        }
        comb.out_regs.tage_pred_out_valid = true;
      }
    }

  }

  // ------------------------------------------------------------------------
  // 三阶段接口
  // ------------------------------------------------------------------------
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
    tage_prepare_comb_read(inp, rd);
  }

  void tage_prepare_comb_read(const InputPayload &inp, ReadData &rd) const {
    rd.new_read_valid = false;
    rd.useful_reset_row_data_valid = false;
    std::memset(rd.useful_reset_row_data, 0, sizeof(rd.useful_reset_row_data));
    rd.pred_read_valid = false;
    std::memset(&rd.pred_idx_tag, 0, sizeof(rd.pred_idx_tag));
    std::memset(&rd.pred_read_data, 0, sizeof(rd.pred_read_data));
    rd.pred_sc_ctr = 0;
    rd.upd_read_valid = false;
    rd.upd_base_idx = 0;
    rd.upd_sc_idx = 0;
    rd.upd_sc_ctr = 0;
    std::memset(&rd.upd_read_data, 0, sizeof(rd.upd_read_data));
    rd.upd_reset_row_valid = false;
    rd.upd_reset_row_idx = 0;
    std::memset(rd.upd_reset_row_data, 0, sizeof(rd.upd_reset_row_data));

    if (inp.pred_req) {
      TagePredIndexCombOut pred_index_out{};
      TagePredIndexCombIn pred_index_in{};
      pred_index_in.pc = inp.pc_pred_in;
      std::memcpy(pred_index_in.fh_in, inp.fh_in, sizeof(pred_index_in.fh_in));
      tage_pred_index_comb(pred_index_in, pred_index_out);

      rd.pred_read_valid = true;
      rd.pred_idx_tag = pred_index_out.index_tag;
      rd.pred_read_data.base_cnt =
          static_cast<uint8_t>(base_counter[pred_index_out.index_tag.base_idx]);
      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t mem_idx =
            pred_index_out.index_tag.index_info.tage_index[i] & TAGE_IDX_MASK;
        rd.pred_read_data.tag[i] = tag_table[i][mem_idx];
        rd.pred_read_data.cnt[i] = cnt_table[i][mem_idx];
        rd.pred_read_data.useful[i] = useful_table[i][mem_idx];
      }
      rd.pred_sc_ctr = sc_ctr_table[tage_sc_idx_from_pc(inp.pc_pred_in)];
    }

    if (inp.update_en) {
      rd.upd_read_valid = true;
      rd.upd_base_idx = (inp.pc_update_in >> 2) & TAGE_BASE_IDX_MASK;
      rd.upd_sc_idx = tage_sc_idx_from_pc(inp.pc_update_in);
      rd.upd_sc_ctr = sc_ctr_table[rd.upd_sc_idx];
      rd.upd_read_data.base_cnt = static_cast<uint8_t>(base_counter[rd.upd_base_idx]);
      for (int i = 0; i < TN_MAX; ++i) {
        const uint32_t mem_idx = inp.tage_idx_flat_in[i] & TAGE_IDX_MASK;
        rd.upd_read_data.tag[i] = tag_table[i][mem_idx];
        rd.upd_read_data.cnt[i] = cnt_table[i][mem_idx];
        rd.upd_read_data.useful[i] = useful_table[i][mem_idx];
      }

      const uint32_t u_cnt = rd.state_in.reset_cnt_reg & 0x7ff;
      if (u_cnt == 0) {
        rd.upd_reset_row_valid = true;
        rd.upd_reset_row_idx = (rd.state_in.reset_cnt_reg >> 11) & TAGE_IDX_MASK;
        for (int i = 0; i < TN_MAX; ++i) {
          rd.upd_reset_row_data[i] = useful_table[i][rd.upd_reset_row_idx];
        }
      }
    }

    if (rd.state_in.upd_calc_winfo_latch.reset_we) {
      uint32_t row = rd.state_in.upd_calc_winfo_latch.reset_row_idx;
      for (int i = 0; i < TN_MAX; i++) {
        rd.useful_reset_row_data[i] = useful_table[i][row];
      }
      rd.useful_reset_row_data_valid = true;
    }

    TageGenIndexCombOut gen_index_out{};
    tage_gen_index_comb(TageGenIndexCombIn{inp, rd.state_in}, gen_index_out);
    rd.idx = gen_index_out.idx;

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
    for (int i = 0; i < 4; ++i) {
      req.LSFR_next[i] = rd.state_in.LSFR[i];
    }

    if (inp.pred_req && rd.pred_read_valid) {
      TagePredSelectCombOut pred_sel_out{};
      tage_pred_select_comb(
          TagePredSelectCombIn{rd.pred_read_data, rd.pred_idx_tag, rd.pred_sc_ctr},
          pred_sel_out);

      out.pred_out = pred_sel_out.pred_res.pred;
      out.alt_pred_out = pred_sel_out.pred_res.alt_pred;
      out.pcpn_out = static_cast<uint8_t>(pred_sel_out.pred_res.pcpn);
      out.altpcpn_out = static_cast<uint8_t>(pred_sel_out.pred_res.altpcpn);
      for (int i = 0; i < TN_MAX; ++i) {
        out.tage_tag_flat_out[i] = pred_sel_out.pred_res.index_info.tag[i];
        out.tage_idx_flat_out[i] = pred_sel_out.pred_res.index_info.tage_index[i];
      }
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
                           rd.state_in.reset_cnt_reg},
          update_out);
      const UpdateRequest &upd_req = update_out.req;

      req.reset_cnt_reg_we = true;
      req.reset_cnt_reg_next = rd.state_in.reset_cnt_reg + 1;

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
      out.tage_update_done = true;
    }

    out.busy = false;
    req.out_regs = out;
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

    sram_delay_active = req.sram_delay_active_next;
    sram_delay_counter = req.sram_delay_counter_next;
    sram_delayed_data = req.sram_delayed_data_next;
    sram_prng_state = req.sram_prng_state_next;
    sram_new_req_this_cycle = false;

    state = req.next_state;
  }

  // ------------------------------------------------------------------------
  // 兼容接口
  // ------------------------------------------------------------------------
  CombResult step_pipeline(const InputPayload &inp) {
    ReadData rd;
    TageCombOut comb_out{};
    tage_seq_read(inp, rd);
    tage_comb(TageCombIn{inp, rd}, comb_out);
    return comb_out.req;
  }

  void step_seq(bool rst_n, const InputPayload &inp, const CombResult &comb) {
    tage_seq_write(inp, comb, rst_n);
  }

  OutputPayload step(bool rst_n, const InputPayload &inp) {
    if (rst_n) {
      reset();
      DEBUG_LOG("[TAGE_TOP] reset\n");
      OutputPayload out_reg_reset;
      std::memset(&out_reg_reset, 0, sizeof(OutputPayload));
      return out_reg_reset;
    }
    ReadData rd;
    TageCombOut comb_out{};
    tage_seq_read(inp, rd);
    tage_comb(TageCombIn{inp, rd}, comb_out);
    tage_seq_write(inp, comb_out.req, false);
    return comb_out.out_regs;
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
        res.pred = res.alt_pred;
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
      const bool alt_used = (pred_dir == pred_res.alt_pred);
      uint8_t new_u = read_vals.useful[pcpn];
      bool should_write_useful = false;
      if (!alt_used) {
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
          for (int i = pcpn + 1; i < TN_MAX; i++) {
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
      bool sc_train_en = (provider_pred_raw != alt_pred_raw);
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
