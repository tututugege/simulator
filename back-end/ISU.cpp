#include "ISU.h"
#include "DAG.h"
#include "TOP.h"
#include "config.h"
#include "util.h"
#include <iostream>

int stall_num[3];
extern Back_Top back;

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

  if (type == ST)
    issue_num = scheduler(OLDEST_FIRST);
  else if (type == LD)
    issue_num = scheduler(OLDEST_FIRST);
  else {
    issue_num = scheduler(GREEDY);
    for (int i = 0; i < issue_num; i++) {
      cout << hex << out.inst[i].pc << " ";
    }
    cout << endl;
  }

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
      if (in.inst[i].src1_en) {
        back.int_iq.dependency(in.inst[i].src1_preg);
        back.ld_iq.dependency(in.inst[i].src1_preg);
      }
      if (in.inst[i].src2_en) {
        back.int_iq.dependency(in.inst[i].src2_preg);
        back.ld_iq.dependency(in.inst[i].src2_preg);
      }

      entry_1[alloc_idx[i]].inst = in.inst[i];
      entry_1[alloc_idx[i]].valid = true;
      num++;
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
  int valid_num = 0;

  if (sched == OLDEST_FIRST) {
    // 遍历查找oldest的指令
    int oldest_idx[fu_num];
    for (int i = 0; i < fu_num; i++)
      oldest_idx[i] = -1;

    for (int i = 0; i < entry_num; i++) {
      // 发射条件 操作数准备好 依赖的STORE完成（无RAW）

      if (entry[i].valid)
        valid_num++;

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
        youngest_idx = -1;
        for (j = 0; j < fu_num; j++) {
          if (youngest_idx == -1) {
            youngest_idx = oldest_idx[j];
            youngest_j = j;
            continue;
          }

          if (entry[oldest_idx[j]].inst.inst_idx >
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
        out.inst[issue_num] = entry[oldest_idx[i]].inst;
        out.valid[issue_num] = true;
        entry_1[oldest_idx[i]].valid = false;
        issue_num++;
      }
    }

    // 退化为顺序多发射
  } else if (sched == IN_ORDER) {
    // 遍历查找oldest的指令
    int oldest_idx[fu_num];
    for (int i = 0; i < fu_num; i++)
      oldest_idx[i] = -1;

    for (int i = 0; i < entry_num; i++) {
      if (entry[i].valid) {
        valid_num++;

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
        youngest_idx = -1;
        for (j = 0; j < fu_num; j++) {
          if (youngest_idx == -1) {
            youngest_idx = oldest_idx[j];
            youngest_j = j;
            continue;
          }

          if (entry[oldest_idx[j]].inst.inst_idx >
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
        int idx = oldest_idx[i];
        if (!entry[idx].inst.src1_busy && !entry[idx].inst.src2_busy &&
            !(type == LD && orR(entry[idx].inst.pre_store, STQ_NUM))) {

          out.inst[issue_num] = entry[idx].inst;
          out.valid[issue_num] = true;
          entry_1[idx].valid = false;
          issue_num++;
        }
      }
    }
  } else if (sched == INDEX) {
    for (int i = 0; i < entry_num && issue_num < fu_num; i++) {
      if (entry[i].valid)
        valid_num++;

      if (entry[i].valid && !entry[i].inst.src1_busy &&
          !entry[i].inst.src2_busy &&
          !(type == LD && orR(entry[i].inst.pre_store, STQ_NUM))) {

        out.inst[issue_num] = entry[i].inst;
        out.valid[issue_num] = true;
        entry_1[i].valid = false;
        issue_num++;
      }
    }
  } else if (sched == DEPENDENCY) {
    int min_idx = -1;
    int oldest_idx[fu_num];
    for (int i = 0; i < entry_num; i++) {
      if (entry[i].valid)
        valid_num++;

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

        // 如果是被依赖得更少的指令，则将被依赖最少的指令替换
        int min_j;
        min_idx = -1;
        for (j = 0; j < fu_num; j++) {
          if (min_idx == -1) {
            min_idx = oldest_idx[j];
            min_j = j;
            continue;
          }

          if (entry[oldest_idx[j]].inst.dependency <
              entry[youngest_idx].inst.dependency) {
            min_idx = oldest_idx[j];
            min_j = j;
          }
        }

        if (entry[i].inst.dependency > entry[youngest_idx].inst.dependency) {
          oldest_idx[min_j] = i;
        }
      }
    }

    for (int i = 0; i < fu_num; i++) {
      if (oldest_idx[i] != -1) {
        out.inst[issue_num] = entry[oldest_idx[i]].inst;
        out.valid[issue_num] = true;
        entry_1[oldest_idx[i]].valid = false;
        issue_num++;
      }
    }
  } else if (sched == GREEDY) {
    for (int j = 0; j < entry_num; j++) {
      if (entry[j].valid)
        valid_num++;
    }

    if (type == INT) {
      search_optimal();

      for (int i : optimal_int_idx) {
        for (int j = 0; j < entry_num; j++) {
          if (entry[j].valid && entry[j].inst.rob_idx == i) {
            out.inst[issue_num] = entry[j].inst;
            out.valid[issue_num] = true;
            entry_1[j].valid = false;
          }
        }
        issue_num++;
      }
    } else if (type == LD) {
      for (int i : optimal_ld_idx) {
        for (int j = 0; j < entry_num; j++) {
          if (entry[j].valid && entry[j].inst.rob_idx == i) {
            out.inst[issue_num] = entry[j].inst;
            out.valid[issue_num] = true;
            entry_1[j].valid = false;
            issue_num++;
          }
        }
      }
    } else {

      for (int i : optimal_st_idx) {
        for (int j = 0; j < entry_num; j++) {
          if (entry[j].valid && entry[j].inst.rob_idx == i) {
            out.inst[issue_num] = entry[j].inst;
            out.valid[issue_num] = true;
            entry_1[j].valid = false;
            issue_num++;
            break;
          }
        }
      }
    }
  }

  valid_num = (valid_num > fu_num) ? fu_num : valid_num;
  stall_num[this->type] += (valid_num - issue_num);

  return issue_num;
}

void IQ::dependency(int dest_idx) {
  for (int i = 0; i < entry_num; i++) {
    if (entry_1[i].valid && entry_1[i].inst.dest_en &&
        entry_1[i].inst.dest_preg == dest_idx) {
      entry_1[i].inst.dependency++;
    }
  }
}
