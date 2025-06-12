#include <EXU.h>
#include <config.h>
#include <util.h>

void alu(Inst_uop &inst);
void bru(Inst_uop &inst);
void ldu(Inst_uop &inst);
void stu(Inst_uop &inst);
void mul(Inst_uop &inst);
void div(Inst_uop &inst);

void FU::exec(Inst_uop &inst) {

  if (cycle == 0) {
    if (inst.op == MUL) { // mul
      latency = 1;
    } else if (inst.op == DIV) { // div
      latency = 1;
    } else if (inst.op == LOAD) {
      latency = 3;
    } else {
      latency = 1;
    }
  }

  cycle++;
  if (cycle == latency) {
    if (is_load(inst.op)) {
      ldu(inst);
    } else if (is_store(inst.op)) {
      stu(inst);
    } else if (is_branch(inst.op)) {
      bru(inst);
    } else if (inst.op == MUL) {
      mul(inst);
    } else if (inst.op == DIV) {
      div(inst);
    } else
      alu(inst);

    complete = true;
    cycle = 0;
  }
}

int fu_config[ISSUE_WAY] = {(1 << FU_ALU) | (1 << FU_MUL),
                            (1 << FU_ALU) | (1 << FU_DIV), 1 << FU_LSU,
                            1 << FU_BRU};

void EXU::init() {}

void EXU::comb_ready() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2iss->ready[i] =
        (!inst_r[i].valid || fu[i].complete) && !io.dec_bcast->mispred;
  }
}

void EXU::comb_exec() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2prf->entry[i].valid = false;
    io.exe2prf->entry[i].uop = inst_r[i].uop;
    if (inst_r[i].valid && !io.dec_bcast->mispred) {
      fu[i].exec(io.exe2prf->entry[i].uop);
      if (fu[i].complete) {
        io.exe2prf->entry[i].valid = true;
      } else {
        io.exe2prf->entry[i].valid = false;
      }
    }
  }

  // TODO: Magic Number
  // store
  if (inst_r[IQ_LS].valid && is_store(inst_r[IQ_LS].uop.op)) {
    io.exe2stq->entry = io.exe2prf->entry[IQ_LS];
  } else {
    io.exe2stq->entry.valid = false;
  }
}

void EXU::comb_csr() {
  io.exe2csr->we = false;
  io.exe2csr->re = false;

  if (inst_r[0].uop.op == CSR) {
    io.exe2csr->we = inst_r[0].uop.func3 == 1 || inst_r[0].uop.src1_areg != 0;

    io.exe2csr->re = inst_r[0].uop.func3 != 1 || inst_r[0].uop.dest_areg != 0;

    io.exe2csr->idx = inst_r[0].uop.csr_idx;
    io.exe2csr->wcmd = inst_r[0].uop.func3 & 0b11;
    if (inst_r[0].uop.src2_is_imm) {
      io.exe2csr->wdata = inst_r[0].uop.imm;
    } else {
      io.exe2csr->wdata = inst_r[0].uop.src1_rdata;
    }
  }
}

void EXU::seq() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (inst_r[i].valid && inst_r[i].uop.op == CSR && io.exe2csr->re) {
      io.exe2prf->entry[i].uop.result = io.csr2exe->rdata;
    }

    if (io.prf2exe->iss_entry[i].valid && io.exe2iss->ready[i]) {
      inst_r[i] = io.prf2exe->iss_entry[i];
      fu[i].complete = false;
      fu[i].cycle = 0;
    } else if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }

  if (io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (io.dec_bcast->br_mask & (1 << inst_r[i].uop.tag))) {
        inst_r[i].valid = false;
        fu[i].complete = false;
        fu[i].cycle = 0;
      }
    }
  }

  if (io.rob_bc->flush) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}
