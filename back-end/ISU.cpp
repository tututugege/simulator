#include "TOP.h"
#include "config.h"
#include <ISU.h>
#include <iostream>
#include <util.h>
#include <vector>

extern Back_Top back;

int isu_stall[ISSUE_WAY];
int isu_ready_num[ISSUE_WAY];
int raw_stall_num[ISSUE_WAY];

void ISU::add_iq(int entry_num, IQ_TYPE type) {
  iq.push_back(IQ(entry_num, type));
}

IQ::IQ(int entry_num, IQ_TYPE type) {
  vector<Inst_entry> new_iq(entry_num);
  this->entry_num = entry_num;
  this->type = type;
  this->num = 0;
  this->num_temp = 0;

  entry.resize(entry_num);
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
  }
}

void ISU::init() {
  add_iq(16, IQ_INTM);
  add_iq(16, IQ_INTD);
  add_iq(16, IQ_INT0);
  add_iq(16, IQ_INT1);
  add_iq(16, IQ_LD);
  add_iq(16, IQ_STA);
  add_iq(16, IQ_STD);
  add_iq(MAX_BR_NUM / 2, IQ_BR0);
  add_iq(MAX_BR_NUM / 2, IQ_BR1);
}

void IQ::enq(Inst_uop *inst) {
  int i;

  for (i = 0; i < entry_num; i++) {
    if (entry[i].valid == false) {
      entry[i].uop = *inst;
      entry[i].valid = true;
      num++;
      break;
    }
  }
  if (i == entry_num) {
    cout << "error";
  }
}

// void IQ::update_prior(Inst_uop &uop) {
//   for (int i = 0; i < entry_num; i++) {
//     if (entry[i].valid) {
//       if (entry[i].uop.dest_en && uop.src1_en &&
//           entry[i].uop.dest_preg == uop.src1_preg) {
//         if (entry[i].uop.prior < 3) {
//           entry[i].uop.prior++;
//         }
//       }
//
//       if (entry[i].uop.dest_en && uop.src2_en &&
//           entry[i].uop.dest_preg == uop.src2_preg) {
//         if (entry[i].uop.prior < 3) {
//           entry[i].uop.prior++;
//         }
//       }
//     }
//   }
//
//   if (is_branch(uop.op) && uop.br_conf < 3) {
//     for (int i = 0; i < entry_num; i++) {
//       if (entry[i].valid && entry[i].uop.tag == uop.tag) {
//         if (entry[i].uop.prior < 3) {
//           entry[i].uop.prior++;
//         }
//       }
//     }
//   }
// }

Inst_entry IQ::deq() {

  Inst_entry ret = scheduler();
  if (ret.valid) {
    num--;
  }

  if (num > 0 && !ret.valid) {
    raw_stall_num[type]++;
  }

  return ret;
}

void ISU::comb_ready() {
  // ready
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->valid[i]) {
      if (iq[io.ren2iss->uop[i].iq_type].num_temp <
          iq[io.ren2iss->uop[i].iq_type].entry_num) {
        io.iss2ren->ready[i] = true;
        iq[io.ren2iss->uop[i].iq_type].num_temp++;
      } else {
        io.iss2ren->ready[i] = false;
        isu_stall[io.ren2iss->uop[i].iq_type]++;
      }
    } else {
      io.iss2ren->ready[i] = true;
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
      io.iss2ren->wake[i].valid = true;
      io.iss2ren->wake[i].preg = io.iss2prf->iss_entry[i].uop.dest_preg;
    } else {
      io.iss2ren->wake[i].valid = false;
    }
  }
}

void ISU::seq() {
  // 入队
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->dis_fire[i]) {

      for (auto &q : iq) {
        if (q.type == io.ren2iss->uop[i].iq_type) {
          if (q.type == IQ_LD) {
            for (int j = 0; j < iq[IQ_STA].entry_num; j++) {
              if (iq[IQ_STA].entry[j].valid)
                io.ren2iss->uop[i].pre_sta[j] = true;
              else
                io.ren2iss->uop[i].pre_sta[j] = false;
            }

            for (int j = 0; j < iq[IQ_STD].entry_num; j++) {
              if (iq[IQ_STD].entry[j].valid)
                io.ren2iss->uop[i].pre_std[j] = true;
              else
                io.ren2iss->uop[i].pre_std[j] = false;
            }
          }
          q.enq(&io.ren2iss->uop[i]);
          break;
        }

        // for (auto &q : iq) {
        //   q.update_prior(io.ren2iss->uop[i]);
        // }
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

    if (io.awake->wake.valid) {
      for (auto &q : iq) {
        q.wake_up(io.awake->wake.preg);
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

    for (auto &q : iq) {
      q.num_temp = q.num;
    }

    for (auto q : iq)
      for (auto e : q.entry)
        if (e.valid && (!e.uop.src1_en || !e.uop.src1_busy) &&
            (!e.uop.src2_en || !e.uop.src2_busy) &&
            !(is_load(e.uop.op) &&
              (orR(e.uop.pre_sta, 16) || orR(e.uop.pre_std, 16)))) {
          isu_ready_num[q.type]++;
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

void IQ::sta_wake_up(int idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry[j].uop.pre_sta[idx] = false;
  }
}

void IQ::std_wake_up(int idx) {
  for (int j = 0; j < entry_num; j++) {
    if (entry[j].valid)
      entry[j].uop.pre_std[idx] = false;
  }
}

/*#define CONFIG_PRIOR*/

// 调度策略
Inst_entry IQ::scheduler() {

  Inst_entry iss_entry;
  int iss_idx;
  iss_entry.valid = false;

  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && (!entry[i].uop.src1_en || !entry[i].uop.src1_busy) &&
        (!entry[i].uop.src2_en || !entry[i].uop.src2_busy) &&
        !(is_load(entry[i].uop.op) &&
          (orR(entry[i].uop.pre_sta, 16) || orR(entry[i].uop.pre_std, 16)))) {
      // #ifdef CONFIG_PRIOR
      // if (!iss_entry.valid) {
      //   iss_entry = entry[i];
      //   iss_idx = i;
      // } else {
      //
      //   if (iss_entry.uop.prior < entry[i].uop.prior) {
      //     iss_entry = entry[i];
      //     iss_idx = i;
      //   } else if (iss_entry.uop.prior == entry[i].uop.prior &&
      //              iss_entry.uop.inst_idx > entry[i].uop.inst_idx) {
      //     iss_entry = entry[i];
      //     iss_idx = i;
      //   }
      // }
      // #else
      // if (!iss_entry.valid || iss_entry.uop.inst_idx > entry[i].uop.inst_idx)
      // {
      //   iss_entry = entry[i];
      //   iss_idx = i;
      // }
      //
      // #endif
      iss_entry = entry[i];
      iss_idx = i;
      break;
    }
  }

  if (iss_entry.valid) {
    // 唤醒load
    if (is_sta(iss_entry.uop.op)) {
      back.isu.iq[IQ_LD].sta_wake_up(iss_idx);
    }

    if (is_std(iss_entry.uop.op)) {
      back.isu.iq[IQ_LD].std_wake_up(iss_idx);
    }

    entry[iss_idx].valid = false;
  }

  return iss_entry;
}
