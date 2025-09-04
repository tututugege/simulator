#include "TOP.h"
#include <EXU.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <ostream>
#include <util.h>
extern Back_Top back;
extern uint32_t *p_memory;
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

void alu(Inst_uop &inst);
void bru(Inst_uop &inst);
void ldu(Inst_uop &inst);
void stu_addr(Inst_uop &inst);
void stu_data(Inst_uop &inst);
void mul(Inst_uop &inst);
void div(Inst_uop &inst);

void FU::exec(Inst_uop &inst) {

  if (cycle == 0) {
    if (inst.op == MUL) { // mul
      latency = 1;
    } else if (inst.op == DIV) { // div
      latency = 1;
    } else if (inst.op == LOAD) {
      latency = 1;
    } else {
      latency = 1;
    }
  }

  cycle++;

  if (cycle == latency && inst.op == LOAD) {
    uint32_t v_addr = inst.src1_rdata + inst.imm;
    inst.result = v_addr;

    bool page_fault = false;
    if (back.csr.CSR_RegFile[number_satp] & 0x80000000 &&
        back.csr.privilege != 3) {
      bool mstatus[32], sstatus[32];
      cvt_number_to_bit_unsigned(mstatus, back.csr.CSR_RegFile[number_mstatus],
                                 32);

      cvt_number_to_bit_unsigned(sstatus, back.csr.CSR_RegFile[number_sstatus],
                                 32);

      page_fault =
          !va2pa(inst.result, v_addr, back.csr.CSR_RegFile[number_satp], 1,
                 mstatus, sstatus, back.csr.privilege, p_memory);
    }

    if (!page_fault && !back.stq.check_load_raw(inst.result, inst.rob_idx))
      latency++;
  }

  if (cycle == latency) {
    if (is_load(inst.op)) {
      ldu(inst);
    } else if (is_sta(inst.op)) {
      stu_addr(inst);
    } else if (is_std(inst.op)) {
      stu_data(inst);
    } else if (is_branch(inst.op)) {
      bru(inst);
    } else if (inst.op == MUL) {
      mul(inst);
    } else if (inst.op == DIV) {
      div(inst);
    } else if (inst.op == SFENCE_VMA) {
      uint32_t vaddr = 0;
      uint32_t asid = 0;
      // TODO: sfence.vma
    } else
      alu(inst);

    complete = true;
    cycle = 0;
  }
}

void EXU::init() { memset(inst_r, 0, sizeof(Inst_entry) * ISSUE_WAY); }

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
    if (inst_r[i].valid) {
      fu[i].exec(io.exe2prf->entry[i].uop);
      if (fu[i].complete) {
        io.exe2prf->entry[i].valid = true;
      } else {
        io.exe2prf->entry[i].valid = false;
      }
    }
  }

  // store
  if (inst_r[IQ_STA].valid) {
    io.exe2stq->addr_entry = io.exe2prf->entry[IQ_STA];
  } else {
    io.exe2stq->addr_entry.valid = false;
  }

  if (inst_r[IQ_STD].valid) {
    io.exe2stq->data_entry = io.exe2prf->entry[IQ_STD];
  } else {
    io.exe2stq->data_entry.valid = false;
  }
}

void EXU::comb_csr() {
  io.exe2csr->we = false;
  io.exe2csr->re = false;

  if (inst_r[0].valid && inst_r[0].uop.op == CSR && !io.rob_bc->flush) {
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
