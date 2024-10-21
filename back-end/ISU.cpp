#include "ISU.h"
#include "config.h"
#include "util.h"

IQ::IQ(int entry_num, int fu_num, IQ_TYPE type) {
  this->entry_num = entry_num;
  this->fu_num = fu_num;
  this->type = type;

  out.inst.resize(fu_num);
  out.valid.resize(fu_num);

  entry.resize(entry_num);
  entry_1.resize(entry_num);
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
  enq_ptr = enq_ptr_1;
}

void IQ::comb_deq() {
  // 仲裁 选择指令发射到对应的FU 压缩式IQ，直接选择最老的
  int issue_num = 0;
  for (int i = 0; i < entry_num && issue_num < fu_num; i++) {

    // 发射条件 操作数准备好 依赖的STORE完成（无RAW）
    if (entry[i].valid && !entry[i].inst.src1_busy &&
        !entry[i].inst.src2_busy &&
        !(type == LD && orR(entry[i].inst.pre_store, STQ_NUM))) {
      out.inst[issue_num] = entry[i].inst;
      out.valid[issue_num] = true;
      entry_1[i].valid = false;
      issue_num++;
      enq_ptr_1--;
    }
  }

  while (issue_num < fu_num) {
    out.valid[issue_num++] = false;
  }

  // 无效指令 ready为1
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i])
      out.ready[i] = true;
  }

  // 唤醒load
  if (type == LD && in.st_valid) {
    for (int i = 0; i < entry_num; i++) {
      if (entry[i].valid) {
        entry_1[i].inst.pre_store[in.st_idx] = false;
      }
    }
  }
}

void IQ::comb_alloc() {
  // 有效指令，iq不够则对应端口ready为false
  int enq_idx = enq_ptr_1;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i]) {
      if (enq_idx < entry_num) {
        out.ready[i] = true;
        enq_idx++;
      } else {
        out.ready[i] = false;
      }
    }
  }
}

void IQ::comb_enq() {

  // 分支处理
  if (in.br.br_taken) {
    for (int j = 0; j < entry_num; j++) {
      if (entry[j].valid && in.br.br_mask[entry[j].inst.tag]) {
        entry_1[j].valid = false;
        enq_ptr_1--;
        for (int k = 0; k < fu_num; k++)
          if (entry[j].inst.rob_idx == out.inst[k].rob_idx && out.valid[k]) {
            enq_ptr_1++;
            break;
          }
      }
    }
  }

  // 压缩，使得IQ中的指令紧密排列
  for (int i = 0; i < entry_num; i++) {
    int j;
    if (!entry_1[i].valid) {
      for (j = i + 1; j < entry_num; j++) {
        if (entry_1[j].valid) {
          entry_1[i] = entry_1[j];
          entry_1[i].valid = true;
          entry_1[j].valid = false;
          break;
        }
      }

      // 上面的所有都为false
      if (j == entry_num)
        break;
    }
  }

  // 进入 分配iq
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      if (enq_ptr_1 < entry_num) {
        entry_1[enq_ptr_1].inst = in.inst[i];
        entry_1[enq_ptr_1].valid = true;
        out.ready[i] = true;
        enq_ptr_1++;
      } else {
        out.ready[i] = false;
      }
    }
  }
}

// 唤醒 发射时即可唤醒 下一周期时即可发射 此时结果已经写回寄存器堆
void IQ::wake_up(Inst_info *issue_inst) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {
      if (issue_inst->dest_en &&
          entry[i].inst.src1_preg == issue_inst->dest_preg) {
        entry_1[i].inst.src1_busy = false;
      }
      if (issue_inst->dest_en &&
          entry[i].inst.src2_preg == issue_inst->dest_preg) {
        entry_1[i].inst.src2_busy = false;
      }
    }
  }
}
