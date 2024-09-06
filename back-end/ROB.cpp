#include "ROB.h"
#include "config.h"

void ROB::ROB_enq(bool rob_pos_bit[], int rob_idx[]) {
  for (int i = 0; i < WAY; i++) {
    if (in.op[i] != NONE) {
      entry[enq_ptr].PC = in.PC[i];
      entry[enq_ptr].op = in.op[i];
      entry[enq_ptr].dest_preg_idx = in.dest_preg_idx[i];
      entry[enq_ptr].dest_areg_idx = in.dest_areg_idx[i];
      entry[enq_ptr].dest_en = in.dest_en[i];
      entry[enq_ptr].old_dest_preg_idx = in.old_dest_preg_idx[i];
      entry[enq_ptr].complete = false;
      rob_pos_bit[i] = entry[enq_ptr].pos_bit;
      rob_idx[i] = enq_ptr;
      enq_ptr = (enq_ptr + 1) % ROB_NUM;
      if (enq_ptr == 0)
        pos_invert();
      count++;
    }
  }
}

void ROB::init() {

  for (int i = 0; i < WAY; i++) {
    entry[i].op = NONE;
    entry[i].pos_bit = 0;
  }
  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}

void ROB::pos_invert() {

  for (int i = 0; i < WAY; i++) {
    entry[i].pos_bit = !entry[i].pos_bit;
  }
}

uint32_t ROB::get_pc(int idx) { return entry[idx].PC; }

void ROB::complete(int idx) { entry[idx].complete = true; }
void ROB::branch(int idx) { entry[idx].branch = true; }

ROB_entry ROB::commit() {
  ROB_entry ret = entry[deq_ptr];
  if (ret.complete) {
    entry[deq_ptr].complete = false;
    entry[deq_ptr].branch = false;
    entry[deq_ptr].op = NONE;
    deq_ptr = (deq_ptr + 1) % ROB_NUM;
    if (deq_ptr == 0)
      pos_invert();
    count--;
  }

  return ret;
}

void ROB::store(int idx, uint32_t address, uint32_t data) {

  entry[idx].store_addr = address;
  entry[idx].store_data = data;
}
