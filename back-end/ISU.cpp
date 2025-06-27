#include "TOP.h"
#include "config.h"
#include <ISU.h>
#include <cmath>
#include <util.h>
#include <vector>

extern Back_Top back;

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

void IQ::br_clear(uint32_t br_mask) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && ((1 << entry[i].uop.bra_tag) & br_mask)) {
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

void IQ::store_wake_up(int stq_idx) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {
      entry[i].uop.st_issue_pend[stq_idx] = false;
    }
  }
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
  assert(i != entry_num);
}

Inst_entry IQ::deq() {

  Inst_entry ret = scheduler();
  if (ret.valid) {
    num--;
  }

  return ret;
}

// 调度策略
Inst_entry IQ::scheduler() {

  Inst_entry iss_entry;
  int iss_idx;
  iss_entry.valid = false;

  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && (!entry[i].uop.src1_en || !entry[i].uop.src1_busy) &&
        (!entry[i].uop.src2_en || !entry[i].uop.src2_busy) &&
        !(is_load(entry[i].uop.op) &&
          orR(entry[i].uop.st_issue_pend, STQ_NUM))) {
      if (!iss_entry.valid || iss_entry.uop.inst_idx > entry[i].uop.inst_idx) {
        iss_entry = entry[i];
        iss_idx = i;
      }
    }
  }

  if (iss_entry.valid)
    entry[iss_idx].valid = false;

  return iss_entry;
}

void ISU::init() {
  iq.push_back(IQ(16, IQ_INTM));
  iq.push_back(IQ(16, IQ_INTD));
  iq.push_back(IQ(16, IQ_LS));
  iq.push_back(IQ(8, IQ_BR));
}

void ISU::default_val() {
  for (int i = 0; i < ALU_PORT; i++)
    io.iss2ren->wake[i].valid = false;
  for (int i = 0; i < LSU_PORT; i++)
    io.iss2ren->st_issue[i].valid = true;
  for (int i = 0; i < ISSUE_WAY; i++)
    io.iss2prf->iss_entry[i].valid = false;
}

// ISU回复Rename的派遣请求
void ISU::comb_ren_rdy() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->valid[i]) {
      if (iq[io.ren2iss->uop[i].iq_type].num_temp <
          iq[io.ren2iss->uop[i].iq_type].entry_num) {
        io.iss2ren->ready[i] = true;
        iq[io.ren2iss->uop[i].iq_type].num_temp++;
      } else
        io.iss2ren->ready[i] = false;
    } else {
      io.iss2ren->ready[i] = true;
    }
  }
}

// ISU发射指令至执行单元，同时更新busy_table和st_issue_pend
void ISU::comb_fire() {
  // ISU指令发射请求
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.exe2iss->ready[i])
      io.iss2exe->iss_entry[i] = io.iss2prf->iss_entry[i] = iq[i].deq();
  }

  // 更新busy_table
  for (int i = 0; i < ALU_PORT; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        io.iss2prf->iss_entry[i].uop.dest_en) {
      io.iss2ren->wake[i].valid = true;
      io.iss2ren->wake[i].preg = io.iss2prf->iss_entry[i].uop.dest_preg;
    }
  }

  // 更新st_issue_pend
  for (int i = 0; i < LSU_PORT; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        is_store(io.iss2prf->iss_entry[i].uop)) {
      io.iss2ren->st_issue[i].valid = true;
      io.iss2ren->st_issue[i].stq_idx = io.iss2prf->iss_entry[i].uop.stq_idx;
    }
  }
}

void ISU::comb_wake() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.iss2prf->iss_entry[i].valid) {
      // 执行延迟为1的指令，发射当拍唤醒其他指令
      if (io.iss2prf->iss_entry[i].uop.dest_en &&
          !is_load(io.iss2prf->iss_entry[i].uop.op)) {
        for (auto &q : iq)
          q.wake_up(io.iss2prf->iss_entry[i].uop.dest_preg);
      }

      // store指令，发射当拍唤醒后续load指令
      if (is_store(io.iss2prf->iss_entry[i].uop.op)) {
        for (auto &q : iq) {
          if (q.type == IQ_LS)
            q.store_wake_up(io.iss2prf->iss_entry[i].uop.stq_idx);
        }
      }
    }
  }

  // load指令，返回结果当拍唤醒其他指令
  if (io.awake->wake.valid) {
    for (auto &q : iq)
      q.wake_up(io.awake->wake.preg);
  }
}

void ISU::seq() {
  // 入队
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->dis_fire[i]) {
      for (auto &q : iq) {
        if (q.type == io.ren2iss->uop[i].iq_type)
          q.enq(&io.ren2iss->uop[i]);
      }
    }
  }

  for (auto &q : iq) {
    q.num_temp = q.num;
  }

  // 分支处理
  if (io.dec_bcast->mispred) {
    for (auto &q : iq) {
      q.br_clear(io.dec_bcast->br_mask);
    }
  }
}
