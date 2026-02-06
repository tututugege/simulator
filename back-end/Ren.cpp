#include <Ren.h>
#include <SimCpu.h>
#include <config.h>
#include <cstdlib>
#include <cstring>

#include <util.h>

// 多个comb复用的中间信号
static wire<1> fire[FETCH_WIDTH];
static wire<1> spec_alloc_flush[Prf_NUM];
static wire<1> spec_alloc_mispred[Prf_NUM];
static wire<1> spec_alloc_normal[Prf_NUM];
static wire<1> free_vec_flush[Prf_NUM];
static wire<1> free_vec_mispred[Prf_NUM];
static wire<1> free_vec_normal[Prf_NUM];
static wire<7> spec_RAT_flush[ARF_NUM + 1];
static wire<7> spec_RAT_mispred[ARF_NUM + 1];
static wire<7> spec_RAT_normal[ARF_NUM + 1];
static wire<1> busy_table_awake[Prf_NUM];

int ren_commit_idx;

void Ren::init() {
  for (int i = 0; i < Prf_NUM; i++) {
    spec_alloc[i] = false;

    if (i < ARF_NUM + 1) {
      spec_RAT[i] = i;
      arch_RAT[i] = i;
      free_vec[i] = false;
    } else {
      free_vec[i] = true;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i].valid = false;
  }

  memcpy(arch_RAT_1, arch_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(spec_RAT_1, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));

  memcpy(spec_alloc_mispred, spec_alloc, Prf_NUM);
  memcpy(spec_alloc_flush, spec_alloc, Prf_NUM);
  memcpy(spec_alloc_normal, spec_alloc, Prf_NUM);
  memcpy(spec_alloc_1, spec_alloc, Prf_NUM);

  memcpy(free_vec_mispred, free_vec, Prf_NUM);
  memcpy(free_vec_flush, free_vec, Prf_NUM);
  memcpy(free_vec_normal, free_vec, Prf_NUM);
  memcpy(free_vec_1, free_vec, Prf_NUM);
}

void Ren::comb_alloc() {
  // 可用寄存器个数 每周期最多使用FETCH_WIDTH个
  wire<7> alloc_reg[FETCH_WIDTH];
  wire<1> alloc_valid[FETCH_WIDTH] = {false};
  int alloc_num = 0;

  for (int i = 0; i < Prf_NUM && alloc_num < FETCH_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_reg[alloc_num] = i;
      alloc_valid[alloc_num] = true;
      alloc_num++;
    }
  }

  // stall相当于需要查看前一条指令是否stall
  // 一条指令stall，后面的也stall
  wire<1> stall = false;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.ren2dis->uop[i] = inst_r[i].uop;
    out.ren2dis->uop[i].dest_preg = alloc_reg[i];
    // 分配寄存器
    if (inst_r[i].valid && inst_r[i].uop.dest_en && !stall) {
      out.ren2dis->valid[i] = alloc_valid[i];
      stall = !alloc_valid[i];
    } else if (inst_r[i].valid && !inst_r[i].uop.dest_en) {
      out.ren2dis->valid[i] = !stall;
    } else {
      out.ren2dis->valid[i] = false;
    }
  }

#ifdef CONFIG_PERF_COUNTER
  if (stall) {
    ctx->perf.ren_reg_stall++;
  }
#endif
}

void Ren::comb_wake() {
  // busy_table wake up
  if (in.prf_awake->wake.valid) {
    busy_table_awake[in.prf_awake->wake.preg] = false;
  }

  for (int i = 0; i < ALU_NUM + 2; i++) {
    if (in.iss_awake->wake[i].valid) {
      busy_table_awake[in.iss_awake->wake[i].preg] = false;
    }
  }
}

void Ren::comb_rename() {

  wire<7> src1_preg_normal[FETCH_WIDTH];
  wire<1> src1_busy_normal[FETCH_WIDTH];
  wire<7> src1_preg_bypass[FETCH_WIDTH];
  wire<1> src1_bypass_hit[FETCH_WIDTH];

  wire<7> src2_preg_normal[FETCH_WIDTH];
  wire<1> src2_busy_normal[FETCH_WIDTH];
  wire<7> src2_preg_bypass[FETCH_WIDTH];
  wire<1> src2_bypass_hit[FETCH_WIDTH];

  wire<7> old_dest_preg_normal[FETCH_WIDTH];
  wire<7> old_dest_preg_bypass[FETCH_WIDTH];
  wire<1> old_dest_bypass_hit[FETCH_WIDTH];

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < FETCH_WIDTH; i++) {
    old_dest_preg_normal[i] = spec_RAT[inst_r[i].uop.dest_areg];
    src1_preg_normal[i] = spec_RAT[inst_r[i].uop.src1_areg];
    src2_preg_normal[i] = spec_RAT[inst_r[i].uop.src2_areg];
    // 用busy_table_awake  存在隐藏的唤醒的bypass
    src1_busy_normal[i] = busy_table_awake[src1_preg_normal[i]];
    src2_busy_normal[i] = busy_table_awake[src2_preg_normal[i]];
  }

  // 针对RAT 和busy_table的raw的bypass
  src1_bypass_hit[0] = false;
  src2_bypass_hit[0] = false;
  old_dest_bypass_hit[0] = false;
  for (int i = 1; i < FETCH_WIDTH; i++) {
    src1_bypass_hit[i] = false;
    src2_bypass_hit[i] = false;
    old_dest_bypass_hit[i] = false;

    // bypass选择最近的 3从012中选 2从01中选 1从0中选
    for (int j = 0; j < i; j++) {
      if (!inst_r[j].valid || !inst_r[j].uop.dest_en)
        continue;

      // Do not bypass from x0 writes (architectural x0 is always 0)
      if (inst_r[j].uop.dest_areg == 0)
        continue;

      if (inst_r[i].uop.src1_areg == inst_r[j].uop.dest_areg) {
        src1_bypass_hit[i] = true;
        src1_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }

      if (inst_r[i].uop.src2_areg == inst_r[j].uop.dest_areg) {
        src2_bypass_hit[i] = true;
        src2_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }

      if (inst_r[i].uop.dest_areg == inst_r[j].uop.dest_areg) {
        old_dest_bypass_hit[i] = true;
        old_dest_preg_bypass[i] = out.ren2dis->uop[j].dest_preg;
      }
    }
  }

  // 重命名 (Rename)
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (src1_bypass_hit[i]) {
      out.ren2dis->uop[i].src1_preg = src1_preg_bypass[i];
      out.ren2dis->uop[i].src1_busy = true;
    } else {
      out.ren2dis->uop[i].src1_preg = src1_preg_normal[i];
      out.ren2dis->uop[i].src1_busy = src1_busy_normal[i];
    }

    if (src2_bypass_hit[i]) {
      out.ren2dis->uop[i].src2_preg = src2_preg_bypass[i];
      out.ren2dis->uop[i].src2_busy = true;
    } else {
      out.ren2dis->uop[i].src2_preg = src2_preg_normal[i];
      out.ren2dis->uop[i].src2_busy = src2_busy_normal[i];
    }

    if (old_dest_bypass_hit[i]) {
      out.ren2dis->uop[i].old_dest_preg = old_dest_preg_bypass[i];
    } else {
      out.ren2dis->uop[i].old_dest_preg = old_dest_preg_normal[i];
    }
  }
}

void Ren::comb_fire() {
  memcpy(busy_table_1, busy_table_awake, sizeof(wire<1>) * Prf_NUM);
  for (int i = 0; i < FETCH_WIDTH; i++) {
    fire[i] = out.ren2dis->valid[i] && in.dis2ren->ready;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (fire[i] && out.ren2dis->uop[i].dest_en) {
      int dest_preg = out.ren2dis->uop[i].dest_preg;
      spec_alloc_normal[dest_preg] = true;
      free_vec_normal[dest_preg] = false;
      spec_RAT_normal[inst_r[i].uop.dest_areg] = dest_preg;
      busy_table_1[dest_preg] = true;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 保存检查点 (Checkpoint)
    if (fire[i] && is_branch(inst_r[i].uop.type)) {
      for (int j = 0; j < ARF_NUM + 1; j++) {
        // 注意这里存在隐藏的旁路 (Bypass)
        // 保存的是本条指令完成后的 spec_RAT，不包括同一周期后续指令对 spec_RAT
        // 的影响
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_normal[j];
      }

      for (int j = 0; j < Prf_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }
  }

  out.ren2dec->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.ren2dec->ready &= fire[i] || !inst_r[i].valid;
  }
}

// 误预测和刷新 (Flush) 不会同时发生
void Ren::comb_branch() {
  // 分支处理
  if (in.dec_bcast
          ->mispred) { // 硬件永远都会生成相关的误预测和刷新信号，然后进行选择
                       // 模拟器进行判断是为了减少不必要的开销，运行得快一点
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_mispred[i] = RAT_checkpoint[in.dec_bcast->br_tag][i];
    }

    // 恢复空闲列表 (Free List)
    for (int j = 0; j < Prf_NUM; j++) {
      free_vec_mispred[j] =
          free_vec[j] || alloc_checkpoint[in.dec_bcast->br_tag][j];
      spec_alloc_mispred[j] =
          spec_alloc[j] && !alloc_checkpoint[in.dec_bcast->br_tag][j];
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
    for (int j = 0; j < Prf_NUM; j++) {
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
      ctx->perf.commit_num++;

      if (ctx->is_ckpt) {
        if (ctx->perf.commit_num == WARMUP && !ctx->perf.perf_start) {
          ctx->perf.perf_reset();
          ctx->perf.perf_start = true;
        }

        if (ctx->perf.commit_num == SIMPOINT_INTERVAL && ctx->perf.perf_start) {
          ctx->perf.perf_print();
          ctx->sim_end = true;
        }
      }

      InstUop *inst = &in.rob_commit->commit_entry[i].uop;

      // free_vec_normal在异常指令提交时对应位不会置为true，不会释放dest_areg的原有映射的寄存器
      // spec_alloc_normal在异常指令提交时对应位不会置为false，这样该指令的dest_preg才能正确在free_vec中被回收
      // 异常指令要看上去没有执行一样
      if (inst->dest_en) {
        if (!is_exception(*inst) && !in.rob_bcast->interrupt) {
          free_vec_normal[inst->old_dest_preg] = true;
          spec_alloc_normal[inst->dest_preg] = false;
        }
      }

      if (LOG) {
        printf("ROB commit PC 0x%08x Inst:0x%08x rob_idx:%d idx %ld\n",
               inst->pc, inst->instruction, inst->rob_idx, inst->inst_idx);
      }
      ren_commit_idx = i;
      if (inst->dest_en && !is_exception(*inst) && !in.rob_bcast->interrupt) {
        arch_RAT_1[inst->dest_areg] = inst->dest_preg;
      }
      // cpu.back.difftest_inst(inst);
    }
  }

#ifdef CONFIG_DIFFTEST
  cpu.back.difftest_cycle();
#endif
}

void Ren ::comb_pipeline() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (in.rob_bcast->flush || in.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (out.ren2dec->ready) {
      inst_r_1[i].uop = in.dec2ren->uop[i];
      inst_r_1[i].valid = in.dec2ren->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !fire[i];
    }
  }

  if (in.rob_bcast->flush) {
    memcpy(spec_alloc_1, spec_alloc_flush, Prf_NUM);
    memcpy(free_vec_1, free_vec_flush, Prf_NUM);
    memcpy(spec_RAT_1, spec_RAT_flush, (ARF_NUM + 1) * sizeof(reg<7>));
  } else if (in.dec_bcast->mispred) {
    memcpy(spec_alloc_1, spec_alloc_mispred, Prf_NUM);
    memcpy(free_vec_1, free_vec_mispred, Prf_NUM);
    memcpy(spec_RAT_1, spec_RAT_mispred, (ARF_NUM + 1) * sizeof(reg<7>));
  } else {
    memcpy(spec_alloc_1, spec_alloc_normal, Prf_NUM);
    memcpy(free_vec_1, free_vec_normal, Prf_NUM);
    memcpy(spec_RAT_1, spec_RAT_normal, (ARF_NUM + 1) * sizeof(reg<7>));
  }
}

void Ren ::seq() {

  memcpy(inst_r, inst_r_1, FETCH_WIDTH * sizeof(InstEntry));
  memcpy(spec_RAT, spec_RAT_1, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(arch_RAT, arch_RAT_1, (ARF_NUM + 1) * sizeof(reg<7>));

  memcpy(free_vec, free_vec_1, Prf_NUM);
  memcpy(busy_table, busy_table_1, Prf_NUM);
  memcpy(spec_alloc, spec_alloc_1, Prf_NUM);

  memcpy(RAT_checkpoint, RAT_checkpoint_1,
         MAX_BR_NUM * (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(alloc_checkpoint, alloc_checkpoint_1,
         MAX_BR_NUM * Prf_NUM * sizeof(reg<1>));

  memcpy(spec_alloc_normal, spec_alloc, Prf_NUM);
  // memcpy(spec_alloc_mispred, spec_alloc, Prf_NUM);
  // memcpy(spec_alloc_flush, spec_alloc, Prf_NUM); //

  memcpy(free_vec_normal, free_vec, Prf_NUM);
  // memcpy(free_vec_mispred, free_vec, Prf_NUM);
  // memcpy(free_vec_flush, free_vec, Prf_NUM);

  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  // memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  // memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM + 1) * sizeof(reg<7>));
  memcpy(busy_table_awake, busy_table, Prf_NUM * sizeof(wire<1>));
}

RenIO Ren::get_hardware_io() {
  RenIO hardware;

  // --- Inputs ---
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.from_dec.valid[i] = in.dec2ren->valid[i];
    // Reconstruct DecRenUop from full InstUop
    hardware.from_dec.uop[i] = DecRenUop::filter(in.dec2ren->uop[i]);
  }
  hardware.from_dis.ready = in.dis2ren->ready;

  hardware.from_rob.flush = in.rob_bcast->flush;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    hardware.from_rob.commit_valid[i] = in.rob_commit->commit_entry[i].valid;
    hardware.from_rob.commit_areg[i] =
        in.rob_commit->commit_entry[i].uop.dest_areg;
    hardware.from_rob.commit_preg[i] =
        in.rob_commit->commit_entry[i].uop.dest_preg;
    hardware.from_rob.commit_dest_en[i] =
        in.rob_commit->commit_entry[i].uop.dest_en;
  }

  for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
    hardware.from_back.wake_valid[i] = in.iss_awake->wake[i].valid;
    hardware.from_back.wake_preg[i] = in.iss_awake->wake[i].preg;
  }

  // --- Outputs ---
  hardware.to_dec.ready = out.ren2dec->ready;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.to_dis.valid[i] = out.ren2dis->valid[i];
    hardware.to_dis.uop[i] = RenDisUop::filter(out.ren2dis->uop[i]);
  }

  return hardware;
}
