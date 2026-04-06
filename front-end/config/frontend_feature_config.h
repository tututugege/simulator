#include "config.h"
#ifndef FRONTEND_FEATURE_CONFIG_H
#define FRONTEND_FEATURE_CONFIG_H

// SRAM delay configs
// #define SRAM_DELAY_ENABLE  // if not defined, no SRAM delay, back to Register
#ifndef SRAM_DELAY_MIN
#define SRAM_DELAY_MIN 0 // n+1
#endif
#ifndef SRAM_DELAY_MAX
#define SRAM_DELAY_MAX 0 // n+1
#endif

// BPU top-level switches
#define SPECULATIVE_ON
// 统一使用全局 config.h 中的 constexpr 宽度配置，避免前后端不一致。
static_assert(FETCH_WIDTH > 0, "FETCH_WIDTH must be positive");
static_assert(COMMIT_WIDTH > 0, "COMMIT_WIDTH must be positive");
#ifndef BPU_BANK_NUM
#define BPU_BANK_NUM FETCH_WIDTH
#endif

// RAS feature switch:
// - keep this macro defined   => enable RAS prediction for BR_RET
// - comment out this macro    => disable RAS and fallback to BTB target
#define ENABLE_BPU_RAS
#ifndef RAS_DEPTH
#define RAS_DEPTH 64
#endif

#ifndef BPU_TYPE_ENTRY_NUM
#define BPU_TYPE_ENTRY_NUM 4096
#endif
#ifndef TYPE_PRED_ENTRY_NUM
#define TYPE_PRED_ENTRY_NUM BPU_TYPE_ENTRY_NUM
#endif
#ifndef TYPE_PRED_WAY_NUM
#define TYPE_PRED_WAY_NUM 2
#endif
#ifndef TYPE_PRED_TAG_WIDTH
#define TYPE_PRED_TAG_WIDTH 12
#endif
#ifndef TYPE_PRED_CONF_BITS
#define TYPE_PRED_CONF_BITS 2
#endif
#ifndef TYPE_PRED_AGE_BITS
#define TYPE_PRED_AGE_BITS 2
#endif
#ifndef Q_DEPTH
#define Q_DEPTH 500 // UQ depth
#endif

// TAGE configs
#ifndef BASE_ENTRY_NUM
#define BASE_ENTRY_NUM 2048
#endif
#ifndef GHR_LENGTH
#define GHR_LENGTH 512
#endif
#ifndef TN_MAX
#define TN_MAX 4
#endif
#ifndef TN_ENTRY_NUM
#define TN_ENTRY_NUM 4096
#endif
#ifndef FH_N_MAX
#define FH_N_MAX 3
#endif
#ifndef TAGE_BASE_IDX_WIDTH
#define TAGE_BASE_IDX_WIDTH clog2(BASE_ENTRY_NUM)
#endif
#ifndef TAGE_SHORT_TAG_WIDTH
#define TAGE_SHORT_TAG_WIDTH 10
#endif
#ifndef TAGE_LONG_TAG_WIDTH
#define TAGE_LONG_TAG_WIDTH 12
#endif
#ifndef TAGE_SHORT_TAG_TABLE_NUM
#define TAGE_SHORT_TAG_TABLE_NUM 4
#endif
#ifndef TAGE_TAG_WIDTH
#define TAGE_TAG_WIDTH 8
#endif
#ifndef TAGE_IDX_WIDTH
#define TAGE_IDX_WIDTH clog2(TN_ENTRY_NUM)
#endif
#ifndef ENABLE_TAGE_USE_ALT_ON_NA
#define ENABLE_TAGE_USE_ALT_ON_NA 1
#endif
#ifndef TAGE_USE_ALT_USEFUL_THRESHOLD
#define TAGE_USE_ALT_USEFUL_THRESHOLD 0
#endif
#ifndef TAGE_USE_ALT_CTR_BITS
#define TAGE_USE_ALT_CTR_BITS 4
#endif
#ifndef TAGE_USE_ALT_CTR_INIT
#define TAGE_USE_ALT_CTR_INIT ((1 << (TAGE_USE_ALT_CTR_BITS - 1)) - 1)
#endif
#ifndef TAGE_USE_ALT_CTR_USE_ALT_THRESHOLD
#define TAGE_USE_ALT_CTR_USE_ALT_THRESHOLD (1 << (TAGE_USE_ALT_CTR_BITS - 1))
#endif
#ifndef TAGE_PROVIDER_WEAK_LOW
#define TAGE_PROVIDER_WEAK_LOW 3
#endif
#ifndef TAGE_PROVIDER_WEAK_HIGH
#define TAGE_PROVIDER_WEAK_HIGH 3
#endif
#ifndef ENABLE_TAGE_SC_LITE
#define ENABLE_TAGE_SC_LITE 1
#endif
#ifndef TAGE_SC_ENTRY_NUM
#define TAGE_SC_ENTRY_NUM 1024
#endif
#ifndef TAGE_SC_USE_WEAK_ONLY
#define TAGE_SC_USE_WEAK_ONLY 1
#endif
#ifndef TAGE_SC_STRONG_ONLY_OVERRIDE
#define TAGE_SC_STRONG_ONLY_OVERRIDE 1
#endif
#ifndef TAGE_SC_PROVIDER_WEAK_LOW
#define TAGE_SC_PROVIDER_WEAK_LOW TAGE_PROVIDER_WEAK_LOW
#endif
#ifndef TAGE_SC_PROVIDER_WEAK_HIGH
#define TAGE_SC_PROVIDER_WEAK_HIGH 4
#endif

// ----------------------------------------------------------------------------
// TAGE-SC-L upgrade switches (new; default disabled to preserve baseline)
// ----------------------------------------------------------------------------
#ifndef ENABLE_TAGE_SC_L
#define ENABLE_TAGE_SC_L 1
#endif

#ifndef ENABLE_TAGE_SC_PATH
#define ENABLE_TAGE_SC_PATH 1
#endif

#ifndef TAGE_SC_PATH_BITS
#define TAGE_SC_PATH_BITS 16
#endif

#ifndef ENABLE_TAGE_LOOP_PRED
#define ENABLE_TAGE_LOOP_PRED 1
#endif

#ifndef TAGE_LOOP_ENTRY_NUM
#define TAGE_LOOP_ENTRY_NUM 1024
#endif

#ifndef TAGE_LOOP_TAG_BITS
#define TAGE_LOOP_TAG_BITS 12
#endif

#ifndef TAGE_LOOP_CONF_BITS
#define TAGE_LOOP_CONF_BITS 3
#endif

#ifndef TAGE_LOOP_AGE_BITS
#define TAGE_LOOP_AGE_BITS 3
#endif

#ifndef TAGE_LOOP_ITER_BITS
#define TAGE_LOOP_ITER_BITS 12
#endif

#ifndef TAGE_LOOP_CONF_THRESHOLD
#define TAGE_LOOP_CONF_THRESHOLD 5
#endif

#ifndef TAGE_SC_L_ENTRY_NUM
#define TAGE_SC_L_ENTRY_NUM 1024
#endif

#ifndef TAGE_SC_L_CTR_BITS
#define TAGE_SC_L_CTR_BITS 6
#endif

#ifndef TAGE_SC_L_THETA_INIT
#define TAGE_SC_L_THETA_INIT 18
#endif

#ifndef TAGE_SC_L_THETA_MIN
#define TAGE_SC_L_THETA_MIN 6
#endif

#ifndef TAGE_SC_L_THETA_MAX
#define TAGE_SC_L_THETA_MAX 64
#endif

#ifndef TAGE_SC_L_OVERRIDE_MARGIN
#define TAGE_SC_L_OVERRIDE_MARGIN 4
#endif

// NOTE: metadata plumbing sizes are defined in `include/config.h` as
// `BPU_SCL_META_*` / `BPU_LOOP_META_*` to keep front/back consistent.

// BTB configs
#ifndef BTB_ENTRY_NUM
#define BTB_ENTRY_NUM 1024
#endif
#ifndef ENABLE_BTB_ALIAS_HASH
#define ENABLE_BTB_ALIAS_HASH 1
#endif
#ifndef BTB_TAG_LEN
#define BTB_TAG_LEN 8
#endif
#ifndef BTB_WAY_NUM
#define BTB_WAY_NUM 4
#endif
#ifndef BTB_TYPE_ENTRY_NUM
#define BTB_TYPE_ENTRY_NUM 4096
#endif
#ifndef BHT_ENTRY_NUM
#define BHT_ENTRY_NUM 2048
#endif
#ifndef TC_ENTRY_NUM
#define TC_ENTRY_NUM 2048
#endif
#ifndef TC_WAY_NUM
#define TC_WAY_NUM 2
#endif
#ifndef TC_TAG_LEN
#define TC_TAG_LEN 10
#endif
#ifndef ENABLE_INDIRECT_BTB_FALLBACK
#define ENABLE_INDIRECT_BTB_FALLBACK 1
#endif
#ifndef ENABLE_INDIRECT_BTB_TRAIN
#define ENABLE_INDIRECT_BTB_TRAIN 1
#endif
#ifndef INDIRECT_BTB_INIT_USEFUL
#define INDIRECT_BTB_INIT_USEFUL 0
#endif
#ifndef INDIRECT_TC_INIT_USEFUL
#define INDIRECT_TC_INIT_USEFUL 1
#endif
#ifndef ENABLE_TC_TARGET_SIGNATURE
#define ENABLE_TC_TARGET_SIGNATURE 1
#endif

// 2-Ahead Predictor configs
#ifndef TWO_AHEAD_TABLE_SIZE
#define TWO_AHEAD_TABLE_SIZE 4096
#endif
#ifndef NLP_TABLE_SIZE
#define NLP_TABLE_SIZE TWO_AHEAD_TABLE_SIZE
#endif
#ifndef NLP_CONF_BITS
#define NLP_CONF_BITS 2
#endif
#ifndef NLP_CONF_THRESHOLD
#define NLP_CONF_THRESHOLD 2
#endif
#ifndef NLP_CONF_INIT
#define NLP_CONF_INIT 1
#endif

// 2-Ahead slot1 predictor
#ifndef ENABLE_2AHEAD_SLOT1_PRED
#define ENABLE_2AHEAD_SLOT1_PRED 1
#endif
#ifndef AHEAD_SLOT1_TABLE_SIZE
#define AHEAD_SLOT1_TABLE_SIZE TWO_AHEAD_TABLE_SIZE
#endif
#ifndef AHEAD_SLOT1_CONF_BITS
#define AHEAD_SLOT1_CONF_BITS 2
#endif
#ifndef AHEAD_SLOT1_CONF_THRESHOLD
#define AHEAD_SLOT1_CONF_THRESHOLD 2
#endif
#ifndef ENABLE_2AHEAD_SLOT1_ADAPTIVE_GATING
#define ENABLE_2AHEAD_SLOT1_ADAPTIVE_GATING 1
#endif
#ifndef AHEAD_GATE_WINDOW
#define AHEAD_GATE_WINDOW 512
#endif
#ifndef AHEAD_GATE_DISABLE_THRESHOLD
#define AHEAD_GATE_DISABLE_THRESHOLD 35
#endif
#ifndef AHEAD_GATE_ENABLE_THRESHOLD
#define AHEAD_GATE_ENABLE_THRESHOLD 60
#endif

// Front-end feature switches
#define FRONTEND_DISABLE_2AHEAD
#ifndef FRONTEND_DISABLE_2AHEAD
#ifndef ENABLE_2AHEAD
#define ENABLE_2AHEAD
#endif
#endif

/* Front-end icache mode switch:
 * 0 = use true icache
 * 1 = use ideal icache
 */
#ifndef FRONTEND_ICACHE_MODE
#define FRONTEND_ICACHE_MODE 0
#endif

/* Front-end dual request switch for ideal icache (1: on, 0: off) */
#ifndef ENABLE_FRONTEND_IDEAL_ICACHE_DUAL_REQ
#define ENABLE_FRONTEND_IDEAL_ICACHE_DUAL_REQ 0
#endif

/* Derive legacy USE_TRUE_ICACHE / USE_IDEAL_ICACHE macros from mode switch */
#if FRONTEND_ICACHE_MODE == 1
#ifdef USE_TRUE_ICACHE
#undef USE_TRUE_ICACHE
#endif
#ifndef USE_IDEAL_ICACHE
#define USE_IDEAL_ICACHE
#endif
#elif FRONTEND_ICACHE_MODE == 0
#ifdef USE_IDEAL_ICACHE
#undef USE_IDEAL_ICACHE
#endif
#ifndef USE_TRUE_ICACHE
#define USE_TRUE_ICACHE
#endif
#else
#error "FRONTEND_ICACHE_MODE must be 0 (true) or 1 (ideal)"
#endif

/* Dual request only active under ideal icache mode */
#if (FRONTEND_ICACHE_MODE == 1) && ENABLE_FRONTEND_IDEAL_ICACHE_DUAL_REQ
#define FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE 1
#else
#define FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE 0
#endif

// 统一使用全局 config.h 中的 ICACHE_LINE_SIZE。
// 真实 icache miss 延迟由 shared AXI / SimDDR 配置建模。

// FIFO sizes
#ifndef INSTRUCTION_FIFO_SIZE
#define INSTRUCTION_FIFO_SIZE 32
#endif
#ifndef PTAB_SIZE
#define PTAB_SIZE 32
#endif
#ifndef FETCH_ADDR_FIFO_SIZE
#define FETCH_ADDR_FIFO_SIZE 32
#endif
#ifndef FRONT2BACK_FIFO_SIZE
#define FRONT2BACK_FIFO_SIZE 64
#endif

#endif
