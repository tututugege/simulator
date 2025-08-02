#include "CSR.h"
#include "IO.h"
#include "frontend.h"
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cstring>
#include <cvt.h>
#include <diff.h>
#include <util.h>

extern int commit_num;

void load_data();

uint32_t br_num[0x10000000 / 4];
uint32_t br_mispred[0x1000000 / 4];

int csr_idx[CSR_NUM] = {number_mtvec,    number_mepc,     number_mcause,
                        number_mie,      number_mip,      number_mtval,
                        number_mscratch, number_mstatus,  number_mideleg,
                        number_medeleg,  number_sepc,     number_stvec,
                        number_scause,   number_sscratch, number_stval,
                        number_sstatus,  number_sie,      number_sip,
                        number_satp,     number_mhartid,  number_misa};

void Back_Top::difftest(Inst_uop *inst) {

  if (inst->dest_en && !inst->page_fault_load)
    rename.arch_RAT[inst->dest_areg] = inst->dest_preg;

#ifdef CONFIG_DIFFTEST
  if (inst->is_last_uop) {

    if (LOG) {
      cout << "Instruction: " << inst->instruction << endl;
      if (inst->page_fault_inst)
        cout << "page fault inst " << endl;
      if (inst->page_fault_load)
        cout << "page fault load " << endl;
      if (inst->page_fault_store)
        cout << "page fault store " << endl;
    }

    for (int i = 0; i < ARF_NUM; i++) {
      dut_cpu.gpr[i] = prf.reg_file[rename.arch_RAT[i]];
    }

    for (int i = 0; i < CSR_NUM; i++) {
      dut_cpu.csr[i] = csr.CSR_RegFile[csr_idx[i]];
    }

    if (inst->op == STORE && !inst->page_fault_store) {
      dut_cpu.store = true;
      dut_cpu.store_addr = stq.entry[inst->stq_idx].addr;
      if (stq.entry[inst->stq_idx].size == 0b00)
        dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFF;
      else if (stq.entry[inst->stq_idx].size == 0b01)
        dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFFFF;
      else
        dut_cpu.store_data = stq.entry[inst->stq_idx].data;

    } else {
      dut_cpu.store = false;
    }

    dut_cpu.pc = inst->pc_next;

    if (inst->difftest_skip) {
      difftest_skip();
    } else {
      difftest_step();
    }
  }
#endif
}

Front_Dec front2dec;
Dec_Front dec2front;

Dec_Ren dec2ren;
Dec_Broadcast dec_bcast;

Ren_Dec ren2dec;
Ren_Iss ren2iss;
Ren_Rob ren2rob;
Ren_Stq ren2stq;

Iss_Ren iss2ren;
Iss_Prf iss2prf;

Prf_Exe prf2exe;
Prf_Stq prf2stq;
Prf_Rob prf2rob;
Prf_Awake awake;
Prf_Dec prf2dec;

Exe_Prf exe2prf;
Exe_Stq exe2stq;
Exe_Iss exe2iss;

Rob_Ren rob2ren;
Rob_Broadcast rob_bc;
Rob_Commit rob_commit;

Stq_Ren stq2ren;
Stq_Iss stq2iss;

Csr_Exe csr2exe;
Exe_Csr exe2csr;

void Back_Top::init() {

  idu.io.front2dec = &front2dec;
  idu.io.dec2front = &dec2front;
  idu.io.dec2ren = &dec2ren;
  idu.io.ren2dec = &ren2dec;
  idu.io.dec_bcast = &dec_bcast;
  idu.io.prf2dec = &prf2dec;
  idu.io.rob_bc = &rob_bc;
  idu.io.commit = &rob_commit;

  rename.io.dec2ren = &dec2ren;
  rename.io.ren2dec = &ren2dec;
  rename.io.ren2iss = &ren2iss;
  rename.io.iss2ren = &iss2ren;
  rename.io.dec_bcast = &dec_bcast;
  rename.io.rob2ren = &rob2ren;
  rename.io.ren2rob = &ren2rob;
  rename.io.ren2stq = &ren2stq;
  rename.io.stq2ren = &stq2ren;
  rename.io.rob_bc = &rob_bc;
  rename.io.rob_commit = &rob_commit;
  rename.io.awake = &awake;

  isu.io.rob_bc = &rob_bc;
  isu.io.dec_bcast = &dec_bcast;
  isu.io.ren2iss = &ren2iss;
  isu.io.iss2ren = &iss2ren;
  isu.io.iss2prf = &iss2prf;
  isu.io.awake = &awake;
  isu.io.stq2iss = &stq2iss;
  isu.io.exe2iss = &exe2iss;

  prf.io.iss2prf = &iss2prf;
  prf.io.prf2rob = &prf2rob;
  prf.io.prf2exe = &prf2exe;
  prf.io.prf2stq = &prf2stq;
  prf.io.exe2prf = &exe2prf;
  prf.io.prf_awake = &awake;
  prf.io.prf2dec = &prf2dec;
  prf.io.dec_bcast = &dec_bcast;
  prf.io.rob_bc = &rob_bc;

  exu.io.prf2exe = &prf2exe;
  exu.io.dec_bcast = &dec_bcast;
  exu.io.rob_bc = &rob_bc;
  exu.io.exe2prf = &exe2prf;
  exu.io.exe2stq = &exe2stq;
  exu.io.exe2iss = &exe2iss;
  exu.io.exe2csr = &exe2csr;
  exu.io.csr2exe = &csr2exe;

  rob.io.ren2rob = &ren2rob;
  rob.io.dec_bcast = &dec_bcast;
  rob.io.prf2rob = &prf2rob;
  rob.io.rob_bc = &rob_bc;
  rob.io.dec_bcast = &dec_bcast;
  rob.io.rob_commit = &rob_commit;
  rob.io.rob2ren = &rob2ren;

  stq.io.exe2stq = &exe2stq;
  stq.io.rob_commit = &rob_commit;
  stq.io.ren2stq = &ren2stq;
  stq.io.stq2iss = &stq2iss;
  stq.io.stq2ren = &stq2ren;
  stq.io.prf2stq = &prf2stq;
  stq.io.dec_bcast = &dec_bcast;
  stq.io.rob_bc = &rob_bc;

  csr.io.exe2csr = &exe2csr;
  csr.io.csr2exe = &csr2exe;
  csr.io.rob_bc = &rob_bc;

  idu.init();
  rename.init();
  isu.init();
  prf.init();
  exu.init();
  csr.init();
  rob.init();
}

void Back_Top::Back_comb() {
  // 输出提交的指令
  for (int i = 0; i < FETCH_WIDTH; i++) {
    idu.io.front2dec->valid[i] = in.valid[i];
    idu.io.front2dec->pc[i] = in.pc[i];
    idu.io.front2dec->inst[i] = in.inst[i];
    idu.io.front2dec->predict_dir[i] = in.predict_dir[i];
    idu.io.front2dec->alt_pred[i] = in.alt_pred[i];
    idu.io.front2dec->altpcpn[i] = in.altpcpn[i];
    idu.io.front2dec->pcpn[i] = in.pcpn[i];
    idu.io.front2dec->predict_next_fetch_address[i] =
        in.predict_next_fetch_address[i];

    idu.io.front2dec->page_fault_inst[i] = in.page_fault_inst[i];
  }

  // exu -> iss -> prf
  // exu -> csr

  // rename -> idu.comb_fire
  // prf->idu.comb_branch
  // isu/stq/rob -> rename.fire -> idu.fire

  idu.comb_decode();
  rob.comb_commit();
  rename.comb_alloc();
  prf.comb_branch();
  idu.comb_branch();
  exu.comb_exec();
  exu.comb_csr();
  exu.comb_ready();
  isu.comb_deq();
  prf.comb_read();
  prf.comb_amo();
  rename.comb_wake();
  rename.comb_rename();
  isu.comb_ready();
  stq.comb();
  rob.comb_ready();
  rob.comb_complete();
  idu.comb_release_tag();
  rename.comb_fire();
  rob.comb_fire();
  idu.comb_fire();
  rob.comb_flush();
  idu.comb_flush();
  csr.comb();
  rob.comb_branch();
  rename.comb_branch();
  rename.comb_store();
  rename.comb_pipeline();

  if (!rob.io.rob_bc->flush) {
    back.out.mispred = prf.io.prf2dec->mispred;
    back.out.stall = !idu.io.dec2front->ready;
    back.out.redirect_pc = prf.io.prf2dec->redirect_pc;
  } else {
    back.out.mispred = true;
    if (rob.io.rob_bc->exception) {
      if (rob.io.rob_bc->mret || rob.io.rob_bc->sret) {
        back.out.redirect_pc = csr.io.csr2exe->epc;
      } else {
        back.out.redirect_pc = csr.io.csr2exe->trap_pc;
      }
    } else {
      back.out.redirect_pc = rob.io.rob_bc->pc;
    }
  }

  // 修正pc_next 以及difftest对应的pc_next
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    back.out.commit_entry[i] = rob.io.rob_commit->commit_entry[i];
    Inst_op op = back.out.commit_entry[i].uop.op;
    if (back.out.commit_entry[i].valid && op == ECALL || op == MRET ||
        op == SRET || is_page_fault(back.out.commit_entry[i].uop) ||
        back.out.commit_entry[i].uop.illegal_inst) {
      back.out.commit_entry[i].uop.pc_next = back.out.redirect_pc;
      rob.io.rob_commit->commit_entry[i].uop.pc_next = back.out.redirect_pc;
    }
  }
  rename.comb_commit();
  rename.comb_flush();

  // 统计分支误预测率
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (back.out.commit_entry[i].valid) {
      Inst_uop uop = back.out.commit_entry[i].uop;
      if (is_branch(uop.op)) {
        br_num[(uop.pc & 0xFFFFFFF) >> 2]++;
        if (uop.mispred)
          br_mispred[(uop.pc & 0xFFFFFFF) >> 2]++;
      }
    }
  }
}

void Back_Top::Back_seq() {
  // rename -> isu/stq/rob
  // exu -> prf
  rename.seq();
  idu.seq();
  isu.seq();
  exu.seq();
  prf.seq();
  rob.seq();
  stq.seq();
  csr.seq();
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.fire[i] = idu.io.dec2front->fire[i];
  }
  update_conf();
}

#define BR_CONF_NUM 1024
#define LOAD_CONF_NUM 1024

int br_conf[BR_CONF_NUM];
int load_conf[LOAD_CONF_NUM];

void Back_Top::update_conf() {
  int idx;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (rob.io.rob_commit->commit_entry[i].valid &&
        is_branch(rob.io.rob_commit->commit_entry[i].uop.op)) {
      idx =
          (rob.io.rob_commit->commit_entry[i].uop.pc >> 2) % (BR_CONF_NUM - 1);
      if (rob.io.rob_commit->commit_entry[i].uop.mispred) {
        if (br_conf[idx] > 0) {
          br_conf[idx]--;
        }
      } else {
        if (br_conf[idx] < 3) {
          br_conf[idx]++;
        }
      }
    } else if (rob.io.rob_commit->commit_entry[i].valid &&
               is_load(rob.io.rob_commit->commit_entry[i].uop.op)) {
      idx = (rob.io.rob_commit->commit_entry[i].uop.pc >> 2) %
            (LOAD_CONF_NUM - 1);
      if (rob.io.rob_commit->commit_entry[i].uop.cache_miss) {
        if (load_conf[idx] > 0) {
          load_conf[idx]--;
        }
      } else {
        if (load_conf[idx] < 3)
          load_conf[idx]++;
      }
    }
  }
}

int read_br_conf(uint32_t pc) {
  int idx = (pc >> 2) % (BR_CONF_NUM - 1);
  return br_conf[idx];
}
