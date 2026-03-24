#ifndef BPU_CONFIGS_H
#define BPU_CONFIGS_H

#include "../config/frontend_feature_config.h"

static_assert(BPU_BANK_NUM >= FETCH_WIDTH,
              "BPU_BANK_NUM must be >= FETCH_WIDTH");
static_assert(BPU_BANK_NUM >= COMMIT_WIDTH,
              "BPU_BANK_NUM must be >= COMMIT_WIDTH");
static_assert(TN_MAX == 4,
              "TN_MAX is fixed to 4 by design; do not change TAGE history level count");
static_assert(BPU_BANK_NUM > 0, "BPU_BANK_NUM must be > 0");
static_assert(BPU_TYPE_ENTRY_NUM > 0, "BPU_TYPE_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(BPU_TYPE_ENTRY_NUM),
              "BPU_TYPE_ENTRY_NUM must be power of two");
static_assert(BPU_TYPE_ENTRY_NUM <= (1u << 16),
              "BPU_TYPE_ENTRY_NUM too large for current wire_for_bits_t coverage");
static_assert(BASE_ENTRY_NUM > 0, "BASE_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(BASE_ENTRY_NUM),
              "BASE_ENTRY_NUM must be power of two");
static_assert(TN_ENTRY_NUM > 0, "TN_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(TN_ENTRY_NUM),
              "TN_ENTRY_NUM must be power of two");
static_assert(TAGE_BASE_IDX_WIDTH == clog2(BASE_ENTRY_NUM),
              "TAGE_BASE_IDX_WIDTH must match BASE_ENTRY_NUM");
static_assert(TAGE_IDX_WIDTH == clog2(TN_ENTRY_NUM),
              "TAGE_IDX_WIDTH must match TN_ENTRY_NUM");
static_assert(TAGE_BASE_IDX_WIDTH > 0 && TAGE_BASE_IDX_WIDTH <= 16,
              "TAGE_BASE_IDX_WIDTH must be in [1, 16]");
static_assert(TAGE_IDX_WIDTH > 0 && TAGE_IDX_WIDTH <= 16,
              "TAGE_IDX_WIDTH must be in [1, 16] with current tage_idx_t typing");

#if TAGE_TAG_WIDTH <= 0 || TAGE_TAG_WIDTH > 16
#error "TAGE_TAG_WIDTH must be in [1, 16] with current tage_tag_t typing"
#endif

#if TAGE_USE_ALT_CTR_BITS <= 1 || TAGE_USE_ALT_CTR_BITS > 8
#error "TAGE_USE_ALT_CTR_BITS must be in [2, 8]"
#endif

#if GHR_LENGTH <= 0
#error "GHR_LENGTH must be > 0"
#endif

#define BPU_TYPE_IDX_MASK (BPU_TYPE_ENTRY_NUM - 1)

#if TYPE_PRED_WAY_NUM <= 0
#error "TYPE_PRED_WAY_NUM must be > 0"
#endif

#if TYPE_PRED_WAY_NUM < 2
#error "TYPE_PRED_WAY_NUM must be >= 2 with current way-index typing assumptions"
#endif

#if TYPE_PRED_WAY_NUM > (1 << 16)
#error "TYPE_PRED_WAY_NUM too large for current wire_for_bits_t coverage"
#endif

#if (TYPE_PRED_ENTRY_NUM % TYPE_PRED_WAY_NUM) != 0
#error "TYPE_PRED_ENTRY_NUM must be divisible by TYPE_PRED_WAY_NUM"
#endif

#if TYPE_PRED_ENTRY_NUM <= 0 || (TYPE_PRED_ENTRY_NUM & (TYPE_PRED_ENTRY_NUM - 1)) != 0
#error "TYPE_PRED_ENTRY_NUM must be power of two and > 0"
#endif

#define TYPE_PRED_SET_NUM (TYPE_PRED_ENTRY_NUM / TYPE_PRED_WAY_NUM)

#if (TYPE_PRED_SET_NUM & (TYPE_PRED_SET_NUM - 1)) != 0
#error "TYPE_PRED_SET_NUM must be power of two"
#endif

#if TYPE_PRED_SET_NUM < 2
#error "TYPE_PRED_SET_NUM must be >= 2 with current set-index typing assumptions"
#endif

#if TYPE_PRED_SET_NUM > (1 << 16)
#error "TYPE_PRED_SET_NUM too large for current wire_for_bits_t coverage"
#endif

#if TYPE_PRED_TAG_WIDTH <= 0 || TYPE_PRED_TAG_WIDTH > 16
#error "TYPE_PRED_TAG_WIDTH must be in [1, 16] with current wire_for_bits_t coverage"
#endif

#if TYPE_PRED_CONF_BITS <= 0 || TYPE_PRED_CONF_BITS > 8
#error "TYPE_PRED_CONF_BITS must be in [1, 8]"
#endif

#if TYPE_PRED_AGE_BITS <= 0 || TYPE_PRED_AGE_BITS > 8
#error "TYPE_PRED_AGE_BITS must be in [1, 8]"
#endif

#define TYPE_PRED_SET_MASK (TYPE_PRED_SET_NUM - 1)
#define TYPE_PRED_CONF_MAX ((1 << TYPE_PRED_CONF_BITS) - 1)
#define TYPE_PRED_AGE_MAX ((1 << TYPE_PRED_AGE_BITS) - 1)
#define TYPE_PRED_TAG_MASK ((1 << TYPE_PRED_TAG_WIDTH) - 1)

#if (TAGE_SC_ENTRY_NUM & (TAGE_SC_ENTRY_NUM - 1)) != 0
#error "TAGE_SC_ENTRY_NUM must be power of two"
#endif

#if TAGE_SC_ENTRY_NUM <= 0
#error "TAGE_SC_ENTRY_NUM must be > 0"
#endif

#define TAGE_SC_IDX_MASK (TAGE_SC_ENTRY_NUM - 1)

#if (TAGE_SC_L_ENTRY_NUM & (TAGE_SC_L_ENTRY_NUM - 1)) != 0
#error "TAGE_SC_L_ENTRY_NUM must be power of two"
#endif

#if TAGE_SC_L_ENTRY_NUM <= 0
#error "TAGE_SC_L_ENTRY_NUM must be > 0"
#endif

#if TAGE_SC_L_CTR_BITS <= 1 || TAGE_SC_L_CTR_BITS > 8
#error "TAGE_SC_L_CTR_BITS must be in [2, 8]"
#endif

#if TAGE_SC_PATH_BITS <= 0 || TAGE_SC_PATH_BITS > 32
#error "TAGE_SC_PATH_BITS must be in [1, 32]"
#endif

#if TAGE_SC_PATH_BITS == 32
#define TAGE_SC_PATH_MASK 0xffffffffu
#else
#define TAGE_SC_PATH_MASK ((1u << TAGE_SC_PATH_BITS) - 1u)
#endif

#if (TAGE_LOOP_ENTRY_NUM & (TAGE_LOOP_ENTRY_NUM - 1)) != 0
#error "TAGE_LOOP_ENTRY_NUM must be power of two"
#endif

#if TAGE_LOOP_TAG_BITS <= 0 || TAGE_LOOP_TAG_BITS > 16
#error "TAGE_LOOP_TAG_BITS must be in [1, 16]"
#endif

#if TAGE_LOOP_CONF_BITS <= 0 || TAGE_LOOP_CONF_BITS > 8
#error "TAGE_LOOP_CONF_BITS must be in [1, 8]"
#endif

#if TAGE_LOOP_AGE_BITS <= 0 || TAGE_LOOP_AGE_BITS > 8
#error "TAGE_LOOP_AGE_BITS must be in [1, 8]"
#endif

#if TAGE_LOOP_ITER_BITS <= 0 || TAGE_LOOP_ITER_BITS > 16
#error "TAGE_LOOP_ITER_BITS must be in [1, 16]"
#endif

#if BTB_ENTRY_NUM <= 0 || (BTB_ENTRY_NUM & (BTB_ENTRY_NUM - 1)) != 0
#error "BTB_ENTRY_NUM must be power of two and > 0"
#endif

#if BTB_ENTRY_NUM > (1 << 16)
#error "BTB_ENTRY_NUM too large for current wire_for_bits_t coverage"
#endif

#if BTB_TAG_LEN <= 0 || BTB_TAG_LEN > 16
#error "BTB_TAG_LEN must be in [1, 16] with current btb_tag_t typing"
#endif

#if BTB_WAY_NUM <= 0
#error "BTB_WAY_NUM must be > 0"
#endif

#if BTB_WAY_NUM < 2
#error "BTB_WAY_NUM must be >= 2 with current way-index typing assumptions"
#endif

#if BTB_WAY_NUM > (1 << 16)
#error "BTB_WAY_NUM too large for current wire_for_bits_t coverage"
#endif

static_assert(BTB_TYPE_ENTRY_NUM > 0, "BTB_TYPE_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(BTB_TYPE_ENTRY_NUM),
              "BTB_TYPE_ENTRY_NUM must be power of two");
static_assert(BTB_TYPE_ENTRY_NUM <= (1u << 16),
              "BTB_TYPE_ENTRY_NUM too large for current btb_type_idx_t typing");
static_assert(BHT_ENTRY_NUM > 0, "BHT_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(BHT_ENTRY_NUM),
              "BHT_ENTRY_NUM must be power of two");
static_assert(BHT_ENTRY_NUM <= (1u << 16),
              "BHT_ENTRY_NUM too large for current bht_idx_t/bht_hist_t typing");
static_assert(TC_ENTRY_NUM > 0, "TC_ENTRY_NUM must be > 0");
static_assert(is_power_of_two_u64(TC_ENTRY_NUM),
              "TC_ENTRY_NUM must be power of two");
static_assert(TC_ENTRY_NUM <= (1u << 16),
              "TC_ENTRY_NUM too large for current tc_idx_t typing");

#define TAGE_TAG_MASK ((1 << TAGE_TAG_WIDTH) - 1)
#define TAGE_IDX_MASK ((1 << TAGE_IDX_WIDTH) - 1)
#define TAGE_BASE_IDX_MASK ((1 << TAGE_BASE_IDX_WIDTH) - 1)

#if TC_TAG_LEN <= 0 || TC_TAG_LEN > 16
#error "TC_TAG_LEN must be in [1, 16] with current wire_for_bits_t coverage"
#endif

#if TC_WAY_NUM <= 0
#error "TC_WAY_NUM must be > 0"
#endif

#if TC_WAY_NUM < 2
#error "TC_WAY_NUM must be >= 2 with current way-index typing assumptions"
#endif

#if TC_WAY_NUM > (1 << 16)
#error "TC_WAY_NUM too large for current wire_for_bits_t coverage"
#endif

#if INDIRECT_BTB_INIT_USEFUL < 0 || INDIRECT_BTB_INIT_USEFUL > 7
#error "INDIRECT_BTB_INIT_USEFUL must be in [0, 7]"
#endif

#if INDIRECT_TC_INIT_USEFUL < 0 || INDIRECT_TC_INIT_USEFUL > 7
#error "INDIRECT_TC_INIT_USEFUL must be in [0, 7]"
#endif

#define BTB_IDX_MASK    (BTB_ENTRY_NUM - 1)
#define BTB_TAG_MASK    ((1 << BTB_TAG_LEN) - 1)
#define BTB_TYPE_IDX_MASK (BTB_TYPE_ENTRY_NUM - 1)
#define BHT_IDX_MASK    (BHT_ENTRY_NUM - 1)
#define TC_ENTRY_MASK   (TC_ENTRY_NUM - 1)
#define TC_TAG_MASK     ((1 << TC_TAG_LEN) - 1)

// Branch Types
#define BR_DIRECT 0 // only cond now
#define BR_CALL 1
#define BR_RET 2 // can merge...
#define BR_IDIRECT 3
#define BR_NONCTL 4
#define BR_JAL 5

#define NLP_CONF_MAX ((1 << NLP_CONF_BITS) - 1)

#if NLP_CONF_THRESHOLD > NLP_CONF_MAX
#error "NLP_CONF_THRESHOLD must be <= NLP_CONF_MAX"
#endif

#define AHEAD_SLOT1_CONF_MAX ((1 << AHEAD_SLOT1_CONF_BITS) - 1)

#if AHEAD_SLOT1_CONF_THRESHOLD > AHEAD_SLOT1_CONF_MAX
#error "AHEAD_SLOT1_CONF_THRESHOLD must be <= AHEAD_SLOT1_CONF_MAX"
#endif

#if AHEAD_GATE_WINDOW <= 0
#error "AHEAD_GATE_WINDOW must be > 0"
#endif

#if AHEAD_GATE_DISABLE_THRESHOLD < 0 || AHEAD_GATE_DISABLE_THRESHOLD > 100
#error "AHEAD_GATE_DISABLE_THRESHOLD must be in [0, 100]"
#endif

#if AHEAD_GATE_ENABLE_THRESHOLD < 0 || AHEAD_GATE_ENABLE_THRESHOLD > 100
#error "AHEAD_GATE_ENABLE_THRESHOLD must be in [0, 100]"
#endif

#if AHEAD_GATE_DISABLE_THRESHOLD > AHEAD_GATE_ENABLE_THRESHOLD
#error "AHEAD_GATE_DISABLE_THRESHOLD must be <= AHEAD_GATE_ENABLE_THRESHOLD"
#endif

#endif
