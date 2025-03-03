#include "../front_IO.h"
#include "../frontend.h"
#include "./target_predictor/btb.h"
#include <cstdint>
#include <cstdio>

#ifndef IO_version
#include "./dir_predictor/demo_tage.h"
#else
#include "./dir_predictor/tage_IO.h"
#endif

uint32_t pc_reg;

void BPU_top(struct BPU_in *in, struct BPU_out *out) {
  // generate pc_reg sending to icache
  if (in->reset) {
    pc_reg = RESET_PC;
    return;
  } else if (in->refetch) {
    pc_reg = in->refetch_address;
  } // else pc_reg should be set by the previous cycle

  // send fetch request to icache
  out->icache_read_valid = true; // now always valid
  out->fetch_address = pc_reg;
  out->PTAB_write_enable = true;

  // update branch predictor
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i]) {
      pred_out pred_out = {in->predict_dir[i], in->alt_pred[i], in->pcpn[i],
                           in->altpcpn[i]};
#ifndef IO_version
      TAGE_do_update(in->predict_base_pc[i], in->actual_dir[i], pred_out);
#else
      if (LOG)
        printf("inpc %x acdir %d\n", in->predict_base_pc[i], in->actual_dir[i]);

      C_TAGE_do_update(in->predict_base_pc[i], in->actual_dir[i], pred_out);
#endif
      bht_update(in->predict_base_pc[i], in->actual_dir[i]);
      if (in->actual_dir[i] == true) {
        btb_update(in->predict_base_pc[i], in->refetch_address,
                   in->actual_br_type[i], in->actual_dir[i]);
      }
    }
  }
#ifndef IO_version
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i]) {
      do_GHR_update(in->actual_dir[i]);
      TAGE_update_FH(in->actual_dir[i]);
    }
  }
#else
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i]) {
      C_TAGE_update_HR(in->actual_dir[i]);
    }
  }
#endif
  // do branch prediction
  // traverse instructions in fetch_group, find the first TAGE prediction
  // that is taken
  bool found_taken_branch = false;
  uint32_t branch_pc = pc_reg;

  // do TAGE for FETCH_WIDTH instructions
  for (int i = 0; i < FETCH_WIDTH; i++) {
    uint32_t current_pc = pc_reg + (i * 4);
    out->predict_base_pc[i] = current_pc;
#ifndef IO_version
    pred_out pred_out = TAGE_get_prediction(current_pc);
#else
    pred_out pred_out = C_TAGE_do_pred(current_pc);
#endif
    out->predict_dir[i] = pred_out.pred;
    out->alt_pred[i] = pred_out.altpred;
    out->pcpn[i] = pred_out.pcpn;
    out->altpcpn[i] = pred_out.altpcpn;
    DEBUG_LOG("[BPU_top] predict_dir[%d]: %d, pc: %x\n", i, out->predict_dir[i],
              current_pc);
    if (out->predict_dir[i] && !found_taken_branch) {
      found_taken_branch = true;
      branch_pc = current_pc;
    }
  }

  if (found_taken_branch) {
    // only do BTB lookup for taken branches
    uint32_t btb_target = btb_pred(branch_pc);
    out->predict_next_fetch_address = btb_target;
    DEBUG_LOG("[BPU_top] base pc: %x, btb_target: %x\n", branch_pc, btb_target);
  } else {
    // no prediction for taken branches, execute sequentially
    out->predict_next_fetch_address = pc_reg + (FETCH_WIDTH * 4);
  }
  pc_reg = out->predict_next_fetch_address;
  DEBUG_LOG("[BPU_top] icache_fetch_address: %x\n", out->fetch_address);
  DEBUG_LOG("[BPU_top] predict_next_fetch_address: %x\n",
            out->predict_next_fetch_address);
  DEBUG_LOG("[BPU_top] predict_dir: %d\n",
            out->predict_dir[0] || out->predict_dir[1] || out->predict_dir[2] ||
                out->predict_dir[3]);
}
