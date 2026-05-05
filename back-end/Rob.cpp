#include "Rob.h"
#include "IO.h"

#include "RealLsu.h"
#include "SimCpu.h"
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
         type == SFENCE_VMA || type == FENCE_I || rob_is_exception(uop) ||
         type == EBREAK ||
         uop.flush_pipe;
}

inline bool rob_is_fence_inst(const RobStoredInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == FENCE || type == FENCE_I || type == SFENCE_VMA;
}

inline bool rob_is_complete(const RobStoredInst &uop) {
  return uop.cplt_mask == uop.expect_mask;
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

// 功能：复制 ROB 当前状态到本拍工作副本（*_1）。
// 输入依赖：entry[][]、enq_ptr/deq_ptr、enq_flag/deq_flag。
// 输出更新：entry_1[][]、enq_ptr_1/deq_ptr_1、enq_flag_1/deq_flag_1。
// 约束：仅状态镜像，不做提交/分配/恢复决策。
void Rob::comb_begin() {
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    for (int j = 0; j < ROB_LINE_NUM; j++) {
      entry_1[i][j] = entry[i][j];
    }
  }
  enq_ptr_1 = enq_ptr;
  deq_ptr_1 = deq_ptr;
  enq_flag_1 = enq_flag;
  deq_flag_1 = deq_flag;
}

// 功能：生成 ROB 对 Dispatch/CSR 的就绪与停顿信息，并标注队头阻塞类型。
// 输入依赖：entry[][deq_ptr]、is_empty()/is_full()、in.lsu2rob->tma.miss_mask。
// 输出更新：out.rob2dis->{stall,tma,empty,ready}、out.rob2csr->{commit,interrupt_resp}。
// 约束：队头遇到 flush 类指令时强制 stall；阻塞归因按最老未完成指令判定。
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

  // Determine Head Status for Memory Bound Calculation (Refined Phase 3.6 -
  // Oldest First) User Feedback: Find the *oldest* incomplete instruction. If
  // entry[0] is a DIV (blocked) and entry[1] is a LOAD (blocked), the stall is
  // caused by the DIV (Core Bound), not the LOAD.

  bool found_stall = false;
  bool stall_is_mem = false;
  bool stall_is_miss = false;

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (entry[i][deq_ptr].valid) {
      bool is_ready = rob_is_complete(entry[i][deq_ptr].uop);

      if (!is_ready) {
        // This is the oldest incomplete instruction. It is the bottleneck.
        found_stall = true;
        if (rob_is_load(entry[i][deq_ptr].uop) ||
            rob_is_store(entry[i][deq_ptr].uop)) {
          stall_is_mem = true;
          uint32_t rob_idx = i + (deq_ptr * ROB_BANK_NUM);
          stall_is_miss = (rob_idx < ROB_NUM)
                              ? in.lsu2rob->tma.miss_mask.test(rob_idx)
                              : false;
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

// 功能：向 FTQ 请求 ROB 队头最老有效指令的 PC。
// 输入依赖：is_empty()、entry[][deq_ptr].valid 及其 ftq_idx/ftq_offset。
// 输出更新：out.ftq_pc_req->req[0]。
// 约束：ROB 为空时不发请求；每拍最多发出 1 条 ROB 侧 PC 请求。
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

// 功能：执行 ROB 提交仲裁（组提交/单提交）、flush/异常/中断广播与提交输出生成。
// 输入依赖：entry[][deq_ptr]、in.dec_bcast、in.csr2rob、in.ftq_pc_resp、in.lsu2rob。
// 输出更新：out.rob_commit、out.rob_bcast、out.rob2csr、out.rob2dis->enq_idx/rob_flag，及 entry_1/deq_ptr_1/deq_flag_1。
// 约束：flush/异常/中断必须在精确提交点触发；特殊指令与中断走单提交路径。
void Rob::comb_commit() {
  out.rob_bcast->flush = out.rob_bcast->exception = out.rob_bcast->mret =
      out.rob_bcast->sret = out.rob_bcast->ecall = out.rob_bcast->fence =
          out.rob_bcast->fence_i = false;

  // interrupt_resp must only be raised at a real precise commit point in this
  // cycle, not in comb_ready().
  out.rob2csr->interrupt_resp = false;
  out.rob_bcast->interrupt = false;

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
          !rob_is_complete(entry[i][deq_ptr].uop)) {
        out.rob_bcast->head_incomplete_valid = true;
        out.rob_bcast->head_incomplete_rob_idx = make_rob_idx(deq_ptr, i);
        break;
      }
    }
  }
  const bool front_stall =
      (in.front_stall != nullptr) && static_cast<bool>(*in.front_stall);
  wire<1> commit = (!is_empty() && !in.dec_bcast->mispred);

  // 组提交约束：同一拍只允许提交队头所在 fetch block（同 ftq_idx）的指令，
  // 防止 ROB retire 跨取指块导致 FTQ/前端需要多出队联动。
  bool group_commit_mask[ROB_BANK_NUM] = {false};
  bool group_has_head = false;
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (!entry[i][deq_ptr].valid) {
      continue;
    }
    if (!group_has_head) {
      group_has_head = true;
      group_commit_mask[i] = true;
      continue;
    }
    group_commit_mask[i] = true;
  }

  // Ghost head line 修复：
  // 在逐项 single-commit 后，队头行可能被清空但指针尚未推进；
  // 若此时 ROB 仍非空（后续行仍有有效项），必须允许本拍推进 deq_ptr，
  // 否则会形成“队头全 invalid 但无法提交”的死锁。
  const bool head_line_empty = !group_has_head;
  if (group_has_head && front_stall) {
    // 真实有提交数据时，front_stall 参与背压（BPU update queue full）。
    commit = false;
  }

  // 检查候选组（同 fetch block）的可提交条件。
  // 允许 head_line_empty 走“空提交推进指针”路径。
  commit = commit && (group_has_head || head_line_empty);
  for (int i = 0; i < ROB_BANK_NUM; i++) {
    if (!group_commit_mask[i]) {
      continue;
    }
    commit = commit && rob_is_complete(entry[i][deq_ptr].uop);
  }

  // 出队行如果存在特殊指令，则进行单指令提交 (Single Commit)
  wire<1> single_commit = false;
  wire<clog2(ROB_BANK_NUM)> single_idx = 0;
  bool progress_single_commit = false;
  bool interrupt_fire = false;
  auto log_incomplete_store_commit = [&](const RobStoredInst &uop,
                                         const char *path, int slot) {
    if (!rob_is_store(uop) || rob_is_complete(uop)) {
      return;
    }
    // Disabled: dump_all() here fires on every incomplete-store commit,
    // flooding the terminal.  The deadlock path (stall_cycle > 10000)
    // already calls dump_all().
    // deadlock_debug::dump_all();
    // std::fflush(stdout);
    // std::fflush(stderr);
  };

  if (!in.dec_bcast->mispred) {
    const bool interrupt_pending = in.csr2rob->interrupt_req;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if ((entry[i][deq_ptr].valid &&
           (rob_is_flush_inst(entry[i][deq_ptr].uop) ||
            rob_is_fence_inst(entry[i][deq_ptr].uop))) ||
          interrupt_pending) {
        single_commit = true;
        break;
      }
    }

    // 看第一个 valid 指令是否完成。
    bool has_head_valid = false;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        has_head_valid = true;
        single_idx = i;
        const auto &head_uop = entry[i][deq_ptr].uop;
        if (!interrupt_pending && !rob_is_complete(head_uop)) {
          single_commit = false;
        }
        if (!interrupt_pending && rob_is_fence_inst(head_uop) &&
            in.lsu2rob != nullptr && in.lsu2rob->committed_store_pending) {
          // fence/fence.i/sfence.vma 统一提交门控：当提交侧 STQ 仍非空时，
          // 必须阻塞到可见队列清空后再提交。
          single_commit = false;
          commit = false;
        }
        if (single_commit && interrupt_pending) {
          interrupt_fire = true;
        }
        break;
      }
    }
    if (!has_head_valid) {
      // Do not single-commit a ghost slot when this ROB line is already empty.
      // Let normal group-commit path pop the empty line.
      single_commit = false;
    }

    // Forward-progress fallback:
    // If group commit is blocked by younger banks in the same ROB line, allow
    // committing the oldest ready non-flush instruction so older stores can
    // still commit/retire and unblock younger loads that are waiting on them.
    if (!commit && !single_commit && !interrupt_pending) {
      for (int i = 0; i < ROB_BANK_NUM; i++) {
        if (!entry[i][deq_ptr].valid) {
          continue;
        }
        const auto &uop = entry[i][deq_ptr].uop;
        const bool ready = rob_is_complete(uop);
        if (ready && !rob_is_flush_inst(uop)) {
          single_commit = true;
          single_idx = i;
          progress_single_commit = true;
        }
        break;
      }
    }
  }
  out.rob2csr->interrupt_resp = interrupt_fire;
  out.rob_bcast->interrupt = interrupt_fire;

  for (int i = 0; i < ROB_BANK_NUM; i++) {
    out.rob_commit->commit_entry[i].uop =
        entry[i][deq_ptr].uop.to_commit_inst();
  }

  // 一组提交
  if (commit && !single_commit) {
    bool line_drained = true;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid && group_commit_mask[i]) {
        const auto &uop = entry[i][deq_ptr].uop;
        // 仅在 Load 已完成且非异常语义时检查地址对齐，避免中断单提交/异常路径下
        // 将 diag_val 的其他语义（如指令字、异常地址）误当作物理地址检查。
        if (rob_is_load(uop) && rob_is_complete(uop) &&
            !rob_is_page_fault(uop)) {
          uint32_t alignment_mask = uop.dbg.mem_align_mask;
          Assert((uop.diag_val & alignment_mask) == 0 &&
                 "DUT: Load address misaligned at commit!");
        }
      }
      out.rob_commit->commit_entry[i].valid =
          entry[i][deq_ptr].valid && group_commit_mask[i];
      if (out.rob_commit->commit_entry[i].valid) {
        log_incomplete_store_commit(entry[i][deq_ptr].uop, "group", i);
        entry_1[i][deq_ptr].valid = false;
      }
      if (entry_1[i][deq_ptr].valid) {
        line_drained = false;
      }
    }

    stall_cycle = 0;
    if (line_drained) {
      if (head_line_empty) {
        DBG_PRINTF("[ROB][ADVANCE_EMPTY_HEAD] cyc=%llu deq_ptr=%u enq_ptr=%u\n",
                   (unsigned long long)ctx->perf.cycle, (unsigned)deq_ptr,
                   (unsigned)enq_ptr);
      }
      LOOP_INC(deq_ptr_1, ROB_LINE_NUM);
      if (deq_ptr_1 == 0) {
        deq_flag_1 = !deq_flag;
      }
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
      if (rob_is_load(uop) && rob_is_complete(uop) &&
          !rob_is_page_fault(uop)) {
        uint32_t alignment_mask = uop.dbg.mem_align_mask;
        Assert((uop.diag_val & alignment_mask) == 0 &&
               "DUT: Load address misaligned at single commit!");
      }
    }

    entry_1[single_idx][deq_ptr].valid = false;
    bool line_drained_after_single = true;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry_1[i][deq_ptr].valid) {
        line_drained_after_single = false;
        break;
      }
    }
    if (line_drained_after_single) {
      LOOP_INC(deq_ptr_1, ROB_LINE_NUM);
      if (deq_ptr_1 == 0) {
        deq_flag_1 = !deq_flag;
      }
    }

    if (progress_single_commit) {
      DBG_PRINTF("[ROB][PROGRESS SINGLE COMMIT] cyc=%llu deq_ptr=%u bank=%u "
                 "rob_idx=%u pc=0x%08x type=%u\n",
                 (unsigned long long)ctx->perf.cycle, (unsigned)deq_ptr,
                 (unsigned)single_idx,
                 (unsigned)entry[single_idx][deq_ptr].uop.rob_idx,
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
      } else if (decode_inst_type(uop.type) == FENCE_I) {
        out.rob_bcast->fence_i = true;
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
  if (stall_cycle > 50000) {
    cout << dec << ctx->perf.cycle << endl;
    cout << "卡死了" << endl;

    // 详细 ROB 调试信息
    printf("[ROB DEBUG] is_empty=%d, deq_ptr=%d, enq_ptr=%d, commit=%d, "
           "single_commit=%d front_stall=%d head_line_empty=%d\n",
           is_empty(), deq_ptr, enq_ptr, (int)commit, (int)single_commit,
           (int)front_stall, (int)head_line_empty);

    // 打印Rob出队行指令 看是哪条指令卡死
    cout << "Rob deq inst:" << endl;
    for (int i = 0; i < ROB_BANK_NUM; i++) {
      if (entry[i][deq_ptr].valid) {
        printf("0x%08x: 0x%08x cplt_mask:0x%x expect_mask:0x%x rob_idx:%d "
               "is_page_fault: %d inst_idx: %lld type: %d\n",
               entry[i][deq_ptr].uop.dbg.pc, entry[i][deq_ptr].uop.diag_val,
               entry[i][deq_ptr].uop.cplt_mask,
               entry[i][deq_ptr].uop.expect_mask,
               (i + (deq_ptr * ROB_BANK_NUM)),
               rob_is_page_fault(entry[i][deq_ptr].uop),
               (long long)entry[i][deq_ptr].uop.dbg.inst_idx,
               decode_inst_type(entry[i][deq_ptr].uop.type));
      } else {
        printf("[Bank %d] INVALID\n", i);
      }
    }

    if (ctx != nullptr && ctx->cpu != nullptr) {
      std::fprintf(stderr, "[ROB DEADLOCK] dumping LSU/MMU state\n");
      ctx->cpu->back.lsu->dump_debug_state(stderr);
      std::fprintf(stderr, "[ROB DEADLOCK] dumping MemSubsystem state\n");
      ctx->cpu->mem_subsystem.dump_debug_state(stderr);
    }
    Assert(0 && "ROB Deadlock detected (stall_cycle > 50000)");
    exit(1);
  }
}

// 功能：接收执行完成回传并更新 ROB 条目的完成位与异常/分支附加信息。
// 输入依赖：in.exu2rob->entry[]（含 rob_idx、result、diag_val、fault/mispred/flush_pipe 等）。
// 输出更新：entry_1[bank][line].uop.{cplt_mask,diag_val,page_fault_*,mispred,br_taken,flush_pipe,dbg}。
// 约束：完成位不可重复置位、不可越界到 expect_mask 之外；多 uop 同指令回传时 flush_pipe 取 OR。
void Rob::comb_complete() {
  //  执行完毕的标记 (Early Completion Phase 2)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (in.exu2rob->entry[i].valid) {
      const auto &wb = in.exu2rob->entry[i].uop;
      bool wb_has_page_fault =
          wb.page_fault_inst || wb.page_fault_load || wb.page_fault_store;
      int bank_idx = get_rob_bank(wb.rob_idx);
      int line_idx = get_rob_line(wb.rob_idx);

      const wire<ROB_CPLT_MASK_WIDTH> cplt_bit =
          rob_cplt_mask_from_issue_port(i);
      if ((entry_1[bank_idx][line_idx].uop.cplt_mask & cplt_bit) != 0) {
        std::fprintf(stderr,
                     "[ROB][DUP-CPLT] cycle=%lld port=%d rob_idx=%u line=%d bank=%d "
                     "wb_pc=0x%08x wb_inst=0x%08x wb_op=%u "
                     "entry_cplt=0x%x expect=0x%x\n",
                     sim_time, i, static_cast<unsigned>(wb.rob_idx), line_idx,
                     bank_idx, static_cast<unsigned>(wb.dbg.pc),
                     static_cast<unsigned>(wb.dbg.instruction),
                     static_cast<unsigned>(wb.op),
                     static_cast<unsigned>(
                         entry_1[bank_idx][line_idx].uop.cplt_mask),
                     static_cast<unsigned>(
                         entry_1[bank_idx][line_idx].uop.expect_mask));
        const auto &euop = entry_1[bank_idx][line_idx].uop;
        std::fprintf(stderr,
                     "[ROB][DUP-CPLT][ENTRY] type=%u func7=0x%02x entry_pc=0x%08x "
                     "entry_inst=0x%08x pf(i/l/s)=%u/%u/%u\n",
                     static_cast<unsigned>(euop.type),
                     static_cast<unsigned>(euop.func7),
                     static_cast<unsigned>(euop.dbg.pc),
                     static_cast<unsigned>(euop.dbg.instruction),
                     static_cast<unsigned>(euop.page_fault_inst),
                     static_cast<unsigned>(euop.page_fault_load),
                     static_cast<unsigned>(euop.page_fault_store));
        Assert(0 && "ROB: duplicate completion bit set");
      }
      entry_1[bank_idx][line_idx].uop.cplt_mask |= cplt_bit;
      Assert((entry_1[bank_idx][line_idx].uop.cplt_mask &
              ~entry_1[bank_idx][line_idx].uop.expect_mask) == 0 &&
             "ROB: completion bit outside expected mask");
      Assert(rob_cplt_popcount(entry_1[bank_idx][line_idx].uop.cplt_mask) <=
                 rob_cplt_popcount(
                     entry_1[bank_idx][line_idx].uop.expect_mask) &&
             "ROB: completion overflow (completed group count > expected)");

      for (int k = 0; k < LSU_LDU_COUNT; k++) {
        if (i == IQ_LD_PORT_BASE + k) {
          // 保存物理地址，用于 Commit 时的对齐检查
          entry_1[bank_idx][line_idx].uop.diag_val = wb.diag_val;

          if (wb_has_page_fault) {
            entry_1[bank_idx][line_idx].uop.diag_val = wb.result;
            entry_1[bank_idx][line_idx].uop.page_fault_inst |=
                wb.page_fault_inst;
            entry_1[bank_idx][line_idx].uop.page_fault_load |=
                wb.page_fault_load;
            entry_1[bank_idx][line_idx].uop.page_fault_store |=
                wb.page_fault_store;
          }
        }
      }

      for (int k = 0; k < LSU_STA_COUNT; k++) {
        if (i == IQ_STA_PORT_BASE + k) {
          // Keep resolved store address in ROB for commit-time policy checks.
          entry_1[bank_idx][line_idx].uop.diag_val = wb.diag_val;
          if (wb_has_page_fault) {
            entry_1[bank_idx][line_idx].uop.diag_val = wb.result;
            entry_1[bank_idx][line_idx].uop.page_fault_load |=
                wb.page_fault_load;
            entry_1[bank_idx][line_idx].uop.page_fault_store |=
                wb.page_fault_store;
            entry_1[bank_idx][line_idx].uop.page_fault_inst |=
                wb.page_fault_inst;
            if (entry_1[bank_idx][line_idx].uop.page_fault_store) {
              entry_1[bank_idx][line_idx].uop.page_fault_load = false;
            }
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

// 功能：处理分支误预测恢复，回退 ROB tail 并失效错误路径条目。
// 输入依赖：in.dec_bcast->{mispred,redirect_rob_idx}、out.rob_bcast->flush、当前 enq_ptr/enq_flag。
// 输出更新：enq_ptr_1/enq_flag_1、entry_1[][]（重定向点之后条目 invalid）、redirect 指令 ftq_is_last。
// 约束：仅在 mispred 且本拍无全局 flush 时生效；需覆盖跨行回滚，避免僵尸提交。
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

// 功能：接收 Dispatch 发射并向 ROB 入队新条目。
// 输入依赖：out.rob2dis->ready、in.dis2rob->dis_fire[]、in.dis2rob->uop[]、enq_ptr/enq_flag。
// 输出更新：entry_1[][enq_ptr]、enq_ptr_1/enq_flag_1。
// 约束：仅在 ROB ready 时接收入队；有任意槽位入队即推进 tail 一行。
void Rob::comb_fire() {
  // 入队
  wire<1> enq = false;
  if (out.rob2dis->ready) {
    for (int i = 0; i < DECODE_WIDTH; i++) {
      if (in.dis2rob->dis_fire[i]) {
        entry_1[i][enq_ptr].valid = true;
        entry_1[i][enq_ptr].uop =
            RobStoredInst::from_dis_rob_inst(in.dis2rob->uop[i]);
        entry_1[i][enq_ptr].uop.cplt_mask = 0;
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

// 功能：在全局 flush 时清空 ROB 并复位头尾指针。
// 输入依赖：out.rob_bcast->flush。
// 输出更新：entry_1[][].valid、enq_ptr_1/deq_ptr_1、enq_flag_1/deq_flag_1。
// 约束：flush 为最终状态覆盖，生效后 ROB 进入空队列初始态。
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
