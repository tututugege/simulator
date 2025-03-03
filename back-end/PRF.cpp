#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <iostream>

void PRF::init() {
  for (int i = 0; i < ISSUE_WAY; i++)
    io.prf2exe->ready[i] = true;
}

void PRF::comb() {
  // 根据分支结果向前端返回信息

  io.prf2id->mispred = false;
  if (inst_r[6].valid && is_branch(inst_r[6].inst.op) &&
      inst_r[6].inst.mispred) {

    io.prf2id->mispred = true;
    io.prf2id->redirect_pc = inst_r[6].inst.pc_next;
    io.prf2id->br_tag = inst_r[6].inst.tag;

    if (LOG)
      cout << "misprediction redirect_pc 0x" << hex << io.prf2id->redirect_pc
           << endl;
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    io.prf2exe->iss_entry[i] = io.iss2prf->iss_entry[i];
    Inst_entry *entry = &io.prf2exe->iss_entry[i];
    if (entry->valid) {
      if (entry->inst.src1_en) {
        entry->inst.src1_rdata = reg_file[entry->inst.src1_preg];
      }
      if (entry->inst.src2_en) {
        entry->inst.src2_rdata = reg_file[entry->inst.src2_preg];
      }
    }
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      io.prf2rob->entry[i] = inst_r[i];
    else
      io.prf2rob->entry[i].valid = false;
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid && inst_r[i].inst.dest_en) {
      io.prf_awake->wake[i].valid = true;
      io.prf_awake->wake[i].preg = inst_r[i].inst.dest_preg;
    } else {
      io.prf_awake->wake[i].valid = false;
    }
  }
}

void PRF::seq() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid && inst_r[i].inst.dest_en) {
      reg_file[inst_r[i].inst.dest_preg] = inst_r[i].inst.result;
    }

    if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r[i] = io.exe2prf->entry[i];
    } else {
      inst_r[i].valid = false;
    }
  }

  if (io.id_bc->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid && (io.id_bc->br_mask & (1 << inst_r[i].inst.tag))) {
        inst_r[i].valid = false;
      }
    }
  }
}
