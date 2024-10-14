#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cvt.h>
#include <diff.h>
#include <util.h>

/*uint32_t load_data();*/
/*void store_data();*/

void operand_mux(Inst_info inst, uint32_t reg_data1, uint32_t reg_data2,
                 uint32_t &operand1, uint32_t &operand2) {
  if (inst.op == AUIPC || inst.op == JAL || inst.op == JALR)
    operand1 = inst.pc;
  else if (inst.op == LUI)
    operand1 = 0;
  else
    operand1 = reg_data1;

  if (inst.src2_is_imm) {
    operand2 = inst.imm;
  } else if (inst.op == JALR && inst.op == JAL) {
    operand2 = 4;
  } else {
    operand2 = reg_data2;
  }
}

void Back_Top::difftest(Inst_info inst) {
  if (inst.dest_en)
    rename.arch_RAT[inst.dest_areg] = inst.dest_preg;

  for (int i = 0; i < ARF_NUM; i++) {
    dut.gpr[i] = prf.debug_read(rename.arch_RAT[i]);
  }
  dut.pc = inst.pc_next;
  difftest_step();
}

Back_Top::Back_Top() : int_iq(8, 2), ld_iq(4, 1), st_iq(4, 1) {}

void Back_Top::init() {
  idu.init();
  rename.init();
  int_iq.init();
  ld_iq.init();
  st_iq.init();
  rob.init();
}

void Back_Top::Back_comb(bool *input_data, bool *output_data) {
  bool *valid = input_data + POS_IN_INST_VALID;
  bool *fire = output_data + POS_OUT_FIRE;
  bool *instruction[INST_WAY];
  uint32_t number_pc_unsigned[INST_WAY];
  bool *bit_this_pc[INST_WAY];
  bool stall[INST_WAY];

  // 输出提交的指令
  rob.comb_commit();

  for (int i = 0; i < ISSUE_WAY; i++) {
    rename.in.commit_valid[i] = rob.out.valid[i];
    rename.in.commit_inst[i] = rob.out.commit_entry[i];
    idu.in.free_valid[i] =
        rob.out.valid[i] && is_branch(rob.out.commit_entry[i]);
    idu.in.free_tag[i] = rob.out.commit_entry[i].tag;
  }

  // 发射指令
  int_iq.comb_deq();
  st_iq.comb_deq();
  ld_iq.comb_deq();

  // 读寄存器
  for (int i = 0; i < ALU_NUM; i++) {
    prf.to_sram.raddr[2 * i] = int_iq.out.inst[i].src1_preg;
    prf.to_sram.raddr[2 * i + 1] = int_iq.out.inst[i].src2_preg;
  }

  prf.to_sram.raddr[2 * (ALU_NUM)] = ld_iq.out.inst[0].src1_preg;

  prf.to_sram.raddr[2 * (ALU_NUM) + 1] = st_iq.out.inst[0].src1_preg;
  prf.to_sram.raddr[2 * (ALU_NUM) + 2] = st_iq.out.inst[0].src2_preg;

  prf.read();

  // 往所有ALU BRU发射指令
  for (int i = 0; i < ALU_NUM; i++) {
    Inst_info inst = int_iq.out.inst[i];
    if (int_iq.out.valid[i]) {
      // 操作数选择
      operand_mux(inst, prf.from_sram.rdata[2 * i],
                  prf.from_sram.rdata[2 * i + 1], alu[i].in.src1,
                  alu[i].in.src2);
      alu[i].in.alu_op.op = inst.op;
      alu[i].in.alu_op.func3 = inst.func3;
      alu[i].in.alu_op.func7_5 = inst.func7_5;
      alu[i].in.alu_op.src2_is_imm = inst.src2_is_imm;
      alu[i].cycle();

      bru[i].in.src1 = prf.from_sram.rdata[2 * 1];
      bru[i].in.pc = inst.pc;
      bru[i].in.alu_out = (bool)alu[i].out.res;
      bru[i].in.off = inst.imm;
      bru[i].in.op = inst.op;
      bru[i].cycle();

      prf.to_sram.we[i] = int_iq.out.inst[i].dest_en;
      prf.to_sram.waddr[i] = int_iq.out.inst[i].dest_preg;
      prf.to_sram.wdata[i] = alu[i].out.res;

    } else {
      alu[i].cycle();
      bru[i].cycle();
    }
  }

  /*// load指令计算地址*/
  /*Inst_info inst = ld_iq.out.inst[0];*/
  /*inst.pc_next = inst.pc + 4;*/
  /*// 操作数选择*/
  /*agu[0].in.base = prf.from_sram.rdata[2 * ALU_NUM];*/
  /*agu[0].in.off = inst.imm;*/
  /*agu[0].cycle();*/
  /**/
  /*// 发出访存请求 地址写入LDQ*/
  /*if (ld_iq.out.valid[0]) {*/
  /*  cvt_number_to_bit(output_data + POS_OUT_LOAD_ADDR, agu[0].out.addr, 32);*/
  /**/
  /*  ldq.in.write.valid = true;*/
  /*  ldq.in.write.rob_idx = ld_iq.out.inst[0].rob_idx;*/
  /*  ldq.in.write.addr = agu[0].out.addr;*/
  /*  ldq.in.write.size = inst.func3;*/
  /**/
  /*  prf.to_sram.we[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_en;*/
  /*  prf.to_sram.waddr[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_preg;*/
  /*  prf.to_sram.wdata[PRF_WR_LD_PORT] = load_data(); */
  /**/
  /*  for (int i = 0; i < ISSUE_WAY; i++) {*/
  /*    ldq.in.commit[i] = (rob.out.commit_entry[i].op == LOAD);*/
  /*  }*/
  /*}*/
  /*rob.in.from_ex_inst[ALU_NUM] = inst;*/
  /**/
  /*inst = st_iq.out.inst[0];*/
  /*inst.pc_next = inst.pc + 4;*/
  /**/
  /*// store指令计算地址 写入store queue*/
  /*// 操作数选择*/
  /*agu[1].in.base = prf.from_sram.rdata[2 * ALU_NUM + 1];*/
  /*agu[1].in.off = inst.imm;*/
  /*agu[1].cycle();*/
  /**/
  /*if (st_iq.out.valid[0]) {*/
  /*  stq.in.write.valid = true;*/
  /*  stq.in.write.addr = agu[1].out.addr;*/
  /*  stq.in.write.size = inst.func3;*/
  /*}*/
  /*rob.in.from_ex_inst[ALU_NUM] = inst;*/

  bool br_taken = false;
  int br_idx;
  for (br_idx = 0; br_idx < BRU_NUM; br_idx++) {
    if (int_iq.out.valid[br_idx] && bru[br_idx].out.br_taken) {
      br_taken = true;
      cvt_number_to_bit_unsigned(output_data + POS_OUT_PC,
                                 bru[br_idx].out.pc_next, 32);
      break;
    }
  }

  *(output_data + POS_OUT_BRANCH) = br_taken;

  // idu处理分支，如果taken会输出需要清除的tag_mask
  for (int i = 0; i < INST_WAY; i++) {
    idu.in.valid[i] = valid[i] && !br_taken;
    idu.in.instruction[i] = input_data + POS_IN_INST + 32 * i;
    bit_this_pc[i] = input_data + POS_IN_PC + 32 * i;
    number_pc_unsigned[i] = cvt_bit_to_number_unsigned(bit_this_pc[i], 32);
  }
  idu.in.br.br_taken = br_taken;
  idu.in.br.br_tag = int_iq.out.inst[br_idx].tag;
  rob.in.br_taken = br_taken;
  rob.in.br_rob_idx = int_iq.out.inst[br_idx].rob_idx;

  // 译码 分配新tag 回收tag 处理分支
  idu.comb_dec();

  for (int i = 0; i < INST_WAY; i++) {
    idu.out.inst[i].pc = number_pc_unsigned[i];
  }

  rename.in.br = idu.out.br;
  int_iq.in.br = idu.out.br;
  ld_iq.in.br = idu.out.br;
  st_iq.in.br = idu.out.br;

  // 完成重命名 根据输入的valid以及寄存器存留个数输出stall
  for (int i = 0; i < INST_WAY; i++) {
    rename.in.valid[i] = idu.out.valid[i];
    rename.in.inst[i] = idu.out.inst[i];
  }

  for (int i = 0; i < ALU_NUM; i++) {
    rename.in.wake[i].valid = int_iq.out.valid[i] && int_iq.out.inst[i].dest_en;
    rename.in.wake[i].preg = int_iq.out.inst[i].dest_preg;
  }

  rename.comb_alloc();

  for (int i = 0; i < ALU_NUM; i++) {
    if (!br_taken || i <= br_idx)
      rob.in.from_ex_valid[i] = int_iq.out.valid[i];
    else
      rob.in.from_ex_valid[i] = false;
    rob.in.from_ex_inst[i] = int_iq.out.inst[i];
    rob.in.from_ex_inst[i].pc_next = bru[i].out.pc_next;
  }
  rob.in.from_ex_valid[ALU_NUM] = ld_iq.out.valid[0] && !br_taken;
  rob.in.from_ex_inst[ALU_NUM] = ld_iq.out.inst[0];
  rob.in.from_ex_inst[ALU_NUM].pc_next = ld_iq.out.inst[0].pc + 4;
  rob.in.from_ex_valid[ALU_NUM + 1] = st_iq.out.valid[0] && !br_taken;
  rob.in.from_ex_inst[ALU_NUM + 1] = st_iq.out.inst[0];
  rob.in.from_ex_inst[ALU_NUM + 1].pc_next = st_iq.out.inst[0].pc + 4;

  for (int i = 0; i < INST_WAY; i++) {
    rob.in.from_ren_valid[i] = rename.out.valid[i];
  }

  rob.comb_complete();

  for (int i = 0; i < INST_WAY; i++) {
    int_iq.in.valid[i] = rename.out.valid[i];
    int_iq.in.inst[i] = rename.out.inst[i];
    int_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;

    st_iq.in.valid[i] = rename.out.valid[i];
    st_iq.in.inst[i] = rename.out.inst[i];
    st_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;

    ld_iq.in.valid[i] = rename.out.valid[i];
    ld_iq.in.inst[i] = rename.out.inst[i];
    ld_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;
  }

  bool dis_fire[INST_WAY];
  bool dis_stall[INST_WAY];

  for (int i = 0; i < INST_WAY; i++) {
    dis_stall[i] = !rob.out.to_ren_ready[i] ||
                   !orR(rename.out.ready, INST_WAY) ||
                   !orR(idu.out.ready, INST_WAY);
    if (idu.out.inst[i].op == LOAD)
      dis_stall[i] = dis_stall[i] || !ld_iq.out.ready[i];
    else if (idu.out.inst[i].op == STORE)
      dis_stall[i] = dis_stall[i] || !st_iq.out.ready[i];
    else
      dis_stall[i] = dis_stall[i] || !int_iq.out.ready[i];

    dis_fire[i] = (!dis_stall[i]) && (rename.out.valid[i]);
    rename.in.dis_fire[i] = dis_fire[i];
    idu.in.dis_fire[i] = dis_fire[i];
    rob.in.dis_fire[i] = dis_fire[i];

    int_iq.in.dis_fire[i] = idu.out.inst[i].op != LOAD &&
                            idu.out.inst[i].op != STORE && dis_fire[i];
    st_iq.in.dis_fire[i] = idu.out.inst[i].op == STORE && dis_fire[i];
    ld_iq.in.dis_fire[i] = idu.out.inst[i].op == LOAD && dis_fire[i];
  }

  rename.comb_fire();
  idu.comb_fire();
  int_iq.comb_enq();
  ld_iq.comb_enq();
  st_iq.comb_enq();

  // rob入队输入
  for (int i = 0; i < INST_WAY; i++) {
    rob.in.from_ren_valid[i] = rename.out.valid[i];
    rob.in.from_ren_inst[i] = rename.out.inst[i];
  }

  rob.comb_enq();

  *(output_data + POS_OUT_STALL) = orR(dis_stall, INST_WAY);

  for (int i = 0; i < INST_WAY; i++) {
    *(output_data + POS_OUT_FIRE + i) = dis_fire[i];
  }
  /*// ld queue分配*/
  /*for (int i = 0; i < INST_WAY; i++) {*/
  /*  if (in.inst[i].op == LOAD) {*/
  /*    ldq.in.alloc[i].dest_preg_idx = rename.out.inst[i].dest_preg;*/
  /*    ldq.in.alloc[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;*/
  /*    ldq.in.alloc[i].valid = true;*/
  /*  } else {*/
  /*    ldq.in.alloc[i].valid = false;*/
  /*  }*/
  /*}*/
  /**/
  /*// st queue分配*/
  /*for (int i = 0; i < INST_WAY; i++) {*/
  /*  if (in.inst[i].op == STORE) {*/
  /*    stq.in.alloc[i].valid = false;*/
  /*  } else {*/
  /*    stq.in.alloc[i].valid = false;*/
  /*  }*/
  /*}*/
}

void Back_Top::Back_seq(bool *input_data, bool *output_data) {
  // 时序逻辑
  // pipeline1: 写入ROB和IQ 重命名表更新
  // pipeline2: 执行结果写回 唤醒等待的指令(目前不考虑) 在ROB中标记执行完毕
  // pipeline3: ROB提交 更新free_list 重命名映射表
  idu.seq();
  prf.write();
  rename.seq();
  rob.seq(); // dispatch写入rob  提交指令  store结果  标记complete
  ldq.seq();
  int_iq.seq(); // dispatch写入发射队列  发射后删除
  ld_iq.seq();  // dispatch写入发射队列  发射后删除
  st_iq.seq();  // dispatch写入发射队列  发射后删除
}
