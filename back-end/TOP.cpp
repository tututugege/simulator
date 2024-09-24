#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>

uint32_t load_data(Inst_op op, bool *offset);
void store_data(Inst_op op);

void operand_mux(Inst_info inst, uint32_t reg_data1, uint32_t reg_data2,
                 uint32_t &operand1, uint32_t &operand2) {
  if (inst.op == AUIPC || inst.op == JALR || inst.op == JAL)
    operand1 = inst.pc;
  else if (inst.op == LUI)
    operand1 = 0;
  else
    operand1 = reg_data1;

  if (inst.type == ITYPE && inst.op != JAL || inst.type == STYPE ||
      inst.type == UTYPE) {
    operand2 = inst.imm;
  } else if (inst.op == JALR && inst.op == JAL) {
    operand2 = 4;
  } else {
    operand2 = reg_data2;
  }
}

void Back_Top::difftest(int pc) {
  for (int i = 0; i < ARF_NUM; i++) {
    dut.gpr[i] = prf.debug_read(rename.arch_RAT[i]);
  }
  dut.pc = pc;
  difftest_step();
}

void Back_Top::arch_update(int areg_idx, int preg_idx) {
  rename.arch_RAT[areg_idx] = preg_idx;
}

Back_Top::Back_Top() : int_iq(8, 2), ld_iq(4, 1), st_iq(4, 1) {}

void Back_Top::init() {
  rename.init();
  int_iq.init();
  ld_iq.init();
  st_iq.init();
  rob.init();
}

void Back_Top::Back_comb(bool *input_data, bool *output_data) {
  // 组合逻辑
  // pipeline1: 重命名 dispatch

  for (int i = 0; i < INST_WAY; i++) {
    rename.in.valid[i] = in.valid[i];
    rename.in.src1_areg_idx[i] = in.inst[i].src1_idx;
    rename.in.src1_areg_en[i] = in.inst[i].src1_en;
    rename.in.src2_areg_idx[i] = in.inst[i].src2_idx;
    rename.in.src2_areg_en[i] = in.inst[i].src2_en;
    rename.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rename.in.dest_areg_en[i] = in.inst[i].dest_en;
  }
  rename.comb();

  // rob入队输入
  for (int i = 0; i < INST_WAY; i++) {
    rob.in.valid[i] = in.valid[i];
    rob.in.PC[i] = in.inst[i].pc;
    rob.in.type[i] = in.inst[i].type;
    rob.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rob.in.dest_preg_idx[i] = rename.out.dest_preg_idx[i];
    rob.in.dest_en[i] = in.inst[i].dest_en;
    rob.in.old_dest_preg_idx[i] = rename.out.old_dest_preg_idx[i];
  }

  // ld queue入队输入
  for (int i = 0; i < INST_WAY; i++) {
    if (in.inst[i].type == LTYPE) {
      /*ldq.in.alloc[i].pc = in.inst[i].pc;*/
      ldq.in.alloc[i].dest_preg_idx = rename.out.dest_preg_idx[i];
      /*ldq.in.alloc[i].rob_bit =*/
      /*    (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit :
       * rob.out.enq_bit;*/
      ldq.in.alloc[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;
      /*ldq.in.alloc[i].size = rename.out.dest_preg_idx[i];*/
    } else {
      ldq.in.alloc[i].valid = false;
    }
  }

  // iq入队输入
  for (int i = 0; i < INST_WAY; i++) {
    int_iq.in.valid[i] =
        in.inst[i].type != LTYPE && in.inst[i].type != STYPE && in.valid[i];
    int_iq.in.src1_ready[i] = true;
    int_iq.in.src2_ready[i] = true;

    int_iq.in.pos_bit[i] =
        (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit : rob.out.enq_bit;

    int_iq.in.pos_idx[i] = (rob.out.enq_idx + i) % ROB_NUM;

    int_iq.in.inst[i].pc = in.inst[i].pc;
    int_iq.in.inst[i].type = in.inst[i].type;
    int_iq.in.inst[i].op = in.inst[i].op;
    int_iq.in.inst[i].imm = in.inst[i].imm;
    int_iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    int_iq.in.inst[i].src1_en = rename.in.src1_areg_en[i];
    int_iq.in.inst[i].src2_idx = rename.out.src2_preg_idx[i];
    int_iq.in.inst[i].src2_en = rename.in.src2_areg_en[i];
    int_iq.in.inst[i].dest_idx = rename.out.dest_preg_idx[i];
    int_iq.in.inst[i].dest_en = rename.in.dest_areg_en[i];

    st_iq.in.valid[i] = in.inst[i].type == STYPE && in.valid[i];
    st_iq.in.src1_ready[i] = !rename.in.src1_areg_en[i];
    st_iq.in.src2_ready[i] = !rename.in.src2_areg_en[i];

    st_iq.in.pos_bit[i] =
        (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit : rob.out.enq_bit;

    st_iq.in.pos_idx[i] = (rob.out.enq_idx + i) % ROB_NUM;
    st_iq.in.inst[i].pc = in.inst[i].pc;
    st_iq.in.inst[i].type = in.inst[i].type;
    st_iq.in.inst[i].op = in.inst[i].op;
    st_iq.in.inst[i].imm = in.inst[i].imm;
    st_iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    st_iq.in.inst[i].src1_en = true;
    st_iq.in.inst[i].src2_idx = rename.out.src2_preg_idx[i];
    st_iq.in.inst[i].src2_en = true;
    st_iq.in.inst[i].dest_en = false;

    ld_iq.in.valid[i] = in.inst[i].type == LTYPE && in.valid[i];
    ld_iq.in.src1_ready[i] = !rename.in.src1_areg_en[i];
    ld_iq.in.src2_ready[i] = !rename.in.src2_areg_en[i];

    ld_iq.in.pos_bit[i] =
        (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit : rob.out.enq_bit;

    ld_iq.in.pos_idx[i] = (rob.out.enq_idx + i) % ROB_NUM;
    ld_iq.in.inst[i].pc = in.inst[i].pc;
    ld_iq.in.inst[i].type = in.inst[i].type;
    ld_iq.in.inst[i].op = in.inst[i].op;
    ld_iq.in.inst[i].imm = in.inst[i].imm;
    ld_iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    ld_iq.in.inst[i].src1_en = rename.in.src1_areg_en[i];
    ld_iq.in.inst[i].src2_en = false;
    ld_iq.in.inst[i].dest_idx = rename.out.dest_preg_idx[i];
    ld_iq.in.inst[i].dest_en = rename.in.dest_areg_en[i];
  }

  // pipeline2:

  // execute 执行

  // select 仲裁 发射指令 ALU_NUM + AGU_NUM
  int_iq.comb();
  ld_iq.comb();
  st_iq.comb();

  // 读寄存器
  for (int i = 0; i < ALU_NUM; i++) {
    prf.to_sram.raddr[2 * i] = int_iq.out.inst[i].src1_idx;
    prf.to_sram.raddr[2 * i + 1] = int_iq.out.inst[i].src2_idx;
  }

  prf.to_sram.raddr[2 * (ALU_NUM)] = ld_iq.out.inst[0].src1_idx;

  prf.to_sram.raddr[2 * (ALU_NUM) + 1] = st_iq.out.inst[0].src1_idx;
  prf.to_sram.raddr[2 * (ALU_NUM) + 2] = st_iq.out.inst[0].src2_idx;

  prf.read();

  // 往所有ALU BRU发射指令
  for (int i = 0; i < ALU_NUM; i++) {
    Inst_info inst = int_iq.out.inst[i];
    if (int_iq.out.valid[i]) {
      // 操作数选择
      operand_mux(inst, prf.from_sram.rdata[2 * i],
                  prf.from_sram.rdata[2 * i + 1], alu[i].in.src1,
                  alu[i].in.src2);
      alu[i].in.op = inst.op;
      alu[i].cycle();

      bru[i].in.src1 = prf.from_sram.rdata[2 * 1];
      bru[i].in.src2 = prf.from_sram.rdata[2 * 1 + 1];
      bru[i].in.pc = inst.pc;
      bru[i].in.off = inst.imm;
      bru[i].in.op = inst.op;
      bru[i].cycle();

      rob.in.complete[i] = true;
      rob.in.idx[i] = int_iq.out.pos_idx[i];

      prf.to_sram.we[i] = int_iq.out.inst[i].dest_en;
      prf.to_sram.waddr[i] = int_iq.out.inst[i].dest_idx;
      prf.to_sram.wdata[i] = alu[i].out.res;

    } else
      rob.in.complete[i] = false;
  }

  // load指令计算地址
  Inst_info inst = ld_iq.out.inst[0];
  // 操作数选择
  agu[0].in.base = prf.from_sram.rdata[2 * ALU_NUM];
  agu[0].in.off = inst.imm;
  agu[0].cycle();

  // 发出访存请求 地址写入LDQ
  if (ld_iq.out.valid[0]) {
    cvt_number_to_bit(output_data + POS_OUT_LOAD_ADDR, agu[0].out.addr, 32);

    ldq.in.write.valid = true;
    ldq.in.write.rob_idx = ld_iq.out.pos_idx[0];
    ldq.in.write.addr = agu[0].out.addr;

    prf.to_sram.we[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_en;
    prf.to_sram.waddr[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_idx;

    /*prf.wdata[PRF_WR_LD_PORT] = read_data();*/

    for (int i = 0; i < ISSUE_WAY; i++) {
      ldq.in.commit[i] = rob.out.commit_entry[i].type == LTYPE;
    }
    /*rob.in.complete[ALU_NUM] = true;*/
    /*rob.in.idx[ALU_NUM] = ld_iq.out.pos_idx[0];*/
    /*rob.in.complete[ALU_NUM] = false;*/
  }

  // store指令计算地址 写入store queue
  // 操作数选择
  agu[1].in.base = prf.from_sram.rdata[2 * ALU_NUM + 1];
  agu[1].in.off = inst.imm;
  agu[1].cycle();

  if (st_iq.out.valid[0]) {
  }

  rob.comb();

  // rob
  for (int i = 0; i < ISSUE_WAY; i++) {
    /*if (rob.out.commit_entry[i].op != NONE &&
     * rob.out.commit_entry[i].dest_en)
     * {*/
    /*  rename.free_reg(rob.out.commit_entry[i].old_dest_preg_idx);*/
    /*}*/
    rename.in.commit_valid[i] = rob.out.valid[i];
    rename.in.commit_dest_en[i] = rob.out.commit_entry[i].dest_en;
    rename.in.commit_dest_preg_idx[i] = rob.out.commit_entry[i].dest_preg_idx;
    rename.in.commit_dest_areg_idx[i] = rob.out.commit_entry[i].dest_areg_idx;
    rename.in.commit_old_dest_preg_idx[i] =
        rob.out.commit_entry[i].old_dest_preg_idx;
  }
}

void Back_Top::Back_seq(bool *input_data, bool *output_data) {

  // 时序逻辑
  // pipeline1: 写入ROB和IQ 重命名表更新
  // pipeline2: 执行结果写回 唤醒等待的指令(目前不考虑) 在ROB中标记执行完毕
  // pipeline3: ROB提交 更新free_list 重命名映射表
  prf.write();
  rename.seq();
  rob.seq(); // dispatch写入rob  提交指令  store结果  标记complete
  ldq.seq();
  int_iq.seq(); // dispatch写入发射队列  发射后删除
  ld_iq.seq();  // dispatch写入发射队列  发射后删除
  st_iq.seq();  // dispatch写入发射队列  发射后删除

  /*  ROB_entry commit_entry;*/
  /*  while ((commit_entry = rob.commit()).complete) {*/
  /*    if (commit_entry.dest_en) {*/
  /*      rename.free_reg(commit_entry.old_dest_preg_idx);*/
  /*      rename.arch_RAT[commit_entry.dest_areg_idx] =
   * commit_entry.dest_preg_idx;*/
  /*    }*/
  /**/
  /*    if (commit_entry.op == SW || commit_entry.op == SH ||*/
  /*        commit_entry.op == SB) {*/
  /*    }*/
  /**/
  /*    if (log) {*/
  /*      cout << "ROB commit PC 0x" << hex << commit_entry.PC << endl;*/
  /*      rename.print_reg();*/
  /*rename.print_RAT();*/
  /*    }*/
  /**/
  /*#ifdef CONFIG_DIFFTEST*/
  /*    for (int i = 0; i < ARF_NUM; i++) {*/
  /*      dut.gpr[i] = rename.reg(i);*/
  /*    }*/
  /**/
  /*    if (commit_entry.branch) {*/
  /**/
  /*      dut.pc = cvt_bit_to_number_unsigned(output_data + POS_OUT_PC, 32);*/
  /*    } else {*/
  /*      dut.pc = commit_entry.PC + 4;*/
  /*    }*/
  /**/
  /*    difftest_step();*/
  /*#endif*/
  /*  }*/
}
