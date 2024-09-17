#include "IQ.h"
#include "config.h"
#include <ROB.h>

void IQ::init() {

  for (int i = 0; i < IQ_NUM; i++) {
    entry[i].inst.type = INVALID;
  }
}

// 重命名后进入IQ
void IQ::seq() {
  int IQ_idx;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.inst[i].type != INVALID) {
      assert((IQ_idx = alloc_IQ()) != -1);

      entry[IQ_idx].pos_idx = in.pos_idx[i];
      entry[IQ_idx].pos_bit = in.pos_bit[i];
      entry[IQ_idx].inst = in.inst[i];
      entry[IQ_idx].src1_ready = in.src1_ready[i];
      entry[IQ_idx].src2_ready = in.src2_ready[i];
    }
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (out.int_entry[i].inst.type != INVALID)
      entry[oldest_i[i]].inst.type = INVALID;
  }

  for (int i = 0; i < AGU_NUM; i++) {
    if (out.mem_entry[i].inst.type != INVALID)
      entry[oldest_i_mem].inst.type = INVALID;
  }
}

// 仲裁 选择指令发射到对应的FU
void IQ::comb() {
  bool oldest_bit;
  int oldest_idx;

  // select n of m
  for (int i = 0; i < ALU_NUM; i++) {
    oldest_i[i] = -1;
  }

  for (int i = 0; i < ALU_NUM; i++) {
    for (int j = 0; j < IQ_NUM; j++) {
      if (entry[j].inst.type != INVALID && entry[j].src1_ready &&
          entry[j].src2_ready)
        continue;

      if (entry[j].inst.type == STYPE || entry[j].inst.type == LTYPE)
        continue;

      bool sel = false;
      for (int k = 0; k < ALU_NUM; k++) {
        if (oldest_i[k] == j) {
          sel = true;
          break;
        }
      }

      if (sel)
        continue;

      if (oldest_i[i] == -1) {
        oldest_i[i] = j;
        oldest_bit = entry[j].pos_bit;
        oldest_idx = entry[j].pos_idx;
      } else {
        if (rob_cmp(entry[j].pos_idx, entry[j].pos_bit, oldest_idx,
                    oldest_bit)) {
          oldest_bit = entry[j].pos_bit;
          oldest_idx = entry[j].pos_idx;
        }
      }
    }
  }

  // select load or store
  for (int i = 0; i < IQ_NUM; i++) {
    if (entry[i].inst.type != INVALID && entry[i].src1_ready &&
        entry[i].src2_ready) {

      if (entry[i].inst.type != STYPE && entry[i].inst.type != LTYPE)
        continue;

      if (oldest_i_mem == -1) {
        oldest_bit = entry[i].pos_bit;
        oldest_idx = entry[i].pos_idx;
        oldest_i_mem = i;
      } else {
        if (rob_cmp(entry[i].pos_idx, entry[i].pos_bit, oldest_idx,
                    oldest_bit)) {
          oldest_bit = entry[i].pos_bit;
          oldest_idx = entry[i].pos_idx;
        }
      }
    }
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (oldest_i[i] != -1) {
      out.int_entry[i] = entry[oldest_i[i]];
    }
  }

  for (int i = 0; i < AGU_NUM; i++) {
    if (oldest_i_mem != -1) {
      out.mem_entry[i] = entry[oldest_i_mem];
    }
  }
}

/*void IQ::IQ_awake(int dest_idx) {*/
/*  for (int i = 0; i < IQ_NUM; i++) {*/
/*    if (entry[i].inst.src1_en && entry[i].inst.src1_idx == dest_idx)*/
/*      entry[i].src1_ready = true;*/
/*    if (entry[i].inst.src2_en && entry[i].inst.src2_idx == dest_idx)*/
/*      entry[i].src2_ready = true;*/
/*  }*/
/*}*/
/**/
int IQ::alloc_IQ() {
  int i;
  for (i = 0; i < IQ_NUM; i++) {
    if (entry[i].inst.type == INVALID)
      return i;
  }

  return -1;
}
