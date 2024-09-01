#include "IQ.h"
#include "config.h"

void IQ::init() {

  for (int i = 0; i < IQ_NUM; i++) {
    entry[i].inst.type = NOP;
  }
}

// 重命名后进入IQ
void IQ::IQ_add_inst() {
  int IQ_idx;
  for (int i = 0; i < WAY; i++) {
    if (in.inst[i].type != NOP) {
      assert((IQ_idx = alloc_IQ()) != -1);

      entry[IQ_idx].inst = in.inst[i];
      entry[IQ_idx].src1_ready = true;
      entry[IQ_idx].src2_ready = true;
    }
  }
}

// 仲裁 选择指令发射到对应的FU
Inst_info IQ::IQ_sel_inst() {
  int oldest_bit = 0;
  int oldest_idx = -1;
  int oldest_i;
  for (int i = 0; i < IQ_NUM; i++) {
    if (entry[i].inst.type != NOP && entry[i].src1_ready &&
        entry[i].src2_ready) {

      if (oldest_idx == -1) {
        oldest_bit = entry[i].pos_bit;
        oldest_idx = entry[i].pos_idx;
        oldest_i = i;
      } else {
        if (oldest_bit == entry[i].pos_bit) {
          if (oldest_idx > entry[i].pos_idx) {
            oldest_idx = entry[i].pos_idx;
            oldest_bit = entry[i].pos_bit;
            oldest_i = i;
          }
        } else {
          if (oldest_idx < entry[i].pos_idx) {
            oldest_idx = entry[i].pos_idx;
            oldest_bit = entry[i].pos_bit;
            oldest_i = i;
          }
        }
      }
    }
  }

  Inst_info inst;

  if (oldest_idx == -1)
    inst.type = NOP;
  else {
    inst = entry[oldest_i].inst;
  }

  return inst;
}

void IQ::IQ_awake(int dest_idx) {
  for (int i = 0; i < IQ_NUM; i++) {
    if (entry[i].inst.src1_en && entry[i].inst.src1_idx == dest_idx)
      entry[i].src1_ready = true;
    if (entry[i].inst.src2_en && entry[i].inst.src2_idx == dest_idx)
      entry[i].src2_ready = true;
  }
}

int IQ::alloc_IQ() {
  int i;
  for (i = 0; i < IQ_NUM; i++) {
    if (entry[i].inst.type == NOP)
      return i;
  }

  return -1;
}
