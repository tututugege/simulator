#include "Ren.h"
#include "config.h"
#include <cstdlib>
#include <cstring>

#include "util.h"

// 多个comb复用的中间信号
static wire<1> fire[DECODE_WIDTH];
static wire<1> spec_alloc_flush[PRF_NUM];
static wire<1> spec_alloc_mispred[PRF_NUM];
static wire<1> spec_alloc_normal[PRF_NUM];
static wire<1> free_vec_flush[PRF_NUM];
static wire<1> free_vec_mispred[PRF_NUM];
static wire<1> free_vec_normal[PRF_NUM];
static wire<PRF_IDX_WIDTH> spec_RAT_flush[ARF_NUM + 1];
static wire<PRF_IDX_WIDTH> spec_RAT_mispred[ARF_NUM + 1];
static wire<PRF_IDX_WIDTH> spec_RAT_normal[ARF_NUM + 1];

void Ren::init() {
  for (int i = 0; i < PRF_NUM; i++) {
    spec_alloc[i] = false;

    if (i < ARF_NUM + 1) {
      spec_RAT[i] = i;
      arch_RAT[i] = i;
      free_vec[i] = false;
    } else {
      free_vec[i] = true;
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r[i] = {};
    inst_valid[i] = false;
    inst_r_1[i] = {};
    inst_valid_1[i] = false;
  }

  memcpy(arch_RAT_1, arch_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(spec_RAT_1, spec_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(spec_RAT_mispred, spec_RAT,
         (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));

  memcpy(spec_alloc_mispred, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_flush, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_normal, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_1, spec_alloc, PRF_NUM);

  memcpy(free_vec_mispred, free_vec, PRF_NUM);
  memcpy(free_vec_flush, free_vec, PRF_NUM);
  memcpy(free_vec_normal, free_vec, PRF_NUM);
  memcpy(free_vec_1, free_vec, PRF_NUM);

  std::memset(RAT_checkpoint, 0, sizeof(RAT_checkpoint));
  std::memset(RAT_checkpoint_1, 0, sizeof(RAT_checkpoint_1));
  std::memset(alloc_checkpoint, 0, sizeof(alloc_checkpoint));
  std::memset(alloc_checkpoint_1, 0, sizeof(alloc_checkpoint_1));
}

void Ren::comb_alloc() {
  // 可用寄存器个数 每周期最多使用DECODE_WIDTH个
  wire<PRF_IDX_WIDTH> alloc_reg[DECODE_WIDTH];
  wire<1> alloc_valid[DECODE_WIDTH] = {false};
  int alloc_num = 0;

  for (int i = 0; i < PRF_NUM && alloc_num < DECODE_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_reg[alloc_num] = i;
      alloc_valid[alloc_num] = true;
      alloc_num++;
    }
  }

  // stall相当于需要查看前一条指令是否stall
  // 一条指令stall，后面的也stall
  wire<1> stall = false;
  for (int i = 0; i < DECODE_WIDTH; i++) {
    out.ren2dis->uop[i] = RenDisIO::RenDisInst::from_dec_ren_inst(inst_r[i]);
    out.ren2dis->uop[i].dest_preg = alloc_reg[i];
    // 分配寄存器
    if (inst_valid[i] && inst_r[i].dest_en && !stall) {
      out.ren2dis->valid[i] = alloc_valid[i];
      stall = !alloc_valid[i];
    } else if (inst_valid[i] && !inst_r[i].dest_en) {
      out.ren2dis->valid[i] = !stall;
    } else {
      out.ren2dis->valid[i] = false;
    }
  }

#ifdef CONFIG_PERF_COUNTER
  if (stall) {
    ctx->perf.ren_reg_stall++;
    ctx->perf.stall_preg_cycles++;
  }
#endif
}

void Ren::comb_rename() {

  wire<PRF_IDX_WIDTH> src1_preg_normal[DECODE_WIDTH];
  wire<PRF_IDX_WIDTH> src1_preg_bypass[DECODE_WIDTH];
  wire<1> src1_bypass_hit[DECODE_WIDTH];

  wire<PRF_IDX_WIDTH> src2_preg_normal[DECODE_WIDTH];
  wire<PRF_IDX_WIDTH> src2_preg_bypass[DECODE_WIDTH];
  wire<1> src2_bypass_hit[DECODE_WIDTH];

  wire<PRF_IDX_WIDTH> old_dest_preg_normal[DECODE_WIDTH];
  wire<PRF_IDX_WIDTH> old_dest_preg_bypass[DECODE_WIDTH];
  wire<1> old_dest_bypass_hit[DECODE_WIDTH];

  // 无waw raw的输出 读spec_RAT
  for (int i = 0; i < DECODE_WIDTH; i++) {
    old_dest_preg_normal[i] = spec_RAT[inst_r[i].dest_areg];
    src1_preg_normal[i] = spec_RAT[inst_r[i].src1_areg];
    src2_preg_normal[i] = spec_RAT[inst_r[i].src2_areg];
  }

  // 针对RAT的raw bypass
  src1_bypass_hit[0] = false;
  src2_bypass_hit[0] = false;
  old_dest_bypass_hit[0] = false;
  for (int i = 1; i < DECODE_WIDTH; i++) {
    src1_bypass_hit[i] = false;
    src2_bypass_hit[i] = false;
    old_dest_bypass_hit[i] = false;

    // bypass选择最近的 3从012中选 2从01中选 1从0中选
    for (int j = 0; j < i; j++) {
      if (!inst_valid[j] || !inst_r[j].dest_en)
        continue;

      // Do not bypass from x0 writes (architectural x0 is always 0)
      if (inst_r[j].dest_areg == 0)
        continue;

      if (inst_r[i].src1_areg == inst_r[j].dest_areg) {
        src1_bypass_hit[i] = true;
        src1_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }

      if (inst_r[i].src2_areg == inst_r[j].dest_areg) {
        src2_bypass_hit[i] = true;
        src2_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }

      if (inst_r[i].dest_areg == inst_r[j].dest_areg) {
        old_dest_bypass_hit[i] = true;
        old_dest_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }
    }
  }

  // 重命名 (Rename)
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (src1_bypass_hit[i]) {
      out.ren2dis->uop[i].src1_preg = src1_preg_bypass[i];
    } else {
      out.ren2dis->uop[i].src1_preg = src1_preg_normal[i];
    }

    if (src2_bypass_hit[i]) {
      out.ren2dis->uop[i].src2_preg = src2_preg_bypass[i];
    } else {
      out.ren2dis->uop[i].src2_preg = src2_preg_normal[i];
    }

    if (old_dest_bypass_hit[i]) {
      out.ren2dis->uop[i].old_dest_preg = old_dest_preg_bypass[i];
    } else {
      out.ren2dis->uop[i].old_dest_preg = old_dest_preg_normal[i];
    }

  }
}

void Ren::comb_fire() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    fire[i] = out.ren2dis->valid[i] && in.dis2ren->ready;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (fire[i] && out.ren2dis->uop[i].dest_en) {
      int dest_preg = out.ren2dis->uop[i].dest_preg;
      spec_alloc_normal[dest_preg] = true;
      free_vec_normal[dest_preg] = false;
      spec_RAT_normal[inst_r[i].dest_areg] = dest_preg;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 保存检查点 (Checkpoint)
    if (fire[i] && is_branch(inst_r[i].type)) {
      const auto br_id = inst_r[i].br_id;
      for (int j = 0; j < ARF_NUM + 1; j++) {
        // 注意这里存在隐藏的旁路 (Bypass)
        // 保存的是本条指令完成后的 spec_RAT，不包括同一周期后续指令对 spec_RAT
        // 的影响
        RAT_checkpoint_1[br_id][j] = spec_RAT_normal[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[br_id][j] = false;
      }
    }
  }

  out.ren2dec->ready = true;
  for (int i = 0; i < DECODE_WIDTH; i++) {
    out.ren2dec->ready &= fire[i] || !inst_valid[i];
  }
}

// 误预测和刷新 (Flush) 不会同时发生
void Ren::comb_branch() {
  // 分支处理
  if (in.dec_bcast
          ->mispred) { // 硬件永远都会生成相关的误预测和刷新信号，然后进行选择
                       // 模拟器进行判断是为了减少不必要的开销，运行得快一点
    const auto br_idx = in.dec_bcast->br_id;
    Assert(br_idx != 0 && "Ren: mispred br_id should not be zero");
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_mispred[i] = RAT_checkpoint[br_idx][i];
    }

    // 恢复空闲列表 (Free List)
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_mispred[j] = free_vec[j] || alloc_checkpoint[br_idx][j];
      spec_alloc_mispred[j] =
          spec_alloc[j] && !alloc_checkpoint[br_idx][j];
    }
  }
}

void Ren ::comb_flush() {
  if (in.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_flush[i] = arch_RAT_1[i];
    }

    // 恢复空闲列表 (Free List)
    for (int j = 0; j < PRF_NUM; j++) {
      // 使用free_vec_normal  当前周期提交的指令释放的寄存器(例如CSRR)要考虑
      free_vec_flush[j] = free_vec_normal[j] || spec_alloc_normal[j];
      spec_alloc_flush[j] = false;
    }
  }
}

void Ren ::comb_commit() {
  // 提交指令修改RAT
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid) {
      const auto &commit_uop = in.rob_commit->commit_entry[i].uop;
      ctx->perf.commit_num++;
      if (is_load(commit_uop)) {
        ctx->perf.commit_load_num++;
      }
      if (is_store(commit_uop)) {
        ctx->perf.commit_store_num++;
      }

      if (ctx->is_ckpt) {
        if (!ctx->perf.perf_start &&
            ctx->perf.commit_num >= ctx->ckpt_warmup_commit_target) {
          ctx->perf.perf_reset();
          ctx->perf.perf_start = true;
        }

        if (ctx->perf.perf_start &&
            ctx->perf.commit_num >= SIMPOINT_INTERVAL) {
          ctx->exit_reason = ExitReason::SIMPOINT;
        }
      }

      InstEntry commit_entry = commit_uop.to_inst_entry(
          in.rob_commit->commit_entry[i].valid);
      InstInfo *inst = &commit_entry.uop;

      // free_vec_normal在异常指令提交时对应位不会置为true，不会释放dest_areg的原有映射的寄存器
      // spec_alloc_normal在异常指令提交时对应位不会置为false，这样该指令的dest_preg才能正确在free_vec中被回收
      // 异常指令要看上去没有执行一样
      if (inst->dest_en) {
        if (!is_exception(*inst) && !in.rob_bcast->interrupt) {
          free_vec_normal[inst->old_dest_preg] = true;
          spec_alloc_normal[inst->dest_preg] = false;
        }
      }

      BE_LOG("ROB commit PC=0x%08x Inst=0x%08x idx=%ld",
             inst->dbg.pc, inst->dbg.instruction, inst->dbg.inst_idx);
      if (inst->dest_en && !is_exception(*inst) && !in.rob_bcast->interrupt) {
        arch_RAT_1[inst->dest_areg] = inst->dest_preg;
      }
      ctx->run_commit_inst(&commit_entry);
#ifdef CONFIG_DIFFTEST
      ctx->run_difftest_inst(&commit_entry);
#endif
    }
  }
}

void Ren ::comb_pipeline() {
  wire<BR_MASK_WIDTH> clear_mask = in.dec_bcast->clear_mask;
  if (in.rob_bcast->flush || in.dec_bcast->mispred) {
#ifdef CONFIG_PERF_COUNTER
    uint64_t killed = 0;
    for (int i = 0; i < DECODE_WIDTH; i++) {
      if (inst_valid[i]) {
        killed++;
      }
    }
    if (killed > 0) {
      if (in.rob_bcast->flush) {
        ctx->perf.squash_flush_ren += killed;
        ctx->perf.squash_flush_total += killed;
        ctx->perf.pending_squash_flush_slots += killed;
      } else {
        ctx->perf.squash_mispred_ren += killed;
        ctx->perf.squash_mispred_total += killed;
        ctx->perf.pending_squash_mispred_slots += killed;
      }
    }
#endif
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (in.rob_bcast->flush || in.dec_bcast->mispred) {
      inst_valid_1[i] = false;
    } else if (out.ren2dec->ready) {
      inst_valid_1[i] = in.dec2ren->valid[i];
      inst_r_1[i] = in.dec2ren->uop[i];
      inst_r_1[i].br_mask &= ~clear_mask;
    } else {
      inst_valid_1[i] = inst_valid[i] && !fire[i];
      inst_r_1[i] = inst_r[i];
    }
  }

  // Ren 阶段清理：对保留在流水寄存器中的存活条目同步清除已解析分支 bit。
  if (clear_mask) {
    for (int i = 0; i < DECODE_WIDTH; i++) {
      if (inst_valid_1[i]) {
        inst_r_1[i].br_mask &= ~clear_mask;
      }
    }
  }

  if (in.rob_bcast->flush) {
    memcpy(spec_alloc_1, spec_alloc_flush, PRF_NUM);
    memcpy(free_vec_1, free_vec_flush, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_flush,
           (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  } else if (in.dec_bcast->mispred) {
    memcpy(spec_alloc_1, spec_alloc_mispred, PRF_NUM);
    memcpy(free_vec_1, free_vec_mispred, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_mispred,
           (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  } else {
    memcpy(spec_alloc_1, spec_alloc_normal, PRF_NUM);
    memcpy(free_vec_1, free_vec_normal, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_normal,
           (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  }
}

void Ren ::seq() {

  memcpy(inst_r, inst_r_1, DECODE_WIDTH * sizeof(DecRenIO::DecRenInst));
  memcpy(inst_valid, inst_valid_1, DECODE_WIDTH * sizeof(reg<1>));
  memcpy(spec_RAT, spec_RAT_1, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(arch_RAT, arch_RAT_1, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));

  memcpy(free_vec, free_vec_1, PRF_NUM);
  memcpy(spec_alloc, spec_alloc_1, PRF_NUM);

  memcpy(RAT_checkpoint, RAT_checkpoint_1,
         MAX_BR_NUM * (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(alloc_checkpoint, alloc_checkpoint_1,
         MAX_BR_NUM * PRF_NUM * sizeof(reg<1>));

  memcpy(spec_alloc_normal, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_mispred, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_flush, spec_alloc, PRF_NUM); //

  memcpy(free_vec_normal, free_vec, PRF_NUM);
  // memcpy(free_vec_mispred, free_vec, PRF_NUM);
  // memcpy(free_vec_flush, free_vec, PRF_NUM);

  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  // memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) *
  // sizeof(reg<PRF_IDX_WIDTH>)); memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM +
  // 1) * sizeof(reg<PRF_IDX_WIDTH>));
}
