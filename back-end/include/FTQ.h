#pragma once
#include "config.h"
#include "wire_types.h"
#include <cstdint>

struct FTQEntry {
  wire<32> slot_pc[FETCH_WIDTH];
  wire<32> next_pc; // Predicted Target of the block
  wire<1> pred_taken_mask[FETCH_WIDTH];

  FTQEntry() {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      slot_pc[i] = 0;
      pred_taken_mask[i] = false;
    }
    next_pc = 0;
  }
};

// Prediction/training sideband metadata that backend execution path does not
// need to random-read. This payload is consumed in FIFO order with FTQ blocks.
struct FTQTrainMetaEntry {
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // Moved from InstUop
  tage_tag_t tage_tag[FETCH_WIDTH][4];
  wire<1> mid_pred[FETCH_WIDTH]; // For future use (mid-block prediction)
  wire<1> alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  wire<1> sc_used[FETCH_WIDTH];
  wire<1> sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire<1> loop_used[FETCH_WIDTH];
  wire<1> loop_hit[FETCH_WIDTH];
  wire<1> loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];

  FTQTrainMetaEntry() {
    for (int i = 0; i < FETCH_WIDTH; i++) {
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
