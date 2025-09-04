#include "TOP.h"
#include "config.h"
#include "frontend.h"
#include <IO.h>
#include <PRF.h>
#include <cstring>
#include <iostream>
#include <util.h>
extern Back_Top back;

void PRF::init() {
  for (int i = 0; i < ISSUE_WAY; i++)
    io.prf2exe->ready[i] = true;

  memset(inst_r, 0, sizeof(Inst_entry) * ISSUE_WAY);
}

void PRF::comb_branch() {
  // 根据分支结果向前端返回信息

  // TODO: Magic number
  io.prf2dec->mispred = false;
  if (inst_r[IQ_BR].valid && is_branch(inst_r[IQ_BR].uop.op) &&
      inst_r[IQ_BR].uop.mispred && !io.rob_bc->flush) {

    io.prf2dec->mispred = true;
    io.prf2dec->redirect_pc = inst_r[IQ_BR].uop.pc_next;
    io.prf2dec->br_tag = inst_r[IQ_BR].uop.tag;

    if (LOG)
      cout << "PC " << hex << inst_r[IQ_BR].uop.pc
           << " misprediction redirect_pc 0x" << hex << io.prf2dec->redirect_pc
           << endl;
  }
}

void PRF::comb_read() {
  // bypass
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.prf2exe->iss_entry[i] = io.iss2prf->iss_entry[i];
    Inst_entry *entry = &io.prf2exe->iss_entry[i];

    if (entry->valid) {
      if (entry->uop.src1_en) {
        entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];
        for (int j = 0; j < ISSUE_WAY; j++) {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = inst_r[j].uop.result;
        }

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

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      io.prf2rob->entry[i] = inst_r[i];
    else
      io.prf2rob->entry[i].valid = false;
  }

  if (inst_r[IQ_LD].valid && inst_r[IQ_LD].uop.dest_en &&
      !inst_r[IQ_LD].uop.page_fault_load) {
    io.prf_awake->wake.valid = true;
    io.prf_awake->wake.preg = inst_r[IQ_LD].uop.dest_preg;
  } else {
    io.prf_awake->wake.valid = false;
  }
}

void PRF::seq() {
  for (int i = 0; i < ALU_NUM + 1; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en &&
        !is_page_fault(inst_r[i].uop)) {
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

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2prf->valid[i]) {
      reg_file[io.ren2prf->dest_preg[i]] = io.ren2prf->reg_wdata[i];
    }
  }

  if (io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (io.dec_bcast->br_mask & (1 << inst_r[i].uop.tag))) {
        inst_r[i].valid = false;
      }
    }
  }

  if (io.rob_bc->flush) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r[i].valid = false;
    }
  }
}
