#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <iostream>
#include <util.h>

void PRF::init() {
  for (int i = 0; i < ISSUE_WAY; i++)
    io.prf2exe->ready[i] = true;
}

void PRF::comb_branch() {
  // 根据分支结果向前端返回信息

  // TODO: Magic number
  io.prf2dec->mispred = false;
  if (inst_r[4].valid && is_branch(inst_r[4].inst.op) &&
      inst_r[4].inst.mispred) {

    io.prf2dec->mispred = true;
    io.prf2dec->redirect_pc = inst_r[4].inst.pc_next;
    io.prf2dec->br_tag = inst_r[4].inst.tag;

    if (LOG)
      cout << "misprediction redirect_pc 0x" << hex << io.prf2dec->redirect_pc
           << endl;
  }
}

void PRF::comb_read() {
  // bypass
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.prf2exe->iss_entry[i] = io.iss2prf->iss_entry[i];
    Inst_entry *entry = &io.prf2exe->iss_entry[i];

    if (entry->valid) {
      if (entry->inst.src1_en) {
        entry->inst.src1_rdata = reg_file[entry->inst.src1_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].inst.dest_en &&
              inst_r[j].inst.dest_preg == entry->inst.src1_preg)
            entry->inst.src1_rdata = inst_r[j].inst.result;
        }

        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].inst.dest_en &&
              io.exe2prf->entry[j].inst.dest_preg == entry->inst.src1_preg)
            entry->inst.src1_rdata = io.exe2prf->entry[j].inst.result;
        }
      }

      if (entry->inst.src2_en) {
        entry->inst.src2_rdata = reg_file[entry->inst.src2_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].inst.dest_en &&
              inst_r[j].inst.dest_preg == entry->inst.src2_preg)
            entry->inst.src2_rdata = inst_r[j].inst.result;
        }

        for (int j = 0; j < ISSUE_WAY; j++) {
          if (io.exe2prf->entry[j].valid && io.exe2prf->entry[j].inst.dest_en &&
              io.exe2prf->entry[j].inst.dest_preg == entry->inst.src2_preg)
            entry->inst.src2_rdata = io.exe2prf->entry[j].inst.result;
        }
      }
    }
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      io.prf2rob->entry[i] = inst_r[i];
    else
      io.prf2rob->entry[i].valid = false;
  }

  // TODO: MAGIC NUMBER
  if (inst_r[6].valid && inst_r[6].inst.dest_en) {
    io.prf_awake->wake.valid = true;
    io.prf_awake->wake.preg = inst_r[6].inst.dest_preg;
  } else {
    io.prf_awake->wake.valid = false;
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

  if (io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (io.dec_bcast->br_mask & (1 << inst_r[i].inst.tag))) {
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
