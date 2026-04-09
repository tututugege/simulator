#ifndef PREDCODE_CHECKER_H
#define PREDCODE_CHECKER_H

#include "frontend.h"
#include "front_IO.h"
#include "predecode.h"

struct predecode_checker_in {
  // from PTAB
  wire1_t predict_dir[FETCH_WIDTH];
  fetch_addr_t predict_next_fetch_address;
  // from instFIFO
  predecode_type_t predecode_type[FETCH_WIDTH];
  target_addr_t predecode_target_address[FETCH_WIDTH];
  pc_t seq_next_pc;
};

struct predecode_checker_out {
    wire1_t predict_dir_corrected[FETCH_WIDTH];
    fetch_addr_t predict_next_fetch_address_corrected;
    wire1_t predecode_flush_enable;
};

struct predecode_checker_read_data {
  predecode_checker_in inp_regs;
};

void predecode_checker_seq_read(struct predecode_checker_in *in,
                                struct predecode_checker_read_data *rd);
void predecode_checker_comb(const predecode_checker_read_data &input,
                            predecode_checker_out &output);
void predecode_checker_seq_write();

#endif
