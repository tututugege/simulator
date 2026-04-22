#ifndef FRONTEND_TRAIN_IO_H
#define FRONTEND_TRAIN_IO_H

#include "front_module.h"
#include "predecode.h"
#include "predecode_checker.h"
#include "BPU/BPU.h"

// Canonical training IO declarations for top-level pure combinational functions.
// All fields must ultimately resolve to wireX_t aliases via the included headers.

struct FetchAddrCombIn {
  fetch_address_FIFO_in inp;
  fetch_address_FIFO_read_data rd;
};

struct FetchAddrCombOut {
  fetch_address_FIFO_out out_regs;
  wire1_t clear_fifo;
  wire1_t push_en;
  fetch_addr_t push_data;
  wire1_t pop_en;
};

struct InstructionCombIn {
  instruction_FIFO_in inp;
  instruction_FIFO_read_data rd;
};

struct InstructionCombOut {
  instruction_FIFO_out out_regs;
  wire1_t clear_fifo;
  wire1_t push_en;
  instruction_FIFO_entry push_entry;
  wire1_t pop_en;
};

struct PtabCombIn {
  PTAB_in inp;
  PTAB_read_data rd;
};

struct PtabCombOut {
  PTAB_out out_regs;
  wire1_t clear_ptab;
  wire1_t push_write_en;
  PTAB_entry push_write_entry;
  wire1_t push_dummy_en;
  PTAB_entry push_dummy_entry;
  wire1_t pop_en;
};

struct Front2BackCombIn {
  front2back_FIFO_in inp;
  front2back_FIFO_read_data rd;
};

struct Front2BackCombOut {
  front2back_FIFO_out out_regs;
  wire1_t clear_fifo;
  wire1_t push_en;
  front2back_FIFO_entry push_entry;
  wire1_t pop_en;
};

using PredecodeCombIn = predecode_read_data;
using PredecodeCombOut = PredecodeResult;
using PredecodeCheckerCombIn = predecode_checker_read_data;
using PredecodeCheckerCombOut = predecode_checker_out;

using BpuPreReadReqCombIn = BPU_TOP::BpuPreReadReqCombIn;
using BpuPreReadReqCombOut = BPU_TOP::BpuPreReadReqCombOut;
using BpuPostReadReqCombIn = BPU_TOP::BpuPostReadReqCombIn;
using BpuPostReadReqCombOut = BPU_TOP::BpuPostReadReqCombOut;
using BpuSubmoduleBindCombIn = BPU_TOP::BpuSubmoduleBindCombIn;
using BpuSubmoduleBindCombOut = BPU_TOP::BpuSubmoduleBindCombOut;
using BpuPredictMainCombIn = BPU_TOP::BpuPredictMainCombIn;
using BpuPredictMainCombOut = BPU_TOP::BpuPredictMainCombOut;
using BpuNlpCombIn = BPU_TOP::BpuNlpCombIn;
using BpuNlpCombOut = BPU_TOP::BpuNlpCombOut;
using BpuHistCombIn = BPU_TOP::BpuHistCombIn;
using BpuHistCombOut = BPU_TOP::BpuHistCombOut;
using BpuQueueCombIn = BPU_TOP::BpuQueueCombIn;
using BpuQueueCombOut = BPU_TOP::BpuQueueCombOut;
using BpuCombIn = BPU_TOP::BpuCombIn;
using BpuCombOut = BPU_TOP::BpuCombOut;

using TypePredPreReadCombIn = TypePredictor::InputPayload;
using TypePredPreReadCombOut = TypePredictor::PreReadCombOut;
using TypePredCombIn = TypePredictor::TypePredCombIn;
using TypePredCombOut = TypePredictor::TypePredCombOut;

using TagePreReadCombIn = TAGE_TOP::TagePreReadCombIn;
using TagePreReadCombOut = TAGE_TOP::TagePreReadCombOut;
using TageCombIn = TAGE_TOP::TageCombIn;
using TageCombOut = TAGE_TOP::TageCombOut;

using BtbPreReadCombIn = BTB_TOP::BtbPreReadCombIn;
using BtbPreReadCombOut = BTB_TOP::BtbPreReadCombOut;
using BtbCombIn = BTB_TOP::BtbCombIn;
using BtbCombOut = BTB_TOP::BtbCombOut;

struct FrontRuntimeStats {
  uint64_t cycles = 0;
  uint64_t active_cycles = 0;
  uint64_t reset_cycles = 0;
  uint64_t ext_refetch_cycles = 0;
  uint64_t delayed_refetch_cycles = 0;
  uint64_t global_refetch_cycles = 0;

  uint64_t backend_demand_cycles = 0;
  uint64_t backend_deliver_cycles = 0;
  uint64_t backend_bubble_cycles = 0;
  uint64_t delivered_groups = 0;
  uint64_t delivered_insts = 0;

  uint64_t bpu_can_run_cycles = 0;
  uint64_t bpu_stall_cycles = 0;
  uint64_t bpu_stall_fetch_addr_full_cycles = 0;
  uint64_t bpu_stall_ptab_full_cycles = 0;
  uint64_t bpu_issue_cycles = 0;

  uint64_t fetch_addr_read_slot0_cycles = 0;
  uint64_t fetch_addr_read_slot1_cycles = 0;
  uint64_t fetch_addr_write_normal_cycles = 0;
  uint64_t fetch_addr_write_twoahead_cycles = 0;
  uint64_t fetch_addr_write_skip_by_mini_flush_correct_cycles = 0;

  uint64_t icache_req_slot0_cycles = 0;
  uint64_t icache_req_slot1_cycles = 0;
  uint64_t icache_resp_slot0_cycles = 0;
  uint64_t icache_resp_slot1_cycles = 0;
  uint64_t icache_req_fire_cycles = 0;
  uint64_t icache_req_blocked_cycles = 0;
  uint64_t icache_resp_fire_cycles = 0;
  uint64_t icache_miss_event_cycles = 0;
  uint64_t icache_miss_busy_cycles = 0;
  uint64_t icache_outstanding_req_cycles = 0;

  uint64_t inst_fifo_write_slot0_cycles = 0;
  uint64_t inst_fifo_write_slot1_cycles = 0;
  uint64_t ptab_write_cycles = 0;

  uint64_t predecode_run_cycles = 0;
  uint64_t predecode_block_fifo_empty_cycles = 0;
  uint64_t predecode_block_ptab_empty_cycles = 0;
  uint64_t predecode_block_front2back_full_cycles = 0;
  uint64_t predecode_block_dummy_ptab_cycles = 0;
  uint64_t checker_run_cycles = 0;
  uint64_t checker_flush_cycles = 0;
  uint64_t mini_flush_req_cycles = 0;
  uint64_t mini_flush_correct_cycles = 0;

  uint64_t front2back_write_cycles = 0;
  uint64_t front2back_read_req_cycles = 0;
  uint64_t front2back_valid_out_cycles = 0;

  uint64_t bypass_fetch_to_icache_opportunity_cycles = 0;
  uint64_t bypass_icache_to_predecode_opportunity_cycles = 0;
  uint64_t bypass_front2back_to_output_opportunity_cycles = 0;
  uint64_t bypass_front2back_to_output_hit_cycles = 0;

  uint64_t bubble_reset_cycles = 0;
  uint64_t bubble_refetch_cycles = 0;
  uint64_t bubble_dummy_ptab_cycles = 0;
  uint64_t bubble_icache_miss_cycles = 0;
  uint64_t bubble_icache_latency_cycles = 0;
  uint64_t bubble_icache_tlb_retry_cycles = 0;
  uint64_t bubble_icache_tlb_fault_cycles = 0;
  uint64_t bubble_icache_cache_backpressure_cycles = 0;
  uint64_t bubble_icache_latency_other_cycles = 0;
  uint64_t bubble_icache_tlb_retry_other_walk_cycles = 0;
  uint64_t bubble_icache_tlb_retry_walk_req_blocked_cycles = 0;
  uint64_t bubble_icache_tlb_retry_wait_walk_resp_cycles = 0;
  uint64_t bubble_icache_tlb_retry_local_walker_cycles = 0;
  uint64_t bubble_bpu_stall_cycles = 0;
  uint64_t bubble_fetch_addr_empty_cycles = 0;
  uint64_t bubble_ptab_empty_cycles = 0;
  uint64_t bubble_inst_fifo_empty_other_cycles = 0;
  uint64_t bubble_other_cycles = 0;

  // Phase2: a mutually-exclusive bubble root-cause classifier (new scheme).
  uint64_t bubble2_reset_cycles = 0;
  uint64_t bubble2_recovery_backend_refetch_cycles = 0;
  uint64_t bubble2_recovery_frontend_flush_cycles = 0;
  uint64_t bubble2_fetch_stall_cycles = 0;
  uint64_t bubble2_glue_or_fifo_cycles = 0;
  uint64_t bubble2_bpu_side_cycles = 0;
  uint64_t bubble2_other_cycles = 0;

  // Phase4: recovery-credit aware bubble classifier (newest scheme).
  uint64_t bubble3_reset_cycles = 0;
  uint64_t bubble3_recovery_backend_refetch_cycles = 0;
  uint64_t bubble3_recovery_frontend_flush_cycles = 0;
  uint64_t bubble3_fetch_stall_cycles = 0;
  uint64_t bubble3_glue_or_fifo_cycles = 0;
  uint64_t bubble3_bpu_side_cycles = 0;
  uint64_t bubble3_other_cycles = 0;

  // Phase4: experimental cold-like tag (early window).
  uint64_t icache_miss_event_cold_window_cycles = 0;
};

struct PendingBpuSeqTxn {
  wire1_t valid = false;
  wire1_t reset = false;
  BPU_TOP::InputPayload inp;
  BPU_TOP::UpdateRequest req;
};

struct PendingFrontState {
  wire1_t valid = false;
  wire32_t next_front_sim_time = 0;
  FrontRuntimeStats next_front_stats{};
  wire1_t next_predecode_refetch = false;
  fetch_addr_t next_predecode_refetch_address = 0;
  wire1_t next_fetch_addr_fifo_full = false;
  wire1_t next_fetch_addr_fifo_empty = true;
  wire1_t next_fifo_full = false;
  wire1_t next_fifo_empty = true;
  wire1_t next_ptab_full = false;
  wire1_t next_ptab_empty = true;
  wire1_t next_front2back_fifo_full = false;
  wire1_t next_front2back_fifo_empty = true;
};

struct FrontReadData {
  wire1_t predecode_refetch_snapshot = false;
  fetch_addr_t predecode_refetch_address_snapshot = 0;
  wire32_t front_sim_time_snapshot = 0;
  FrontRuntimeStats front_stats_snapshot{};
  fetch_address_FIFO_read_data fetch_addr_fifo_rd_snapshot{};
  instruction_FIFO_read_data fifo_rd_snapshot{};
  PTAB_read_data ptab_rd_snapshot{};
  front2back_FIFO_read_data front2back_fifo_rd_snapshot{};
  wire1_t fetch_addr_fifo_full_latch_snapshot = false;
  wire1_t fetch_addr_fifo_empty_latch_snapshot = true;
  wire1_t fifo_full_latch_snapshot = false;
  wire1_t fifo_empty_latch_snapshot = true;
  wire1_t ptab_full_latch_snapshot = false;
  wire1_t ptab_empty_latch_snapshot = true;
  wire1_t front2back_fifo_full_latch_snapshot = false;
  wire1_t front2back_fifo_empty_latch_snapshot = true;
};

struct FrontUpdateRequest {
  front_top_out out_regs;
  PendingBpuSeqTxn bpu_seq_txn;
  PendingFrontState front_state;
  FetchAddrCombOut fetch_addr_fifo_req{};
  InstructionCombOut fifo_req{};
  PtabCombOut ptab_req{};
  Front2BackCombOut front2back_fifo_req{};
};

struct FrontBpuInputCombIn {
  BPU_in bpu_seed;
  wire1_t do_refetch;
  fetch_addr_t refetch_addr;
  wire1_t icache_ready;
};

struct FrontBpuInputCombOut {
  BPU_in bpu_in;
};

struct FrontGlobalControlCombIn {
  wire1_t reset;
  wire1_t backend_refetch;
  fetch_addr_t backend_refetch_address;
  wire1_t predecode_refetch_snapshot;
  fetch_addr_t predecode_refetch_address_snapshot;
};

struct FrontGlobalControlCombOut {
  wire1_t global_reset;
  wire1_t global_refetch;
  fetch_addr_t refetch_address;
};

struct FrontReadEnableCombIn {
  wire1_t backend_fifo_read_enable;
  wire1_t fetch_addr_fifo_empty_latch_snapshot;
  wire1_t fifo_empty_latch_snapshot;
  wire1_t ptab_empty_latch_snapshot;
  wire1_t front2back_fifo_full_latch_snapshot;
  wire1_t global_reset;
  wire1_t global_refetch;
  wire1_t icache_ready;
  wire1_t icache_ready_2;
};

struct FrontReadEnableCombOut {
  wire1_t fetch_addr_fifo_read_enable_slot0;
  wire1_t fetch_addr_fifo_read_enable_slot1_candidate;
  wire1_t predecode_can_run_old;
  wire1_t inst_fifo_read_enable;
  wire1_t ptab_read_enable;
  wire1_t front2back_read_enable;
};

struct FrontReadStageInputCombIn {
  wire1_t backend_refetch;
  wire1_t global_reset;
  wire1_t global_refetch;
  wire1_t fetch_addr_fifo_read_enable_slot0;
  wire1_t inst_fifo_read_enable;
  wire1_t ptab_read_enable;
  wire1_t front2back_read_enable;
};

struct FrontReadStageInputCombOut {
  wire1_t fetch_addr_fifo_reset;
  wire1_t fetch_addr_fifo_refetch;
  wire1_t fetch_addr_fifo_read_enable;
  wire1_t fifo_reset;
  wire1_t fifo_refetch;
  wire1_t fifo_read_enable;
  wire1_t ptab_reset;
  wire1_t ptab_refetch;
  wire1_t ptab_read_enable;
  wire1_t front2back_fifo_reset;
  wire1_t front2back_fifo_refetch;
  wire1_t front2back_fifo_read_enable;
};

struct FrontBpuControlCombIn {
  BPU_in bpu_in_seed;
  wire1_t fetch_addr_fifo_full_latch_snapshot;
  wire1_t ptab_full_latch_snapshot;
  wire1_t global_reset;
  wire1_t global_refetch;
  fetch_addr_t refetch_address;
};

struct FrontBpuControlCombOut {
  wire1_t bpu_stall;
  wire1_t bpu_can_run;
  wire1_t bpu_icache_ready;
  BPU_in bpu_in;
  BPU_TOP::InputPayload bpu_input;
};

struct FrontBpuOutputCombIn {
  BPU_TOP::OutputPayload bpu_output;
};

struct FrontBpuOutputCombOut {
  BPU_out bpu_out;
};

struct FrontPtabWriteCombIn {
  BPU_TOP::OutputPayload bpu_output;
  wire1_t global_reset;
  wire1_t global_refetch;
  wire1_t ptab_can_write;
};

struct FrontPtabWriteCombOut {
  PTAB_in ptab_in;
};

struct FrontCheckerInputCombIn {
  instruction_FIFO_out fifo_out;
  PTAB_out ptab_out;
};

struct FrontCheckerInputCombOut {
  predecode_checker_in checker_in;
};

struct FrontFront2backWriteCombIn {
  instruction_FIFO_out fifo_out;
  PTAB_out ptab_out;
  predecode_checker_out checker_out;
  wire1_t use_front2back_output_bypass;
};

struct FrontFront2backWriteCombOut {
  front2back_FIFO_in front2back_fifo_in;
  front2back_FIFO_out bypass_front2back_fifo_out;
};

struct FrontOutputCombIn {
  front2back_FIFO_out saved_front2back_fifo_out;
  front2back_FIFO_out bypass_front2back_fifo_out;
  wire1_t use_front2back_output_bypass;
};

struct FrontOutputCombOut {
  front_top_out out;
};

#endif
