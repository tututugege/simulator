#include "IO.h"
#include <RISCV.h>
#include <Rob.h>
#include <cmath>
#include <config.h>
#include <cstdlib>
#include <iostream>
#include <util.h>
#include <AbstractLsu.h>

void Rob::init() {
  deq_ptr = deq_ptr_1 = 0;
  enq_ptr = enq_ptr_1 = 0;
  enq_flag = enq_flag_1 = false;
  deq_flag = deq_flag_1 = false;

  for (int i = 0; i < ROB_LINE_NUM; i++) {
    for (int j = 0; j < ROB_BANK_NUM; j++) {
      entry[j][i].valid = false;
      entry_1[j][i].valid = false;
    }
  }
}

void Rob::comb_ready() {
  out.rob2dis->stall = false;
  out.rob2csr->commit = false;
  out.rob2csr->interrupt_resp = false;

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (entry[i][deq_ptr].valid && is_flush_inst(entry[i][deq_ptr].uop)) {
      out.rob2dis->stall = true;
      break;
    }
  }

  if (!is_empty() && in.csr2rob->interrupt_req && !in.dec_bcast->mispred) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        out.rob2csr->interrupt_resp = true;
        break;
      }
    }
  }
  // Determine Head Status for Memory Bound Calculation (Refined Phase 3.6 -
  // Oldest First) User Feedback: Find the *oldest* incomplete instruction. If
  // entry[0] is a DIV (blocked) and entry[1] is a LOAD (blocked), the stall is
  // caused by the DIV (Core Bound), not the LOAD.

  bool found_stall = false;
  bool stall_is_mem = false;
  bool stall_is_miss = false;

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (entry[i][deq_ptr].valid) {
      bool is_ready =
          (entry[i][deq_ptr].uop.cplt_num == entry[i][deq_ptr].uop.uop_num);

      if (!is_ready) {
        // This is the oldest incomplete instruction. It is the bottleneck.
        found_stall = true;
        if (is_load(entry[i][deq_ptr].uop) || is_store(entry[i][deq_ptr].uop)) {
          stall_is_mem = true;
          uint32_t rob_idx = i + (deq_ptr * ROB_BANK_NUM);
          stall_is_miss = (in.lsu2rob->miss_mask >> rob_idx) & 1;
        } else {
          stall_is_mem = false;
          stall_is_miss = false;
        }
        break; // Stop scanning once the first blocker is found.
      }
      // If valid but ready, it's not the blocker. Continue to the next younger
      // instruction.
    }
  }

  out.rob2dis->head_not_ready = found_stall;
  out.rob2dis->head_is_memory = (found_stall && stall_is_mem);
  out.rob2dis->head_is_miss = (found_stall && stall_is_miss);

  out.rob2dis->empty = is_empty();
  out.rob2dis->ready = !is_full();
}

void Rob::comb_commit() {

  static int stall_cycle = 0; // 检查是否卡死
  out.rob_bcast->flush = out.rob_bcast->exception = out.rob_bcast->mret =
      out.rob_bcast->sret = out.rob_bcast->ecall = false;

  out.rob_bcast->interrupt = out.rob2csr->interrupt_resp;

  out.rob_bcast->page_fault_inst = out.rob_bcast->page_fault_load =
      out.rob_bcast->page_fault_store = out.rob_bcast->illegal_inst = false;

  wire<1> commit = (!is_empty() && !in.dec_bcast->mispred);

  // 检查 BANK 的同一行是否都已完成
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    commit = commit &&
             (!entry[i][deq_ptr].valid || (entry[i][deq_ptr].uop.cplt_num ==
                                           entry[i][deq_ptr].uop.uop_num));
  }

  // 出队行如果存在特殊指令，则进行单指令提交 (Single Commit)
  wire<1> single_commit = false;
  wire<clog2(ROB_BANK_NUM)> single_idx = 0;

  if (!in.dec_bcast->mispred) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if ((entry[i][deq_ptr].valid && is_flush_inst(entry[i][deq_ptr].uop)) ||
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
    out.rob_commit->commit_entry[i].extra_data = entry[i][deq_ptr].extra_data;
  }

  // 一组提交
  if (commit && !single_commit) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      out.rob_commit->commit_entry[i].valid = entry[i][deq_ptr].valid;
      entry_1[i][deq_ptr].valid = false;
    }

    stall_cycle = 0;
    LOOP_INC(deq_ptr_1, ROB_LINE_NUM);
    if (deq_ptr_1 == 0) {
      deq_flag_1 = !deq_flag;
    }

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
        out.rob_bcast->trap_val = lsu->get_stq_entry(entry[single_idx][deq_ptr].uop.stq_idx).addr;
      } else if (entry[single_idx][deq_ptr].uop.page_fault_load) {
        out.rob_bcast->page_fault_load = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.result;
      } else if (entry[single_idx][deq_ptr].uop.page_fault_inst) {
        out.rob_bcast->page_fault_inst = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].uop.pc;
      } else if (entry[single_idx][deq_ptr].uop.illegal_inst) {
        out.rob_bcast->illegal_inst = true;
        out.rob_bcast->trap_val = entry[single_idx][deq_ptr].extra_data.instruction;
      } else if (entry[single_idx][deq_ptr].uop.type == EBREAK) {
        ctx->exit_reason = ExitReason::EBREAK;
      } else if (entry[single_idx][deq_ptr].uop.type == WFI) {
        ctx->exit_reason = ExitReason::WFI;
      } else if (entry[single_idx][deq_ptr].uop.type == CSR) {
        out.rob2csr->commit = true;
      } else if (entry[single_idx][deq_ptr].uop.type == SFENCE_VMA) {
        out.rob_bcast->fence = true;
      } else if (entry[single_idx][deq_ptr].uop.flush_pipe) {
        // MMIO-triggered flush, no extra CSR/MMU actions needed here
        out.rob_bcast->pc = entry[single_idx][deq_ptr].uop.pc;
      } else {
        if (entry[single_idx][deq_ptr].uop.type != CSR) {
          Assert(0 && "ERROR: unknown instruction during commit");
        }
      }
    }
  } else {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      out.rob_commit->commit_entry[i].valid = false;
    }
  }

  out.rob2dis->enq_idx = enq_ptr;
  out.rob2dis->rob_flag = enq_flag;

  stall_cycle++;
  if (stall_cycle > 500) {
    cout << dec << ctx->perf.cycle << endl;
    cout << "卡死了" << endl;

    // 详细 ROB 调试信息
    printf("[ROB DEBUG] is_empty=%d, deq_ptr=%d, enq_ptr=%d, commit=%d, "
           "single_commit=%d\n",
           is_empty(), deq_ptr, enq_ptr, (int)commit, (int)single_commit);

    // 打印Rob出队行指令 看是哪条指令卡死
    cout << "Rob deq inst:" << endl;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        printf("0x%08x: 0x%08x cplt_num: %d  uop_num: %d rob_idx:%d "
               "is_page_fault: %d inst_idx: %lld type: %d op: %d\n",
                entry[i][deq_ptr].uop.pc, entry[i][deq_ptr].extra_data.instruction,
                entry[i][deq_ptr].uop.cplt_num, entry[i][deq_ptr].uop.uop_num,
                (i + (deq_ptr * ROB_BANK_NUM)),
                is_page_fault(entry[i][deq_ptr].uop),
                (long long)entry[i][deq_ptr].uop.inst_idx,
                entry[i][deq_ptr].uop.type, entry[i][deq_ptr].uop.op);
      } else {
        printf("[Bank %d] INVALID\n", i);
      }
    }

    Assert(0 && "ROB Deadlock detected (stall_cycle > 500)");
  }
}

void Rob::comb_complete() {
  //  执行完毕的标记
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (in.prf2rob->entry[i].valid) {
      int bank_idx = get_rob_bank(in.prf2rob->entry[i].uop.rob_idx);
      int line_idx = get_rob_line(in.prf2rob->entry[i].uop.rob_idx);

      entry_1[bank_idx][line_idx].uop.cplt_num++;

      for (int k = 0; k < LSU_LDU_COUNT; k++) {
        if (i == IQ_LD_PORT_BASE + k) {
          if (is_page_fault(in.prf2rob->entry[i].uop)) {
            entry_1[bank_idx][line_idx].uop.result =
                in.prf2rob->entry[i].uop.result;
            entry_1[bank_idx][line_idx].uop.page_fault_load = true;
          }
        }
      }

      for (int k = 0; k < LSU_STA_COUNT; k++) {
        if (i == IQ_STA_PORT_BASE + k) {
          if (is_page_fault(in.prf2rob->entry[i].uop)) {
            entry_1[bank_idx][line_idx].uop.result =
                in.prf2rob->entry[i].uop.result;
            entry_1[bank_idx][line_idx].uop.page_fault_store = true;
          }
        }
      }

      // for debug
      entry_1[bank_idx][line_idx].uop.flush_pipe =
          in.prf2rob->entry[i].uop.flush_pipe;
      entry_1[bank_idx][line_idx].uop.difftest_skip =
          in.prf2rob->entry[i].uop.difftest_skip;
      if (is_branch_uop(in.prf2rob->entry[i].uop.op)) {
        entry_1[bank_idx][line_idx].extra_data.pc_next =
            in.prf2rob->entry[i].uop.diag_val;
        entry_1[bank_idx][line_idx].uop.mispred =
            in.prf2rob->entry[i].uop.mispred;
        entry_1[bank_idx][line_idx].uop.br_taken =
            in.prf2rob->entry[i].uop.br_taken;
      }
    }
  }
}

void Rob::comb_branch() {
  // 分支预测失败
  if (in.dec_bcast->mispred && !out.rob_bcast->flush) {
    enq_ptr_1 =
        (get_rob_line(in.dec_bcast->redirect_rob_idx) + 1) % (ROB_LINE_NUM);

    if (enq_ptr_1 > enq_ptr) {
      enq_flag_1 = !enq_flag;
    }

    // 修正：明确使从重定向点到旧 Tail 的所有条目失效
    // 这可以处理多行回溯并防止“僵尸提交”
    int ptr = get_rob_line(in.dec_bcast->redirect_rob_idx); // 行索引
    int start_bank =
        get_rob_bank(in.dec_bcast->redirect_rob_idx) + 1; // 行内 BANK 索引

    // 1. 清除第一行（重定向行）的剩余条目
    for (int i = start_bank; i < ROB_BANK_NUM; i++) {
      entry_1[i][ptr].valid = false;
    }

    // 2. 清除所有后续行，直到旧的 enq_ptr 行
    // 采用循环迭代每一行是最安全的做法
    // ptr 向前移动（考虑循环），直到等于 enq_ptr（旧 Tail）

    int current_tail_line = enq_ptr; // 我们正在写入的行（或下一个空闲行）
    ptr = (ptr + 1) % ROB_LINE_NUM;

    // 循环直到触及旧 Tail 行
    // 在这里清除整行
    while (ptr != current_tail_line) {
      for (int i = 0; i < ROB_BANK_NUM; i++) {
        entry_1[i][ptr].valid = false;
      }
      ptr = (ptr + 1) % ROB_LINE_NUM;
    }
  }
}

void Rob::comb_fire() {
  // 入队
  wire<1> enq = false;
  if (out.rob2dis->ready) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (in.dis2rob->dis_fire[i]) {
        entry_1[i][enq_ptr].valid = true;
        entry_1[i][enq_ptr].uop = in.dis2rob->uop[i];
        entry_1[i][enq_ptr].uop.cplt_num = 0;
        // Instruction bits are transported via diag_val at dispatch
        entry_1[i][enq_ptr].extra_data.instruction = in.dis2rob->uop[i].diag_val;
        enq = true;
      }
    }
  }

  if (enq) {
    LOOP_INC(enq_ptr_1, ROB_LINE_NUM);
    if (enq_ptr_1 == 0) {
      enq_flag_1 = !enq_flag;
    }
  }
}

void Rob::comb_flush() {
  if (out.rob_bcast->flush) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      for (int j = 0; j < ROB_LINE_NUM; j++) {
        entry_1[i][j].valid = false;
      }
    }

    enq_ptr_1 = 0;
    deq_ptr_1 = 0;
    enq_flag_1 = false;
    deq_flag_1 = false;
  }
}

void Rob::seq() {
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    for (int j = 0; j < ROB_LINE_NUM; j++) {
      entry[i][j] = entry_1[i][j];
    }
  }

  deq_ptr = deq_ptr_1;
  enq_ptr = enq_ptr_1;
  enq_flag = enq_flag_1;
  deq_flag = deq_flag_1;
}

RobIO Rob::get_hardware_io() {
  RobIO hardware;

  // --- Inputs ---
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.from_dis.valid[i] = in.dis2rob->valid[i];
    hardware.from_dis.uop[i] = RobUop::filter(in.dis2rob->uop[i]);
  }
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    hardware.from_exe.valid[i] = in.prf2rob->entry[i].valid;
    hardware.from_exe.uop[i] = ExeWbUop::filter(in.prf2rob->entry[i].uop);
  }

  // --- Outputs ---
  hardware.to_dis.stall = out.rob2dis->stall;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    hardware.to_ren.commit_valid[i] = out.rob_commit->commit_entry[i].valid;
    hardware.to_ren.commit_areg[i] =
        out.rob_commit->commit_entry[i].uop.dest_areg;
    hardware.to_ren.commit_preg[i] =
        out.rob_commit->commit_entry[i].uop.dest_preg;
    hardware.to_ren.commit_dest_en[i] =
        out.rob_commit->commit_entry[i].uop.dest_en;
  }
  hardware.to_all.flush = out.rob_bcast->flush;

  return hardware;
}
