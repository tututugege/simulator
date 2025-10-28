#include "IO.h"
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <cmath>
#include <config.h>
#include <cstdlib>
#include <iostream>
#include <util.h>

void ROB::comb_ready() {
  int num = count;

  io.rob2dis->stall = false;
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    for (int j = 0; j < ROB_LINE_NUM; j++) {
      if (entry[i][j].valid &&
          (is_exception(entry[i][j].uop) || is_flush_inst(entry[i][j].uop))) {
        io.rob2dis->stall = true;
        break;
      }
    }
  }

  io.rob2dis->empty = (count == 0);
  io.rob2dis->ready =
      !(io.rob_bcast->flush) && !(enq_ptr == deq_ptr && count != 0);
}

void ROB::comb_commit() {

  static int stall_cycle = 0; // 检查是否卡死
  io.rob_bcast->flush = io.rob_bcast->exception = io.rob_bcast->mret =
      io.rob_bcast->sret = io.rob_bcast->ecall = false;

  io.rob_bcast->page_fault_inst = io.rob_bcast->page_fault_load =
      io.rob_bcast->page_fault_store = io.rob_bcast->illegal_inst = false;

  // bank的同一行是否都完成？
  bool commit = !(enq_ptr == deq_ptr && count == 0) && !io.dec_bcast->mispred;
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    commit = commit &&
             (!entry[i][deq_ptr].valid ||
              entry[i][deq_ptr].uop.cmp_num == entry[i][deq_ptr].uop.uop_num);
  }

  if (commit) {
    count_1--;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      io.rob_commit->commit_entry[i] = entry[i][deq_ptr];
      if (entry[i][deq_ptr].valid && !io.rob_bcast->flush) {
        entry_1[i][deq_ptr].valid = false;
        if (is_flush_inst(entry[i][deq_ptr].uop)) {
          io.rob_bcast->flush = true;
          io.rob_bcast->exception = is_exception(entry[i][deq_ptr].uop);
          if (entry[i][deq_ptr].uop.type == ECALL) {
            io.rob_bcast->ecall = true;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[i][deq_ptr].uop.type == MRET) {
            io.rob_bcast->mret = true;
          } else if (entry[i][deq_ptr].uop.type == SRET) {
            io.rob_bcast->sret = true;
          } else if (entry[i][deq_ptr].uop.page_fault_store) {
            io.rob_bcast->page_fault_store = true;
            io.rob_bcast->trap_val = entry[i][deq_ptr].uop.result;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[i][deq_ptr].uop.page_fault_load) {
            io.rob_bcast->page_fault_load = true;
            io.rob_bcast->trap_val = entry[i][deq_ptr].uop.result;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[i][deq_ptr].uop.page_fault_inst) {
            io.rob_bcast->page_fault_inst = true;
            io.rob_bcast->trap_val = entry[i][deq_ptr].uop.pc;
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
          } else if (entry[i][deq_ptr].uop.illegal_inst) {
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc;
            io.rob_bcast->illegal_inst = true;
            io.rob_bcast->trap_val = entry[i][deq_ptr].uop.instruction;
          } else if (entry[i][deq_ptr].uop.type == EBREAK) {
            extern bool sim_end;
            sim_end = true;
          } else {
            if (entry[i][deq_ptr].uop.type != CSR &&
                entry[i][deq_ptr].uop.type != SFENCE_VMA) {
              cout << hex << entry[i][deq_ptr].uop.instruction << endl;
              exit(1);
            }
            io.rob_bcast->pc = io.rob_commit->commit_entry[i].uop.pc + 4;
          }
        }
      } else {
        io.rob_commit->commit_entry[i].valid = false;
      }
    }
    LOOP_INC(deq_ptr_1, ROB_LINE_NUM);
    stall_cycle = 0;
  } else {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      io.rob_commit->commit_entry[i].valid = false;
    }
  }

  io.rob2dis->enq_idx = enq_ptr;

  stall_cycle++;
  if (stall_cycle > 50) {
    cout << dec << sim_time << endl;
    cout << "卡死了" << endl;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        cout << hex << entry[i][deq_ptr].uop.instruction << endl;
        break;
      }
    }
    exit(1);
  }
}

void ROB::comb_complete() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2rob->entry[i].valid) {
      int bank_idx = io.prf2rob->entry[i].uop.rob_idx & 0b11;
      int line_idx = io.prf2rob->entry[i].uop.rob_idx >> 2;
      entry_1[bank_idx][line_idx].uop.cmp_num++;

      if (i == IQ_LD) {
        if (is_page_fault(io.prf2rob->entry[i].uop)) {
          entry_1[bank_idx][line_idx].uop.result =
              io.prf2rob->entry[i].uop.result;
          entry_1[bank_idx][line_idx].uop.page_fault_load = true;
        }
      }

      if (i == IQ_STA) {
        if (is_page_fault(io.prf2rob->entry[i].uop)) {
          entry_1[bank_idx][line_idx].uop.result =
              io.prf2rob->entry[i].uop.result;
          entry_1[bank_idx][line_idx].uop.page_fault_store = true;
        }
      }

      // for debug
      entry_1[bank_idx][line_idx].uop.difftest_skip =
          io.prf2rob->entry[i].uop.difftest_skip;
      if (i == IQ_BR0 || i == IQ_BR1) {
        entry_1[bank_idx][line_idx].uop.pc_next =
            io.prf2rob->entry[i].uop.pc_next;
        entry_1[bank_idx][line_idx].uop.mispred =
            io.prf2rob->entry[i].uop.mispred;
        entry_1[bank_idx][line_idx].uop.br_taken =
            io.prf2rob->entry[i].uop.br_taken;
      }
    }
  }
}

void ROB::comb_branch() {
  // 分支预测失败
  if (io.dec_bcast->mispred && !io.rob_bcast->flush) {
    enq_ptr_1 = ((io.dec_bcast->redirect_rob_idx >> 2) + 1) % (ROB_LINE_NUM);
    count_1 = count - (enq_ptr + ROB_LINE_NUM - enq_ptr_1) % ROB_LINE_NUM;
    for (int i = (io.dec_bcast->redirect_rob_idx & 0b11) + 1; i < ROB_BANK_NUM;
         i++) {
      entry_1[i][io.dec_bcast->redirect_rob_idx >> 2].valid = false;
    }
  }
}

void ROB::comb_fire() {
  // 入队
  bool enq = false;
  if (io.rob2dis->ready) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (io.dis2rob->dis_fire[i]) {
        entry_1[i][enq_ptr].valid = true;
        entry_1[i][enq_ptr].uop = io.dis2rob->uop[i];
        entry_1[i][enq_ptr].uop.cmp_num = 0;
        enq = true;
      } else {
        entry_1[i][enq_ptr].valid = false;
      }
    }
  }

  if (enq) {
    LOOP_INC(enq_ptr_1, ROB_LINE_NUM);
    count_1++;
  }
}

void ROB::comb_flush() {
  if (io.rob_bcast->flush) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      for (int j = 0; j < ROB_LINE_NUM; j++) {
        entry_1[i][j].valid = false;
      }
    }

    enq_ptr_1 = 0;
    deq_ptr_1 = 0;
    count_1 = 0;
  }
}

void ROB::seq() {
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    for (int j = 0; j < ROB_LINE_NUM; j++) {
      entry[i][j] = entry_1[i][j];
    }
  }

  deq_ptr = deq_ptr_1;
  enq_ptr = enq_ptr_1;
  count = count_1;
  if (count != ROB_LINE_NUM &&
      count != (enq_ptr + ROB_LINE_NUM - deq_ptr) % ROB_LINE_NUM) {
    cout << "erro" << endl;
  }
}

void ROB::init() {
  deq_ptr = 0;
  enq_ptr = 0;
  count = 0;
}
