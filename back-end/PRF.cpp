#include "config.h"
#include <IO.h>
#include <PRF.h>
#include <cstring>
#include <util.h>

void PRF::init() {
  for (int i = 0; i < ISSUE_WAY; i++)
    out.prf2exe->ready[i] = true;
}

void PRF::comb_br_check() {
  // 根据分支结果向前端返回信息
  bool mispred = false;
  Inst_uop *mispred_uop;

  for (int i = 0; i < BRU_NUM; i++) {
    int iq_br = IQ_BR0 + i;
    if (inst_r[iq_br].valid && inst_r[iq_br].uop.mispred) {
      if (!mispred) {
        mispred = true;
        mispred_uop = &inst_r[iq_br].uop;
      } else if (cmp_inst_age(*mispred_uop, inst_r[iq_br].uop)) {
        mispred_uop = &inst_r[iq_br].uop;
      }
    }
  }

  out.prf2dec->mispred = mispred;
  if (mispred) {
    out.prf2dec->redirect_pc = mispred_uop->pc_next;
    out.prf2dec->redirect_rob_idx = mispred_uop->rob_idx;
    out.prf2dec->br_tag = mispred_uop->tag;
  }
}

void PRF::comb_read() {
  // bypass
  for (int i = 0; i < ISSUE_WAY; i++) {
    out.prf2exe->iss_entry[i] = in.iss2prf->iss_entry[i];
    Inst_entry *entry = &out.prf2exe->iss_entry[i];

    if (entry->valid) {
      if (entry->uop.src1_en) {
        entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];
        for (int j = 0; j < ALU_NUM + 1; j++) {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM + 1; j++) {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src1_preg)
            entry->uop.src1_rdata = in.exe2prf->entry[j].uop.result;
        }
      }

      if (entry->uop.src2_en) {
        entry->uop.src2_rdata = reg_file[entry->uop.src2_preg];
        for (int j = 0; j < ALU_NUM + 1; j++) {
          if (inst_r[j].valid && inst_r[j].uop.dest_en &&
              inst_r[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = inst_r[j].uop.result;
        }

        for (int j = 0; j < ALU_NUM + 1; j++) {
          if (in.exe2prf->entry[j].valid && in.exe2prf->entry[j].uop.dest_en &&
              in.exe2prf->entry[j].uop.dest_preg == entry->uop.src2_preg)
            entry->uop.src2_rdata = in.exe2prf->entry[j].uop.result;
        }
      }
    }
  }
}

void PRF::comb_complete() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid)
      out.prf2rob->entry[i] = inst_r[i];
    else
      out.prf2rob->entry[i].valid = false;
  }
}

void PRF::comb_awake() {
  if (inst_r[IQ_LD].valid && inst_r[IQ_LD].uop.dest_en &&
      !inst_r[IQ_LD].uop.page_fault_load) {
    out.prf_awake->wake.valid = true;
    out.prf_awake->wake.preg = inst_r[IQ_LD].uop.dest_preg;
  } else {
    out.prf_awake->wake.valid = false;
  }
}

void PRF::comb_branch() {
  if (in.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (in.dec_bcast->br_mask & (1 << inst_r[i].uop.tag))) {
        inst_r_1[i].valid = false;
      }
    }
  }
}

void PRF::comb_flush() {
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r_1[i].valid = false;
    }
  }
}

void PRF::comb_write() {
  for (int i = 0; i < ALU_NUM + 1; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en &&
        !is_page_fault(inst_r[i].uop)) {
      reg_file_1[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }
}

void PRF::comb_pipeline() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.exe2prf->entry[i].valid && out.prf2exe->ready[i]) {
      inst_r_1[i] = in.exe2prf->entry[i];
    } else {
      inst_r_1[i].valid = false;
    }
  }
}

void PRF::seq() {
  for (int i = 0; i < PRF_NUM; i++) {
    reg_file[i] = reg_file_1[i];
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    inst_r[i] = inst_r_1[i];
  }
}
