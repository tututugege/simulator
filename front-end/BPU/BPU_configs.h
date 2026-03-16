#ifndef BPU_CONFIGS_H
#define BPU_CONFIGS_H

#include "../config/frontend_feature_config.h"

#define BPU_TYPE_IDX_MASK (BPU_TYPE_ENTRY_NUM - 1)

#if (TAGE_SC_ENTRY_NUM & (TAGE_SC_ENTRY_NUM - 1)) != 0
#error "TAGE_SC_ENTRY_NUM must be power of two"
#endif
#define TAGE_SC_IDX_MASK (TAGE_SC_ENTRY_NUM - 1)

#if (TAGE_SC_L_ENTRY_NUM & (TAGE_SC_L_ENTRY_NUM - 1)) != 0
#error "TAGE_SC_L_ENTRY_NUM must be power of two"
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

#define TAGE_TAG_MASK ((1 << TAGE_TAG_WIDTH) - 1)
#define TAGE_IDX_MASK ((1 << TAGE_IDX_WIDTH) - 1)
#define TAGE_BASE_IDX_MASK ((1 << TAGE_BASE_IDX_WIDTH) - 1)

#if TC_TAG_LEN <= 0 || TC_TAG_LEN > 31
#error "TC_TAG_LEN must be in [1, 31]"
#endif

#if TC_WAY_NUM <= 0
#error "TC_WAY_NUM must be > 0"
#endif

#if INDIRECT_BTB_INIT_USEFUL < 0 || INDIRECT_BTB_INIT_USEFUL > 7
#error "INDIRECT_BTB_INIT_USEFUL must be in [0, 7]"
#endif

#if INDIRECT_TC_INIT_USEFUL < 0 || INDIRECT_TC_INIT_USEFUL > 7
#error "INDIRECT_TC_INIT_USEFUL must be in [0, 7]"
#endif

#define BTB_IDX_LEN     9     // log2(512)
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
