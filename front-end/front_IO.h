#ifndef FRONT_IO_H
#define FRONT_IO_H

#include "frontend.h"
#include "wire_types.h"
#include <cstdint>
#include <type_traits>

struct CsrStatusIO;

struct front_top_in {
  wire1_t reset;
  // from back-end
  wire1_t back2front_valid[COMMIT_WIDTH];
  wire1_t refetch;
  wire1_t itlb_flush;
  wire1_t fence_i;
  fetch_addr_t refetch_address;
  pc_t predict_base_pc[COMMIT_WIDTH];
  wire1_t predict_dir[COMMIT_WIDTH];
  wire1_t actual_dir[COMMIT_WIDTH];
  br_type_t actual_br_type[COMMIT_WIDTH];
  target_addr_t actual_target[COMMIT_WIDTH];
  wire1_t alt_pred[COMMIT_WIDTH];
  pcpn_t altpcpn[COMMIT_WIDTH];
  pcpn_t pcpn[COMMIT_WIDTH];
  tage_idx_t tage_idx[COMMIT_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[COMMIT_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[COMMIT_WIDTH];
  wire1_t sc_pred[COMMIT_WIDTH];
  tage_scl_meta_sum_t sc_sum[COMMIT_WIDTH];
  tage_scl_meta_idx_t sc_idx[COMMIT_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[COMMIT_WIDTH];
  wire1_t loop_hit[COMMIT_WIDTH];
  wire1_t loop_pred[COMMIT_WIDTH];
  tage_loop_meta_idx_t loop_idx[COMMIT_WIDTH];
  tage_loop_meta_tag_t loop_tag[COMMIT_WIDTH];
  wire1_t FIFO_read_enable;
  CsrStatusIO *csr_status;
};

struct front_top_out {
  // to back-end
  wire1_t FIFO_valid;
  wire1_t commit_stall;
  pc_t pc[FETCH_WIDTH];
  inst_word_t instructions[FETCH_WIDTH];
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
};

struct BPU_in {
  wire1_t reset;
  // from back-end
  wire1_t back2front_valid[COMMIT_WIDTH];
  wire1_t refetch;
  fetch_addr_t refetch_address;
  pc_t predict_base_pc[COMMIT_WIDTH];
  wire1_t predict_dir[COMMIT_WIDTH];
  wire1_t actual_dir[COMMIT_WIDTH];
  br_type_t actual_br_type[COMMIT_WIDTH];
  target_addr_t actual_target[COMMIT_WIDTH];
  // for TAGE update
  wire1_t alt_pred[COMMIT_WIDTH];
  pcpn_t altpcpn[COMMIT_WIDTH];
  pcpn_t pcpn[COMMIT_WIDTH];
  tage_idx_t tage_idx[COMMIT_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[COMMIT_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[COMMIT_WIDTH];
  wire1_t sc_pred[COMMIT_WIDTH];
  tage_scl_meta_sum_t sc_sum[COMMIT_WIDTH];
  tage_scl_meta_idx_t sc_idx[COMMIT_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[COMMIT_WIDTH];
  wire1_t loop_hit[COMMIT_WIDTH];
  wire1_t loop_pred[COMMIT_WIDTH];
  tage_loop_meta_idx_t loop_idx[COMMIT_WIDTH];
  tage_loop_meta_tag_t loop_tag[COMMIT_WIDTH];
  // from icache
  wire1_t icache_read_ready;
};

struct BPU_out {
  // to icache
  wire1_t icache_read_valid;
  fetch_addr_t fetch_address;
  // to PTAB
  wire1_t PTAB_write_enable;
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  pc_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
  // 2-Ahead Predictor outputs
  wire1_t two_ahead_valid;
  fetch_addr_t two_ahead_target;
  wire1_t mini_flush_req;
  wire1_t mini_flush_correct;
  fetch_addr_t mini_flush_target;
};

struct icache_in {
  wire1_t reset;
  wire1_t refetch;
  wire1_t itlb_flush;
  wire1_t fence_i;
  // cancel in-flight/front-end-visible request state only; keep cache contents
  wire1_t invalidate_req;
  // from BPU
  wire1_t icache_read_valid;
  fetch_addr_t fetch_address;
  wire1_t icache_read_valid_2;
  fetch_addr_t fetch_address_2;
  CsrStatusIO *csr_status;
  wire1_t run_comb_only;
};

struct icache_out {
  // to BPU & instruction FIFO
  wire1_t icache_read_ready;
  wire1_t icache_read_complete; // fetched current fetch group
  wire1_t icache_read_ready_2;
  wire1_t icache_read_complete_2;
  // perf / debug observability for front-end attribution
  wire1_t perf_req_fire;
  wire1_t perf_req_blocked;
  wire1_t perf_resp_fire;
  wire1_t perf_miss_event;
  wire1_t perf_miss_busy;
  wire1_t perf_outstanding_req;
  wire1_t perf_itlb_hit;
  wire1_t perf_itlb_miss;
  wire1_t perf_itlb_fault;
  wire1_t perf_itlb_retry;
  wire1_t perf_itlb_retry_other_walk;
  wire1_t perf_itlb_retry_walk_req_blocked;
  wire1_t perf_itlb_retry_wait_walk_resp;
  wire1_t perf_itlb_retry_local_walker_busy;
  // to instruction FIFO
  inst_word_t fetch_group[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  inst_word_t fetch_group_2[FETCH_WIDTH];
  wire1_t page_fault_inst_2[FETCH_WIDTH];
  wire1_t inst_valid_2[FETCH_WIDTH];
  pc_t fetch_pc; // PC address of the cache line (the address used to fetch instructions from icache)
  pc_t fetch_pc_2;
};

struct instruction_FIFO_in {
  wire1_t reset;
  wire1_t refetch;
  // from icache
  wire1_t write_enable;
  inst_word_t fetch_group[FETCH_WIDTH];
  pc_t pc[FETCH_WIDTH]; // THIS IS ONLY FOR DEBUG!!! 
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  // from back-end
  wire1_t read_enable;
  // from predecode
  predecode_type_t predecode_type[FETCH_WIDTH];
  target_addr_t predecode_target_address[FETCH_WIDTH];
  pc_t seq_next_pc;
};

struct instruction_FIFO_out {
  wire1_t full;
  wire1_t empty;
  // to back-end
  wire1_t FIFO_valid;
  inst_word_t instructions[FETCH_WIDTH];
  pc_t pc[FETCH_WIDTH]; // THIS IS ONLY FOR DEBUG!!! 
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  predecode_type_t predecode_type[FETCH_WIDTH];
  target_addr_t predecode_target_address[FETCH_WIDTH];
  pc_t seq_next_pc;
};

struct PTAB_in {
  wire1_t reset;
  wire1_t refetch;
  // from BPU
  wire1_t write_enable;
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  pc_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
  // from back-end
  wire1_t read_enable;
  // for 2-Ahead
  wire1_t need_mini_flush;
};

struct PTAB_out {
  // 为了2-ahead处理添加的dummy entry
  wire1_t dummy_entry;
  wire1_t full;
  wire1_t empty;
  // to back-end
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  pc_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
};

struct front2back_FIFO_in {
  wire1_t reset;
  wire1_t refetch;
  wire1_t write_enable;
  wire1_t read_enable;
  inst_word_t fetch_group[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  // to back-end
  wire1_t predict_dir_corrected[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address_corrected;
  pc_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
};

struct front2back_FIFO_out {
  wire1_t full;
  wire1_t empty;
  // to back-end
  wire1_t front2back_FIFO_valid; // same as the previous FIFO_valid
  inst_word_t fetch_group[FETCH_WIDTH];
  wire1_t page_fault_inst[FETCH_WIDTH];
  wire1_t inst_valid[FETCH_WIDTH];
  wire1_t predict_dir_corrected[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address_corrected;
  pc_t predict_base_pc[FETCH_WIDTH];
  wire1_t alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire1_t sc_used[FETCH_WIDTH];
  wire1_t sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire1_t loop_used[FETCH_WIDTH];
  wire1_t loop_hit[FETCH_WIDTH];
  wire1_t loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
};

struct fetch_address_FIFO_in {
  wire1_t reset;
  wire1_t refetch;
  wire1_t read_enable;
  wire1_t write_enable;
  fetch_addr_t fetch_address;
};

struct fetch_address_FIFO_out {
  wire1_t full;
  wire1_t empty;
  wire1_t read_valid;      // 这一拍是否真的弹出了一条地址
  fetch_addr_t fetch_address;
};

using front2back_altpcpn_lane_t = std::remove_reference_t<decltype(((front2back_FIFO_in *)nullptr)->altpcpn[0])>;
using front2back_pcpn_lane_t = std::remove_reference_t<decltype(((front2back_FIFO_in *)nullptr)->pcpn[0])>;
using front2back_tage_idx_lane_t = std::remove_reference_t<decltype(((front2back_FIFO_in *)nullptr)->tage_idx[0][0])>;
using front2back_tage_tag_lane_t = std::remove_reference_t<decltype(((front2back_FIFO_in *)nullptr)->tage_tag[0][0])>;
static_assert(sizeof(front2back_altpcpn_lane_t) * 8 >= pcpn_t_BITS,
              "front2back_FIFO_in.altpcpn lane width is narrower than pcpn_t_BITS");
static_assert(sizeof(front2back_pcpn_lane_t) * 8 >= pcpn_t_BITS,
              "front2back_FIFO_in.pcpn lane width is narrower than pcpn_t_BITS");
static_assert(sizeof(front2back_tage_idx_lane_t) * 8 >= tage_idx_t_BITS,
              "front2back_FIFO_in.tage_idx lane width is narrower than tage_idx_t_BITS");
static_assert(sizeof(front2back_tage_tag_lane_t) * 8 >= tage_tag_t_BITS,
              "front2back_FIFO_in.tage_tag lane width is narrower than tage_tag_t_BITS");

#endif
