#ifndef FRONT_IO_H
#define FRONT_IO_H

#include "frontend.h"
#include "wire_types.h"
#include <cstdint>
#include <type_traits>

struct CsrStatusIO;

struct front_top_in {
  bool reset;
  // from back-end
  bool back2front_valid[COMMIT_WIDTH];
  bool refetch;
  uint32_t refetch_address;
  uint32_t predict_base_pc[COMMIT_WIDTH];
  bool predict_dir[COMMIT_WIDTH];
  bool actual_dir[COMMIT_WIDTH];
  br_type_t actual_br_type[COMMIT_WIDTH];
  uint32_t actual_target[COMMIT_WIDTH];
  bool alt_pred[COMMIT_WIDTH];
  pcpn_t altpcpn[COMMIT_WIDTH];
  pcpn_t pcpn[COMMIT_WIDTH];
  tage_idx_t tage_idx[COMMIT_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[COMMIT_WIDTH][4]; // TN_MAX = 4
  bool FIFO_read_enable;
  CsrStatusIO *csr_status;
};

struct front_top_out {
  // to back-end
  bool FIFO_valid;
  uint32_t pc[FETCH_WIDTH];
  uint32_t instructions[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
};

struct BPU_in {
  bool reset;
  // from back-end
  bool back2front_valid[COMMIT_WIDTH];
  bool refetch;
  uint32_t refetch_address;
  uint32_t predict_base_pc[COMMIT_WIDTH];
  bool predict_dir[COMMIT_WIDTH];
  bool actual_dir[COMMIT_WIDTH];
  br_type_t actual_br_type[COMMIT_WIDTH];
  uint32_t actual_target[COMMIT_WIDTH];
  // for TAGE update
  bool alt_pred[COMMIT_WIDTH];
  pcpn_t altpcpn[COMMIT_WIDTH];
  pcpn_t pcpn[COMMIT_WIDTH];
  tage_idx_t tage_idx[COMMIT_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[COMMIT_WIDTH][4]; // TN_MAX = 4
  // from icache
  bool icache_read_ready;
};

struct BPU_out {
  // to icache
  bool icache_read_valid;
  uint32_t fetch_address;
  // to PTAB
  bool PTAB_write_enable;
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  uint32_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  // 2-Ahead Predictor outputs
  bool two_ahead_valid;
  uint32_t two_ahead_target;
  bool mini_flush_req;
  bool mini_flush_correct;
  uint32_t mini_flush_target;
};

struct icache_in {
  bool reset;
  bool refetch;
  // from BPU
  bool icache_read_valid;
  uint32_t fetch_address;
  bool icache_read_valid_2;
  uint32_t fetch_address_2;
  CsrStatusIO *csr_status;
};

struct icache_out {
  // to BPU & instruction FIFO
  bool icache_read_ready;
  bool icache_read_complete; // fetched current fetch group
  bool icache_read_ready_2;
  bool icache_read_complete_2;
  // to instruction FIFO
  uint32_t fetch_group[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  uint32_t fetch_pc; // PC address of the cache line (the address used to fetch instructions from icache)
  uint32_t fetch_group_2[FETCH_WIDTH];
  bool page_fault_inst_2[FETCH_WIDTH];
  bool inst_valid_2[FETCH_WIDTH];
  uint32_t fetch_pc_2;
};

struct instruction_FIFO_in {
  bool reset;
  bool refetch;
  // from icache
  bool write_enable;
  uint32_t fetch_group[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH]; // THIS IS ONLY FOR DEBUG!!! 
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  // from back-end
  bool read_enable;
  // from predecode
  predecode_type_t predecode_type[FETCH_WIDTH];
  uint32_t predecode_target_address[FETCH_WIDTH];
  uint32_t seq_next_pc;
};

struct instruction_FIFO_out {
  bool full;
  bool empty;
  // to back-end
  bool FIFO_valid;
  uint32_t instructions[FETCH_WIDTH];
  uint32_t pc[FETCH_WIDTH]; // THIS IS ONLY FOR DEBUG!!! 
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  predecode_type_t predecode_type[FETCH_WIDTH];
  uint32_t predecode_target_address[FETCH_WIDTH];
  uint32_t seq_next_pc;
};

struct PTAB_in {
  bool reset;
  bool refetch;
  // from BPU
  bool write_enable;
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  uint32_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  // from back-end
  bool read_enable;
  // for 2-Ahead
  bool need_mini_flush;
};

struct PTAB_out {
  // 为了2-ahead处理添加的dummy entry
  bool dummy_entry;
  bool read_valid;
  bool full;
  bool empty;
  // to back-end
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  uint32_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
};

struct front2back_FIFO_in {
  bool reset;
  bool refetch;
  bool write_enable;
  bool read_enable;
  uint32_t fetch_group[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  // to back-end
  bool predict_dir_corrected[FETCH_WIDTH];
  uint32_t predict_next_fetch_address_corrected;
  uint32_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
};

struct front2back_FIFO_out {
  bool full;
  bool empty;
  // to back-end
  bool front2back_FIFO_valid; // same as the previous FIFO_valid
  uint32_t fetch_group[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
  bool inst_valid[FETCH_WIDTH];
  bool predict_dir_corrected[FETCH_WIDTH];
  uint32_t predict_next_fetch_address_corrected;
  uint32_t predict_base_pc[FETCH_WIDTH];
  bool alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
};

struct fetch_address_FIFO_in {
  bool reset;
  bool refetch;
  bool read_enable;
  bool write_enable;
  uint32_t fetch_address;
};

struct fetch_address_FIFO_out {
  bool full;
  bool empty;
  bool read_valid;      // 这一拍是否真的弹出了一条地址
  uint32_t fetch_address;
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
