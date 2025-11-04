#include "config.h"
#include <ISU.h>
#include <util.h>
#include <vector>

void ISU::add_iq(int entry_num, IQ_TYPE type) {
  iq.push_back(IQ(entry_num, type));
}

IQ::IQ(int entry_num, IQ_TYPE type) {
  vector<Inst_entry> new_iq(entry_num);
  this->entry_num = entry_num;
  this->type = type;
  this->num = 0;

  entry.resize(entry_num);
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
  }
}

void ISU::init() {
  add_iq(16, IQ_INTM);
  add_iq(16, IQ_INTD);
  add_iq(16, IQ_LD);
  add_iq(16, IQ_STA);
  add_iq(16, IQ_STD);
  add_iq(MAX_BR_NUM / 2, IQ_BR0);
  add_iq(MAX_BR_NUM / 2, IQ_BR1);
}

void IQ::enq(Inst_uop &inst) {
  int i;

  for (i = 0; i < entry_num; i++) {
    if (entry[i].valid == false) {
      entry[i].uop = inst;
      entry[i].valid = true;
      num++;
      break;
    }
  }
}

Inst_entry IQ::deq() {

  Inst_entry ret = scheduler();
  if (ret.valid) {
    num--;
  }

  return ret;
}

void ISU::comb_ready() {
  // ready
  for (int i = 0; i < IQ_NUM; i++) {
    if (iq[i].entry_num - iq[i].num >= 2) {
      io.iss2dis->ready[i][0] = true;
      io.iss2dis->ready[i][1] = true;
    } else if (iq[i].entry_num - iq[i].num == 1) {
      io.iss2dis->ready[i][0] = true;
      io.iss2dis->ready[i][1] = false;
    } else {
      io.iss2dis->ready[i][0] = false;
      io.iss2dis->ready[i][1] = false;
    }
  }
}

void ISU::comb_deq() {

  // 出队
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.exe2iss->ready[i])
      io.iss2prf->iss_entry[i] = iq[i].deq();
    else
      io.iss2prf->iss_entry[i].valid = false;
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        io.iss2prf->iss_entry[i].uop.dest_en) {
      io.iss_awake->wake[i].valid = true;
      io.iss_awake->wake[i].preg = io.iss2prf->iss_entry[i].uop.dest_preg;
    } else {
      io.iss_awake->wake[i].valid = false;
    }
  }
}

void ISU::seq() {
  // 入队
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < 2; j++) {
      if (io.dis2iss->dis_fire[i][j]) {
        if (i == IQ_LD) {
          for (int k = 0; k < iq[IQ_STA].entry_num; k++) {
            if (iq[IQ_STA].entry[k].valid) {
              io.dis2iss->uop[i][j].pre_sta_mask |=
                  (1 << iq[IQ_STA].entry[k].uop.stq_idx);
            }
          }

          for (int k = 0; k < iq[IQ_STD].entry_num; k++) {
            if (iq[IQ_STD].entry[k].valid) {
              io.dis2iss->uop[i][j].pre_std_mask |=
                  (1 << iq[IQ_STD].entry[k].uop.stq_idx);
            }
          }
        }
        iq[i].enq(io.dis2iss->uop[i][j]);
      }
    }
  }

  // 唤醒
  for (int i = 0; i < ALU_NUM; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        io.iss2prf->iss_entry[i].uop.dest_en) {
      for (auto &q : iq) {
        q.wake_up(io.iss2prf->iss_entry[i].uop.dest_preg);
      }
    }
  }

  // 唤醒load
  if (io.iss2prf->iss_entry[IQ_STA].valid) {
    iq[IQ_LD].sta_wake_up(io.iss2prf->iss_entry[IQ_STA].uop.stq_idx);
  }

  if (io.iss2prf->iss_entry[IQ_STD].valid) {
    iq[IQ_LD].std_wake_up(io.iss2prf->iss_entry[IQ_STD].uop.stq_idx);
  }

  if (io.prf_awake->wake.valid) {
    for (auto &q : iq) {
      q.wake_up(io.prf_awake->wake.preg);
    }
  }

  // 分支处理
  if (io.dec_bcast->mispred) {
    for (auto &q : iq) {
      q.br_clear(io.dec_bcast->br_mask);
    }
  }

  if (io.rob_bcast->flush) {
    for (auto &q : iq) {
      q.br_clear((1 << MAX_BR_NUM) - 1);
    }
  }
}

void IQ::br_clear(uint32_t br_mask) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && ((1 << entry[i].uop.tag) & br_mask)) {
      entry[i].valid = false;
      num--;
    }
  }
}

// 唤醒 发射时即可唤醒 下一周期时即可发射 此时结果已经写回寄存器堆
void IQ::wake_up(uint32_t dest_preg) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {
      if (entry[i].uop.src1_en && entry[i].uop.src1_preg == dest_preg) {
        entry[i].uop.src1_busy = false;
      }

      if (entry[i].uop.src2_en && entry[i].uop.src2_preg == dest_preg) {
        entry[i].uop.src2_busy = false;
      }
    }
  }
}

void IQ::sta_wake_up(int stq_idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry[j].uop.pre_sta_mask &= ~(1 << stq_idx);
  }
}

void IQ::std_wake_up(int stq_idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry[j].uop.pre_std_mask &= ~(1 << stq_idx);
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
    entry[iss_idx].valid = false;
  }

  return iss_entry;
}
