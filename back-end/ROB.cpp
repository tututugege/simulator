#include "IO.h"
#include "frontend.h"
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <cmath>
#include <config.h>
#include <cstdlib>
#include <iostream>
#include <util.h>

void ROB::comb_ready() {
  bool exception_stall = false;
  bool csr_stall =
      (entry[deq_ptr].uop.op == CSR || entry[deq_ptr].uop.op == SFENCE_VMA) &&
      entry[deq_ptr].valid;
  int num = count;

  for (int i = 0; i < ROB_NUM; i++) {
    exception_stall = exception_stall || (exception[i] && entry[i].valid);
    if (exception_stall)
      break;
  }

  io.rob2ren->empty = (count == 0);
  io.rob2ren->stall = csr_stall || exception_stall;

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (csr_stall || exception_stall || io.rob_bcast->flush) {
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
}

void ROB::comb_commit() {

  static int stall_cycle = 0; // 检查是否卡死

  // 提交指令
  int commit_num = 0;
  io.rob_bcast->flush = io.rob_bcast->exception = io.rob_bcast->mret =
      io.rob_bcast->sret = io.rob_bcast->ecall = false;

  io.rob_bcast->page_fault_inst = io.rob_bcast->page_fault_load =
      io.rob_bcast->page_fault_store = io.rob_bcast->illegal_inst = false;

  stall_cycle++;
  int i = 0;
  while (i < COMMIT_WIDTH) {
    int idx = (deq_ptr + i) % ROB_NUM;
    int uop_num = entry[idx].uop.uop_num;
    if (i + uop_num >= COMMIT_WIDTH) {
      break;
    }

    // 判断下一个inst的所有uop是否执行完毕
    int temp_idx = idx;
    int temp_i = i;
    bool all_complete = true;
    for (int j = 0; j < uop_num; j++) {
      if (temp_i == 0) {
        io.rob_commit->commit_entry[temp_i].valid =
            entry[temp_idx].valid && complete[temp_idx];
      } else {
        io.rob_commit->commit_entry[temp_i].valid =
            entry[temp_idx].valid && complete[temp_idx] &&
            !exception[temp_idx] &&
            io.rob_commit->commit_entry[temp_i - 1].valid;
      }

      if (!io.rob_commit->commit_entry[temp_i].valid) {
        all_complete = false;
        break;
      }
      LOOP_INC(temp_idx, ROB_NUM);
      temp_i++;
    }

    if (all_complete && uop_num >= 1) {
      stall_cycle = 0;
      for (int j = 0; j < uop_num; j++) {
        io.rob_commit->commit_entry[i].uop = entry[idx].uop;
        complete_1[idx] = false;
        entry_1[idx].valid = false;

        if (exception[idx] || entry[idx].uop.op == CSR ||
            entry[idx].uop.op == SFENCE_VMA) {
          io.rob_bcast->flush = true;
          io.rob_bcast->exception = exception[idx];
          exception_1[idx] = false;

          if (entry[idx].uop.op == ECALL) {
            io.rob_bcast->ecall = true;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[idx].uop.op == MRET) {
            io.rob_bcast->mret = true;
          } else if (entry[idx].uop.op == SRET) {
            io.rob_bcast->sret = true;
          } else if (entry[idx].uop.page_fault_store) {
            io.rob_bcast->page_fault_store = true;
            io.rob_bcast->trap_val = entry[idx].uop.result;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[idx].uop.page_fault_load) {
            io.rob_bcast->page_fault_load = true;
            io.rob_bcast->trap_val = entry[idx].uop.result;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[idx].uop.page_fault_inst) {
            io.rob_bcast->page_fault_inst = true;
            io.rob_bcast->trap_val = entry[idx].uop.pc;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[idx].uop.illegal_inst) {
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
            io.rob_bcast->illegal_inst = true;
            io.rob_bcast->trap_val = entry[idx].uop.instruction;
          } else if (entry[idx].uop.op == EBREAK) {
            extern bool sim_end;
            sim_end = true;
          } else {
            if (entry[idx].uop.op != CSR && entry[idx].uop.op != SFENCE_VMA) {
              cout << hex << entry[idx].uop.instruction << endl;
              exit(1);
            }

            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc + 4;
          }
        }
        i++;
        LOOP_INC(idx, ROB_NUM);
        commit_num++;
      }
    } else {
      break;
    }

    if (io.rob_bcast->flush)
      break;
  }
  deq_ptr_1 = (deq_ptr + commit_num) % ROB_NUM;
  count_1 -= commit_num;

  for (int i = commit_num; i < COMMIT_WIDTH; i++) {
    io.rob_commit->commit_entry[i].valid = false;
  }

  io.rob2ren->enq_idx = enq_ptr;

  if (stall_cycle > 100) {
    cout << dec << sim_time << endl;
    cout << "卡死了" << endl;
    cout << hex << entry[deq_ptr].uop.instruction;
    exit(1);
  }
}

void ROB::comb_complete() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2rob->entry[i].valid) {
      complete_1[io.prf2rob->entry[i].uop.rob_idx] = true;
      entry_1[io.prf2rob->entry[i].uop.rob_idx].uop.difftest_skip =
          io.prf2rob->entry[i].uop.difftest_skip;
      if (i == IQ_BR0 || i == IQ_BR1) {
        entry_1[io.prf2rob->entry[i].uop.rob_idx].uop.pc_next =
            io.prf2rob->entry[i].uop.pc_next;
        entry_1[io.prf2rob->entry[i].uop.rob_idx].uop.mispred =
            io.prf2rob->entry[i].uop.mispred;
        entry_1[io.prf2rob->entry[i].uop.rob_idx].uop.br_taken =
            io.prf2rob->entry[i].uop.br_taken;
      }

      if (i == IQ_LD || i == IQ_STA) {
        if (is_page_fault(io.prf2rob->entry[i].uop)) {
          int idx, init_idx;
          idx = init_idx = io.prf2rob->entry[i].uop.rob_idx;
          while (!entry[idx].uop.is_last_uop) {
            LOOP_INC(idx, ROB_NUM);
          }

          entry_1[idx].uop.is_last_uop = false;
          if (idx != init_idx)
            for (int j = 0; j < entry[idx].uop.uop_num - 1; j++) {
              LOOP_DEC(idx, ROB_NUM);
            }

          exception_1[idx] = true;
          entry_1[idx].uop.is_last_uop = true;
          entry_1[idx].uop.result = io.prf2rob->entry[i].uop.result;
          entry_1[idx].uop.page_fault_load =
              io.prf2rob->entry[i].uop.page_fault_load;
          entry_1[idx].uop.page_fault_store =
              io.prf2rob->entry[i].uop.page_fault_store;
        }
      }
    }
  }
}

void ROB::comb_branch() {
  // 分支预测失败
  if (io.dec_bcast->mispred && !io.rob_bcast->flush) {
    int idx = (enq_ptr - 1 + ROB_NUM) % ROB_NUM;
    while (entry[idx].valid &&
           ((1 << entry[idx].uop.tag) & io.dec_bcast->br_mask)) {
      entry_1[idx].valid = false;
      complete_1[idx] = 0;
      count_1--;
      LOOP_DEC(idx, ROB_NUM);
      LOOP_DEC(enq_ptr_1, ROB_NUM);
    }
  }
}

void ROB::comb_fire() {
  // 入队
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2rob->dis_fire[i]) {
      entry_1[enq_ptr_1].valid = true;
      entry_1[enq_ptr_1].uop = io.ren2rob->uop[i];
      complete_1[enq_ptr_1] = false;
      if (io.ren2rob->uop[i].op == ECALL || io.ren2rob->uop[i].op == MRET ||
          io.ren2rob->uop[i].op == EBREAK || io.ren2rob->uop[i].op == SRET ||
          is_page_fault(io.ren2rob->uop[i]) || io.ren2rob->uop[i].illegal_inst)
        exception_1[enq_ptr_1] = true;
      else
        exception_1[enq_ptr_1] = false;
      LOOP_INC(enq_ptr_1, ROB_NUM);
      count_1++;
    }
  }
}

void ROB::comb_flush() {
  if (io.rob_bcast->flush) {
    for (int i = 0; i < ROB_NUM; i++) {
      complete_1[i] = false;
      entry_1[i].valid = false;
    }
    enq_ptr_1 = 0;
    deq_ptr_1 = 0;
    count_1 = 0;
  }
}

void ROB::seq() {
  for (int i = 0; i < ROB_NUM; i++) {
    entry[i] = entry_1[i];
    complete[i] = complete_1[i];
    exception[i] = exception_1[i];
  }

  deq_ptr = deq_ptr_1;
  enq_ptr = enq_ptr_1;
  count = count_1;
}

void ROB::init() {
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}
