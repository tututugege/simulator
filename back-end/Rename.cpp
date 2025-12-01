#include "TOP.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <cvt.h>
#include <iterator>
#include <util.h>

// 多个comb复用的中间信号
static wire1_t fire[FETCH_WIDTH];
static wire1_t spec_alloc_flush[PRF_NUM];
static wire1_t spec_alloc_mispred[PRF_NUM];
static wire1_t spec_alloc_normal[PRF_NUM];
static wire1_t free_vec_flush[PRF_NUM];
static wire1_t free_vec_mispred[PRF_NUM];
static wire1_t free_vec_normal[PRF_NUM];
static wire7_t spec_RAT_flush[ARF_NUM + 1];
static wire7_t spec_RAT_mispred[ARF_NUM + 1];
static wire7_t spec_RAT_normal[ARF_NUM + 1];

static wire1_t busy_table_awake[PRF_NUM];

// difftest
extern Back_Top back;
const int ALLOC_NUM =
    PRF_NUM / FETCH_WIDTH; // 分配寄存器时将preg分成FETCH_WIDTH个部分

// for Difftest va2pa_fixed() debug
int ren_commit_idx;

Rename::Rename() {
  for (int i = 0; i < PRF_NUM; i++) {
    spec_alloc[i] = false;

    // 初始化的时候平均分到free_vec的四个部分
    if (i < ARF_NUM) {
      spec_RAT[i] = (i % FETCH_WIDTH) * ALLOC_NUM + i / FETCH_WIDTH;
      arch_RAT[i] = (i % FETCH_WIDTH) * ALLOC_NUM + i / FETCH_WIDTH;
      free_vec[(i % FETCH_WIDTH) * ALLOC_NUM + i / FETCH_WIDTH] = false;
    } else {
      free_vec[(i % FETCH_WIDTH) * ALLOC_NUM + i / FETCH_WIDTH] = true;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i].valid = false;
  }

  memcpy(arch_RAT_1, arch_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(spec_RAT_1, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));

  memcpy(spec_alloc_mispred, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_flush, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_normal, spec_alloc, PRF_NUM);
  memcpy(spec_alloc_1, spec_alloc, PRF_NUM);

  memcpy(free_vec_mispred, free_vec, PRF_NUM);
  memcpy(free_vec_flush, free_vec, PRF_NUM);
  memcpy(free_vec_normal, free_vec, PRF_NUM);
  memcpy(free_vec_1, free_vec, PRF_NUM);
}

void Rename::comb_alloc() {
  // 可用寄存器个数 每周期最多使用FETCH_WIDTH个
  wire7_t alloc_reg[FETCH_WIDTH];
  wire1_t alloc_valid[FETCH_WIDTH] = {false};

  // for (int i = 0; i < FETCH_WIDTH; i++) {
  //   for (int j = 0; j < ALLOC_NUM; j++) {
  //     if (free_vec[i * ALLOC_NUM + j]) {
  //       alloc_reg[i] = i * ALLOC_NUM + j;
  //       alloc_valid[i] = true;
  //       break;
  //     }
  //   }
  // }

  int alloc_num = 0;
  for (int i = 0; i < PRF_NUM && alloc_num < FETCH_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_valid[alloc_num] = true;
      alloc_reg[alloc_num] = i;
      alloc_num++;
    }
  }

  for (; alloc_num < FETCH_WIDTH; alloc_num++) {
    alloc_valid[alloc_num] = false;
    alloc_reg[alloc_num] = 0;
  }

  // for (int i = 0; i < FETCH_WIDTH; i++) {
  //   if (!alloc_valid[i]) {
  //     cout << sim_time << endl;
  //   }
  // }

  // stall相当于需要查看前一条指令是否stall
  // 一条指令stall，后面的也stall
  wire1_t stall = false;
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

  if (stall) {
    perf.ren_reg_stall++;
  }
}

void Rename::comb_wake() {
  // busy_table wake up
  memcpy(busy_table_awake, busy_table, sizeof(wire1_t) * PRF_NUM);

  if (in.prf_awake->wake.valid) {
    busy_table_awake[in.prf_awake->wake.preg] = false;
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (in.iss_awake->wake[i].valid) {
      busy_table_awake[in.iss_awake->wake[i].preg] = false;
    }
  }
}

void Rename::comb_rename() {

  wire7_t src1_preg_normal[FETCH_WIDTH];
  wire1_t src1_busy_normal[FETCH_WIDTH];
  wire7_t src1_preg_bypass[FETCH_WIDTH];
  wire1_t src1_bypass_hit[FETCH_WIDTH];

  wire7_t src2_preg_normal[FETCH_WIDTH];
  wire1_t src2_busy_normal[FETCH_WIDTH];
  wire7_t src2_preg_bypass[FETCH_WIDTH];
  wire1_t src2_bypass_hit[FETCH_WIDTH];

  wire7_t old_dest_preg_normal[FETCH_WIDTH];
  wire7_t old_dest_preg_bypass[FETCH_WIDTH];
  wire1_t old_dest_bypass_hit[FETCH_WIDTH];

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

  // 根据是否bypass选择normal or bypass
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (src1_bypass_hit[i]) {
      out.ren2dis->uop[i].src1_preg = src1_preg_bypass[i];
      out.ren2dis->uop[i].src1_busy = true;
      // 这里相当于处理了同组写busy_table的bypass
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

  // 特殊处理 临时使用的32号寄存器提交时可以直接回收物理寄存器
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (out.ren2dis->uop[i].dest_areg == 32) {
      out.ren2dis->uop[i].old_dest_preg = out.ren2dis->uop[i].dest_preg;
    }
  }
}

void Rename::comb_fire() {
  memcpy(busy_table_awake, busy_table, sizeof(wire1_t) * PRF_NUM);

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

    // 保存checkpoint
    if (fire[i] && is_branch(inst_r[i].uop.type)) {
      for (int j = 0; j < ARF_NUM + 1; j++) {
        // 注意这里存在隐藏的旁路
        // 保存的是本条指令完成后的spec_RAT，不包括同一周期后续指令对spec_RAT的影响
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_normal[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }
  }

  out.ren2dec->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.ren2dec->ready &= fire[i] || !inst_r[i].valid;
  }
}

// mispred和flush不会同时发生
void Rename::comb_branch() {
  // 分支处理
  if (in.dec_bcast->mispred) { // 硬件永远都会生成xx_mispred和xx_flush，然后选择
                               // 模拟器判断一下为了不做无用功跑快点
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_mispred[i] = RAT_checkpoint[in.dec_bcast->br_tag][i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_mispred[j] =
          free_vec[j] || alloc_checkpoint[in.dec_bcast->br_tag][j];
      spec_alloc_mispred[j] =
          spec_alloc[j] && !alloc_checkpoint[in.dec_bcast->br_tag][j];
    }
  }
}

void Rename ::comb_flush() {
  if (in.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_flush[i] = arch_RAT_1[i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      // 使用free_vec_normal  当前周期提交的指令释放的寄存器(例如CSRR)要考虑
      free_vec_flush[j] = free_vec_normal[j] || spec_alloc_normal[j];
      spec_alloc_flush[j] = false;
    }
  }
}

void Rename ::comb_commit() {
  // 提交指令修改RAT
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.rob_commit->commit_entry[i].valid) {
      perf.commit_num++;
      Inst_uop *inst = &in.rob_commit->commit_entry[i].uop;
      if (inst->dest_en) {

        // free_vec_normal在异常指令提交时对应位不会置为true，不会释放dest_areg的原有映射的寄存器
        // spec_alloc_normal在异常指令提交时对应位不会置为false，这样该指令的dest_preg才能正确在free_vec中被回收
        // 异常指令要看上去没有执行一样
        if (!inst->page_fault_load && !in.rob_bcast->interrupt &&
            !in.rob_bcast->illegal_inst) {
          free_vec_normal[inst->old_dest_preg] = true;
          spec_alloc_normal[inst->dest_preg] = false;
        }
      }

      if (LOG) {
        cout << "ROB commit PC 0x" << hex << inst->pc << " idx "
             << inst->inst_idx << endl;
      }
      ren_commit_idx = i;
      if (inst->dest_en && !inst->page_fault_load && !in.rob_bcast->interrupt) {
        arch_RAT_1[inst->dest_areg] = inst->dest_preg;
      }
      back.difftest_inst(inst);
    }
  }

#ifdef CONFIG_DIFFTEST

  // back.difftest_cycle();
#endif
}

void Rename ::comb_pipeline() {
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
    memcpy(spec_alloc_1, spec_alloc_flush, PRF_NUM);
    memcpy(free_vec_1, free_vec_flush, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_flush, (ARF_NUM + 1) * sizeof(reg7_t));
  } else if (in.dec_bcast->mispred) {
    memcpy(spec_alloc_1, spec_alloc_mispred, PRF_NUM);
    memcpy(free_vec_1, free_vec_mispred, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_mispred, (ARF_NUM + 1) * sizeof(reg7_t));
  } else {
    memcpy(spec_alloc_1, spec_alloc_normal, PRF_NUM);
    memcpy(free_vec_1, free_vec_normal, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_normal, (ARF_NUM + 1) * sizeof(reg7_t));
  }
}

void Rename ::seq() {

  memcpy(inst_r, inst_r_1, FETCH_WIDTH * sizeof(Inst_entry));
  memcpy(spec_RAT, spec_RAT_1, (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(arch_RAT, arch_RAT_1, (ARF_NUM + 1) * sizeof(reg7_t));

  memcpy(free_vec, free_vec_1, PRF_NUM);
  memcpy(busy_table, busy_table_1, PRF_NUM);
  memcpy(spec_alloc, spec_alloc_1, PRF_NUM);

  memcpy(RAT_checkpoint, RAT_checkpoint_1,
         MAX_BR_NUM * (ARF_NUM + 1) * sizeof(reg7_t));
  memcpy(alloc_checkpoint, alloc_checkpoint_1,
         MAX_BR_NUM * PRF_NUM * sizeof(reg1_t));

  memcpy(spec_alloc_normal, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_mispred, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_flush, spec_alloc, PRF_NUM); //

  memcpy(free_vec_normal, free_vec, PRF_NUM);
  // memcpy(free_vec_mispred, free_vec, PRF_NUM);
  // memcpy(free_vec_flush, free_vec, PRF_NUM);

  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  // memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  // memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));

  // 监控是否产生寄存器泄露
  // 每次flush时 free_vec_num应该等于 PRF_NUM - ARF_NUM
  // static int count = 0;
  // count++;
  // int num = 0;
  // if (count % 10000 == 0) {
  // for (int i = 0; i < PRF_NUM; i++) {
  //   if (free_vec[i])
  //     num++;
  // }
  // cout << num << endl;
  // }

  // if (in.rob_bcast->flush) {
  //   count++;
  //   if (count % 100 == 0) {
  //     int free_vec_num = 0;
  //     for (int i = 0; i < PRF_NUM; i++) {
  //       if (free_vec[i])
  //         free_vec_num++;
  //     }
  //
  //     assert(free_vec_num == PRF_NUM - ARF_NUM);
  // cout << "free_vec num: " << hex << free_vec_num << endl;
  // }
  // }
}
