#include "TOP.h"
#include "config.h"
#include <ISU.h>
#include <cmath>
#include <util.h>
#include <vector>

extern Back_Top back;

void ISU::add_iq(int entry_num, int out_num, IQ_TYPE type) {
  iq.push_back(IQ(entry_num, out_num, type));
  iq_num++;
}

IQ::IQ(int entry_num, int out_num, IQ_TYPE type) {
  vector<Inst_entry> new_iq(entry_num);
  this->entry_num = entry_num;
  this->out_num = out_num;
  this->type = type;
  this->num = 0;
  this->num_temp = 0;

  entry.resize(entry_num);
  for (int i = 0; i < entry_num; i++) {
    entry[i].valid = false;
  }
}

void ISU::init() {
  add_iq(64, 4, IQ_INT);
  add_iq(1, 1, IQ_CSR);
  add_iq(16, 1, IQ_LD);
  add_iq(8, 1, IQ_ST);
  add_iq(8, 1, IQ_BR);
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

vector<Inst_entry> IQ::deq(int ready_num) {

  vector<Inst_entry> ret = scheduler(ready_num);
  for (auto &e : ret) {
    if (e.valid) {
      num--;
    }
  }

  return ret;
}

void ISU::comb_ready() {
  // ready
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (io.ren2iss->valid[i][j]) {
        if (iq[i].num_temp < iq[i].entry_num) {
          io.iss2ren->ready[i][j] = true;
          iq[i].num_temp++;
        }

        else
          io.iss2ren->ready[i][j] = false;
      } else {
        io.iss2ren->ready[i][j] = true;
      }
    }
  }
}

void ISU::comb_deq() {

  // 出队
  int issue_idx = 0;
  int ready_num[IQ_NUM] = {0};
  if (!io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (io.exe2iss->ready[i])
        ready_num[fu_config[i]]++;
    }
  }

  // int
  vector<Inst_entry> iss_entry = iq[0].deq(ready_num[0]);
  for (auto entry : iss_entry) {
    io.iss2prf->iss_entry[issue_idx] = entry;
    issue_idx++;
  }

  // TODO: Magic Number
  io.iss2prf->iss_entry[CSR_ISS_IDX] =
      iq[CSR_IQ_IDX].deq(ready_num[CSR_IQ_IDX])[0]; // csr
  io.iss2prf->iss_entry[LDU_ISS_IDX] =
      iq[LD_IQ_IDX].deq(ready_num[LD_IQ_IDX])[0]; // load
  io.iss2prf->iss_entry[STU_ISS_IDX] =
      iq[ST_IQ_IDX].deq(ready_num[ST_IQ_IDX])[0]; // store
  io.iss2prf->iss_entry[BRU_ISS_IDX] =
      iq[BR_IQ_IDX].deq(ready_num[BR_IQ_IDX])[0]; // br

  for (int i = 0; i < ALU_NUM + 1; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        io.iss2prf->iss_entry[i].inst.dest_en) {
      io.iss2ren->wake[i].valid = true;
      io.iss2ren->wake[i].preg = io.iss2prf->iss_entry[i].inst.dest_preg;
    } else {
      io.iss2ren->wake[i].valid = false;
    }
  }
}

void ISU::seq() {
  // 入队
  for (int i = 0; i < IQ_NUM; i++) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (io.ren2iss->dis_fire[i][j]) {
        iq[i].enq(&io.ren2iss->inst[j]);
      }
    }

    iq[i].num_temp = iq[i].num;
  }

  // 唤醒
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.iss2prf->iss_entry[i].valid &&
        io.iss2prf->iss_entry[i].inst.dest_en &&
        io.iss2prf->iss_entry[i].inst.op != LOAD) {
      for (auto &q : iq) {
        q.wake_up(io.iss2prf->iss_entry[i].inst.dest_preg);
      }
    }
  }

  if (io.awake->wake.valid) {
    for (auto &q : iq) {
      q.wake_up(io.awake->wake.preg);
    }
  }

  // 唤醒load
  for (auto &q : iq) {
    if (q.type == IQ_LD) {
      q.store_wake_up(io.stq2iss->valid);
    }
  }

  // 分支处理
  if (io.dec_bcast->mispred) {
    for (auto &q : iq) {
      q.br_clear(io.dec_bcast->br_mask);
    }
  }
}

void IQ::br_clear(uint32_t br_mask) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid && ((1 << entry[i].inst.tag) & br_mask)) {
      entry[i].valid = false;
      num--;
    }
  }
}

// 唤醒 发射时即可唤醒 下一周期时即可发射 此时结果已经写回寄存器堆
void IQ::wake_up(uint32_t dest_preg) {
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid) {
      if (entry[i].inst.src1_en && entry[i].inst.src1_preg == dest_preg) {
        entry[i].inst.src1_busy = false;
      }

      if (entry[i].inst.src2_en && entry[i].inst.src2_preg == dest_preg) {
        entry[i].inst.src2_busy = false;
      }
    }
  }
}

void IQ::store_wake_up(bool valid[]) {
  for (int i = 0; i < STQ_NUM; i++) {
    if (valid[i]) {
      for (int j = 0; j < entry_num; j++) {
        if (entry[j].valid) {
          entry[j].inst.pre_store[i] = false;
        }
      }
    }
  }
}

Inst_entry IQ::pop_oldest(vector<Inst_entry> &valid_entry,
                          vector<int> &valid_idx) {
  Inst_entry ret;

  if (valid_entry.size() == 0) {
    ret.valid = false;
    return ret;
  }

  int oldest_idx = 0;
  int i;

  for (i = 1; i < valid_entry.size(); i++) {
    if (valid_entry[i].inst.inst_idx < valid_entry[oldest_idx].inst.inst_idx) {
      oldest_idx = i;
    }
  }

  ret = valid_entry[oldest_idx];
  entry[valid_idx[oldest_idx]].valid = false;
  valid_entry.erase(valid_entry.begin() + oldest_idx);
  valid_idx.erase(valid_idx.begin() + oldest_idx);

  return ret;
}

// 调度策略
vector<Inst_entry> IQ::scheduler(int ready_num) {

  int issue_num = 0;
  int valid_num = 0;
  vector<Inst_entry> iss_entry(out_num);
  vector<Inst_entry> valid_entry;
  vector<int> valid_idx;
  for (int i = 0; i < entry_num; i++) {
    if (entry[i].valid &&
        (!entry[i].inst.src1_en || !entry[i].inst.src1_busy) &&
        (!entry[i].inst.src2_en || !entry[i].inst.src2_busy) &&
        !(entry[i].inst.op == LOAD && orR(entry[i].inst.pre_store, STQ_NUM))) {
      valid_entry.push_back(entry[i]);
      valid_idx.push_back(i);
    }
  }

  for (int i = 0; i < out_num; i++) {
    if (issue_num < ready_num) {
      iss_entry[i] = pop_oldest(valid_entry, valid_idx);
      if (iss_entry[i].valid)
        issue_num++;
    } else {
      iss_entry[i].valid = false;
    }
  }

  return iss_entry;
}
