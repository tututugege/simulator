#ifndef FRONT_IO_H
#define FRONT_IO_H

#include "frontend.h"
#include <cstdint>

struct front_top_in {
  bool reset;
  // from back-end
  bool back2front_valid[COMMIT_WIDTH];
  bool refetch;
  uint32_t refetch_address;
  uint32_t predict_base_pc[COMMIT_WIDTH];
  bool predict_dir[COMMIT_WIDTH];
  bool actual_dir[COMMIT_WIDTH];
  uint32_t actual_br_type[COMMIT_WIDTH];
  uint32_t actual_target[COMMIT_WIDTH];
  bool alt_pred[COMMIT_WIDTH];
  uint8_t altpcpn[COMMIT_WIDTH];
  uint8_t pcpn[COMMIT_WIDTH];
  bool FIFO_read_enable;
};

struct front_top_out {
  // to back-end
  bool FIFO_valid;
  uint32_t pc[FETCH_WIDTH];
  uint32_t instructions[FETCH_WIDTH];
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
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
  uint32_t actual_br_type[COMMIT_WIDTH];
  uint32_t actual_target[COMMIT_WIDTH];
  // for TAGE update
  bool alt_pred[COMMIT_WIDTH];
  uint8_t altpcpn[COMMIT_WIDTH];
  uint8_t pcpn[COMMIT_WIDTH];
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
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
};

struct icache_in {
  bool reset;
  // from BPU
  bool icache_read_valid;
  uint32_t fetch_address;
};

struct icache_out {
  // to BPU & instruction FIFO
  bool icache_read_ready;
  // to instruction FIFO
  uint32_t fetch_group[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
};

struct instruction_FIFO_in {
  bool reset;
  bool refetch;
  // from icache
  bool write_enable;
  uint32_t fetch_group[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
  // from back-end
  bool read_enable;
};

struct instruction_FIFO_out {
  bool full;
  bool empty;
  // to back-end
  bool FIFO_valid;
  uint32_t instructions[FETCH_WIDTH];
  bool page_fault_inst[FETCH_WIDTH];
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
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  // from back-end
  bool read_enable;
};

struct PTAB_out {
  bool full;
  bool empty;
  // to back-end
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  uint32_t predict_base_pc[FETCH_WIDTH];
  // for TAGE update
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
};
#endif
