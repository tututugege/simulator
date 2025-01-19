#include "config.h"
#include <ISU.h>
#include <cmath>
#include <util.h>
#include <vector>

void ISU::add_iq(int entry_num, int out_num, IQ_TYPE type) {
  iq.push_back(IQ(entry_num, out_num, type));
  iq_num++;
}

IQ::IQ(int entry_num, int out_num, IQ_TYPE type) {
  vector<Inst_entry> new_iq(entry_num);
  this->entry_num = entry_num;
  this->out_num = out_num;
  this->type = type;

  entry.resize(entry_num);
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
  }
}

void ISU::init() {
  add_iq(8, 4, IQ_INT);
  add_iq(8, 1, IQ_MEM);
  add_iq(8, 1, IQ_CSR);
}

void IQ::enq(Inst_info *inst) {
  int i;
  for (i = 0; i < entry_num; i++) {
    if (entry[i].valid == false) {
      entry[i].inst = *inst;
      entry[i].valid = true;
      num++;
      break;
    }
  }
  assert(i != entry_num);
}

vector<Inst_entry> IQ::deq() {
  vector<Inst_entry> ret = scheduler(OLDEST_FIRST);
  for (auto &e : ret) {
    if (e.valid)
      num--;
  }

  return ret;

  /*while (issue_num < iss_port_num) {*/
  /*  io.iq2isu->valid[issue_num++] = false;*/
  /*}*/
  /**/
  /*// 无效指令 ready为1*/
  /*for (int i = 0; i < iss_port_num; i++) {*/
  /*  if (!io.isu2iq->valid[i])*/
  /*    io.iq2isu->ready[i] = true;*/
  /*}*/
  /**/
  /*// 唤醒load*/
  /*if (type == MEM && in.st_valid) {*/
  /*  for (int i = 0; i < entry_num; i++) {*/
  /*    if (entry[i].valid) {*/
  /*      entry_1[i].inst.pre_store[in.st_idx] = false;*/
  /*    }*/
  /*  }*/
  /*}*/
}

void ISU::comb() {

  // ready
  for (int i = 0; i < INST_WAY; i++) {
    if (io.ren2iss->valid[i]) {
      if (iq[io.ren2iss->inst[i].iq_type].num_temp <
          iq[io.ren2iss->inst[i].iq_type].entry_num) {
        io.iss2ren->ready[i] = true;
        iq[io.ren2iss->inst[i].iq_type].num_temp++;
      }

      else
        io.iss2ren->ready[i] = false;
    } else {
      io.iss2ren->ready[i] = true;
    }
  }

  // 出队
  int issue_idx = 0;
  for (int i = 0; i < iq_num; i++) {
    vector<Inst_entry> iss_entry = iq[i].deq();
    for (auto entry : iss_entry) {
      io.iss2prf->iss_pack[issue_idx][0] = entry;
      issue_idx++;
    }
  }
}

void ISU::seq() {
  // 入队
  for (int i = 0; i < INST_WAY; i++) {
    if (io.ren2iss->dis_fire[i]) {
      for (auto &q : iq) {
        if (q.type == io.ren2iss->inst[i].iq_type) {
          q.enq(&io.ren2iss->inst[i]);
        }
      }
    }
  }
  for (auto &q : iq) {
    q.num_temp = q.num;
  }
}

/*void IQ::comb_enq() {*/
/**/
/*  // 分支处理*/
/*  if (in.br.mispred) {*/
/*    for (int j = 0; j < entry_num; j++) {*/
/*      if (entry[j].valid && in.br.br_mask[entry[j].inst.tag]) {*/
/*        entry_1[j].valid = false;*/
/*      }*/
/*    }*/
/*  }*/
/**/
// 异常处理
/*if (in.rollback) {*/
/*  for (int j = 0; j < entry_num; j++) {*/
/*    entry_1[j].valid = false;*/
/*  }*/
/*  return;*/
/*}*/
/**/
/*// 进入iq*/
/*for (int i = 0; i < INST_WAY; i++) {*/
/*  if (in.dis_fire[i]) {*/
/*    if (in.inst[i].src1_en) {*/
/*      back.int_iq.dependency(in.inst[i].src1_preg);*/
/*      back.ld_iq.dependency(in.inst[i].src1_preg);*/
/*    }*/
/*    if (in.inst[i].src2_en) {*/
/*      back.int_iq.dependency(in.inst[i].src2_preg);*/
/*      back.ld_iq.dependency(in.inst[i].src2_preg);*/
/*    }*/
/**/
/*    entry_1[alloc_idx[i]].inst = in.inst[i];*/
/*    entry_1[alloc_idx[i]].valid = true;*/
/*    num++;*/
/*  }*/
/*}*/
/*}*/

// 唤醒 发射时即可唤醒 下一周期时即可发射 此时结果已经写回寄存器堆
void IQ::wake_up(Inst_info *issue_inst) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {
      if (issue_inst->dest_en &&
          entry[i].inst.src1_preg == issue_inst->dest_preg) {
        entry[i].inst.src1_busy = false;
      }
      if (issue_inst->dest_en &&
          entry[i].inst.src2_preg == issue_inst->dest_preg) {
        entry[i].inst.src2_busy = false;
      }
    }
  }
}

// 调度策略
vector<Inst_entry> IQ::scheduler(Sched_type sched) {

  int issue_num = 0;
  int youngest_idx = -1;
  int valid_num = 0;
  vector<Inst_entry> iss_entry(out_num);

  if (sched == OLDEST_FIRST) {
    // 遍历查找oldest的指令
    int oldest_idx[out_num];
    for (int i = 0; i < out_num; i++)
      oldest_idx[i] = -1;

    for (int i = 0; i < entry_num; i++) {
      // 发射条件 操作数准备好 依赖的STORE完成（无RAW）

      if (entry[i].valid)
        valid_num++;

      if (entry[i].valid && !entry[i].inst.src1_busy &&
          !entry[i].inst.src2_busy &&
          !(entry[i].inst.op == LOAD &&
            orR(entry[i].inst.pre_store, STQ_NUM))) {

        // 如果oldest_idx仍有-1的空位
        int j;
        for (j = 0; j < out_num; j++) {
          if (oldest_idx[j] == -1) {
            oldest_idx[j] = i;
            break;
          }
        }
        if (j != out_num)
          continue;

        // 如果是更老的指令，则将最年轻的指令替换
        int youngest_j;
        youngest_idx = -1;
        for (j = 0; j < out_num; j++) {
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

    for (int i = 0; i < out_num; i++) {
      if (oldest_idx[i] != -1) {
        iss_entry[issue_num] = entry[oldest_idx[i]];
        entry[oldest_idx[i]].valid = false;
        issue_num++;
      }
    }
  }

  // 退化为顺序多发射
  /*} else if (sched == IN_ORDER) {*/
  // 遍历查找oldest的指令
  /*  int oldest_idx[out_num];*/
  /*  for (int i = 0; i < out_num; i++)*/
  /*    oldest_idx[i] = -1;*/
  /**/
  /*  for (int i = 0; i < entry_num; i++) {*/
  /*    if (entry[i].valid) {*/
  /*      valid_num++;*/
  /**/
  /*      // 如果oldest_idx仍有-1的空位*/
  /*      int j;*/
  /*      for (j = 0; j < out_num; j++) {*/
  /*        if (oldest_idx[j] == -1) {*/
  /*          oldest_idx[j] = i;*/
  /*          break;*/
  /*        }*/
  /*      }*/
  /*      if (j != out_num)*/
  /*        continue;*/
  /**/
  /*      // 如果是更老的指令，则将最年轻的指令替换*/
  /*      int youngest_j;*/
  /*      youngest_idx = -1;*/
  /*      for (j = 0; j < out_num; j++) {*/
  /*        if (youngest_idx == -1) {*/
  /*          youngest_idx = oldest_idx[j];*/
  /*          youngest_j = j;*/
  /*          continue;*/
  /*        }*/
  /**/
  /*        if (entry[oldest_idx[j]].inst.inst_idx >*/
  /*            entry[youngest_idx].inst.inst_idx) {*/
  /*          youngest_idx = oldest_idx[j];*/
  /*          youngest_j = j;*/
  /*        }*/
  /*      }*/
  /**/
  /*      if (entry[i].inst.inst_idx < entry[youngest_idx].inst.inst_idx) {*/
  /*        oldest_idx[youngest_j] = i;*/
  /*      }*/
  /*    }*/
  /*  }*/
  /**/
  /*  for (int i = 0; i < out_num; i++) {*/
  /*    if (oldest_idx[i] != -1) {*/
  /*      int idx = oldest_idx[i];*/
  /*      if (!entry[idx].inst.src1_busy && !entry[idx].inst.src2_busy &&*/
  /*          !(entry[idx].inst.op == LOAD &&*/
  /*            orR(entry[idx].inst.pre_store, STQ_NUM))) {*/
  /**/
  /*        iss_entry[issue_num] = entry[idx];*/
  /*        issue_num++;*/
  /*      }*/
  /*    }*/
  /*  }*/
  /*} else if (sched == INDEX) {*/
  /*  for (int i = 0; i < entry_num && issue_num < out_num; i++) {*/
  /*    if (entry[i].valid)*/
  /*      valid_num++;*/
  /**/
  /*    if (entry[i].valid && !entry[i].inst.src1_busy &&*/
  /*        !entry[i].inst.src2_busy &&*/
  /*        !(entry[i].inst.op == LOAD &&*/
  /*          orR(entry[i].inst.pre_store, STQ_NUM))) {*/
  /**/
  /*      iss_entry[issue_num] = entry[i];*/
  /*      issue_num++;*/
  /*    }*/
  /*  }*/
  /*} else if (sched == DEPENDENCY) {*/
  /*  int min_idx = -1;*/
  /*  int oldest_idx[out_num];*/
  /*  for (int i = 0; i < entry_num; i++) {*/
  /*    if (entry[i].valid)*/
  /*      valid_num++;*/
  /**/
  /*    if (entry[i].valid && !entry[i].inst.src1_busy &&*/
  /*        !entry[i].inst.src2_busy &&*/
  /*        !(entry[i].inst.op == LOAD &&*/
  /*          orR(entry[i].inst.pre_store, STQ_NUM))) {*/
  /**/
  /*      // 如果oldest_idx仍有-1的空位*/
  /*      int j;*/
  /*      for (j = 0; j < out_num; j++) {*/
  /*        if (oldest_idx[j] == -1) {*/
  /*          oldest_idx[j] = i;*/
  /*          break;*/
  /*        }*/
  /*      }*/
  /*      if (j != out_num)*/
  /*        continue;*/
  /**/
  /*      // 如果是被依赖得更少的指令，则将被依赖最少的指令替换*/
  /*      int min_j;*/
  /*      min_idx = -1;*/
  /*      for (j = 0; j < out_num; j++) {*/
  /*        if (min_idx == -1) {*/
  /*          min_idx = oldest_idx[j];*/
  /*          min_j = j;*/
  /*          continue;*/
  /*        }*/
  /**/
  /*        if (entry[oldest_idx[j]].inst.dependency <*/
  /*            entry[youngest_idx].inst.dependency) {*/
  /*          min_idx = oldest_idx[j];*/
  /*          min_j = j;*/
  /*        }*/
  /*      }*/
  /**/
  /*      if (entry[i].inst.dependency > entry[youngest_idx].inst.dependency)
   * {*/
  /*        oldest_idx[min_j] = i;*/
  /*      }*/
  /*    }*/
  /*  }*/
  /**/
  /*  for (int i = 0; i < out_num; i++) {*/
  /*    if (oldest_idx[i] != -1) {*/
  /*      iss_entry[issue_num] = entry[oldest_idx[i]];*/
  /*      issue_num++;*/
  /*    }*/
  /*  }*/
  /*} else if (sched == GREEDY) {*/
  /*  for (int j = 0; j < entry_num; j++) {*/
  /*    if (entry[j].valid)*/
  /*      valid_num++;*/
  /*  }*/
  /**/
  /*if (type == INT) {*/
  /*  search_optimal();*/
  /**/
  /*  for (int i : optimal_int_idx) {*/
  /*    for (int j = 0; j < entry_num; j++) {*/
  /*      if (entry[j].valid && entry[j].inst.rob_idx == i) {*/
  /*        out.inst[issue_num] = entry[j].inst;*/
  /*        out.valid[issue_num] = true;*/
  /*        entry_1[j].valid = false;*/
  /*      }*/
  /*    }*/
  /*    issue_num++;*/
  /*  }*/
  /*} else if (type == LD) {*/
  /*  for (int i : optimal_ld_idx) {*/
  /*    for (int j = 0; j < entry_num; j++) {*/
  /*      if (entry[j].valid && entry[j].inst.rob_idx == i) {*/
  /*        out.inst[issue_num] = entry[j].inst;*/
  /*        out.valid[issue_num] = true;*/
  /*        entry_1[j].valid = false;*/
  /*        issue_num++;*/
  /*      }*/
  /*    }*/
  /*  }*/
  /*} else {*/
  /**/
  /*  for (int i : optimal_st_idx) {*/
  /*    for (int j = 0; j < entry_num; j++) {*/
  /*      if (entry[j].valid && entry[j].inst.rob_idx == i) {*/
  /*        out.inst[issue_num] = entry[j].inst;*/
  /*        out.valid[issue_num] = true;*/
  /*        entry_1[j].valid = false;*/
  /*        issue_num++;*/
  /*        break;*/
  /*      }*/
  /*    }*/
  /*  }*/
  /*}*/
  /*}*/

  /*valid_num = (valid_num > out_num) ? out_num : valid_num;*/
  /*stall_num[this->type] += (valid_num - issue_num);*/

  return iss_entry;
}

/*void IQ::dependency(int dest_idx) {*/
/*  for (int i = 0; i < entry_num; i++) {*/
/*    if (entry_1[i].valid && entry_1[i].inst.dest_en &&*/
/*        entry_1[i].inst.dest_preg == dest_idx) {*/
/*      entry_1[i].inst.dependency++;*/
/*    }*/
/*  }*/
/*}*/
