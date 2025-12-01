#ifndef PREDCODE_CHECKER_H
#define PREDCODE_CHECKER_H

#include "frontend.h"
#include "front_IO.h"
#include "predecode.h"

struct predecode_checker_in {
  // from PTAB
  bool predict_dir[FETCH_WIDTH];
  uint32_t predict_next_fetch_address;
  // from instFIFO
  uint8_t predecode_type[FETCH_WIDTH];
  uint32_t predecode_target_address[FETCH_WIDTH];
  uint32_t seq_next_pc;
};

struct predecode_checker_out {
    bool predict_dir_corrected[FETCH_WIDTH];
    uint32_t predict_next_fetch_address_corrected;
    bool predecode_flush_enable;
};

void predecode_checker_top(struct predecode_checker_in *in, struct predecode_checker_out *out);

#endif