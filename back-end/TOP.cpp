#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdio>
#include <cvt.h>
#include <diff.h>

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
  // pipeline1:

  // 分配分支tag
  for (int i = 0; i < INST_WAY; i++) {
    br_tag.in.valid[i] =
        in.valid[i] &&
        (in.inst[i].op == BR || in.inst[i].op == JALR || in.inst[i].op == JAL);
    in.inst[i].tag = br_tag.out.tag[i];
  }

  // 重命名
  for (int i = 0; i < INST_WAY; i++) {
    rename.in.valid[i] = in.valid[i];
    rename.in.inst[i] = in.inst[i];
  }
  rename.comb_0();

  // rob入队输入
  for (int i = 0; i < INST_WAY; i++) {
    rob.in.from_ren_valid[i] = in.valid[i];
    rob.in.inst[i] = rename.out.inst[i];
  }

  // ld queue分配
  for (int i = 0; i < INST_WAY; i++) {
    if (in.inst[i].op == LOAD) {
      ldq.in.alloc[i].dest_preg_idx = rename.out.inst[i].dest_preg;
      ldq.in.alloc[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;
      ldq.in.alloc[i].valid = true;
    } else {
      ldq.in.alloc[i].valid = false;
    }
  }

  // st queue分配
  for (int i = 0; i < INST_WAY; i++) {
    if (in.inst[i].op == STORE) {
      stq.in.alloc[i].valid = false;
    } else {
      stq.in.alloc[i].valid = false;
    }
  }

  // pipeline2:
  // execute 执行
  // iq入队输入
  // 仲裁 发射指令

  for (int i = 0; i < INST_WAY; i++) {
    int_iq.in.valid[i] =
        in.inst[i].op != LOAD && in.inst[i].op != STORE && in.valid[i];
    int_iq.in.inst[i] = rename.out.inst[i];
    int_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;

    st_iq.in.valid[i] = in.inst[i].op == STORE && in.valid[i];
    st_iq.in.inst[i] = rename.out.inst[i];
    st_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;

    ld_iq.in.valid[i] = in.inst[i].op == LOAD && in.valid[i];
    ld_iq.in.inst[i] = rename.out.inst[i];
    ld_iq.in.inst[i].rob_idx = (rob.out.enq_idx + i) % ROB_NUM;
  }

  int_iq.comb();
  ld_iq.comb();
  st_iq.comb();

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

      rob.in.from_ex_valid[i] = true;
      rob.in.rob_idx[i] = int_iq.out.inst[i].rob_idx;

      prf.to_sram.we[i] = int_iq.out.inst[i].dest_en;
      prf.to_sram.waddr[i] = int_iq.out.inst[i].dest_preg;
      prf.to_sram.wdata[i] = alu[i].out.res;

    } else {
      rob.in.from_ex_valid[i] = false;
      alu[i].cycle();
      bru[i].cycle();
    }

    /*for (int i = 0; i < BRU_NUM; i++) {*/
    /*  if (int_iq.out.valid[i] && bru[i].out.br_taken) {*/
    /*    rob.in.br_taken = true;*/
    /*    rob.in.br_tag = int_iq.out.inst[i].tag;*/
    /**/
    /*    int_iq.in.br_taken = true;*/
    /*    int_iq.in.br_tag = int_iq.out.inst[i].tag;*/
    /*    ld_iq.in.br_taken = true;*/
    /*    ld_iq.in.br_tag = int_iq.out.inst[i].tag;*/
    /*    st_iq.in.br_taken = true;*/
    /*    st_iq.in.br_tag = int_iq.out.inst[i].tag;*/
    /*    break;*/
    /*  }*/
    /*}*/

    /*if (*(output_data + POS_OUT_BRANCH) == false) {*/
    /*  *(output_data + POS_OUT_BRANCH) = rob.in.br_taken;*/
    /*  cvt_number_to_bit_unsigned(output_data + POS_OUT_PC,
     * bru[i].out.pc_next,*/
    /*                             32);*/
    /*}*/
  }

  /*rename.in.br_taken = *(output_data + POS_OUT_BRANCH);*/

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
    ldq.in.write.rob_idx = ld_iq.out.inst[0].rob_idx;
    ldq.in.write.addr = agu[0].out.addr;
    ldq.in.write.size = inst.func3;

    prf.to_sram.we[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_en;
    prf.to_sram.waddr[PRF_WR_LD_PORT] = ld_iq.out.inst[0].dest_preg;
    /*prf.to_sram.wdata[PRF_WR_LD_PORT] = load_data();*/

    for (int i = 0; i < ISSUE_WAY; i++) {
      ldq.in.commit[i] = (rob.out.commit_entry[i].op == LOAD);
    }
    rob.in.from_ex_valid[ALU_NUM] = true;
    /*rob.in.idx[ALU_NUM] = ld_iq.out.pos_idx[0];*/
  }

  inst = st_iq.out.inst[0];

  // store指令计算地址 写入store queue
  // 操作数选择
  agu[1].in.base = prf.from_sram.rdata[2 * ALU_NUM + 1];
  agu[1].in.off = inst.imm;
  agu[1].cycle();

  if (st_iq.out.valid[0]) {
    stq.in.write.valid = true;
    stq.in.write.addr = agu[1].out.addr;
    stq.in.write.size = inst.func3;
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
    rename.in.commit_inst[i] = rob.out.commit_entry[i];
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
}
