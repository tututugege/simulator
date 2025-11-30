#include "TOP.h"
#include <Cache.h>
#include <EXU.h>
#include <MMU.h>
#include <cmath>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <util.h>
extern Back_Top back;
extern MMU mmu;
extern uint32_t *p_memory;

Cache cache; // Cache模拟
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);
void alu(Inst_uop &inst);
void bru(Inst_uop &inst);
// void ldu(Inst_uop &inst);
bool ldu(Inst_uop &inst, bool mmu_page_fault, uint32_t mmu_ppn);
// void stu_addr(Inst_uop &inst);
void stu_addr(Inst_uop &inst, bool page_fault_mmu, uint32_t mmu_ppn);
void stu_data(Inst_uop &inst);
void mul(Inst_uop &inst);
void div(Inst_uop &inst);

// Calculate the state of mmu_lsu_req[M] channel
wire1_t alloc_mmu_req_slot[MAX_LSU_REQ_NUM];

// 请求 LSU 槽位 (grant or not)
static inline bool comb_apply_slot(mmu_slot_t &slot) {
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    if (!alloc_mmu_req_slot[i]) {
      slot.idx = i;
      slot.valid = true;
      alloc_mmu_req_slot[i] = true;
      return true;
    }
  }
  slot.valid = false;
  slot.idx = 0;
  return false;
}

void FU::exec(Inst_uop &inst) {

  if (cycle == 0) {
    if (inst.op == UOP_MUL) { // mul
      latency = 1;
    } else if (inst.op == UOP_DIV) { // div
      latency = 1;
    } else if (inst.op == UOP_LOAD) {
      // latency = cache.cache_access(inst.src1_rdata + inst.imm);
      latency = 1;
    } else if (inst.op == UOP_STA) {
      latency = 1;
    } else {
      latency = 1;
    }
  }

  cycle++;

  // deal with mmu: if miss, return and wait for next cycle
  if (is_load_uop(inst.op) || is_sta_uop(inst.op)) {
    // step0: reset wire mmu_lsu_slot_r_1, which is
    // only useful for load/sta uop
    uint32_t vaddr = inst.src1_rdata + inst.imm;
    // step1: try to find a free slot if not allocated yet
    if (!mmu_lsu_slot_r.valid) {
      // slot not allocated yet
      bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
      if (granted) {
        // free slot found, send mmu request and
        // waiting for next cycle
        mmu_req_master_t req = {
            .valid = true,
            .vtag = (vaddr >> 12), // vaddr[31:12]
            .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
        int idx = mmu_lsu_slot_r_1.idx;
        mmu.io.in.mmu_lsu_req[idx] = req;
      }
      return;
    }

    // step2: slot aleardy allocated, check mmu resp to see if hit
    mmu_resp_master_t resp = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
    bool hit = resp.valid && !resp.miss;
    // if (!hit) {
    if (!hit || cycle < latency) {
      // miss, reallocate and replay the request
      bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
      if (granted) {
        mmu_req_master_t req = {
            .valid = true,
            .vtag = (vaddr >> 12), // vaddr[31:12]
            .op_type = is_load_uop(inst.op) ? mmu_n::OP_LOAD : mmu_n::OP_STORE};
        mmu.io.in.mmu_lsu_req[mmu_lsu_slot_r_1.idx] = req;
      }
      return;
    }
  }

  if (cycle >= latency) {
    if (is_load_uop(inst.op)) {
      mmu_resp_master_t resp = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool page_fault = resp.valid && resp.excp;
      uint32_t mmu_ppn = resp.ptag;
      // ldu(inst);
      bool stall_load = ldu(inst, page_fault, mmu_ppn);
      if (stall_load) {
        // load failed due to waiting forward data from
        // store queue reallocate and replay the request
        bool granted = comb_apply_slot(mmu_lsu_slot_r_1);
        if (granted) {
          uint32_t vaddr = inst.src1_rdata + inst.imm;
          mmu_req_master_t req = {.valid = true,
                                  .vtag = (vaddr >> 12), // vaddr[31:12]
                                  .op_type = mmu_n::OP_LOAD};
        }
        return; // not complete yet
      }
    } else if (is_sta_uop(inst.op)) {
      mmu_resp_master_t resp = mmu.io.out.mmu_lsu_resp[mmu_lsu_slot_r.idx];
      bool page_fault = resp.valid && resp.excp;
      uint32_t mmu_ppn = resp.ptag;
      stu_addr(inst, page_fault, mmu_ppn);
    } else if (is_std_uop(inst.op)) {
      stu_data(inst);
    } else if (is_branch_uop(inst.op)) {
      bru(inst);
    } else if (inst.op == UOP_MUL) {
      mul(inst);
    } else if (inst.op == UOP_DIV) {
      div(inst);
    } else if (inst.op == UOP_SFENCE_VMA) {
      uint32_t vaddr = inst.src1_rdata;
      uint32_t asid = inst.src2_rdata;
      mmu.io.in.tlb_flush.flush_asid = asid;
      mmu.io.in.tlb_flush.flush_vpn = vaddr >> 12;
      mmu.io.in.tlb_flush.flush_valid = true;
    } else
      alu(inst);

    complete = true;
    cycle = 0;
  }
}

void EXU::init() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    inst_r[i].valid = false;
  }
}

void EXU::comb_ready() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2iss->ready[i] = (!inst_r[i].valid || fu[i].complete) &&
                           !io.dec_bcast->mispred && !io.rob_bcast->flush;
  }
}

void EXU::comb_exec() {
  // reset alloc_mmu_req_slot
  for (int i = 0; i < MAX_LSU_REQ_NUM; i++) {
    alloc_mmu_req_slot[i] = 0;
    mmu.io.in.mmu_lsu_req[i] = {}; // clear lsu req by default
  }
  // comb_exec
  for (int i = 0; i < ISSUE_WAY; i++) {
    fu[i].mmu_lsu_slot_r_1 = {};
    io.exe2prf->entry[i].valid = false;
    io.exe2prf->entry[i].uop = inst_r[i].uop;
    if (inst_r[i].valid) {
      fu[i].exec(io.exe2prf->entry[i].uop);
      if (fu[i].complete &&
          !(io.dec_bcast->mispred &&
            ((1 << inst_r[i].uop.tag) & io.dec_bcast->br_mask)) &&
          !io.rob_bcast->flush) {
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

void EXU::comb_to_csr() {
  io.exe2csr->we = false;
  io.exe2csr->re = false;

  if (inst_r[0].valid && inst_r[0].uop.op == UOP_CSR && !io.rob_bcast->flush) {
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

void EXU::comb_from_csr() {
  if (inst_r[0].valid && inst_r[0].uop.op == UOP_CSR && io.exe2csr->re) {
    io.exe2prf->entry[0].uop.result = io.csr2exe->rdata;
  }
}

void EXU::comb_pipeline() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2exe->iss_entry[i].valid && io.exe2iss->ready[i]) {
      inst_r_1[i] = io.prf2exe->iss_entry[i];
      fu[i].complete = false;
      fu[i].cycle = 0;
    } else if (io.exe2prf->entry[i].valid && io.prf2exe->ready[i]) {
      inst_r_1[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}

void EXU::comb_branch() {
  if (io.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid &&
          (io.dec_bcast->br_mask & (1 << inst_r[i].uop.tag))) {
        inst_r_1[i].valid = false;
        fu[i].complete = false;
        fu[i].cycle = 0;
      }
    }
  }
}

void EXU::comb_flush() {
  if (io.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      inst_r_1[i].valid = false;
      fu[i].complete = false;
      fu[i].cycle = 0;
    }
  }
}

void EXU::seq() {

  for (int i = 0; i < ISSUE_WAY; i++) {
    inst_r[i] = inst_r_1[i];
    fu[i].mmu_lsu_slot_r = fu[i].mmu_lsu_slot_r_1;
  }
}
