#include "IO.h"
#include <RISCV.h>
#include <ROB.h>
#include <TOP.h>
#include <cmath>
#include <config.h>
#include <cstdlib>
#include <iostream>
#include <util.h>

void ROB::init() {
  deq_ptr = deq_ptr_1 = 0;
  enq_ptr = enq_ptr_1 = 0;
  count = count_1 = 0;
  flag = flag_1 = false;

  for (int i = 0; i < ROB_LINE_NUM; i++) {
    for (int j = 0; j < ROB_BANK_NUM; j++) {
      entry[j][i].valid = false;
    }
  }
}

void ROB::comb_ready() {
  out.rob2dis->stall = false;
  out.rob2csr->commit = false;
  out.rob2csr->interrupt_resp = false;

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (entry[i][deq_ptr].valid && is_flush_inst(entry[i][deq_ptr].uop)) {
      out.rob2dis->stall = true;
      break;
    }
  }

  if (count != 0 && in.csr2rob->interrupt_req && !in.dec_bcast->mispred) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        out.rob2csr->interrupt_resp = true;
        break;
      }
    }
  }
  out.rob2dis->empty = (count == 0);
  out.rob2dis->ready = !(enq_ptr == deq_ptr && count != 0);
}

void ROB::comb_commit() {

  static int stall_cycle = 0; // 检查是否卡死
  out.rob_bcast->flush = out.rob_bcast->exception = out.rob_bcast->mret =
      out.rob_bcast->sret = out.rob_bcast->ecall = false;

  out.rob_bcast->interrupt = out.rob2csr->interrupt_resp;

  out.rob_bcast->page_fault_inst = out.rob_bcast->page_fault_load =
      out.rob_bcast->page_fault_store = out.rob_bcast->illegal_inst = false;

  wire1_t commit =
      (!(enq_ptr == deq_ptr && count == 0) && !in.dec_bcast->mispred);

  // bank的同一行是否都完成
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    commit = commit &&
             (!entry[i][deq_ptr].valid || (entry[i][deq_ptr].uop.cplt_num ==
                                           entry[i][deq_ptr].uop.uop_num));
  }

  // 出队一行存在特殊指令则single commit
  wire1_t single_commit = false;
  wire2_t single_idx;

  if (!in.dec_bcast->mispred) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid && is_flush_inst(entry[i][deq_ptr].uop) ||
          out.rob2csr->interrupt_resp) {
        single_commit = true;
        break;
      }
    }

    // 看第一个valid的inst是否完成 或者是interrupt，如果完成则single_commit
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        single_idx = i;
        if (!out.rob_bcast->interrupt &&
            entry[i][deq_ptr].uop.cplt_num != entry[i][deq_ptr].uop.uop_num) {
          single_commit = false;
        }
        break;
      }
    }
  }

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    out.rob_commit->commit_entry[i].uop = entry[i][deq_ptr].uop;
  }

  // 一组提交
  if (commit && !single_commit) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      out.rob_commit->commit_entry[i].valid = entry[i][deq_ptr].valid;
    }

    stall_cycle = 0;
    LOOP_INC(deq_ptr_1, ROB_LINE_NUM);
    count_1--;

  } else if (single_commit) {
    stall_cycle = 0;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (i == single_idx)
        out.rob_commit->commit_entry[i].valid = true;
      else
        out.rob_commit->commit_entry[i].valid = false;
    }

    entry_1[single_idx][deq_ptr].valid = false;
    if (is_flush_inst(entry[single_idx][deq_ptr].uop) ||
        out.rob2csr->interrupt_resp) {
      out.rob_bcast->flush = true;
      out.rob_bcast->exception = is_exception(entry[single_idx][deq_ptr].uop) ||
                                 out.rob2csr->interrupt_resp;
      out.rob_bcast->pc = out.rob_commit->commit_entry[single_idx].uop.pc;

      if (out.rob2csr->interrupt_resp) {
        // interrupt拥有最高优先级
      } else if (entry[single_idx][deq_ptr].uop.type == ECALL) {
        out.rob_bcast->ecall = true;
        out.rob_bcast->pc = out.rob_commit->commit_entry[single_idx].uop.pc;
      } else if (entry[single_idx][deq_ptr].uop.type == MRET) {
        out.rob_bcast->mret = true;
      } else if (entry[single_idx][deq_ptr].uop.type == SRET) {
        out.rob_bcast->sret = true;
      } else if (entry[single_idx][deq_ptr].uop.page_fault_store) {
        out.rob_bcast->page_fault_store = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.result;
      } else if (entry[single_idx][deq_ptr].uop.page_fault_load) {
        out.rob_bcast->page_fault_load = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.result;
      } else if (entry[single_idx][deq_ptr].uop.page_fault_inst) {
        out.rob_bcast->page_fault_inst = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.pc;
      } else if (entry[single_idx][deq_ptr].uop.illegal_inst) {
        out.rob_bcast->illegal_inst = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.instruction;
      } else if (entry[single_idx][deq_ptr].uop.type == EBREAK) {
        extern bool sim_end;
        sim_end = true;
      } else if (entry[single_idx][deq_ptr].uop.type == CSR) {
        out.rob2csr->commit = true;
      } else {
        if (entry[single_idx][deq_ptr].uop.type != CSR &&
            entry[single_idx][deq_ptr].uop.type != SFENCE_VMA) {
          cout << hex << entry[single_idx][deq_ptr].uop.instruction << endl;
          exit(1);
        }
      }
    }
  } else {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      out.rob_commit->commit_entry[i].valid = false;
    }
  }

  out.rob2dis->enq_idx = enq_ptr;
  out.rob2dis->rob_flag = flag;

  stall_cycle++;
  if (stall_cycle > 1000) {
    cout << dec << sim_time << endl;
    cout << "卡死了" << endl;

    // 打印ROB出队行指令 看是哪条指令卡死
    cout << "ROB deq inst:" << endl;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        cout << hex << entry[i][deq_ptr].uop.instruction
             << " cplt_num: " << entry[i][deq_ptr].uop.cplt_num
             << "is_page_fault: " << is_page_fault(entry[i][deq_ptr].uop)
             << endl;
      }
    }

    int free_preg_num = 0;
    for (int i = 0; i < PRF_NUM; i++) {
      if (back.rename.free_vec[i]) {
        free_preg_num++;
      }
    }
    cout << "free preg num: " << dec << free_preg_num << endl;

    int free_tag_num = 0;
    for (int i = 0; i < MAX_BR_NUM; i++) {
      if (back.idu.tag_vec[i]) {
        free_tag_num++;
      }
    }
    cout << "free tag num: " << dec << free_tag_num << endl;

    int free_stq_num = 0;
    for (int i = 0; i < STQ_NUM; i++) {
      if (!back.stq.entry[i].valid) {
        free_stq_num++;
      }
    }
    cout << "free stq num: " << dec << free_stq_num << endl;
    cout << "dis2ren ready: " << dec << back.rename.in.dis2ren->ready << endl;
    cout << "ren2dec ready: " << dec << back.rename.out.ren2dec->ready << endl;
    cout << "dec2front ready: " << dec << back.idu.out.dec2front->ready << endl;

    exit(1);
  }
}

void ROB::comb_complete() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.prf2rob->entry[i].valid) {
      int bank_idx = in.prf2rob->entry[i].uop.rob_idx & 0b11;
      int line_idx = in.prf2rob->entry[i].uop.rob_idx >> 2;
      entry_1[bank_idx][line_idx].uop.cplt_num++;

      if (i == IQ_LD) {
        if (is_page_fault(in.prf2rob->entry[i].uop)) {
          entry_1[bank_idx][line_idx].uop.result =
              in.prf2rob->entry[i].uop.result;
          entry_1[bank_idx][line_idx].uop.page_fault_load = true;
        }
      }

      if (i == IQ_STA) {
        if (is_page_fault(in.prf2rob->entry[i].uop)) {
          entry_1[bank_idx][line_idx].uop.result =
              in.prf2rob->entry[i].uop.result;
          entry_1[bank_idx][line_idx].uop.page_fault_store = true;
        }
      }

      // for debug
      entry_1[bank_idx][line_idx].uop.difftest_skip =
          in.prf2rob->entry[i].uop.difftest_skip;
      if (i == IQ_BR0 || i == IQ_BR1) {
        entry_1[bank_idx][line_idx].uop.pc_next =
            in.prf2rob->entry[i].uop.pc_next;
        entry_1[bank_idx][line_idx].uop.mispred =
            in.prf2rob->entry[i].uop.mispred;
        entry_1[bank_idx][line_idx].uop.br_taken =
            in.prf2rob->entry[i].uop.br_taken;
      }
    }
  }
}

void ROB::comb_branch() {
  // 分支预测失败
  if (in.dec_bcast->mispred && !out.rob_bcast->flush) {
    enq_ptr_1 = ((in.dec_bcast->redirect_rob_idx >> 2) + 1) % (ROB_LINE_NUM);
    count_1 = count - (enq_ptr + ROB_LINE_NUM - enq_ptr_1) % ROB_LINE_NUM;

    if (enq_ptr_1 > enq_ptr) {
      flag_1 = !flag;
    }

    for (int i = (in.dec_bcast->redirect_rob_idx & 0b11) + 1; i < ROB_BANK_NUM;
         i++) {
      entry_1[i][in.dec_bcast->redirect_rob_idx >> 2].valid = false;
    }
  }
}

void ROB::comb_fire() {
  // 入队
  wire1_t enq = false;
  if (out.rob2dis->ready) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (in.dis2rob->dis_fire[i]) {
        entry_1[i][enq_ptr].valid = true;
        entry_1[i][enq_ptr].uop = in.dis2rob->uop[i];
        entry_1[i][enq_ptr].uop.cplt_num = 0;
        enq = true;
      } else {
        entry_1[i][enq_ptr].valid = false;
      }
    }
  }

  if (enq) {
    LOOP_INC(enq_ptr_1, ROB_LINE_NUM);
    count_1++;
    if (enq_ptr_1 == 0) {
      flag_1 = !flag;
    }
  }
}

void ROB::comb_flush() {
  if (out.rob_bcast->flush) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      for (int j = 0; j < ROB_LINE_NUM; j++) {
        entry_1[i][j].valid = false;
      }
    }

    enq_ptr_1 = 0;
    deq_ptr_1 = 0;
    count_1 = 0;
    flag_1 = false;
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
  flag = flag_1;
  assert(count == ROB_LINE_NUM ||
         count == (enq_ptr + ROB_LINE_NUM - deq_ptr) % ROB_LINE_NUM);
}
