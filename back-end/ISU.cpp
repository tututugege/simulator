#include "ISU.h"
#include "config.h"
#include "util.h"

IQ::IQ(int entry_num, int fu_num, IQ_TYPE type) {
  this->entry_num = entry_num;
  this->fu_num = fu_num;
  this->type = type;

  alloc_idx.resize(fu_num);
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
}

void IQ::comb_deq() {
  // 仲裁 选择指令发射到对应的FU 压缩式IQ，直接选择最老的
  /*int issue_num = 0;*/
  /*for (int i = 0; i < entry_num && issue_num < fu_num; i++) {*/
  /**/
  /*  // 发射条件 操作数准备好 依赖的STORE完成（无RAW）*/
  /*  if (entry[i].valid && !entry[i].inst.src1_busy &&*/
  /*      !entry[i].inst.src2_busy &&*/
  /*      !(type == LD && orR(entry[i].inst.pre_store, STQ_NUM))) {*/
  /*    out.inst[issue_num] = entry[i].inst;*/
  /*    out.valid[issue_num] = true;*/
  /*    entry_1[i].valid = false;*/
  /*    issue_num++;*/
  /*    enq_ptr_1--;*/
  /*  }*/
  /*}*/

  int issue_num;

  issue_num = scheduler(OLDEST_FIRST);
  while (issue_num < fu_num) {
    out.valid[issue_num++] = false;
  }

  // 无效指令 ready为1
  for (int i = 0; i < fu_num; i++) {
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
  int alloc_num = 0;
  int j = 0;

  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i]) {
      for (; j < entry_num; j++) {
        if (entry[j].valid == false) {
          out.ready[i] = true;
          alloc_idx[i] = j;
          break;
        }
      }

      if (j == entry_num)
        out.ready[i] = false;
      j++;
    } else {
      out.ready[i] = true;
    }
  }
}

void IQ::comb_enq() {

  // 分支处理
  if (in.br.mispred) {
    for (int j = 0; j < entry_num; j++) {
      if (entry[j].valid && in.br.br_mask[entry[j].inst.tag]) {
        entry_1[j].valid = false;
        /*for (int k = 0; k < fu_num; k++)*/
        /*  if (entry[j].inst.rob_idx == out.inst[k].rob_idx && out.valid[k])
         * {*/
        /*    break;*/
        /*  }*/
      }
    }
  }

  // 异常处理
  if (in.rollback) {
    for (int j = 0; j < entry_num; j++) {
      entry_1[j].valid = false;
    }
    return;
  }

  // 进入iq
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      entry_1[alloc_idx[i]].inst = in.inst[i];
      entry_1[alloc_idx[i]].valid = true;
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

// 调度策略
int IQ::scheduler(Sched_type sched) {

  int issue_num = 0;
  int youngest_idx = -1;
  if (sched == OLDEST_FIRST) {
    // 遍历查找oldest的指令
    int oldest_idx[fu_num];
    for (int i = 0; i < fu_num; i++)
      oldest_idx[i] = -1;

    for (int i = 0; i < entry_num; i++) {
      // 发射条件 操作数准备好 依赖的STORE完成（无RAW）
      if (entry[i].valid && !entry[i].inst.src1_busy &&
          !entry[i].inst.src2_busy &&
          !(type == LD && orR(entry[i].inst.pre_store, STQ_NUM))) {

        // 如果oldest_idx仍有-1的空位
        int j;
        for (j = 0; j < fu_num; j++) {
          if (oldest_idx[j] == -1) {
            oldest_idx[j] = i;
            break;
          }
        }
        if (j != fu_num)
          continue;

        // 如果是更老的指令，则将最年轻的指令替换
        int youngest_j;
        for (j = 0; j < fu_num; j++) {
          if (youngest_idx == -1) {
            youngest_idx = oldest_idx[j];
            youngest_j = j;
            continue;
          }

          if (entry[oldest_idx[j]].inst.inst_idx <
              entry[youngest_idx].inst.inst_idx) {
            youngest_idx = oldest_idx[j];
            youngest_j = j;
          }
        }

        if (entry[i].inst.inst_idx < entry[youngest_idx].inst.inst_idx) {
          oldest_idx[youngest_j] = i;
        }
      }
    }

    for (int i = 0; i < fu_num; i++) {
      if (oldest_idx[i] != -1) {
        out.inst[i] = entry[oldest_idx[i]].inst;
        out.valid[i] = true;
        entry_1[oldest_idx[i]].valid = false;
        issue_num++;
      } else {
        out.valid[i] = false;
      }
    }
  } else if (sched == GREEDY) {
  }

  return issue_num;
}
