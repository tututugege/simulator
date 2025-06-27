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
  // 设置latency
  if (cycle == 0) {
    if (inst.op == MUL) { // mul
      latency = 1;
    } else if (inst.op == DIV) { // div
      latency = 1;
    } else {
      latency = 1;
    }
  }

  cycle++;
  if (cycle == latency) {
    if (is_branch(inst.op)) {
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

void EXU::default_val() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2prf->entry[i].valid = false;
    if (i != IQ_LS)
      io.exe2prf->entry[i].uop = inst_r[i].uop;
  }
}

// EXU回应ISU的指令发射请求
void EXU::comb_iss_rdy() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2iss->ready[i] =
        (!inst_r[i].valid || fu[i].complete) && !io.dec_bcast->mispred;
  }
}

// EXU向写回级发起请求
void EXU::comb_wb() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (fu[i].complete)
      io.exe2prf->entry[i].valid = true;
  }
}

// 除了LSU以外的执行单元执行
void EXU::comb_exec() {
  for (int i = 0; i < ISSUE_WAY; i++) {

    if (i == IQ_LS)
      continue;

    if (inst_r[i].valid && !io.dec_bcast->mispred)
      fu[i].exec(io.exe2prf->entry[i].uop);
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

    if (io.iss2exe->iss_entry[i].valid && io.exe2iss->ready[i]) {
      inst_r[i] = io.iss2exe->iss_entry[i];
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
          (io.dec_bcast->br_mask & (1 << inst_r[i].uop.bra_tag))) {
        inst_r[i].valid = false;
        fu[i].complete = false;
        fu[i].cycle = 0;
      }
    }
  }

  if (io.rob_bc->rollback) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}
