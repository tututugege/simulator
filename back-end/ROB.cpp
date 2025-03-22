#include "IO.h"
#include "frontend.h"
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <config.h>
#include <diff.h>
#include <util.h>

extern int commit_num;
extern int branch_num;
extern int mispred_num;

int dir_ok_addr_error;
int taken_num;

void ROB::comb_ready() {

  int num = count;

  for (int i = 0; i < FETCH_WIDTH; i++) {
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

void ROB::comb_commit() {
  // 提交指令
  int commit_num = 0;
  io.rob_bc->rollback = io.rob_bc->exception = io.rob_bc->mret = false;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    int idx = (deq_ptr + i) % ROB_NUM;
    io.rob_commit->commit_entry[i].inst = entry[idx].inst;
    if (i == 0) {
      io.rob_commit->commit_entry[i].valid =
          entry[idx].valid && complete[idx] && !io.dec_bcast->mispred;
    } else {
      io.rob_commit->commit_entry[i].valid =
          entry[idx].valid && complete[idx] &&
          io.rob_commit->commit_entry[i - 1].valid;
    }

    if (io.rob_commit->commit_entry[i].valid) {
      commit_num++;
      complete_1[idx] = false;
      entry_1[idx].valid = false;
    } else {
      break;
    }
  }

  deq_ptr_1 = (deq_ptr + commit_num) % ROB_NUM;
  count_1 -= commit_num;

  for (int i = commit_num; i < COMMIT_WIDTH; i++) {
    io.rob_commit->commit_entry[i].valid = false;
  }
  io.rob2ren->enq_idx = enq_ptr;
}

void ROB::comb_complete() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2rob->entry[i].valid) {
      complete_1[io.prf2rob->entry[i].inst.rob_idx] = true;
      entry_1[io.prf2rob->entry[i].inst.rob_idx].inst =
          io.prf2rob->entry[i].inst;
    }
  }
}

void ROB::comb_branch() {
  // 分支预测失败
  if (io.dec_bcast->mispred) {
    int idx = (enq_ptr - 1 + ROB_NUM) % ROB_NUM;
    while (entry[idx].valid &&
           ((1 << entry[idx].inst.tag) & io.dec_bcast->br_mask)) {
      entry_1[idx].valid = false;
      LOOP_DEC(idx, ROB_NUM);
      count_1--;
      LOOP_DEC(enq_ptr_1, ROB_NUM);
    }
  }
}

void ROB::comb_fire() {
  // 入队
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2rob->dis_fire[i]) {
      entry_1[enq_ptr_1].valid = true;
      entry_1[enq_ptr_1].inst = io.ren2rob->inst[i];
      complete_1[enq_ptr_1] = false;
      LOOP_INC(enq_ptr_1, ROB_NUM);
      count_1++;
    }
  }
}

extern bool difftest_skip;
void ROB::seq() {
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

  for (int i = 0; i < ROB_NUM; i++) {
    entry[i] = entry_1[i];
    complete[i] = complete_1[i];
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
