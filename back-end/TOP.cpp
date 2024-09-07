#include "config.h"
#include <RISCV.h>
#include <TOP.h>
#include <cvt.h>
#include <diff.h>

Inst_res execute(bool *input_data, Inst_info inst, uint32_t pc);

uint32_t load_data(Inst_op op, bool *offset);
void store_data(Inst_op op);

void Back_Top::init(bool *output_data) {
  rename.init();
  iq.init();
  rob.init();
  back.rename.preg_base = output_data;
}

void Back_Top::Back_cycle(bool *input_data, bool *output_data) {
  // 组合逻辑
  // pipeline1: 重命名 dispatch

  for (int i = 0; i < WAY; i++) {
    rename.in.src1_areg_idx[i] = in.inst[i].src1_idx;
    rename.in.src1_areg_en[i] = in.inst[i].src1_en;
    rename.in.src2_areg_idx[i] = in.inst[i].src2_idx;
    rename.in.src2_areg_en[i] = in.inst[i].src2_en;
    rename.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rename.in.dest_areg_en[i] = in.inst[i].dest_en;
  }
  rename.cycle();

  // pipeline2: 从IQ中选择指令执行

  // select 仲裁
  Inst_info inst[WAY];
  int rob_idx[WAY];
  for (int i = 0; i < WAY; i++) {
    inst[i] = iq.IQ_sel_inst(rob_idx + i);
  }

  // execute 执行
  Inst_res res[WAY];
  for (int i = 0; i < WAY; i++) {
    if (inst[i].type != NOP) {
      res[i] = execute(input_data, inst[i], rob.get_pc(rob_idx[i]));

      if (res[i].branch && !*(output_data + POS_OUT_STALL)) {
        // 分支预测失败 停止取指
        bool bit_temp[32];
        *(output_data + POS_OUT_STALL) = true;
        cvt_number_to_bit_unsigned(bit_temp, res[i].pc_next, 32);
        copy_indice(output_data, POS_OUT_PC, bit_temp, 0, 32);
      }

      // load
      if (inst[i].op == LW || inst[i].op == LH || inst[i].op == LB ||
          inst[i].op == LBU || inst[i].op == LHU) {
        bool bit_temp[32];
        cvt_number_to_bit_unsigned(bit_temp, res[i].result, 32);
        copy_indice(output_data, POS_OUT_LOAD_ADDR, bit_temp, 0, 32);
        res[i].result = load_data(inst[i].op, bit_temp + 30);
      }
    }
  }
  // pipeline3: ROB提交指令

  // 时序逻辑
  // pipeline1: 写入ROB和IQ
  for (int i = 0; i < WAY; i++) {
    rob.in.PC[i] = in.PC[i];
    rob.in.op[i] = in.inst[i].op;
    rob.in.dest_areg_idx[i] = in.inst[i].dest_idx;
    rob.in.dest_preg_idx[i] = rename.out.dest_preg_idx[i];
    rob.in.dest_en[i] = in.inst[i].dest_en;
    rob.in.old_dest_preg_idx[i] = rename.out.old_dest_preg_idx[i];
  }
  rob.ROB_enq(iq.in.pos_bit, iq.in.pos_idx);

  for (int i = 0; i < WAY; i++) {
    iq.in.inst[i].type = in.inst[i].type;
    iq.in.inst[i].op = in.inst[i].op;
    iq.in.inst[i].imm = in.inst[i].imm;
    iq.in.inst[i].src1_idx = rename.out.src1_preg_idx[i];
    iq.in.inst[i].src1_en = rename.in.src1_areg_en[i];
    iq.in.inst[i].src2_idx = rename.out.src2_preg_idx[i];
    iq.in.inst[i].src2_en = rename.in.src2_areg_en[i];
    iq.in.inst[i].dest_idx = rename.out.dest_preg_idx[i];
    iq.in.inst[i].dest_en = rename.in.dest_areg_en[i];
  }
  iq.IQ_add_inst();

  // pipeline2: 执行结果写回 唤醒等待的指令(目前不考虑) 在ROB中标记执行完毕

  for (int i = 0; i < WAY; i++) {
    if (inst[i].type != NOP) {
      bool bit_temp[32];
      if (inst[i].dest_en) {
        cvt_number_to_bit_unsigned(bit_temp, res[i].result, 32);
        copy_indice(output_data, 32 * inst[i].dest_idx, bit_temp, 0, 32);
      }
    }
  }

  for (int i = 0; i < WAY; i++) {
    if (inst[i].dest_en) {
      // TODO
      /*iq.IQ_awake(inst[i].dest_idx);*/
    }
  }

  // pipeline3: ROB提交 更新free_list 重命名映射表
  ROB_entry commit_entry;
  while ((commit_entry = rob.commit()).complete) {
    if (commit_entry.dest_en) {
      rename.free_reg(commit_entry.old_dest_preg_idx);
      rename.arch_RAT[commit_entry.dest_areg_idx] = commit_entry.dest_preg_idx;
    }

    if (commit_entry.op == SW || commit_entry.op == SH ||
        commit_entry.op == SB) {

      bool bit_temp[32];
      cvt_number_to_bit_unsigned(bit_temp, commit_entry.store_addr, 32);
      copy_indice(output_data, POS_OUT_STORE_ADDR, bit_temp, 0, 32);
      cvt_number_to_bit_unsigned(bit_temp, commit_entry.store_data, 32);
      copy_indice(output_data, POS_OUT_STORE_DATA, bit_temp, 0, 32);
      store_data(commit_entry.op);
    }

    if (log) {
      cout << "ROB commit PC 0x" << hex << commit_entry.PC << endl;
      rename.print_reg();
      /*rename.print_RAT();*/
    }

#ifdef CONFIG_DIFFTEST
    for (int i = 0; i < ARF_NUM; i++) {
      dut.gpr[i] = rename.reg(i);
    }

    if (commit_entry.branch) {

      dut.pc = cvt_bit_to_number_unsigned(output_data, POS_OUT_PC);
    } else {
      dut.pc = commit_entry.PC + 4;
    }

    difftest_step();
#endif

    // 如果分支预测失败的指令提交
    // 则清空流水线
    if (commit_entry.branch) {
      rename.recover();
      iq.init();
      rob.init();
      *(output_data + POS_OUT_STALL) = false;

      for (int i = 0; i < WAY; i++)
        inst[i].type = NOP;
    }
  }

  for (int i = 0; i < WAY; i++) {
    if (inst[i].type != NOP) {

      rob.complete(rob_idx[i]);

      if (res[i].branch) {
        rob.branch(rob_idx[i]);
      }

      // store
      if (inst[i].op == SB || inst[i].op == SH || inst[i].op == SW) {
        uint32_t data =
            cvt_bit_to_number_unsigned(input_data + 32 * inst[i].src2_idx, 32);
        rob.store(rob_idx[i], res[i].result, data);
      }
    }
  }
}
