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
    dut.gpr[i] = prf.PRF[rename.arch_RAT[i]];
  }
  dut.pc = pc;
  difftest_step();
}

void Back_Top::init(bool *output_data) {
  rename.init();
  iq.init();
  rob.init();
  back.rename.preg_base = output_data;
}

void Back_Top::Back_comb(bool *input_data, bool *output_data) {
  // 组合逻辑
  // pipeline1: 重命名 dispatch

  for (int i = 0; i < INST_WAY; i++) {
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
    rob.in.PC[i] = in.inst[i].pc;
    rob.in.op[i] = rename.out.full ? NONE : in.inst[i].op;
    rob.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rob.in.dest_preg_idx[i] = rename.out.dest_preg_idx[i];
    rob.in.dest_en[i] = in.inst[i].dest_en;
    rob.in.old_dest_preg_idx[i] = rename.out.old_dest_preg_idx[i];
  }

  // ld queue入队输入
  for (int i = 0; i < INST_WAY; i++) {
    if (in.inst[i].type == LTYPE) {
      ldq.in.alloc[i].pc = in.inst[i].pc;
      ldq.in.alloc[i].dest_preg_idx = rename.out.dest_preg_idx[i];
      ldq.in.alloc[i].rob_bit =
          (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit : rob.out.enq_bit;
      ldq.in.alloc[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;
      /*ldq.in.alloc[i].size = rename.out.dest_preg_idx[i];*/
    } else {
      ldq.in.alloc[i].valid = false;
    }
  }

  // iq入队输入
  for (int i = 0; i < INST_WAY; i++) {
    /*iq.in.src1_ready[i] =*/
    /*    !rename.in.src1_areg_en[i] ||*/
    /*    !(rename.out.src1_raw[i] ||
     * rob.check_raw(rename.out.src1_preg_idx[i]));*/
    /*iq.in.src2_ready[i] =*/
    /*    !rename.in.src2_areg_en[i] ||*/
    /*    !(rename.out.src2_raw[i] ||
     * rob.check_raw(rename.out.src2_preg_idx[i]));*/
    iq.in.src1_ready[i] = !rename.in.src1_areg_en[i];
    iq.in.src2_ready[i] = !rename.in.src2_areg_en[i];

    iq.in.pos_bit[i] =
        (rob.out.enq_idx + i >= ROB_NUM) ? !rob.out.enq_bit : rob.out.enq_bit;

    iq.in.pos_idx[i] = (rob.out.enq_idx + i) % ROB_NUM;

    iq.in.inst[i].pc = in.inst[i].pc;
    iq.in.inst[i].type = rename.out.full ? INVALID : in.inst[i].type;
    iq.in.inst[i].op = rename.out.full ? NONE : in.inst[i].op;
    iq.in.inst[i].imm = in.inst[i].imm;
    iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    iq.in.inst[i].src1_en = rename.in.src1_areg_en[i];
    iq.in.inst[i].src2_idx = rename.out.src2_preg_idx[i];
    iq.in.inst[i].src2_en = rename.in.src2_areg_en[i];
    iq.in.inst[i].dest_idx = rename.out.dest_preg_idx[i];
    iq.in.inst[i].dest_en = rename.in.dest_areg_en[i];
  }

  // pipeline2:

  Inst_info mem_inst;
  int mem_rob_idx[1];
  int mem_rob_bit[1];

  // execute 执行

  // select 仲裁 发射指令 ALU_NUM + AGU_NUM
  iq.comb();

  // 读寄存器
  int preg_idx[PRF_RD_NUM];
  int preg_rdata[PRF_RD_NUM];
  for (int i = 0; i < ALU_NUM; i++) {
    preg_idx[2 * i] = iq.out.int_entry[i].inst.src1_idx;
    preg_idx[2 * i + 1] = iq.out.int_entry[i].inst.src2_idx;
  }

  for (int i = 0; i < AGU_NUM; i++) {
    preg_idx[2 * (i + ALU_NUM)] = iq.out.mem_entry[i].inst.src1_idx;
    preg_idx[2 * (i + ALU_NUM) + 1] = iq.out.mem_entry[i].inst.src2_idx;
  }

  prf.read(preg_idx, preg_rdata);

  // 往所有ALU BRU发射指令
  for (int i = 0; i < ALU_NUM; i++) {
    Inst_info inst = iq.out.int_entry[i].inst;
    if (inst.type != INVALID) {
      // 操作数选择
      operand_mux(inst, preg_rdata[2 * i], preg_rdata[2 * i + 1],
                  alu[i].in.src1, alu[i].in.src2);
      alu[i].in.op = inst.op;
      alu[i].cycle();

      bru[i].in.src1 = preg_rdata[2 * 1];
      bru[i].in.src2 = preg_rdata[2 * 1 + 1];
      bru[i].in.pc = inst.pc;
      bru[i].in.off = inst.imm;
      bru[i].in.op = inst.op;
      bru[i].cycle();

      rob.in.complete[i] = true;
      rob.in.idx[i] = iq.out.int_entry[i].pos_idx;

      prf.wen[i] = iq.out.int_entry[i].inst.dest_en;
      prf.wr_idx[i] = iq.out.int_entry[i].inst.dest_idx;
      prf.wdata[i] = alu[i].out.res;

    } else
      rob.in.complete[i] = false;
  }

  // 访存指令计算地址
  for (int i = 0; i < AGU_NUM; i++) {
    Inst_info inst = iq.out.mem_entry[i].inst;
    if (inst.type != INVALID) {
      // 操作数选择
      agu[i].in.base = preg_rdata[2 * (i + ALU_NUM)];
      agu[i].in.off = inst.imm;
      agu[i].cycle();

      bru[i + ALU_NUM].in.src1 = preg_rdata[2 * (i + ALU_NUM)];
      bru[i + ALU_NUM].in.src2 = preg_rdata[2 * (i + ALU_NUM) + 1];
      bru[i + ALU_NUM].in.pc = inst.pc;
      bru[i + ALU_NUM].in.off = inst.imm;
      bru[i + ALU_NUM].in.op = inst.op;
      bru[i + ALU_NUM].cycle();

      rob.in.complete[i + ALU_NUM] = true;
      rob.in.idx[i + ALU_NUM] = iq.out.mem_entry[i].pos_idx;
    } else {
      rob.in.complete[i + ALU_NUM] = false;
    }
  }

  // 发出访存请求 地址写入LDQ
  if (iq.out.mem_entry[0].inst.type == LTYPE) {
    cvt_number_to_bit(output_data + POS_OUT_LOAD_ADDR, agu[0].out.addr, 32);

    ldq.in.write.valid = true;
    ldq.in.write.rob_idx = iq.out.mem_entry[0].pos_idx;
    ldq.in.write.addr = agu[0].out.addr;

    prf.wen[ALU_NUM] = iq.out.mem_entry[0].inst.dest_en;
    prf.wr_idx[ALU_NUM] = iq.out.mem_entry[0].inst.dest_idx;

    /*prf.wdata[ALU_NUM] = read_data();*/
  }

  rob.comb();
  ldq.in.commit_num = rob.out.ld_commit_num;

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (rob.out.commit_entry[i].op != NONE && rob.out.commit_entry[i].dest_en) {
      rename.free_reg(rob.out.commit_entry[i].old_dest_preg_idx);
    }
    rename.in.commit_dest_en[i] = rob.out.commit_entry[i].dest_en;
    rename.in.commit_dest_preg_idx[i] = rob.out.commit_entry[i].dest_preg_idx;
    rename.in.commit_dest_areg_idx[i] = rob.out.commit_entry[i].dest_areg_idx;
    rename.in.commit_old_dest_areg_idx[i] =
        rob.out.commit_entry[i].old_dest_preg_idx;
  }

  // ROB
}

void Back_Top::Back_seq(bool *input_data, bool *output_data) {

  // 时序逻辑
  // pipeline1: 写入ROB和IQ 重命名表更新
  // pipeline2: 执行结果写回 唤醒等待的指令(目前不考虑) 在ROB中标记执行完毕
  // pipeline3: ROB提交 更新free_list 重命名映射表
  rename.seq();
  rob.seq(); // dispatch写入rob  提交指令  store结果  标记complete
  ldq.seq(); // dispatch写入rob  提交指令  store结果  标记complete
  iq.seq();  // dispatch写入发射队列  发射后删除：w
  prf.write();

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
