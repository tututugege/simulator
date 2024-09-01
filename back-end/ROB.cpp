#include "ROB.h"
#include "config.h"
void ROB::ROB_enq(bool rob_pos_bit[], int rob_idx[]) {
  for (int i = 0; i < WAY; i++) {
    if (in.type[i] != NOP) {
      entry[enq_ptr].type = in.type[i];
      entry[enq_ptr].dest_preg_idx = in.dest_preg_idx[i];
      entry[enq_ptr].old_dest_preg_idx = in.old_dest_preg_idx[i];
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
    entry[i].type = NOP;
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
