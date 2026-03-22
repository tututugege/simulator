#include "Rob.h"
#include "IO.h"

#include "DeadlockDebug.h"
#include "RISCV.h"
#include "config.h"
#include "util.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
inline bool rob_is_store(const RobStoredInst &uop) {
  return uop.tma.mem_commit_is_store;
}

inline bool rob_is_load(const RobStoredInst &uop) {
  return uop.tma.mem_commit_is_load;
}

inline bool rob_is_page_fault(const RobStoredInst &uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool rob_is_exception(const RobStoredInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return rob_is_page_fault(uop) || uop.illegal_inst || type == ECALL;
}

inline bool rob_is_flush_inst(const RobStoredInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == CSR || type == ECALL || type == MRET || type == SRET ||
         type == SFENCE_VMA || rob_is_exception(uop) || type == EBREAK ||
         uop.flush_pipe;
}
} // namespace

void Rob::init() {
  deq_ptr = deq_ptr_1 = 0;
  enq_ptr = enq_ptr_1 = 0;
  enq_flag = enq_flag_1 = false;
  deq_flag = deq_flag_1 = false;
  stall_cycle = 0;

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
    if (entry[i][deq_ptr].valid && rob_is_flush_inst(entry[i][deq_ptr].uop)) {
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
        if (rob_is_load(entry[i][deq_ptr].uop) ||
            rob_is_store(entry[i][deq_ptr].uop)) {
          stall_is_mem = true;
          uint32_t rob_idx = i + (deq_ptr * ROB_BANK_NUM);
          stall_is_miss =
              (rob_idx < ROB_NUM) ? in.lsu2rob->tma.miss_mask.test(rob_idx) : false;
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

  out.rob2dis->tma.head_not_ready = found_stall;
  out.rob2dis->tma.head_is_memory = (found_stall && stall_is_mem);
  out.rob2dis->tma.head_is_miss = (found_stall && stall_is_miss);

  out.rob2dis->empty = is_empty();
  out.rob2dis->ready = !is_full();
}

void Rob::comb_ftq_pc_req() {
  out.ftq_pc_req->req[0] = {};

  if (is_empty()) {
    return;
  }

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (entry[i][deq_ptr].valid) {
      out.ftq_pc_req->req[0].valid = true;
      out.ftq_pc_req->req[0].ftq_idx = entry[i][deq_ptr].uop.ftq_idx;
      out.ftq_pc_req->req[0].ftq_offset = entry[i][deq_ptr].uop.ftq_offset;
      break;
    }
  }
}

void Rob::comb_commit() {
  out.rob_bcast->flush = out.rob_bcast->exception = out.rob_bcast->mret =
      out.rob_bcast->sret = out.rob_bcast->ecall = out.rob_bcast->fence =
          out.rob_bcast->fence_i = false;

  out.rob_bcast->interrupt = out.rob2csr->interrupt_resp;

  out.rob_bcast->page_fault_inst = out.rob_bcast->page_fault_load =
      out.rob_bcast->page_fault_store = out.rob_bcast->illegal_inst = false;

  // 广播队头行的第一个 valid 项，以及该行里第一个未完成项。
  // LSU 使用后者决定 MMIO 访存何时可以发射，避免与 ROB 的整行提交
  // 策略形成循环等待。
  out.rob_bcast->head_valid = false;
  out.rob_bcast->head_rob_idx = 0;
  out.rob_bcast->head_incomplete_valid = false;
  out.rob_bcast->head_incomplete_rob_idx = 0;
  if (!is_empty()) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        out.rob_bcast->head_valid = true;
        out.rob_bcast->head_rob_idx = make_rob_idx(deq_ptr, i);
        break;
      }
    }
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid &&
          entry[i][deq_ptr].uop.cplt_num != entry[i][deq_ptr].uop.uop_num) {
        out.rob_bcast->head_incomplete_valid = true;
        out.rob_bcast->head_incomplete_rob_idx = make_rob_idx(deq_ptr, i);
        break;
      }
    }
  }
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
  bool progress_single_commit = false;
  auto log_incomplete_store_commit = [&](const RobStoredInst &uop,
                                         const char *path, int slot) {
    if (!rob_is_store(uop) || uop.cplt_num == uop.uop_num) {
      return;
    }
    // std::printf(
    //     "[ROB][WARN][INCOMPLETE_STORE_COMMIT] cyc=%llu path=%s slot=%d pc=0x%08x "
    //     "inst=0x%08x type=%u rob=%u flag=%u stq=%u stqf=%u cplt_num=%u uop_num=%u "
    //     "interrupt=%d flush=%d mispred=%d\n",
    //     (unsigned long long)ctx->perf.cycle, path, slot, (uint32_t)uop.pc,
    //     (uint32_t)uop.instruction, (unsigned)uop.type, (unsigned)uop.rob_idx,
    //     (unsigned)uop.rob_flag, (unsigned)uop.stq_idx, (unsigned)uop.stq_flag,
    //     (unsigned)uop.cplt_num, (unsigned)uop.uop_num,
    //     (int)out.rob_bcast->interrupt, (int)out.rob_bcast->flush,
    //     (int)in.dec_bcast->mispred);
    deadlock_debug::dump_all();
  };

  if (!in.dec_bcast->mispred) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if ((entry[i][deq_ptr].valid &&
           rob_is_flush_inst(entry[i][deq_ptr].uop)) ||
          out.rob2csr->interrupt_resp) {
        single_commit = true;
        break;
      }
    }

    // 看第一个 valid 指令是否完成。即使是 interrupt 触发的 single_commit，
    // 也必须等待该最老指令完成，避免提交未完成的内存指令。
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        single_idx = i;
        if (entry[i][deq_ptr].uop.cplt_num != entry[i][deq_ptr].uop.uop_num) {
          single_commit = false;
        }
        if (decode_inst_type(entry[i][deq_ptr].uop.type) == SFENCE_VMA &&
            in.lsu2rob != nullptr &&
            in.lsu2rob->committed_store_pending) {
          // SFENCE.VMA 需要单提交并触发 flush；当提交侧仍有已提交 store
          // 未落地时， 必须阻塞提交，不能退化为组提交吞掉该指令。
          single_commit = false;
          commit = false;
        }
        break;
      }
    }

    // Forward-progress fallback:
    // If group commit is blocked by younger banks in the same ROB line, allow
    // committing the oldest ready non-flush instruction so older stores can
    // still commit/retire and unblock younger loads that are waiting on them.
    if (!commit && !single_commit && !out.rob_bcast->interrupt) {
      for (int i = 0; i < ROB_BANK_NUM; i++) {
        if (!entry[i][deq_ptr].valid) {
          continue;
        }
        const auto &uop = entry[i][deq_ptr].uop;
        const bool ready = (uop.cplt_num == uop.uop_num);
        if (ready && !rob_is_flush_inst(uop)) {
          single_commit = true;
          single_idx = i;
          progress_single_commit = true;
        }
        break;
      }
    }
  }

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    out.rob_commit->commit_entry[i].uop = entry[i][deq_ptr].uop.to_commit_inst();
  }

  // 一组提交
  if (commit && !single_commit) {
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        const auto &uop = entry[i][deq_ptr].uop;
        // 仅在 Load 已完成且非异常语义时检查地址对齐，避免中断单提交/异常路径下
        // 将 diag_val 的其他语义（如指令字、异常地址）误当作物理地址检查。
        if (rob_is_load(uop) && (uop.cplt_num == uop.uop_num) &&
            !rob_is_page_fault(uop)) {
          uint32_t alignment_mask = uop.dbg.mem_align_mask;
          Assert((uop.diag_val & alignment_mask) == 0 &&
                 "DUT: Load address misaligned at commit!");
        }
      }
      out.rob_commit->commit_entry[i].valid = entry[i][deq_ptr].valid;
      if (out.rob_commit->commit_entry[i].valid) {
        log_incomplete_store_commit(entry[i][deq_ptr].uop, "group",
                                    i);
      }
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

    if (entry[single_idx][deq_ptr].valid) {
      const auto &uop = entry[single_idx][deq_ptr].uop;
      log_incomplete_store_commit(uop, "single", single_idx);
      // 中断触发的 single_commit 也要求首条指令已经完成。
      if (rob_is_load(uop) && (uop.cplt_num == uop.uop_num) &&
          !rob_is_page_fault(uop)) {
        uint32_t alignment_mask = uop.dbg.mem_align_mask;
        Assert((uop.diag_val & alignment_mask) == 0 &&
               "DUT: Load address misaligned at single commit!");
      }
    }

    entry_1[single_idx][deq_ptr].valid = false;
    if (progress_single_commit) {
      DBG_PRINTF("[ROB][PROGRESS SINGLE COMMIT] cyc=%llu deq_ptr=%u bank=%u rob_idx=%u pc=0x%08x type=%u\n",
                 (unsigned long long)ctx->perf.cycle, (unsigned)deq_ptr,
                 (unsigned)single_idx, (unsigned)entry[single_idx][deq_ptr].uop.rob_idx,
                 entry[single_idx][deq_ptr].uop.dbg.pc,
                 (unsigned)entry[single_idx][deq_ptr].uop.type);
    }
    if (rob_is_flush_inst(entry[single_idx][deq_ptr].uop) ||
        out.rob2csr->interrupt_resp) {
      const auto &uop = entry[single_idx][deq_ptr].uop;
      Assert(in.ftq_pc_resp->resp[0].valid);
      uint32_t single_pc = in.ftq_pc_resp->resp[0].pc;
      out.rob_bcast->flush = true;
      out.rob_bcast->exception =
          rob_is_exception(uop) || out.rob2csr->interrupt_resp;
      out.rob_bcast->pc = single_pc;

      if (out.rob2csr->interrupt_resp) {
        // interrupt拥有最高优先级
      } else if (decode_inst_type(uop.type) == ECALL) {
        out.rob_bcast->ecall = true;
        out.rob_bcast->pc = single_pc;
      } else if (decode_inst_type(uop.type) == MRET) {
        out.rob_bcast->mret = true;
      } else if (decode_inst_type(uop.type) == SRET) {
        out.rob_bcast->sret = true;
      } else if (uop.page_fault_store) {
        out.rob_bcast->page_fault_store = true;
        out.rob_bcast->trap_val = uop.diag_val;
      } else if (uop.page_fault_load) {
        out.rob_bcast->page_fault_load = true;
        out.rob_bcast->trap_val = uop.diag_val;
      } else if (uop.page_fault_inst) {
        out.rob_bcast->page_fault_inst = true;
        out.rob_bcast->trap_val = single_pc;
      } else if (uop.illegal_inst) {
        out.rob_bcast->illegal_inst = true;
        out.rob_bcast->trap_val = uop.diag_val;
      } else if (decode_inst_type(uop.type) == EBREAK) {
        ctx->exit_reason = ExitReason::EBREAK;
      } else if (decode_inst_type(uop.type) == WFI) {
        ctx->exit_reason = ExitReason::WFI;
      } else if (decode_inst_type(uop.type) == CSR) {
        out.rob2csr->commit = true;
      } else if (decode_inst_type(uop.type) == SFENCE_VMA) {
        out.rob_bcast->fence = true;
      } else if (uop.flush_pipe) {
        // MMIO-triggered flush, no extra CSR/MMU actions needed here
        out.rob_bcast->pc = single_pc;
      } else {
        if (decode_inst_type(uop.type) != CSR) {
          Assert(0 && "Error: Who is Rem? This pointer is forgotten.");
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
  if (stall_cycle > 10000) {
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
               "is_page_fault: %d inst_idx: %lld type: %d\n",
               entry[i][deq_ptr].uop.dbg.pc, entry[i][deq_ptr].uop.diag_val,
               entry[i][deq_ptr].uop.cplt_num, entry[i][deq_ptr].uop.uop_num,
               (i + (deq_ptr * ROB_BANK_NUM)),
               rob_is_page_fault(entry[i][deq_ptr].uop),
               (long long)entry[i][deq_ptr].uop.dbg.inst_idx,
               decode_inst_type(entry[i][deq_ptr].uop.type));
      } else {
        printf("[Bank %d] INVALID\n", i);
      }
    }

    deadlock_debug::dump_all();
    Assert(0 && "ROB Deadlock detected (stall_cycle > 10000)");
  }
}

void Rob::comb_complete() {
  //  执行完毕的标记 (Early Completion Phase 2)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (in.exu2rob->entry[i].valid) {
      const auto &wb = in.exu2rob->entry[i].uop;
      bool wb_has_page_fault =
          wb.page_fault_inst || wb.page_fault_load || wb.page_fault_store;
      int bank_idx = get_rob_bank(wb.rob_idx);
      int line_idx = get_rob_line(wb.rob_idx);

      entry_1[bank_idx][line_idx].uop.cplt_num++;
      if (entry_1[bank_idx][line_idx].uop.cplt_num >
          entry_1[bank_idx][line_idx].uop.uop_num) {
        Assert(0 && "ROB: completion overflow (cplt_num > uop_num)");
      }

      for (int k = 0; k < LSU_LDU_COUNT; k++) {
        if (i == IQ_LD_PORT_BASE + k) {
          // 保存物理地址，用于 Commit 时的对齐检查
          entry_1[bank_idx][line_idx].uop.diag_val = wb.diag_val;

          if (wb_has_page_fault) {
            entry_1[bank_idx][line_idx].uop.diag_val = wb.result;
            entry_1[bank_idx][line_idx].uop.page_fault_load = true;
          }
        }
      }

      for (int k = 0; k < LSU_STA_COUNT; k++) {
        if (i == IQ_STA_PORT_BASE + k) {
          if (wb_has_page_fault) {
            entry_1[bank_idx][line_idx].uop.diag_val = wb.result;
            entry_1[bank_idx][line_idx].uop.page_fault_store = true;
          }
        }
      }

      // 同一条指令可能由多个 uop 回写（例如 STA/STD），flush_pipe
      // 需要保持置位， 不能被后到达的 uop 覆盖为 0。
      entry_1[bank_idx][line_idx].uop.flush_pipe =
          entry_1[bank_idx][line_idx].uop.flush_pipe || wb.flush_pipe;
      entry_1[bank_idx][line_idx].uop.dbg.difftest_skip = wb.dbg.difftest_skip;
      if (is_branch_uop(wb.op)) {
        entry_1[bank_idx][line_idx].uop.diag_val = wb.diag_val;
        entry_1[bank_idx][line_idx].uop.mispred = wb.mispred;
        entry_1[bank_idx][line_idx].uop.br_taken = wb.br_taken;
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

    // Fix: Mark the branch that caused misprediction as ftq_is_last.
    // Since everything after it in the same FTQ block is flushed, it
    // effectively becomes the 'last' instruction that will ever commit for this
    // FTQ entry.
    int redirect_bank = get_rob_bank(in.dec_bcast->redirect_rob_idx);
    int redirect_line = get_rob_line(in.dec_bcast->redirect_rob_idx);
    entry_1[redirect_bank][redirect_line].uop.ftq_is_last = true;

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
    for (int i = 0; i < DECODE_WIDTH; i++) {
      if (in.dis2rob->dis_fire[i]) {
        entry_1[i][enq_ptr].valid = true;
        entry_1[i][enq_ptr].uop =
            RobStoredInst::from_dis_rob_inst(in.dis2rob->uop[i]);
        entry_1[i][enq_ptr].uop.cplt_num = 0;
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
