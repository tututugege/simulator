#include "config.h"
#include <ISU.h>
#include <cstdint>
#include <util.h>
#include <vector>

void ISU::add_iq(int entry_num, int type) { iq.push_back(IQ(entry_num, type)); }

IQ::IQ(int entry_num, int type) {
  vector<Inst_entry> new_iq(entry_num);
  this->entry_num = entry_num;
  this->type = type;
  this->num = this->num_1 = 0;

  entry.resize(entry_num);
  entry_1.resize(entry_num);
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
    entry_1[i].valid = false;
  }
}

void ISU::init() {
  add_iq(32, IQ_INTM);
  add_iq(32, IQ_INTD);
  add_iq(16, IQ_LD);
  add_iq(16, IQ_STA);
  add_iq(16, IQ_STD);
  add_iq(MAX_BR_NUM / 2, IQ_BR0);
  add_iq(MAX_BR_NUM / 2, IQ_BR1);
}

void IQ::enq(Inst_uop &inst) {
  int i;

  for (i = 0; i < entry_num; i++) {
    if (entry_1[i].valid == false) {
      entry_1[i].uop = inst;
      entry_1[i].valid = true;
      num_1++;
      break;
    }
  }
}

Inst_entry IQ::deq() {

  Inst_entry ret = scheduler();

  if (ret.valid) {
    num_1--;
  }

#ifdef CONFIG_PERF_COUNTER
  if (num > 0 && !ret.valid) {
    perf.isu_raw_stall[type]++;
  }
#endif

  return ret;
}

void ISU::comb_ready() {
  // ready
  for (int i = 0; i < IQ_NUM; i++) {
    if (iq[i].entry_num - iq[i].num >= 2) {
      out.iss2dis->ready[i][0] = true;
      out.iss2dis->ready[i][1] = true;
    } else if (iq[i].entry_num - iq[i].num == 1) {
      out.iss2dis->ready[i][0] = true;
      out.iss2dis->ready[i][1] = false;
    } else {
      out.iss2dis->ready[i][0] = false;
      out.iss2dis->ready[i][1] = false;
    }
  }
}

void ISU::comb_deq() {

  // 出队
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.exe2iss->ready[i] && !in.rob_bcast->flush && !in.dec_bcast->mispred)
      out.iss2prf->iss_entry[i] = iq[i].deq(); // deq相当于一个选择逻辑
    else
      out.iss2prf->iss_entry[i].valid = false;
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (out.iss2prf->iss_entry[i].valid &&
        out.iss2prf->iss_entry[i].uop.dest_en) {
      out.iss_awake->wake[i].valid = true;
      out.iss_awake->wake[i].preg = out.iss2prf->iss_entry[i].uop.dest_preg;
      // if (out.iss2prf->iss_entry[i].uop.op == UOP_MUL) {
      //   out.iss_awake->wake[i].latency = 2;
      // } else {
      // out.iss_awake->wake[i].latency = 1;
      // }
    } else {
      out.iss_awake->wake[i].valid = false;
    }
  }
}

void ISU::comb_enq() {
  // 入队
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < 2; j++) {
      if (in.dis2iss->dis_fire[i][j]) {
        if (i == IQ_LD) {
          for (int k = 0; k < iq[IQ_STA].entry_num; k++) {
            // 这里使用entry_1 有隐藏的旁路逻辑
            // 本周期已经发射的就不在内
            // 本周期同时入队的也被处理
            if (iq[IQ_STA].entry_1[k].valid) {
              in.dis2iss->uop[i][j].pre_sta_mask |=
                  (1 << iq[IQ_STA].entry_1[k].uop.stq_idx);
            }
          }

          for (int k = 0; k < iq[IQ_STD].entry_num; k++) {
            if (iq[IQ_STD].entry_1[k].valid) {
              in.dis2iss->uop[i][j].pre_std_mask |=
                  (1 << iq[IQ_STD].entry_1[k].uop.stq_idx);
            }
          }
        }
        iq[i].enq(in.dis2iss->uop[i][j]);
      }
    }
  }
}

void ISU::comb_awake() {
  // 唤醒
  for (int i = 0; i < ALU_NUM; i++) {
    if (out.iss_awake->wake[i].valid) {
      for (auto &q : iq) {
        q.wake_up(out.iss_awake->wake[i].preg, out.iss_awake->wake[i].latency);
      }
    }
  }

  // 唤醒load
  if (out.iss2prf->iss_entry[IQ_STA].valid) {
    iq[IQ_LD].sta_wake_up(out.iss2prf->iss_entry[IQ_STA].uop.stq_idx);
  }

  if (out.iss2prf->iss_entry[IQ_STD].valid) {
    iq[IQ_LD].std_wake_up(out.iss2prf->iss_entry[IQ_STD].uop.stq_idx);
  }

  if (in.prf_awake->wake.valid) {
    for (auto &q : iq) {
      q.wake_up(in.prf_awake->wake.preg, 0);
    }
  }
}

void ISU::comb_branch() {
  // 分支处理
  if (in.dec_bcast->mispred) {
    for (auto &q : iq) {
      q.br_clear(in.dec_bcast->br_mask);
    }
  }
}

void ISU::comb_flush() {
  if (in.rob_bcast->flush) {
    for (auto &q : iq) {
      q.br_clear((1L << MAX_BR_NUM) - 1);
    }
  }
}

void ISU::seq() {
  for (auto &q : iq) {
    q.num = q.num_1;
    q.entry = q.entry_1;
  }

#ifdef CONFIG_PERF_COUNTER
  for (auto q : iq) {
    for (auto e : q.entry) {
      if (e.valid && (!e.uop.src1_en || !e.uop.src1_busy) &&
          (!e.uop.src2_en || !e.uop.src2_busy) &&
          !(is_load_uop(e.uop.op) &&
            (e.uop.pre_sta_mask || e.uop.pre_std_mask))) {
        perf.isu_ready_num[q.type]++;
      }
    }
  }
#endif
}

void IQ::br_clear(uint32_t br_mask) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && ((1 << entry[i].uop.tag) & br_mask)) {
      entry_1[i].valid = false;
      num_1--;
    }
  }
}

// void IQ::latency_wake() {
//   for (int i = 0; i < entry_num; i++) {
//     if (entry[i].valid) {
//       if (entry[i].uop.src1_en && !entry[i].uop.src1_busy &&
//           entry[i].uop.src1_latency != 0) {
//         entry[i].uop.src1_latency--;
//       }
//
//       if (entry[i].uop.src2_en && !entry[i].uop.src2_busy &&
//           entry[i].uop.src2_latency != 0) {
//         entry[i].uop.src2_latency--;
//       }
//     }
//   }
// }

// 唤醒 发射时即可唤醒 下一周期时即可发射 此时结果已经写回寄存器堆
void IQ::wake_up(uint32_t dest_preg, uint32_t latency) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {

      if (entry[i].uop.src1_en && entry[i].uop.src1_preg == dest_preg) {
        entry_1[i].uop.src1_busy = false;
        entry_1[i].uop.src1_latency = latency - 1;
      }

      if (entry[i].uop.src2_en && entry[i].uop.src2_preg == dest_preg) {
        entry_1[i].uop.src2_busy = false;
        entry_1[i].uop.src2_latency = latency - 1;
      }
    }
  }
}

void IQ::sta_wake_up(int stq_idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry_1[j].uop.pre_sta_mask &= ~(1 << stq_idx);
  }
}

void IQ::std_wake_up(int stq_idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry_1[j].uop.pre_std_mask &= ~(1 << stq_idx);
  }
}

// 调度策略
Inst_entry IQ::scheduler() {

  Inst_entry iss_entry;
  int iss_idx;
  iss_entry.valid = false;

  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && (!entry[i].uop.src1_en || !entry[i].uop.src1_busy) &&
        (!entry[i].uop.src2_en || !entry[i].uop.src2_busy) &&
        !(is_load_uop(entry[i].uop.op) &&
          (entry[i].uop.pre_sta_mask || entry[i].uop.pre_std_mask))) {

      // 根据IQ位置判断优先级 也可以随机 或者oldest-first
      // 这里是oldest-first
      if (!iss_entry.valid || cmp_inst_age(iss_entry.uop, entry[i].uop)) {
        iss_entry = entry[i];
        iss_idx = i;
      }
    }
  }

  if (iss_entry.valid) {
    entry_1[iss_idx].valid = false;
  }

  return iss_entry;
}
