#include <DAG.h>
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <iostream>
#include <util.h>

// 提交指令
void ROB::comb_commit() {
  int complete_num;
  out.rollback = false;
  for (int i = 0; i < ISSUE_WAY; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    entry.to_sram.raddr[i] = idx;
  }
  entry.read();

  out.rollback = out.exception = out.mret = false;
  for (complete_num = 0; complete_num < ISSUE_WAY; complete_num++) {
    int idx = (deq_ptr + complete_num) % ROB_NUM;
    out.commit_entry[complete_num] = entry.from_sram.rdata[complete_num];
    out.commit_entry[complete_num].pc_next = pc_next[idx];
    out.valid[complete_num] = valid[idx] && complete[idx];

    if (out.valid[complete_num]) {
      complete_1[idx] = false;
      valid_1[idx] = false;
      exception_1[idx] = false;
      if (exception[idx]) {
        out.rollback = true;
        if (out.commit_entry[complete_num].op == ECALL)
          out.exception = true;
        else if (out.commit_entry[complete_num].op == MRET)
          out.mret = true;
        complete_num++;
        break;
      }
    } else
      break;
  }

  for (int i = complete_num; i < ISSUE_WAY; i++) {
    out.valid[i] = false;
  }

  if (out.rollback) {
    enq_ptr_1 = 0;
    deq_ptr_1 = 0;
    count_1 = 0;
  } else {
    deq_ptr_1 = (deq_ptr + complete_num) % ROB_NUM;
    count_1 = count - complete_num;
  }

  out.empty = (count == 0);
}

// 生成ready
void ROB::comb_complete() {
  // dispatch进入rob
  bool csr_stall = (entry.from_sram.rdata[0].op == CSR) && valid[deq_ptr];

  bool exception_stall = false;
  for (int i = 0; i < ROB_NUM; i++) {
    exception_stall = exception_stall || (exception[i] && valid[i]);
    if (exception_stall)
      break;
  }

  int num = count;
  for (int i = 0; i < INST_WAY; i++) {
    if (csr_stall || exception_stall) {
      out.to_ren_ready[i] = false;
    } else {
      if (!in.from_ren_valid[i]) {
        out.to_ren_ready[i] = true;
      } else if (num < ROB_NUM) {
        out.to_ren_ready[i] = true;
        num++;
      } else {
        out.to_ren_ready[i] = false;
      }
    }
  }

  //  执行完毕的标记
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.from_ex_valid[i]) {
      complete_1[in.from_ex_inst[i].rob_idx] = true;
      if (in.from_ex_inst[i].op != STORE)
        dag_del_node(in.from_ex_inst[i].rob_idx);
      diff_1[in.from_ex_inst[i].rob_idx] = in.from_ex_diff[i];
    }
  }

  if (in.mispred) {
    int idx = (enq_ptr - 1 + ROB_NUM) % ROB_NUM;
    enq_ptr_1 = (in.br_rob_idx + 1) % ROB_NUM;
    while (idx != in.br_rob_idx) {
      valid_1[idx] = false;
      idx = (idx - 1 + ROB_NUM) % ROB_NUM;
      count_1--;
    }
  }

  out.enq_idx = enq_ptr_1;

#ifdef CONFIG_DIFFTEST
  for (int i = 0; i < ALU_NUM + AGU_NUM; i++) {
    if (in.from_ex_valid[i]) {
      pc_next_1[in.from_ex_inst[i].rob_idx] = in.from_ex_inst[i].pc_next;
    }
  }
#endif // DEBUG
}

void ROB::comb_enq() {
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i]) {
      Inst_info enq_entry = in.from_ren_inst[i];
      entry.to_sram.waddr[i] = enq_ptr_1;
      entry.to_sram.we[i] = true;
      entry.to_sram.wdata[i] = enq_entry;
      valid_1[enq_ptr_1] = true;
      complete_1[enq_ptr_1] = false;
      tag_1[enq_ptr_1] = in.from_ex_inst[i].tag;
      if (in.from_ren_inst[i].op == ECALL || in.from_ren_inst[i].op == MRET)
        exception_1[enq_ptr_1] = true;
      LOOP_INC(enq_ptr_1, ROB_NUM);
      count_1++;

    } else {
      entry.to_sram.we[i] = false;
    }
  }
}

extern bool difftest_skip;
void ROB::seq() {

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (out.valid[i]) {
      if (out.commit_entry[i].op == STORE)
        dag_del_node(out.commit_entry[i].rob_idx);
#ifdef CONFIG_DIFFTEST
      difftest_skip = !diff[out.commit_entry[i].rob_idx];
      back.difftest(out.commit_entry[i]);
#endif
      if (LOG) {
        cout << "ROB commit PC 0x" << hex << out.commit_entry[i].pc << endl;
      }
    }
  }

  entry.write();
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i])
      dag_add_node(&entry.data[entry.to_sram.waddr[i]]);
  }

  for (int i = 0; i < ROB_NUM; i++) {
    complete[i] = complete_1[i];
    valid[i] = valid_1[i];
    tag[i] = tag_1[i];
    exception[i] = exception_1[i];

#ifdef CONFIG_DIFFTEST
    pc_next[i] = pc_next_1[i];
    diff[i] = diff_1[i];
#endif
  }

  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  count = count_1;
}

void ROB::init() {

  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}
