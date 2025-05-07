#include "CSR.h"
#include "IO.h"
#include "frontend.h"
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstring>
#include <cvt.h>
#include <diff.h>
#include <util.h>

extern int commit_num;

void load_data();
/*void store_data();*/

void Back_Top::difftest(Inst_info *inst) {
  if (inst->dest_en)
    rename.arch_RAT[inst->dest_areg] = inst->dest_preg;

  for (int i = 0; i < ARF_NUM; i++) {
    dut.gpr[i] = prf.reg_file[rename.arch_RAT[i]];
  }

  dut.pc = inst->pc_next;
  difftest_step();
}

/*Back_Top::Back_Top() : int_iq(8, 2, INT), ld_iq(4, 1, LD), st_iq(4, 1, ST)
 * {}*/

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
  exu.comb();
  isu.comb_ready();
  isu.comb_deq();
  prf.comb_read();
  prf.comb_amo();
  rename.comb_wake();
  rename.comb_rename();
  stq.comb();
  rob.comb_ready();
  rob.comb_complete();
  idu.comb_release_tag();
  rename.comb_fire();
  rob.comb_fire();
  idu.comb_fire();
  rob.comb_rollback();
  idu.comb_rollback();
  csr.comb();
  rob.comb_branch();
  rename.comb_branch();
  rename.comb_store();
  rename.comb_pipeline();

  if (!rob.io.rob_bc->rollback) {
    back.out.mispred = prf.io.prf2dec->mispred;
    back.out.stall = !idu.io.dec2front->ready;
    back.out.redirect_pc = prf.io.prf2dec->redirect_pc;
  } else {
    back.out.mispred = true;
    if (rob.io.rob_bc->exception) {
      back.out.redirect_pc = csr.io.csr2exe->mtvec;
    } else if (rob.io.rob_bc->mret) {
      back.out.redirect_pc = csr.io.csr2exe->mepc;
    }
  }

  // 修正pc_next 以及difftest对应的pc_next
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    back.out.commit_entry[i] = rob.io.rob_commit->commit_entry[i];
    if (back.out.commit_entry[i].valid &&
            back.out.commit_entry[i].inst.op == ECALL ||
        back.out.commit_entry[i].inst.op == MRET) {
      back.out.commit_entry[i].inst.pc_next = back.out.redirect_pc;
      rob.io.rob_commit->commit_entry[i].inst.pc_next = back.out.redirect_pc;
    }
  }
  rename.comb_commit();
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

  /*idu.reg_gen();*/
}
