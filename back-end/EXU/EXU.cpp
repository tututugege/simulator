#include <EXU.h>
#include <config.h>
#include <util.h>

void alu(Inst_info *inst, FU &fu);
void bru(Inst_info *inst, FU &fu);
void ldu_comb(Inst_info *inst, FU &fu);
void ldu_seq(Inst_info *inst, FU &fu);
void stu_comb(Inst_info *inst, FU &fu);

FU_TYPE fu_config[ISSUE_WAY] = {FU_ALU, FU_ALU, FU_BRU, FU_LDU, FU_STU};

void (*fu_comb[FU_NUM])(Inst_info *, FU &) = {alu, bru, ldu_comb, stu_comb};
void (*fu_seq[FU_NUM])(Inst_info *, FU &) = {nullptr, nullptr, ldu_seq,
                                             nullptr};

void EXU::init() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    fu[i].type = fu_config[i];
    fu[i].comb = fu_comb[fu[i].type];
    fu[i].seq = fu_seq[fu[i].type];
  }
}

void EXU::comb() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2prf->entry[i].valid = false;
    io.exe2prf->entry[i].inst = inst_r[i].inst;
    if (inst_r[i].valid && !io.dec_bcast->mispred) {
      fu[i].comb(&io.exe2prf->entry[i].inst, fu[i]);
      if (fu[i].complete) {
        io.exe2prf->entry[i].valid = true;
      } else {
        io.exe2prf->entry[i].valid = false;
      }
    }

    io.exe2prf->ready[i] =
        !inst_r[i].valid || io.exe2prf->entry[i].valid && io.prf2exe->ready[i];

    io.exe2iss->ready[i] =
        !inst_r[i].valid || io.exe2prf->entry[i].valid && io.prf2exe->ready[i];
  }

  // TODO: Magic Number
  // store
  if (inst_r[STU_IDX].valid) {
    io.exe2stq->entry = io.exe2prf->entry[4];
  } else {
    io.exe2stq->entry.valid = false;
  }
}

void EXU::seq() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2exe->iss_entry[i].valid && io.exe2prf->ready[i]) {
      inst_r[i] = io.prf2exe->iss_entry[i];
    } else if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r[i].valid = false;
    }

    if (fu[i].seq)
      fu[i].seq(&io.exe2prf->entry[i].inst, fu[i]);
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
