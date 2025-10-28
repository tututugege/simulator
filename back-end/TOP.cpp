#include "CSR.h"
#include "IO.h"
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstring>
#include <cvt.h>
#include <diff.h>
#include <util.h>

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

  if (inst->type == STORE) {
    dut_cpu.store = true;
    dut_cpu.store_addr = stq.entry[inst->stq_idx].addr;
    if (stq.entry[inst->stq_idx].size == 0b00)
      dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFF;
    else if (stq.entry[inst->stq_idx].size == 0b01)
      dut_cpu.store_data = stq.entry[inst->stq_idx].data & 0xFFFF;
    else
      dut_cpu.store_data = stq.entry[inst->stq_idx].data;

    dut_cpu.store_data = dut_cpu.store_data << (dut_cpu.store_addr & 0b11) * 8;

  } else if (inst->amoop == SC) {
    dut_cpu.store = true;
    int rob_idx = inst->rob_idx;
    LOOP_DEC(rob_idx, ROB_NUM);
    int stq_idx = rob.entry[rob_idx & 0b11][rob_idx >> 2].uop.stq_idx;
    dut_cpu.store_addr = stq.entry[stq_idx].addr;
    dut_cpu.store_data = stq.entry[stq_idx].data;

  } else
    dut_cpu.store = false;

  dut_cpu.pc = inst->pc_next;

  if (inst->difftest_skip) {
    difftest_skip();
  } else {
    difftest_step();
  }
#endif
}

Front_Dec front2dec;
Dec_Front dec2front;

Dec_Ren dec2ren;
Dec_Broadcast dec_bcast;

Ren_Dec ren2dec;
Ren_Dis ren2dis;

Dis_Ren dis2ren;
Dis_Iss dis2iss;
Dis_Rob dis2rob;
Dis_Stq dis2stq;

Iss_Awake iss_awake;
Iss_Prf iss2prf;
Iss_Dis iss2dis;

Prf_Exe prf2exe;
Prf_Rob prf2rob;
Prf_Awake prf_awake;
Prf_Dec prf2dec;

Exe_Prf exe2prf;
Exe_Stq exe2stq;
Exe_Iss exe2iss;

Rob_Dis rob2dis;
Rob_Broadcast rob_bcast;
Rob_Commit rob_commit;

Stq_Dis stq2dis;

Csr_Exe csr2exe;
Exe_Csr exe2csr;

void Back_Top::init() {

  idu.io.front2dec = &front2dec;
  idu.io.dec2front = &dec2front;
  idu.io.dec2ren = &dec2ren;
  idu.io.ren2dec = &ren2dec;
  idu.io.dec_bcast = &dec_bcast;
  idu.io.prf2dec = &prf2dec;
  idu.io.rob_bcast = &rob_bcast;
  idu.io.commit = &rob_commit;

  rename.io.dec2ren = &dec2ren;
  rename.io.ren2dec = &ren2dec;
  rename.io.ren2dis = &ren2dis;
  rename.io.dis2ren = &dis2ren;
  rename.io.iss_awake = &iss_awake;
  rename.io.dec_bcast = &dec_bcast;
  rename.io.rob_bcast = &rob_bcast;
  rename.io.rob_commit = &rob_commit;
  rename.io.prf_awake = &prf_awake;

  dis.io.dis2ren = &dis2ren;
  dis.io.ren2dis = &ren2dis;
  dis.io.dis2iss = &dis2iss;
  dis.io.iss2dis = &iss2dis;
  dis.io.dis2rob = &dis2rob;
  dis.io.rob2dis = &rob2dis;
  dis.io.dis2stq = &dis2stq;
  dis.io.stq2dis = &stq2dis;
  dis.io.prf_awake = &prf_awake;
  dis.io.iss_awake = &iss_awake;
  dis.io.rob_bcast = &rob_bcast;
  dis.io.dec_bcast = &dec_bcast;

  isu.io.rob_bcast = &rob_bcast;
  isu.io.dec_bcast = &dec_bcast;
  isu.io.dis2iss = &dis2iss;
  isu.io.iss2dis = &iss2dis;
  isu.io.iss2prf = &iss2prf;
  isu.io.prf_awake = &prf_awake;
  isu.io.iss_awake = &iss_awake;
  isu.io.exe2iss = &exe2iss;

  prf.io.iss2prf = &iss2prf;
  prf.io.prf2rob = &prf2rob;
  prf.io.prf2exe = &prf2exe;
  prf.io.exe2prf = &exe2prf;
  prf.io.prf_awake = &prf_awake;
  prf.io.prf2dec = &prf2dec;
  prf.io.dec_bcast = &dec_bcast;
  prf.io.rob_bcast = &rob_bcast;

  exu.io.prf2exe = &prf2exe;
  exu.io.dec_bcast = &dec_bcast;
  exu.io.rob_bcast = &rob_bcast;
  exu.io.exe2prf = &exe2prf;
  exu.io.exe2stq = &exe2stq;
  exu.io.exe2iss = &exe2iss;
  exu.io.exe2csr = &exe2csr;
  exu.io.csr2exe = &csr2exe;

  rob.io.dis2rob = &dis2rob;
  rob.io.dec_bcast = &dec_bcast;
  rob.io.prf2rob = &prf2rob;
  rob.io.rob_bcast = &rob_bcast;
  rob.io.dec_bcast = &dec_bcast;
  rob.io.rob_commit = &rob_commit;
  rob.io.rob2dis = &rob2dis;

  stq.io.exe2stq = &exe2stq;
  stq.io.rob_commit = &rob_commit;
  stq.io.dis2stq = &dis2stq;
  stq.io.stq2dis = &stq2dis;
  stq.io.dec_bcast = &dec_bcast;
  stq.io.rob_bcast = &rob_bcast;

  csr.io.exe2csr = &exe2csr;
  csr.io.csr2exe = &csr2exe;
  csr.io.rob_bcast = &rob_bcast;

  idu.init();
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
  prf.comb_br_check();
  idu.comb_branch();
  rob.comb_commit();
  rename.comb_alloc();
  dis.comb_alloc();
  prf.comb_complete();
  prf.comb_awake();
  exu.comb_exec();
  exu.comb_to_csr();
  exu.comb_ready();
  isu.comb_deq();
  prf.comb_read();
  rename.comb_wake();
  dis.comb_wake();
  rename.comb_rename();
  isu.comb_ready();
  dis.comb_dispatch();
  stq.comb();
  rob.comb_ready();
  rob.comb_complete();
  idu.comb_release_tag();
  dis.comb_fire();
  rename.comb_fire();
  rob.comb_fire();
  idu.comb_fire();
  rob.comb_flush();
  idu.comb_flush();
  csr.comb();
  exu.comb_from_csr();
  rob.comb_branch();
  rename.comb_branch();
  rename.comb_pipeline();
  exu.comb_branch();
  exu.comb_pipeline();
  exu.comb_flush();
  prf.comb_write();
  prf.comb_branch();
  prf.comb_pipeline();
  prf.comb_flush();
  dis.comb_pipeline();

  back.out.flush = rob.io.rob_bcast->flush;

  if (!rob.io.rob_bcast->flush) {
    back.out.mispred = prf.io.prf2dec->mispred;
    back.out.stall = !idu.io.dec2front->ready;
    back.out.redirect_pc = prf.io.prf2dec->redirect_pc;
  } else {

    if (LOG)
      cout << "flush" << endl;
    back.out.mispred = true;
    if (rob.io.rob_bcast->exception) {
      if (rob.io.rob_bcast->mret || rob.io.rob_bcast->sret) {
        back.out.redirect_pc = csr.io.csr2exe->epc;
      } else {
        back.out.redirect_pc = csr.io.csr2exe->trap_pc;
      }
    } else {
      back.out.redirect_pc = rob.io.rob_bcast->pc;
    }
  }

  // 修正pc_next 以及difftest对应的pc_next
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    back.out.commit_entry[i] = rob.io.rob_commit->commit_entry[i];
    Inst_type type = back.out.commit_entry[i].uop.type;
    if (back.out.commit_entry[i].valid && type == ECALL || type == MRET ||
        type == SRET || is_page_fault(back.out.commit_entry[i].uop) ||
        back.out.commit_entry[i].uop.illegal_inst) {
      back.out.commit_entry[i].uop.pc_next = back.out.redirect_pc;
      rob.io.rob_commit->commit_entry[i].uop.pc_next = back.out.redirect_pc;
    }
  }
  rename.comb_commit();
  rename.comb_flush();
}

void Back_Top::Back_seq() {
  // rename -> isu/stq/rob
  // exu -> prf
  rename.seq();
  dis.seq();
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
}
