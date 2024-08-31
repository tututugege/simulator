#include "IQ.h"
#include "config.h"

// 重命名后进入IQ
void IQ::IQ_add_inst() {
  int IQ_idx;
  for (int i = 0; i < WAY; i++) {
    if (in.op[i] != NOP) {
      assert((IQ_idx = alloc_IQ()) != -1);
      entry[IQ_idx].src1_idx = in.src1_preg_idx[i];
      entry[IQ_idx].src2_idx = in.src2_preg_idx[i];
      entry[IQ_idx].dest_idx = in.dest_preg_idx[i];

      entry[IQ_idx].src1_en = in.src1_preg_en[i];
      entry[IQ_idx].src2_en = in.src2_preg_en[i];
      entry[IQ_idx].dest_en = in.dest_preg_en[i];

      entry[IQ_idx].src1_ready = true;
      entry[IQ_idx].src2_ready = true;

      entry[IQ_idx].op = in.op[i];
    }
  }
}

// 发射指令到对应的FU
void IQ::IQ_sel_inst() {}

int IQ::alloc_IQ() {
  int i;
  for (i = 0; i < IQ_NUM; i++) {
    if (entry[i].op == NOP)
      return i;
  }

  return -1;
}
