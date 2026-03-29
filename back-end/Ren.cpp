#include "Ren.h"
#include "config.h"
#include <cstdlib>
#include <cstring>

#include "util.h"

// 多个comb复用的中间信号
static wire<1> fire[DECODE_WIDTH];
static wire<PRF_IDX_WIDTH> alloc_reg[DECODE_WIDTH];

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
  memcpy(spec_alloc_1, spec_alloc, PRF_NUM);

  memcpy(free_vec_1, free_vec, PRF_NUM);

}

/*
 * comb_begin
 * 功能: 组合阶段开始时，将所有时序状态复制到 *_1 工作副本。
 * 输入依赖: inst_r/inst_valid, spec_RAT, arch_RAT, free_vec, spec_alloc, RAT_checkpoint, alloc_checkpoint。
 * 输出更新: inst_r_1/inst_valid_1, spec_RAT_1, arch_RAT_1, free_vec_1, spec_alloc_1, RAT_checkpoint_1, alloc_checkpoint_1。
 * 约束: 仅做状态镜像，不进行分配/回收决策，不驱动握手输出。
 */
void Ren::comb_begin() {
  memcpy(inst_r_1, inst_r, DECODE_WIDTH * sizeof(DecRenIO::DecRenInst));
  memcpy(inst_valid_1, inst_valid, DECODE_WIDTH * sizeof(reg<1>));
  memcpy(spec_RAT_1, spec_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(arch_RAT_1, arch_RAT, (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(free_vec_1, free_vec, PRF_NUM);
  memcpy(spec_alloc_1, spec_alloc, PRF_NUM);
  memcpy(RAT_checkpoint_1, RAT_checkpoint,
         MAX_BR_NUM * (ARF_NUM + 1) * sizeof(reg<PRF_IDX_WIDTH>));
  memcpy(alloc_checkpoint_1, alloc_checkpoint, MAX_BR_NUM * PRF_NUM);
}

/*
 * comb_alloc
 * 功能: 从 free_vec 预选目的物理寄存器，并生成 ren2dis.valid 的资源可用性结果。
 * 输入依赖: free_vec, inst_valid, inst_r[].dest_en。
 * 输出更新: alloc_reg, out.ren2dis->valid[]（以及性能计数器相关统计）。
 * 约束: 顺序分配且一旦前序指令因寄存器不足 stall，后续槽位同拍全部阻塞。
 */
void Ren::comb_alloc() {
  // 可用寄存器个数 每周期最多使用DECODE_WIDTH个
  wire<1> alloc_valid[DECODE_WIDTH] = {false};
  int alloc_num = 0;

  for (int i = 0; i < PRF_NUM && alloc_num < DECODE_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_reg[alloc_num] = i;
      alloc_valid[alloc_num] = true;
      alloc_num++;
    }
  }
  for (int i = alloc_num; i < DECODE_WIDTH; i++) {
    alloc_reg[i] = 0;
  }

  // stall相当于需要查看前一条指令是否stall
  // 一条指令stall，后面的也stall
  wire<1> stall = false;
  for (int i = 0; i < DECODE_WIDTH; i++) {
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

/*
 * comb_rename
 * 功能: 基于 spec_RAT 完成源/目的寄存器重命名，并处理同拍 RAW/WAW 旁路。
 * 输入依赖: inst_r/inst_valid, spec_RAT, alloc_reg, out.ren2dis->uop[j].dest_preg（同拍前序旁路）。
 * 输出更新: out.ren2dis->uop[] 的 src1_preg/src2_preg/old_dest_preg/dest_preg。
 * 约束: 旁路仅来自同拍更早槽位；x0 写入不参与旁路；旁路优先于 spec_RAT 常规查表结果。
 */
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
    out.ren2dis->uop[i] = RenDisIO::RenDisInst::from_dec_ren_inst(inst_r[i]);
    out.ren2dis->uop[i].dest_preg = alloc_reg[i];
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

/*
 * comb_fire
 * 功能: 在 ren->dis 握手成功时提交重命名状态更新，并处理 commit/flush/mispred 恢复。
 * 输入依赖: out.ren2dis->valid, in.dis2ren->ready, inst_r, in.rob_commit, in.rob_bcast, in.dec_bcast, arch/spec RAT 与 checkpoint 状态。
 * 输出更新: spec_RAT_1, arch_RAT_1, free_vec_1, spec_alloc_1, RAT_checkpoint_1, alloc_checkpoint_1, out.ren2dec->ready。
 * 约束: flush 优先于 mispred；mispred 从 checkpoint 恢复；flush 从 arch_RAT 恢复；分支 fire 时保存 checkpoint。
 */
void Ren::comb_fire() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    fire[i] = out.ren2dis->valid[i] && in.dis2ren->ready;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (fire[i] && out.ren2dis->uop[i].dest_en) {
      int dest_preg = out.ren2dis->uop[i].dest_preg;
      spec_alloc_1[dest_preg] = true;
      free_vec_1[dest_preg] = false;
      spec_RAT_1[inst_r[i].dest_areg] = dest_preg;
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
        RAT_checkpoint_1[br_id][j] = spec_RAT_1[j];
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
  if (in.rob_bcast->flush || in.dec_bcast->mispred) {
    out.ren2dec->ready = false;
  }

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
          free_vec_1[inst->old_dest_preg] = true;
          spec_alloc_1[inst->dest_preg] = false;
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

  // 在 comb_fire 直接决定下一拍 freelist/RAT 状态。
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_1[i] = arch_RAT_1[i];
    }
    for (int j = 0; j < PRF_NUM; j++) {
      // 本拍 fire/commit 对 _1 的更新也应被 flush 恢复逻辑看到。
      const wire<1> was_spec = spec_alloc_1[j];
      free_vec_1[j] = free_vec_1[j] || was_spec;
      spec_alloc_1[j] = false;
    }
  } else if (in.dec_bcast->mispred) { // flush/mispred 不会同时发生
    const auto br_idx = in.dec_bcast->br_id;
    Assert(br_idx != 0 && "Ren: mispred br_id should not be zero");
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_1[i] = RAT_checkpoint[br_idx][i];
    }
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_1[j] = free_vec[j] || alloc_checkpoint[br_idx][j];
      spec_alloc_1[j] = spec_alloc[j] && !alloc_checkpoint[br_idx][j];
    }
  }
}

/*
 * comb_pipeline
 * 功能: 推进 Rename 级流水寄存器，处理 flush/mispred 清空与 clear_mask 清理。
 * 输入依赖: in.dec2ren, out.ren2dec->ready, fire[], in.rob_bcast->flush, in.dec_bcast->{mispred, clear_mask}, inst_valid/inst_r。
 * 输出更新: inst_valid_1[], inst_r_1[]。
 * 约束: flush/mispred 时本级全部无效；ready=1 时从 dec2ren 采样新输入；保留项需清除已解析分支 bit。
 */
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



}
