#include "front_IO.h"
#include "front_module.h"
#include "predecode.h"
#include "predecode_checker.h"
#include <RISCV.h>
#include <cstdio>
void front_top(struct front_top_in *in, struct front_top_out *out) {
  static struct BPU_in bpu_in;
  static struct BPU_out bpu_out;
  static struct icache_in icache_in;
  static struct icache_out icache_out;
  static struct instruction_FIFO_in fifo_in;
  static struct instruction_FIFO_out fifo_out;
  static struct PTAB_in ptab_in;
  static struct PTAB_out ptab_out;
  static struct front2back_FIFO_in front2back_fifo_in;
  static struct front2back_FIFO_out front2back_fifo_out;

  if (!fifo_out.full || in->reset || in->refetch) {
    // set BPU input
    bpu_in.reset = in->reset;
    bpu_in.refetch = in->refetch;
    bpu_in.refetch_address = in->refetch_address;
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      bpu_in.back2front_valid[i] = in->back2front_valid[i];
      bpu_in.predict_base_pc[i] = in->predict_base_pc[i];
      bpu_in.refetch_address = in->refetch_address;
      bpu_in.actual_dir[i] = in->actual_dir[i];
      bpu_in.actual_br_type[i] = in->actual_br_type[i];
      bpu_in.actual_target[i] = in->actual_target[i];
      bpu_in.predict_dir[i] = in->predict_dir[i];
      bpu_in.alt_pred[i] = in->alt_pred[i];
      bpu_in.altpcpn[i] = in->altpcpn[i];
      bpu_in.pcpn[i] = in->pcpn[i];
      for (int j = 0; j < 4; j++) { // TN_MAX = 4
        bpu_in.tage_idx[i][j] = in->tage_idx[i][j];
      }
    }
#ifdef USE_TRUE_ICACHE
    // get icache_read_ready signal for this cycle
    icache_in.reset = in->reset;
    icache_in.refetch = in->refetch;
    icache_in.run_comb_only = true;
    icache_top(&icache_in, &icache_out);
#endif

    if (sim_time == 0 && !in->reset) {
      icache_out.icache_read_ready = true;
    }
    bpu_in.icache_read_ready = icache_out.icache_read_ready;
    // run BPU
    BPU_top(&bpu_in, &bpu_out);

    // set icache input
    icache_in.reset = in->reset;
    icache_in.run_comb_only = false;
    icache_in.refetch = in->refetch;
    icache_in.icache_read_valid = bpu_out.icache_read_valid;
    icache_in.fetch_address = bpu_out.fetch_address;

    // run icache
    icache_top(&icache_in, &icache_out);

    // set FIFO input
    fifo_in.reset = in->reset;
    fifo_in.refetch = in->refetch;
    // fifo_in.read_enable = in->FIFO_read_enable;
    // read is allow only if Backend wants to read and icache has already
    // prepared new instructions fifo_in.read_enable = in->FIFO_read_enable &&
    // (icache_out.icache_read_complete || (!fifo_out.empty && !in->refetch));
    fifo_in.read_enable = false;
    fifo_in.write_enable = icache_out.icache_read_complete;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      fifo_in.fetch_group[i] = icache_out.fetch_group[i];
      fifo_in.page_fault_inst[i] = icache_out.page_fault_inst[i];
      fifo_in.inst_valid[i] = icache_out.inst_valid[i];
    }

    // set PTAB input
    ptab_in.reset = in->reset;
    ptab_in.refetch = in->refetch;
    // ptab_in.read_enable = in->FIFO_read_enable;
    // ptab_in.read_enable = in->FIFO_read_enable &&
    // icache_out.icache_read_complete;
    ptab_in.read_enable = fifo_in.read_enable;
    // ptab_in.write_enable = bpu_out.PTAB_write_enable &&
    // icache_out.icache_read_ready;
    ptab_in.write_enable = bpu_out.PTAB_write_enable;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      ptab_in.predict_dir[i] = bpu_out.predict_dir[i];
      ptab_in.predict_base_pc[i] = bpu_out.predict_base_pc[i];
      ptab_in.alt_pred[i] = bpu_out.alt_pred[i];
      ptab_in.altpcpn[i] = bpu_out.altpcpn[i];
      ptab_in.pcpn[i] = bpu_out.pcpn[i];
      for (int j = 0; j < 4; j++) { // TN_MAX = 4
        ptab_in.tage_idx[i][j] = bpu_out.tage_idx[i][j];
      }
    }
    DEBUG_LOG_SMALL_3("bpu_out.predict_next_fetch_address: %x\n",
                      bpu_out.predict_next_fetch_address);
    ptab_in.predict_next_fetch_address = bpu_out.predict_next_fetch_address;

    // predecode
    // if (false) {
    if (icache_out.icache_read_complete) {
      PredecodeResult predecode_results[FETCH_WIDTH];

      for (int i = 0; i < FETCH_WIDTH; i++) {
        if (icache_out.inst_valid[i]) {
          uint32_t current_pc = icache_out.fetch_pc + (i * 4);
          predecode_results[i] =
              predecode_instruction(icache_out.fetch_group[i], current_pc);
          fifo_in.predecode_type[i] = predecode_results[i].type;
          fifo_in.predecode_target_address[i] =
              predecode_results[i].target_address;
        } else {
          fifo_in.predecode_type[i] = PREDECODE_NON_BRANCH;
          fifo_in.predecode_target_address[i] = 0;
        }
      }
      uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
      fifo_in.seq_next_pc = icache_out.fetch_pc + (FETCH_WIDTH * 4);
      if ((fifo_in.seq_next_pc & mask) != (icache_out.fetch_pc & mask)) {
        fifo_in.seq_next_pc &= mask;
      }
    }

  } else {
    // set BPU input
    bpu_in.reset = false;
    bpu_in.refetch = false;
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      bpu_in.back2front_valid[i] = in->back2front_valid[i];
      bpu_in.predict_base_pc[i] = in->predict_base_pc[i];
      bpu_in.refetch_address = in->refetch_address;
      bpu_in.actual_dir[i] = in->actual_dir[i];
      bpu_in.actual_br_type[i] = in->actual_br_type[i];
      bpu_in.actual_target[i] = in->actual_target[i];
      bpu_in.predict_dir[i] = in->predict_dir[i];
      bpu_in.alt_pred[i] = in->alt_pred[i];
      bpu_in.altpcpn[i] = in->altpcpn[i];
      bpu_in.pcpn[i] = in->pcpn[i];
      for (int j = 0; j < 4; j++) { // TN_MAX = 4
        bpu_in.tage_idx[i][j] = in->tage_idx[i][j];
      }
    }
    bpu_in.icache_read_ready = false;

    // run BPU
    BPU_top(&bpu_in, &bpu_out);

    // set icache input
    icache_in.reset = in->reset;
    icache_in.refetch = false;
    icache_in.run_comb_only = false;
    icache_in.icache_read_valid = bpu_out.icache_read_valid;
    icache_in.fetch_address = bpu_out.fetch_address;

    fifo_in.reset = false;
    fifo_in.refetch = false;
    fifo_in.read_enable = false;
    fifo_in.write_enable = false;

    ptab_in.reset = false;
    ptab_in.refetch = false;
    ptab_in.read_enable = false;
    ptab_in.write_enable = false;
  }

  // run FIFO
  instruction_FIFO_top(&fifo_in, &fifo_out);

  // run PTAB
  PTAB_top(&ptab_in, &ptab_out);

  bool predecode_checker_valid = false;
  predecode_checker_valid =
      !fifo_out.empty && !ptab_out.empty && !front2back_fifo_out.full;
  if (predecode_checker_valid) {
    // read from PTAB and instFIFO
    ptab_in.read_enable = true;
    ptab_in.refetch = false;
    ptab_in.write_enable = false;
    fifo_in.read_enable = true;
    fifo_in.refetch = false;
    fifo_in.write_enable = false;
    instruction_FIFO_top(&fifo_in, &fifo_out);
    PTAB_top(&ptab_in, &ptab_out);

    // send data to predecode checker
    predecode_checker_in predecode_checker_in;
    predecode_checker_out predecode_checker_out;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      predecode_checker_in.predict_dir[i] = ptab_out.predict_dir[i];
      predecode_checker_in.predecode_type[i] = fifo_out.predecode_type[i];
      predecode_checker_in.predecode_target_address[i] =
          fifo_out.predecode_target_address[i];
    }
    DEBUG_LOG_SMALL_3("ptab_out.predict_next_fetch_address: %x\n",
                      ptab_out.predict_next_fetch_address);
    predecode_checker_in.seq_next_pc = fifo_out.seq_next_pc;
    predecode_checker_in.predict_next_fetch_address =
        ptab_out.predict_next_fetch_address;
    predecode_checker_top(&predecode_checker_in, &predecode_checker_out);

    if (predecode_checker_out.predecode_flush_enable) {
      // flush PTAB and instFIFO and change PC_reg
      ptab_in.reset = true;
      fifo_in.reset = true;
      instruction_FIFO_top(&fifo_in, &fifo_out);
      PTAB_top(&ptab_in, &ptab_out);

      // make sure inflight icache request is discarded
      icache_in.reset = true;
      icache_top(&icache_in, &icache_out);

      BPU_change_pc_reg(
          predecode_checker_out.predict_next_fetch_address_corrected);
    }

    // send the result to new FIFO
    front2back_fifo_in.reset = in->reset;
    front2back_fifo_in.refetch = in->refetch;
    front2back_fifo_in.read_enable = in->FIFO_read_enable;
    front2back_fifo_in.write_enable = true;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      front2back_fifo_in.fetch_group[i] = fifo_out.instructions[i];
      front2back_fifo_in.page_fault_inst[i] = fifo_out.page_fault_inst[i];
      front2back_fifo_in.inst_valid[i] = fifo_out.inst_valid[i];
      front2back_fifo_in.predict_dir_corrected[i] =
          predecode_checker_out.predict_dir_corrected[i];
      front2back_fifo_in.predict_base_pc[i] = ptab_out.predict_base_pc[i];
      front2back_fifo_in.alt_pred[i] = ptab_out.alt_pred[i];
      front2back_fifo_in.altpcpn[i] = ptab_out.altpcpn[i];
      front2back_fifo_in.pcpn[i] = ptab_out.pcpn[i];
      for (int j = 0; j < 4; j++) { // TN_MAX = 4
        front2back_fifo_in.tage_idx[i][j] = ptab_out.tage_idx[i][j];
      }
    }
    DEBUG_LOG_SMALL_3(
        "predecode_checker_out.predict_next_fetch_address_corrected: %x\n",
        predecode_checker_out.predict_next_fetch_address_corrected);
    front2back_fifo_in.predict_next_fetch_address_corrected =
        predecode_checker_out.predict_next_fetch_address_corrected;
  } else {
    // send the result to new FIFO
    front2back_fifo_in.reset = in->reset;
    front2back_fifo_in.refetch = in->refetch;
    front2back_fifo_in.read_enable = in->FIFO_read_enable;
    front2back_fifo_in.write_enable = false;
  }

  // run front2back FIFO
  front2back_FIFO_top(&front2back_fifo_in, &front2back_fifo_out);

  // set output
  out->FIFO_valid = front2back_fifo_out.front2back_FIFO_valid;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out->instructions[i] = front2back_fifo_out.fetch_group[i];
    out->page_fault_inst[i] = front2back_fifo_out.page_fault_inst[i];
    out->predict_dir[i] = front2back_fifo_out.predict_dir_corrected[i];
    out->pc[i] = front2back_fifo_out.predict_base_pc[i];
    out->alt_pred[i] = front2back_fifo_out.alt_pred[i];
    out->altpcpn[i] = front2back_fifo_out.altpcpn[i];
    out->pcpn[i] = front2back_fifo_out.pcpn[i];
    for (int j = 0; j < 4; j++) { // TN_MAX = 4
      out->tage_idx[i][j] = front2back_fifo_out.tage_idx[i][j];
    }
    out->inst_valid[i] = front2back_fifo_out.inst_valid[i];

    // if(out->pc[i] == 0x80000a48) {
    //   DEBUG_LOG_SMALL_2("out->pc[%d]: %x, pred_addr: %x\n", i, out->pc[i],
    //   front2back_fifo_out.predict_next_fetch_address_corrected);
    // }
  }
  DEBUG_LOG_SMALL_3(
      "front2back_fifo_out.predict_next_fetch_address_corrected: %x\n",
      front2back_fifo_out.predict_next_fetch_address_corrected);
  out->predict_next_fetch_address =
      front2back_fifo_out.predict_next_fetch_address_corrected;
}
