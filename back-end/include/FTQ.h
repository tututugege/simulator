#pragma once
#include "IO.h"
#include "config.h"
#include <cstdint>

struct FTQEntry {
  uint32_t start_pc;
  uint32_t next_pc; // Predicted Target of the block
  bool pred_taken_mask[FETCH_WIDTH];
  uint32_t tage_idx[FETCH_WIDTH][4]; // Moved from InstUop
  uint32_t tage_tag[FETCH_WIDTH][4];
  bool mid_pred[FETCH_WIDTH]; // For future use (mid-block prediction)
  bool alt_pred[FETCH_WIDTH];
  uint8_t altpcpn[FETCH_WIDTH];
  uint8_t pcpn[FETCH_WIDTH];
  bool sc_used[FETCH_WIDTH];
  bool sc_pred[FETCH_WIDTH];
  int16_t sc_sum[FETCH_WIDTH];
  uint16_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  bool loop_used[FETCH_WIDTH];
  bool loop_hit[FETCH_WIDTH];
  bool loop_pred[FETCH_WIDTH];
  uint16_t loop_idx[FETCH_WIDTH];
  uint16_t loop_tag[FETCH_WIDTH];
  bool valid;

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

class FTQLookupIO {
public:
  FTQEntry entries[FTQ_SIZE];
};

inline uint32_t ftq_lookup_pc(const FTQLookupIO *lookup, uint32_t ftq_idx,
                              uint32_t ftq_offset) {
  if (lookup == nullptr) {
    return 0;
  }
  return lookup->entries[ftq_idx].start_pc + (ftq_offset << 2);
}
