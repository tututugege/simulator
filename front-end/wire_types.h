#ifndef FRONTEND_WIRE_TYPES_H
#define FRONTEND_WIRE_TYPES_H

#include <cstdint>

#include "config.h"
#include "BPU/BPU_configs.h"

constexpr int ceil_log2_u32(uint32_t value) {
  int width = 0;
  uint32_t threshold = 1;
  while (threshold < value) {
    threshold <<= 1;
    ++width;
  }
  return width;
}

using wire1_t = bool;
static constexpr int wire1_t_BITS = 1;
using wire2_t = uint8_t;
static constexpr int wire2_t_BITS = 2;
using wire3_t = uint8_t;
static constexpr int wire3_t_BITS = 3;
using wire4_t = uint8_t;
static constexpr int wire4_t_BITS = 4;
using wire5_t = uint8_t;
static constexpr int wire5_t_BITS = 5;
using wire6_t = uint8_t;
static constexpr int wire6_t_BITS = 6;
using wire7_t = uint8_t;
static constexpr int wire7_t_BITS = 7;
using wire8_t = uint8_t;
static constexpr int wire8_t_BITS = 8;
using wire9_t = uint16_t;
static constexpr int wire9_t_BITS = 9;
using wire10_t = uint16_t;
static constexpr int wire10_t_BITS = 10;
using wire11_t = uint16_t;
static constexpr int wire11_t_BITS = 11;
using wire12_t = uint16_t;
static constexpr int wire12_t_BITS = 12;
using wire13_t = uint16_t;
static constexpr int wire13_t_BITS = 13;
using wire14_t = uint16_t;
static constexpr int wire14_t_BITS = 14;
using wire15_t = uint16_t;
static constexpr int wire15_t_BITS = 15;
using wire16_t = uint16_t;
static constexpr int wire16_t_BITS = 16;
using wire30_t = uint32_t;
static constexpr int wire30_t_BITS = 30;
using wire32_t = uint32_t;
static constexpr int wire32_t_BITS = 32;

template <int Bits>
struct wire_for_bits;

template <>
struct wire_for_bits<1> {
  using type = wire1_t;
};
template <>
struct wire_for_bits<2> {
  using type = wire2_t;
};
template <>
struct wire_for_bits<3> {
  using type = wire3_t;
};
template <>
struct wire_for_bits<4> {
  using type = wire4_t;
};
template <>
struct wire_for_bits<5> {
  using type = wire5_t;
};
template <>
struct wire_for_bits<6> {
  using type = wire6_t;
};
template <>
struct wire_for_bits<7> {
  using type = wire7_t;
};
template <>
struct wire_for_bits<8> {
  using type = wire8_t;
};
template <>
struct wire_for_bits<9> {
  using type = wire9_t;
};
template <>
struct wire_for_bits<10> {
  using type = wire10_t;
};
template <>
struct wire_for_bits<11> {
  using type = wire11_t;
};
template <>
struct wire_for_bits<12> {
  using type = wire12_t;
};
template <>
struct wire_for_bits<13> {
  using type = wire13_t;
};
template <>
struct wire_for_bits<14> {
  using type = wire14_t;
};
template <>
struct wire_for_bits<15> {
  using type = wire15_t;
};
template <>
struct wire_for_bits<16> {
  using type = wire16_t;
};
template <>
struct wire_for_bits<30> {
  using type = wire30_t;
};
template <>
struct wire_for_bits<32> {
  using type = wire32_t;
};

template <int Bits>
using wire_for_bits_t = typename wire_for_bits<Bits>::type;

using br_type_t = wire3_t;
static constexpr int br_type_t_BITS = 3;
using pcpn_t = wire3_t;
static constexpr int pcpn_t_BITS = 3;
using predecode_type_t = wire2_t;
static constexpr int predecode_type_t_BITS = 2;

using tage_tag_t = wire_for_bits_t<TAGE_TAG_WIDTH>;
static constexpr int tage_tag_t_BITS = TAGE_TAG_WIDTH;
using tage_idx_t = wire_for_bits_t<TAGE_IDX_WIDTH>;
static constexpr int tage_idx_t_BITS = TAGE_IDX_WIDTH;

using btb_tag_t = wire_for_bits_t<BTB_TAG_LEN>;
static constexpr int btb_tag_t_BITS = BTB_TAG_LEN;
using btb_idx_t = wire_for_bits_t<ceil_log2_u32(BTB_ENTRY_NUM)>;
static constexpr int btb_idx_t_BITS = ceil_log2_u32(BTB_ENTRY_NUM);
using btb_type_idx_t = wire_for_bits_t<ceil_log2_u32(BTB_TYPE_ENTRY_NUM)>;
static constexpr int btb_type_idx_t_BITS = ceil_log2_u32(BTB_TYPE_ENTRY_NUM);
using bht_idx_t = wire_for_bits_t<ceil_log2_u32(BHT_ENTRY_NUM)>;
static constexpr int bht_idx_t_BITS = ceil_log2_u32(BHT_ENTRY_NUM);
using tc_idx_t = wire_for_bits_t<ceil_log2_u32(TC_ENTRY_NUM)>;
static constexpr int tc_idx_t_BITS = ceil_log2_u32(TC_ENTRY_NUM);
using bht_hist_t = wire_for_bits_t<ceil_log2_u32(BHT_ENTRY_NUM)>;
static constexpr int bht_hist_t_BITS = ceil_log2_u32(BHT_ENTRY_NUM);
using tc_tag_t = wire_for_bits_t<TC_TAG_LEN>;
static constexpr int tc_tag_t_BITS = TC_TAG_LEN;
using btb_way_sel_t = wire_for_bits_t<ceil_log2_u32(BTB_WAY_NUM)>;
static constexpr int btb_way_sel_t_BITS = ceil_log2_u32(BTB_WAY_NUM);
using tc_way_sel_t = wire_for_bits_t<ceil_log2_u32(TC_WAY_NUM)>;
static constexpr int tc_way_sel_t_BITS = ceil_log2_u32(TC_WAY_NUM);
using tage_state_t = wire2_t;
static constexpr int tage_state_t_BITS = 2;
using btb_state_t = wire2_t;
static constexpr int btb_state_t_BITS = 2;
using bpu_state_t = wire2_t;
static constexpr int bpu_state_t_BITS = 2;
using tage_base_idx_t = wire_for_bits_t<TAGE_BASE_IDX_WIDTH>;
static constexpr int tage_base_idx_t_BITS = TAGE_BASE_IDX_WIDTH;
using tage_cnt_t = wire3_t;
static constexpr int tage_cnt_t_BITS = 3;
using tage_useful_t = wire2_t;
static constexpr int tage_useful_t_BITS = 2;
using tage_base_cnt_t = wire2_t;
static constexpr int tage_base_cnt_t_BITS = 2;
using tage_sc_ctr_t = wire2_t;
static constexpr int tage_sc_ctr_t_BITS = 2;
using tage_sc_idx_t = wire_for_bits_t<ceil_log2_u32(TAGE_SC_ENTRY_NUM)>;
static constexpr int tage_sc_idx_t_BITS = ceil_log2_u32(TAGE_SC_ENTRY_NUM);
using tage_reset_ctr_t = wire32_t;
static constexpr int tage_reset_ctr_t_BITS = TAGE_IDX_WIDTH + 11;
using tage_lsfr_rand_t = wire2_t;
static constexpr int tage_lsfr_rand_t_BITS = 2;

using tage_use_alt_ctr_t = wire_for_bits_t<TAGE_USE_ALT_CTR_BITS>;
static constexpr int tage_use_alt_ctr_t_BITS = TAGE_USE_ALT_CTR_BITS;

using tage_scl_ctr_t = wire_for_bits_t<TAGE_SC_L_CTR_BITS>;
static constexpr int tage_scl_ctr_t_BITS = TAGE_SC_L_CTR_BITS;

using tage_scl_theta_t = wire16_t;
static constexpr int tage_scl_theta_t_BITS = 16;

// -----------------------------------------------------------------------------
// TAGE-SC-L metadata plumbing types (predict -> FTQ -> commit -> BPU)
// -----------------------------------------------------------------------------
using tage_scl_meta_idx_t = wire_for_bits_t<BPU_SCL_META_IDX_BITS>;
static constexpr int tage_scl_meta_idx_t_BITS = BPU_SCL_META_IDX_BITS;
using tage_scl_meta_sum_t = wire16_t;
static constexpr int tage_scl_meta_sum_t_BITS = 16;

using tage_loop_meta_idx_t = wire_for_bits_t<BPU_LOOP_META_IDX_BITS>;
static constexpr int tage_loop_meta_idx_t_BITS = BPU_LOOP_META_IDX_BITS;
using tage_loop_meta_tag_t = wire_for_bits_t<BPU_LOOP_META_TAG_BITS>;
static constexpr int tage_loop_meta_tag_t_BITS = BPU_LOOP_META_TAG_BITS;
using tage_loop_tag_t = wire_for_bits_t<TAGE_LOOP_TAG_BITS>;
static constexpr int tage_loop_tag_t_BITS = TAGE_LOOP_TAG_BITS;
using tage_loop_iter_t = wire_for_bits_t<TAGE_LOOP_ITER_BITS>;
static constexpr int tage_loop_iter_t_BITS = TAGE_LOOP_ITER_BITS;
using tage_loop_conf_t = wire_for_bits_t<TAGE_LOOP_CONF_BITS>;
static constexpr int tage_loop_conf_t_BITS = TAGE_LOOP_CONF_BITS;
using tage_loop_age_t = wire_for_bits_t<TAGE_LOOP_AGE_BITS>;
static constexpr int tage_loop_age_t_BITS = TAGE_LOOP_AGE_BITS;
using tage_path_hist_t = wire32_t;
static constexpr int tage_path_hist_t_BITS = TAGE_SC_PATH_BITS;
using bpu_type_idx_t = wire_for_bits_t<ceil_log2_u32(BPU_TYPE_ENTRY_NUM)>;
static constexpr int bpu_type_idx_t_BITS = ceil_log2_u32(BPU_TYPE_ENTRY_NUM);
using type_pred_set_idx_t = wire_for_bits_t<ceil_log2_u32(TYPE_PRED_SET_NUM)>;
static constexpr int type_pred_set_idx_t_BITS = ceil_log2_u32(TYPE_PRED_SET_NUM);
using type_pred_tag_t = wire_for_bits_t<TYPE_PRED_TAG_WIDTH>;
static constexpr int type_pred_tag_t_BITS = TYPE_PRED_TAG_WIDTH;
using type_pred_way_t = wire_for_bits_t<ceil_log2_u32(TYPE_PRED_WAY_NUM)>;
static constexpr int type_pred_way_t_BITS = ceil_log2_u32(TYPE_PRED_WAY_NUM);
using type_pred_conf_t = wire_for_bits_t<TYPE_PRED_CONF_BITS>;
static constexpr int type_pred_conf_t_BITS = TYPE_PRED_CONF_BITS;
using type_pred_age_t = wire_for_bits_t<TYPE_PRED_AGE_BITS>;
static constexpr int type_pred_age_t_BITS = TYPE_PRED_AGE_BITS;
using fetch_addr_t = wire32_t;
static constexpr int fetch_addr_t_BITS = 32;
using pc_t = wire32_t;
static constexpr int pc_t_BITS = 32;
using inst_word_t = wire32_t;
static constexpr int inst_word_t_BITS = 32;
using target_addr_t = wire32_t;
static constexpr int target_addr_t_BITS = 32;
using nlp_conf_t = wire_for_bits_t<NLP_CONF_BITS>;
static constexpr int nlp_conf_t_BITS = NLP_CONF_BITS;
using nlp_index_t = wire_for_bits_t<ceil_log2_u32(NLP_TABLE_SIZE)>;
static constexpr int nlp_index_t_BITS = ceil_log2_u32(NLP_TABLE_SIZE);
using nlp_tag_t = wire30_t;
static constexpr int nlp_tag_t_BITS = 30;
using bpu_bank_sel_t = wire_for_bits_t<ceil_log2_u32(BPU_BANK_NUM)>;
static constexpr int bpu_bank_sel_t_BITS = ceil_log2_u32(BPU_BANK_NUM);
using queue_ptr_t = wire_for_bits_t<ceil_log2_u32(Q_DEPTH)>;
static constexpr int queue_ptr_t_BITS = ceil_log2_u32(Q_DEPTH);
using queue_count_t = wire_for_bits_t<ceil_log2_u32(Q_DEPTH + 1)>;
static constexpr int queue_count_t_BITS = ceil_log2_u32(Q_DEPTH + 1);
using ras_index_t = wire_for_bits_t<ceil_log2_u32(RAS_DEPTH)>;
static constexpr int ras_index_t_BITS = ceil_log2_u32(RAS_DEPTH);
using ras_count_t = wire_for_bits_t<ceil_log2_u32(RAS_DEPTH + 1)>;
static constexpr int ras_count_t_BITS = ceil_log2_u32(RAS_DEPTH + 1);
using fetch_addr_fifo_size_t = wire_for_bits_t<ceil_log2_u32(FETCH_ADDR_FIFO_SIZE + 1)>;
static constexpr int fetch_addr_fifo_size_t_BITS = ceil_log2_u32(FETCH_ADDR_FIFO_SIZE + 1);
using instruction_fifo_size_t =
    wire_for_bits_t<ceil_log2_u32(INSTRUCTION_FIFO_SIZE + 1)>;
static constexpr int instruction_fifo_size_t_BITS =
    ceil_log2_u32(INSTRUCTION_FIFO_SIZE + 1);
using ptab_size_t = wire_for_bits_t<ceil_log2_u32(PTAB_SIZE + 1)>;
static constexpr int ptab_size_t_BITS = ceil_log2_u32(PTAB_SIZE + 1);
using front2back_fifo_size_t =
    wire_for_bits_t<ceil_log2_u32(FRONT2BACK_FIFO_SIZE + 1)>;
static constexpr int front2back_fifo_size_t_BITS =
    ceil_log2_u32(FRONT2BACK_FIFO_SIZE + 1);
using bpu_bank_sel_ext_t = wire_for_bits_t<ceil_log2_u32(BPU_BANK_NUM + 1)>;
static constexpr int bpu_bank_sel_ext_t_BITS = ceil_log2_u32(BPU_BANK_NUM + 1);
static constexpr bpu_bank_sel_ext_t BPU_BANK_SEL_INVALID = BPU_BANK_NUM;

#endif
