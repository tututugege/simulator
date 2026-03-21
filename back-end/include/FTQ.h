#pragma once
#include "IO.h"
#include "config.h"
#include <cstdint>

struct FTQEntry {
  wire<32> start_pc;
  wire<32> next_pc; // Predicted Target of the block
  wire<1> pred_taken_mask[FETCH_WIDTH];
  wire<32> tage_idx[FETCH_WIDTH][4]; // Moved from InstUop
  wire<32> tage_tag[FETCH_WIDTH][4];
  wire<1> mid_pred[FETCH_WIDTH]; // For future use (mid-block prediction)
  wire<1> alt_pred[FETCH_WIDTH];
  wire<8> altpcpn[FETCH_WIDTH];
  wire<8> pcpn[FETCH_WIDTH];
  wire<1> sc_used[FETCH_WIDTH];
  wire<1> sc_pred[FETCH_WIDTH];
  wire<16> sc_sum[FETCH_WIDTH];
  wire<16> sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire<1> loop_used[FETCH_WIDTH];
  wire<1> loop_hit[FETCH_WIDTH];
  wire<1> loop_pred[FETCH_WIDTH];
  wire<16> loop_idx[FETCH_WIDTH];
  wire<16> loop_tag[FETCH_WIDTH];
  wire<1> valid;

  // Debug/Trace info
  uint64_t allocation_time;

  FTQEntry() {
    valid = false;
    start_pc = 0;
    next_pc = 0;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      pred_taken_mask[i] = false;
      mid_pred[i] = false;
      alt_pred[i] = false;
      altpcpn[i] = 0;
      pcpn[i] = 0;
      sc_used[i] = false;
      sc_pred[i] = false;
      sc_sum[i] = 0;
      loop_used[i] = false;
      loop_hit[i] = false;
      loop_pred[i] = false;
      loop_idx[i] = 0;
      loop_tag[i] = 0;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        sc_idx[i][t] = 0;
      }
      for (int j = 0; j < 4; j++) {
        tage_idx[i][j] = 0;
        tage_tag[i][j] = 0;
      }
    }
  }
};
