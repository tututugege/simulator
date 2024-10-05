#include "IQ.h"
#include "config.h"
#include <ROB.h>

IQ::IQ(int entry_num, int fu_num)
    : entry(fu_num, INST_WAY, entry_num, sizeof(Inst_info) * 8) {
  this->entry_num = entry_num;
  this->fu_num = fu_num;

  out.inst.resize(fu_num);

  valid.resize(entry_num);
  pos_bit.resize(entry_num); // rob位置信息 用于仲裁找出最老指令
  pos_idx.resize(entry_num);
  src1_ready.resize(entry_num);
  src2_ready.resize(entry_num);

  valid_1.resize(entry_num);
  pos_bit_1.resize(entry_num);
  pos_idx_1.resize(entry_num);
  src1_ready_1.resize(entry_num);
  src2_ready_1.resize(entry_num);
}

void IQ::init() {
  for (int i = 0; i < entry_num; i++) {
    valid[i] = false;
  }
}

// 重命名后进入IQ
void IQ::seq() {
  int IQ_idx;
  entry.write();
  for (int i = 0; i < entry_num; i++) {
    valid[i] = valid_1[i];
    pos_idx[i] = pos_idx_1[i];
    pos_bit[i] = pos_bit_1[i];
    src1_ready[i] = src1_ready_1[i];
    src2_ready[i] = src2_ready_1[i];
  }
}

// 仲裁 选择指令发射到对应的FU
void IQ::comb() {

  int IQ_idx[INST_WAY];
  alloc_IQ(IQ_idx);
  // 指令进入发射队列
  bool full = (count_1 == entry_num);
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i] && !(in.br_taken && in.inst[i].tag == in.br_tag)) {
      out.ready[i] = true;
      valid_1[IQ_idx[i]] = true;
      pos_idx_1[IQ_idx[i]] = in.pos_idx[i];
      pos_bit_1[IQ_idx[i]] = in.pos_bit[i];
      src1_ready_1[IQ_idx[i]] = in.src1_ready[i];
      src2_ready_1[IQ_idx[i]] = in.src2_ready[i];

      entry.to_sram.we[i] = true;
      entry.to_sram.waddr[i] = IQ_idx[i];
      entry.to_sram.wdata[i] = in.inst[i];
    } else {
      out.ready[i] = false;
      entry.to_sram.we[i] = false;
    }
  }

  bool oldest_bit;
  int oldest_idx;
  int oldest_i[fu_num];

  // select n of m
  for (int i = 0; i < fu_num; i++) {
    oldest_i[i] = -1;
  }

  for (int i = 0; i < fu_num; i++) {
    for (int j = 0; j < entry_num; j++) {
      if (!valid[j] || !src1_ready[j] || !src2_ready[j] ||
          in.br_taken && tag[j] == in.br_tag)
        continue;

      bool sel = false;
      for (int k = 0; k < fu_num; k++) {
        if (oldest_i[k] == j) {
          sel = true;
          break;
        }
      }

      if (sel)
        continue;

      if (oldest_i[i] == -1) {
        oldest_i[i] = j;
        oldest_bit = pos_bit[j];
        oldest_idx = pos_idx[j];
      } else {
        if (rob_cmp(pos_idx[j], pos_bit[j], oldest_idx, oldest_bit)) {
          oldest_bit = pos_bit[j];
          oldest_idx = pos_idx[j];
        }
      }
    }
  }

  // 发射指令
  for (int i = 0; i < fu_num; i++) {
    if (oldest_i[i] != -1) {
      entry.to_sram.raddr[i] = oldest_i[i];
      valid_1[oldest_i[i]] = false;
      out.valid[i] = true;
    } else {
      out.valid[i] = false;
    }
  }
  entry.read();

  for (int i = 0; i < fu_num; i++) {
    if (oldest_i[i] != -1) {
      out.inst[i] = entry.from_sram.rdata[i];
      out.pos_idx[i] = pos_idx[oldest_i[i]];
    }
  }

  // 清理发射队列
  for (int i = 0; i < IQ_NUM; i++) {
    if (in.br_taken && tag[i] == in.br_tag) {
      valid_1[i] = false;
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

void IQ::alloc_IQ(int *IQ_idx) {
  int i;
  int num = 0;
  for (i = 0; i < IQ_NUM && num < INST_WAY; i++) {
    if (valid[i] == false) {
      IQ_idx[num] = i;
      num++;
    }
  }

  assert(num == INST_WAY);
}
