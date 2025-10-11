#include "front_IO.h"
#include "front_module.h"
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
    }
#ifdef USE_TRUE_ICACHE
    // get icache_read_ready signal for this cycle
    icache_in.reset = in->reset;
    icache_in.refetch = in->refetch;
    icache_in.run_comb_only = true;
    icache_top(&icache_in, &icache_out);
#endif
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
    // read is allow only if Backend wants to read and icache has already prepared new instructions
    fifo_in.read_enable = in->FIFO_read_enable && 
      (icache_out.icache_read_complete || (!fifo_out.empty && !in->refetch));
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
    // ptab_in.read_enable = in->FIFO_read_enable && icache_out.icache_read_complete;
    ptab_in.read_enable = fifo_in.read_enable;
    ptab_in.write_enable = bpu_out.PTAB_write_enable && icache_out.icache_read_ready;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      ptab_in.predict_dir[i] = bpu_out.predict_dir[i];
      ptab_in.predict_base_pc[i] = bpu_out.predict_base_pc[i];
      ptab_in.alt_pred[i] = bpu_out.alt_pred[i];
      ptab_in.altpcpn[i] = bpu_out.altpcpn[i];
      ptab_in.pcpn[i] = bpu_out.pcpn[i];
    }
    ptab_in.predict_next_fetch_address = bpu_out.predict_next_fetch_address;

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
    fifo_in.read_enable = in->FIFO_read_enable;
    fifo_in.write_enable = false;

    ptab_in.reset = false;
    ptab_in.refetch = false;
    ptab_in.read_enable = in->FIFO_read_enable;
    ptab_in.write_enable = false;
  }

  // run FIFO
  instruction_FIFO_top(&fifo_in, &fifo_out);

  // run PTAB
  PTAB_top(&ptab_in, &ptab_out);

  // set output
  out->FIFO_valid = fifo_out.FIFO_valid;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out->instructions[i] = fifo_out.instructions[i];
    out->page_fault_inst[i] = fifo_out.page_fault_inst[i];
    out->predict_dir[i] = ptab_out.predict_dir[i];
    out->pc[i] = ptab_out.predict_base_pc[i];
    out->alt_pred[i] = ptab_out.alt_pred[i];
    out->altpcpn[i] = ptab_out.altpcpn[i];
    out->pcpn[i] = ptab_out.pcpn[i];
#ifdef USE_TRUE_ICACHE
    out->inst_valid[i] = fifo_out.inst_valid[i];
#else
    out->inst_valid[i] = true; // when not using true icache model, all instructions are valid
#endif
  }
  out->predict_next_fetch_address = ptab_out.predict_next_fetch_address;
}
