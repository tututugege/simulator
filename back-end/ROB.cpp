#include "IO.h"
#include "frontend.h"
#include <DAG.h>
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <iostream>
#include <util.h>

extern int commit_num;
extern int branch_num;
extern int mispred_num;

int dir_ok_addr_error;
int taken_num;

// 提交指令
void ROB::comb() {

  bool csr_stall = (entry[deq_ptr].inst.op == CSR) && entry[deq_ptr].valid;
  bool exception_stall = false;
  for (int i = 0; i < ROB_NUM; i++) {
    exception_stall = exception_stall || (exception[i] && entry[i].valid);
    if (exception_stall)
      break;
  }

  int num = count;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (csr_stall || exception_stall) {
      io.rob2ren->ready[i] = false;
    } else {
      if (!io.ren2rob->valid[i]) {
        io.rob2ren->ready[i] = true;
      } else if (num < ROB_NUM) {
        io.rob2ren->ready[i] = true;
        num++;
      } else {
        io.rob2ren->ready[i] = false;
      }
    }
  }

  int commit_num = 0;
  io.rob_bc->rollback = io.rob_bc->exception = io.rob_bc->mret = false;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    io.rob_commit->commit_entry[i].inst = entry[idx].inst;
    if (i == 0) {
      io.rob_commit->commit_entry[i].valid = entry[idx].valid && complete[idx];
    } else {
      io.rob_commit->commit_entry[i].valid =
          entry[idx].valid && complete[idx] &&
          io.rob_commit->commit_entry[i - 1].valid;
    }

    if (io.rob_commit->commit_entry[i].valid) {
      commit_num++;
      complete[idx] = false;
      entry[idx].valid = false;

      if (exception[idx]) {
        io.rob_bc->rollback = true;
        exception[idx] = false;
        if (entry[idx].inst.op == ECALL)
          io.rob_bc->exception = true;
        else if (entry[idx].inst.op == MRET)
          io.rob_bc->mret = true;
        break;
      }
    } else {
      break;
    }
  }
  deq_ptr = (deq_ptr + commit_num) % ROB_NUM;
  count -= commit_num;

  for (int i = commit_num; i < COMMIT_WIDTH; i++) {
    io.rob_commit->commit_entry[i].valid = false;
  }
}

extern bool difftest_skip;
void ROB::seq() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2rob->entry[i].valid) {
      complete[io.prf2rob->entry[i].inst.rob_idx] = true;
      entry[io.prf2rob->entry[i].inst.rob_idx].inst = io.prf2rob->entry[i].inst;
      /*if (io.exe2rob->compelte_entry[i].inst.op != STORE)*/
      /*  dag_del_node(io.exe2rob->compelte_entry[i].inst.rob_idx);*/
    }
  }

  // 分支预测失败
  if (io.id_bc->mispred) {
    int idx = (enq_ptr - 1 + ROB_NUM) % ROB_NUM;
    while (entry[idx].valid &&
           ((1 << entry[idx].inst.tag) & io.id_bc->br_mask)) {
      entry[idx].valid = false;
      idx = (idx - 1 + ROB_NUM) % ROB_NUM;
      count--;
      LOOP_DEC(enq_ptr, ROB_NUM);
    }
  }

  if (io.rob_bc->rollback) {
    enq_ptr = 0;
    deq_ptr = 0;
    count = 0;
  }

  // 入队
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2rob->dis_fire[i]) {
      entry[enq_ptr].valid = true;
      entry[enq_ptr].inst = io.ren2rob->inst[i];
      complete[enq_ptr] = false;
      if (io.ren2rob->inst[i].op == ECALL || io.ren2rob->inst[i].op == MRET)
        exception[enq_ptr] = true;
      LOOP_INC(enq_ptr, ROB_NUM);
      count++;
    }
  }

  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.rob_commit->commit_entry[i].valid) {
      if (is_branch(io.rob_commit->commit_entry[i].inst.op)) {
        Inst_info *inst = &io.rob_commit->commit_entry[i].inst;
        if (inst->mispred) {
          mispred_num++;
          if (inst->mispred && (inst->br_taken && inst->pred_br_taken ||
                                !inst->br_taken && !inst->pred_br_taken)) {
            dir_ok_addr_error++;
            assert(inst->pred_br_pc != inst->pc_next);
          }
        }
        branch_num++;
      }

      extern bool sim_end;
      if (io.rob_commit->commit_entry[i].inst.op == EBREAK)
        sim_end = true;
    }
  }

  /*for (int i = 0; i < FETCH_WIDTH; i++) {*/
  /*  if (io.ren2rob->dis_fire[i])*/
  /*  dag_add_node(&entry.data[entry.to_sram.waddr[i]]);*/
  /*}*/

  io.rob2ren->enq_idx = enq_ptr;
}

void ROB::init() {

  count = 0;
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}
