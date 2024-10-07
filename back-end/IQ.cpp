#include "IQ.h"
#include "config.h"

IQ::IQ(int entry_num, int fu_num) {
  this->entry_num = entry_num;
  this->fu_num = fu_num;

  out.inst.resize(fu_num);

  entry.resize(entry_num);
}

void IQ::init() {
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
  }
}

void IQ::seq() {
  for (int i = 0; i < entry_num; i++) {
    entry[i] = entry_1[i];
  }
}

void IQ::comb_0() {
  // 仲裁 选择指令发射到对应的FU 压缩式IQ，直接选择最老的
  int issue_num = 0;
  for (int i = 0; i < entry_num && issue_num < fu_num; i++) {
    if (entry[i].valid && !entry[i].src1_busy && !entry[i].src2_busy) {
      out.inst[issue_num] = entry[i].inst;
      out.valid[issue_num] = true;
      issue_num++;
    }
  }

  while (issue_num < fu_num) {
    out.valid[issue_num] = false;
  }

  // 无效指令 ready为1
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i])
      out.ready[i] = true;
  }

  // 进入IQ
  int num = 0;
  int alloc_iq[INST_WAY];
  for (int i = 0; i < entry_num && num < INST_WAY; i++) {
    if (entry_1[i].valid == false) {
      alloc_iq[num] = i;
      num++;
    }
  }

  // 有效指令，iq不够则对应端口ready为false
  int alloc_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i]) {
      // 分配iq
      if (alloc_num < num) {
        entry_1[alloc_iq[alloc_num]].inst = in.inst[i];
        alloc_num++;
      } else {
        out.ready[i] = false;
      }
    }
  }
}

void IQ::comb_1() {
  out.all_ready = out.ready[0];
  for (int i = 1; i < INST_WAY; i++) {
    out.all_ready = out.ready[i] && out.all_ready;
  }
  out.all_ready = out.all_ready && in.all_ready;
}

void IQ::comb_2() {

  // 成功发射的entry号
  int fire_issue_idx[fu_num];
  int fire_issue_num;
  for (int i = 0; i < fu_num; i++) {
    if (out.valid[i] && in.ready[i]) {
      fire_issue_idx[fire_issue_num] = i;
      fire_issue_num++;
    }
  }

  // 压缩，使得IQ中的指令紧密排列
  for (int i = 0; i < entry_num; i++) {
    int compress_num = 0;
    for (int j = 0; j < fire_issue_num; j++) {
      if (i <= fire_issue_idx[j])
        compress_num++;
    }

    if (entry[i + compress_num].valid)
      entry_1[i] = entry[i + compress_num];
  }

  // 分支处理
  if (in.br.br_taken) {
    for (int i = 0; i < MAX_BR_NUM; i++) {
      if (in.br.br_mask[i]) {
        for (int j = 0; j < entry_num; j++) {
          if (entry[j].valid && entry[j].inst.tag == i)
            entry_1[j].valid = false;
        }
      }
    }
  }
}
