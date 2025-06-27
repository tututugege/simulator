#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <iostream>
#include <util.h>

void PRF::init() {
  for (int i = 0; i < ISSUE_WAY; i++)
    io.prf2exe->ready[i] = true;
}

void PRF::default_val() {
  io.prf2dec->mispred = false;
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.prf2rob->entry[i].valid = false;
  }
  io.prf_awake->wake.valid = false;
}

void PRF::comb_amo() {
  io.prf2stq->amoop = inst_r[IQ_LS].uop.amoop;
  io.prf2stq->load_data = inst_r[IQ_LS].uop.result;
  io.prf2stq->stq_idx = inst_r[IQ_LS].uop.stq_idx;
  if (inst_r[IQ_LS].valid && inst_r[IQ_LS].uop.op == LOAD &&
      inst_r[IQ_LS].uop.amoop != AMONONE) {
    io.prf2stq->valid = true;
  } else {
    io.prf2stq->valid = false;
  }
}

// 向前端返回分支预测对错信息
void PRF::comb_branch() {
  // TODO: Magic number
  if (inst_r[IQ_BR].valid && is_branch(inst_r[IQ_BR].uop.op) &&
      inst_r[IQ_BR].uop.mispred) {

    io.prf2dec->mispred = true;
    io.prf2dec->redirect_pc = inst_r[IQ_BR].uop.pc_next;
    io.prf2dec->br_tag = inst_r[IQ_BR].uop.bra_tag;

    if (LOG)
      cout << "misprediction redirect_pc 0x" << hex << io.prf2dec->redirect_pc
           << endl;
  }
}

// 读物理寄存器并完成数据前递
void PRF::comb_read() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    Inst_entry *entry = &io.iss2prf->iss_entry[i];

    if (entry->valid) {
      if (entry->uop.src1_en) {
        entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];
        // 写回级数据前递
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = inst_r[j].uop.result;
        }

        // 执行级数据前递
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].uop.dest_en &&
              io.exe2prf->entry[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = io.exe2prf->entry[j].uop.result;
        }
      }

      if (entry->uop.src2_en) {
        entry->uop.src2_rdata = reg_file[entry->uop.src2_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].uop.dest_en &&
              io.exe2prf->entry[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = io.exe2prf->entry[j].uop.result;
        }
      }
    }
  }
}

// 通知ROB指令已写回
void PRF::comb_rob_complete() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      io.prf2rob->entry[i] = inst_r[i];
  }
}

// TODO: MAGIC NUMBER
// 写回级唤醒后续有依赖的指令
void PRF::comb_wake() {
  if (inst_r[IQ_LS].valid && inst_r[IQ_LS].uop.dest_en) {
    io.prf_awake->wake.valid = true;
    io.prf_awake->wake.preg = inst_r[IQ_LS].uop.dest_preg;
  }
}

void PRF::seq() {
  for (int i = 0; i < ALU_PORT + LSU_PORT; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en) {
      reg_file[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r[i] = io.exe2prf->entry[i];
    } else {
      inst_r[i].valid = false;
    }
  }

  if (io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (io.dec_bcast->br_mask & (1 << inst_r[i].uop.bra_tag))) {
        inst_r[i].valid = false;
      }
    }
  }

  if (io.rob_bc->rollback) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r[i].valid = false;
    }
  }
}
