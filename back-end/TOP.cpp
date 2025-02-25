#include "CSR.h"
#include "IO.h"
#include <DAG.h>
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
  commit_num++;
}

/*Back_Top::Back_Top() : int_iq(8, 2, INT), ld_iq(4, 1, LD), st_iq(4, 1, ST)
 * {}*/

Front_Dec front2id;
Dec_Front id2front;

Dec_Ren id2ren;
Dec_Broadcast id_bc;

Ren_Dec ren2id;
Ren_Iss ren2iss;
Ren_Rob ren2rob;
Ren_Stq ren2stq;

Iss_Ren iss2ren;
Iss_Prf iss2prf;

Prf_Exe prf2exe;
Prf_Rob prf2rob;
Prf_Awake awake;
Prf_Dec prf2id;

Exe_Prf exe2prf;
Exe_Stq exe2stq;
Exe_Iss exe2iss;

Rob_Ren rob2ren;
Rob_Broadcast rob_bc;
Rob_Commit rob_commit;

Stq_Ren stq2ren;
Stq_Iss stq2iss;

void Back_Top::init() {

  idu.io.front2id = &front2id;
  idu.io.id2front = &id2front;
  idu.io.id2ren = &id2ren;
  idu.io.ren2id = &ren2id;
  idu.io.id_bc = &id_bc;
  idu.io.prf2id = &prf2id;
  idu.io.rob_bc = &rob_bc;
  idu.io.commit = &rob_commit;

  rename.io.dec2ren = &id2ren;
  rename.io.ren2dec = &ren2id;
  rename.io.ren2iss = &ren2iss;
  rename.io.iss2ren = &iss2ren;
  rename.io.id_bc = &id_bc;
  rename.io.rob2ren = &rob2ren;
  rename.io.ren2rob = &ren2rob;
  rename.io.ren2stq = &ren2stq;
  rename.io.stq2ren = &stq2ren;
  rename.io.rob_bc = &rob_bc;
  rename.io.rob_commit = &rob_commit;
  rename.io.awake = &awake;

  isu.io.rob_bc = &rob_bc;
  isu.io.id_bc = &id_bc;
  isu.io.ren2iss = &ren2iss;
  isu.io.iss2ren = &iss2ren;
  isu.io.iss2prf = &iss2prf;
  isu.io.awake = &awake;
  isu.io.stq2iss = &stq2iss;
  isu.io.exe2iss = &exe2iss;

  prf.io.iss2prf = &iss2prf;
  prf.io.prf2rob = &prf2rob;
  prf.io.prf2exe = &prf2exe;
  prf.io.exe2prf = &exe2prf;
  prf.io.prf_awake = &awake;
  prf.io.prf2id = &prf2id;
  prf.io.id_bc = &id_bc;

  exu.io.prf2exe = &prf2exe;
  exu.io.id_bc = &id_bc;
  exu.io.exe2prf = &exe2prf;
  exu.io.exe2stq = &exe2stq;
  exu.io.exe2iss = &exe2iss;

  rob.io.ren2rob = &ren2rob;
  rob.io.id_bc = &id_bc;
  rob.io.prf2rob = &prf2rob;
  rob.io.rob_bc = &rob_bc;
  rob.io.id_bc = &id_bc;
  rob.io.rob_commit = &rob_commit;
  rob.io.rob2ren = &rob2ren;

  stq.io.exe2stq = &exe2stq;
  stq.io.rob_commit = &rob_commit;
  stq.io.ren2stq = &ren2stq;
  stq.io.stq2iss = &stq2iss;
  stq.io.stq2ren = &stq2ren;
  stq.io.id_bc = &id_bc;

  idu.init();
  rename.init();
  isu.init();
  prf.init();
  exu.init();
  csru.init();
  rob.init();
  init_dag();
}

void Back_Top::Back_comb() {
  // 输出提交的指令
  for (int i = 0; i < INST_WAY; i++) {
    idu.io.front2id->valid[i] = in.valid[i];
    idu.io.front2id->pc[i] = in.pc[i];
    idu.io.front2id->inst[i] = in.inst[i];
  }

  // 顺序：rename -> stq/rob/isu
  // exu -> iss -> prf
  rename.comb();
  stq.comb();
  rob.comb();
  exu.comb();
  isu.comb();
  prf.comb();
  idu.comb();
  back.out.mispred = prf.io.prf2id->mispred;
  back.out.redirect_pc = prf.io.prf2id->redirect_pc;
}

void Back_Top::Back_seq() {
  // 更新Reg SRAM
  rename.seq();
  idu.seq();
  isu.seq();
  prf.seq();
  exu.seq();
  rob.seq();
  stq.seq();

  for (int i = 0; i < INST_WAY; i++) {
    out.fire[i] = idu.io.id2front->dec_fire[i];
  }
}
