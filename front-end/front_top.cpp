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
  bpu_in.icache_read_ready = icache_out.icache_read_ready;

  // run BPU
  BPU_top(&bpu_in, &bpu_out);

  // set icache input
  icache_in.reset = in->reset;
  icache_in.icache_read_valid = bpu_out.icache_read_valid;
  icache_in.fetch_address = bpu_out.fetch_address;

  // run icache
  icache_top(&icache_in, &icache_out);

  // set FIFO input
  fifo_in.reset = in->reset;
  fifo_in.refetch = in->refetch;
  fifo_in.read_enable = in->FIFO_read_enable;
  fifo_in.write_enable = icache_out.icache_read_ready;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    fifo_in.fetch_group[i] = icache_out.fetch_group[i];
  }

  // run FIFO
  instruction_FIFO_top(&fifo_in, &fifo_out);

  // set PTAB input
  ptab_in.reset = in->reset;
  ptab_in.refetch = in->refetch;
  ptab_in.read_enable = in->FIFO_read_enable;
  ptab_in.write_enable = bpu_out.PTAB_write_enable;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    ptab_in.predict_dir[i] = bpu_out.predict_dir[i];
    ptab_in.predict_base_pc[i] = bpu_out.predict_base_pc[i];
    ptab_in.alt_pred[i] = bpu_out.alt_pred[i];
    ptab_in.altpcpn[i] = bpu_out.altpcpn[i];
    ptab_in.pcpn[i] = bpu_out.pcpn[i];
  }
  ptab_in.predict_next_fetch_address = bpu_out.predict_next_fetch_address;

  // run PTAB
  PTAB_top(&ptab_in, &ptab_out);

  // set output
  out->FIFO_valid = fifo_out.FIFO_valid;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out->instructions[i] = fifo_out.instructions[i];
    out->predict_dir[i] = ptab_out.predict_dir[i];
    out->pc[i] = ptab_out.predict_base_pc[i];
    out->alt_pred[i] = ptab_out.alt_pred[i];
    out->altpcpn[i] = ptab_out.altpcpn[i];
    out->pcpn[i] = ptab_out.pcpn[i];
  }
  out->predict_next_fetch_address = ptab_out.predict_next_fetch_address;
}
