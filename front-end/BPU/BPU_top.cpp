#include "../front_IO.h"
#include "../frontend.h"
#include "config.h"
#include <SimCpu.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
using namespace std;

#ifndef IO_version
#include "./dir_predictor/demo_tage.h"
#include "./target_predictor/btb.h"
#include "./target_predictor/target_cache.h"
#else
#include "./dir_predictor/dir_predictor_IO/tage_IO.h"
#include "./target_predictor/target_predictor_IO/btb_IO.h"
#include "./target_predictor/target_predictor_IO/target_cache_IO.h"
#include "./train_IO_gen.h"
#endif

#define INST_TYPE_ENTRY_NUM 4096
#define INST_TYPE_ENTRY_MASK (INST_TYPE_ENTRY_NUM - 1)
uint8_t inst_type[INST_TYPE_ENTRY_NUM];

// int io_gen_cnt = 10;
uint32_t pc_reg;
int tage_cnt = 0;
int tage_miss = 0;

void BPU_change_pc_reg(uint32_t new_pc) { pc_reg = new_pc; }

void BPU_top(struct BPU_in *in, struct BPU_out *out) {
  // generate pc_reg sending to icache
  if (in->reset) {
    pc_reg = cpu.back.number_PC;
    out->PTAB_write_enable = false;
    out->icache_read_valid = false;
    return;
  } else if (in->refetch) {
    pc_reg = in->refetch_address;
    // return;
  } // else pc_reg should be set by the previous cycle
  // send fetch request to icache
  out->icache_read_valid = true; // now always valid
  out->fetch_address = pc_reg;
  // out->PTAB_write_enable = true;
  out->PTAB_write_enable = false; // first set to false in case of only update

  // update branch predictor
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i]) {

      inst_type[(in->predict_base_pc[i] >> 2) & INST_TYPE_ENTRY_MASK] =
          in->actual_br_type[i];

      // pred_out pred_out = {in->predict_dir[i], in->alt_pred[i], in->pcpn[i],
      //                      in->altpcpn[i]};
      pred_out pred_out;
      pred_out.pred = in->predict_dir[i];
      pred_out.altpred = in->alt_pred[i];
      pred_out.pcpn = in->pcpn[i];
      pred_out.altpcpn = in->altpcpn[i];
      for (int j = 0; j < TN_MAX; j++) {
        pred_out.tage_idx[j] = in->tage_idx[i][j];
      }
#ifndef IO_version
      if (in->actual_br_type[i] == BR_DIRECT)
        TAGE_do_update(in->predict_base_pc[i], in->actual_dir[i], pred_out);
      if (in->actual_dir[i] == in->predict_dir[i])
        tage_cnt++;
      else
        tage_miss++;
#else
      C_TAGE_do_update_wrapper(in->predict_base_pc[i], in->actual_dir[i],
                               pred_out);
#endif
#ifndef IO_version
      if (in->actual_br_type[i] != BR_NONCTL)
        bht_update(in->predict_base_pc[i], in->actual_dir[i]);
#else
      C_bht_update_wrapper(in->predict_base_pc[i], in->actual_dir[i]);
#endif
      if (in->actual_dir[i] == true) {
#ifndef IO_version
        btb_update(in->predict_base_pc[i], in->actual_target[i],
                   in->actual_br_type[i], in->actual_dir[i]);
#else
        C_btb_update_wrapper(in->predict_base_pc[i], in->actual_target[i],
                             in->actual_br_type[i], in->actual_dir[i]);
#endif
      }
    }
  }
#ifndef IO_version
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i] && in->actual_br_type[i] == BR_DIRECT) {
      do_GHR_update(in->actual_dir[i]);
      TAGE_update_FH(in->actual_dir[i]);
    }
  }
#else
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in->back2front_valid[i]) {
      C_TAGE_update_HR_wrapper(in->actual_dir[i]);
    }
  }
#endif

  if (in->icache_read_ready) {
    // do branch prediction
    // traverse instructions in fetch_group, find the first TAGE prediction
    // that is taken
    out->PTAB_write_enable =
        true; // now can always give one prediction at one cycle
    bool found_taken_branch = false;
    uint32_t branch_pc = pc_reg;

#ifdef IO_GEN_MODE
    // io_gen_cnt--;
    // if (io_gen_cnt >= 0) {
#ifdef IO_version
    print_IO_data(pc_reg);
    printf("\n");
#endif
    // }
#endif

    uint32_t mask = ~(ICACHE_LINE_SIZE - 1);

    // do TAGE for FETCH_WIDTH instructions
    uint32_t pc_boundry = pc_reg + (FETCH_WIDTH * 4);
    if ((pc_reg & mask) != (pc_boundry & mask)) {
      pc_boundry = pc_boundry & mask;
    }
    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t current_pc = pc_reg + (i * 4);
      out->predict_base_pc[i] = current_pc;
      if (current_pc < pc_boundry) { // 检查是否超出cacheline边界

        uint8_t cur_inst_type =
            inst_type[(current_pc >> 2) & INST_TYPE_ENTRY_MASK];
#ifndef IO_version
        pred_out pred_out = TAGE_get_prediction(current_pc);
#else
        pred_out pred_out = C_TAGE_do_pred_wrapper(current_pc);
#endif
        out->predict_dir[i] = pred_out.pred;
        out->alt_pred[i] = pred_out.altpred;
        out->pcpn[i] = pred_out.pcpn;
        out->altpcpn[i] = pred_out.altpcpn;
        for (int k = 0; k < TN_MAX; k++) {
          out->tage_idx[i][k] = pred_out.tage_idx[k];
        }

        if (cur_inst_type == BR_NONCTL) {
          out->predict_dir[i] = false;
        } else if (cur_inst_type == BR_RET || cur_inst_type == BR_CALL ||
                   cur_inst_type == BR_IDIRECT || cur_inst_type == BR_JAL) {
          out->predict_dir[i] = true;
        }

        DEBUG_LOG("[BPU_top] predict_dir[%d]: %d, pc: %x\n", i,
                  out->predict_dir[i], current_pc);
        if (out->predict_dir[i] && !found_taken_branch) {
          found_taken_branch = true;
          branch_pc = current_pc;
        }
      }
    }

    if (found_taken_branch) {
      // only do BTB lookup for taken branches
#ifndef IO_version
      uint32_t btb_target = btb_pred(branch_pc);
#else
      uint32_t btb_target = C_btb_pred_wrapper(branch_pc);
#endif
      out->predict_next_fetch_address = btb_target;
      DEBUG_LOG("[BPU_top] base pc: %x, btb_target: %x\n", branch_pc,
                btb_target);
    } else {
      // no prediction for taken branches, execute sequentially
      out->predict_next_fetch_address = pc_reg + (FETCH_WIDTH * 4);
      if ((out->predict_next_fetch_address & mask) !=
          (pc_reg & mask)) { // cross cacheline boundary(32 bytes)
        out->predict_next_fetch_address &= mask;
        DEBUG_LOG(
            "[BPU_top] cross cacheline boundary, next_fetch_address: %x\n",
            out->predict_next_fetch_address);
      }
    }
    pc_reg = out->predict_next_fetch_address;
    DEBUG_LOG("[BPU_top] icache_fetch_address: %x\n", out->fetch_address);
    DEBUG_LOG("[BPU_top] predict_next_fetch_address: %x\n",
              out->predict_next_fetch_address);
    DEBUG_LOG("[BPU_top] predict_dir: %d\n",
              out->predict_dir[0] || out->predict_dir[1] ||
                  out->predict_dir[2] || out->predict_dir[3]);
  }
}
