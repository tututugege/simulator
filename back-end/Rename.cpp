#include "TOP.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <cvt.h>
#include <util.h>

extern Back_Top back;

const int ALLOC_NUM =
    PRF_NUM / FETCH_WIDTH; // 分配寄存器时将preg分成FETCH_WIDTH个部分
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
  wire1_t alloc_valid[FETCH_WIDTH];

  for (int i = 0; i < FETCH_WIDTH; i++) {
    alloc_valid[i] = false;
    for (int j = 0; j < ALLOC_NUM; j++) {
      if (free_vec[i * ALLOC_NUM + j]) {
        alloc_reg[i] = i * ALLOC_NUM + j;
        alloc_valid[i] = true;
        break;
      }
    }
  }

  // stall相当于需要查看前一条指令是否stall
  // 一条指令stall，后面的也stall
  wire1_t stall = false;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dis->uop[i] = inst_r[i].uop;
    // 分配寄存器
    if (inst_r[i].valid && inst_r[i].uop.dest_en && !stall) {
      io.ren2dis->valid[i] = alloc_valid[i];
      io.ren2dis->uop[i].dest_preg = alloc_reg[i];
      stall = !alloc_valid[i];
    } else if (inst_r[i].valid && !inst_r[i].uop.dest_en) {
      io.ren2dis->valid[i] = !stall;
    } else {
      io.ren2dis->valid[i] = false;
    }
  }
}

void Rename::comb_wake() {
  // busy_table wake up
  if (io.prf_awake->wake.valid) {
    busy_table_1[io.prf_awake->wake.preg] = false;
  }

  for (int i = 0; i < ALU_NUM; i++) {
    if (io.iss_awake->wake[i].valid) {
      busy_table_1[io.iss_awake->wake[i].preg] = false;
    }
  }
}

void Rename::comb_rename() {

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dis->uop[i].old_dest_preg = spec_RAT[inst_r[i].uop.dest_areg];
    io.ren2dis->uop[i].src1_preg = spec_RAT[inst_r[i].uop.src1_areg];
    io.ren2dis->uop[i].src2_preg = spec_RAT[inst_r[i].uop.src2_areg];
    // 唤醒的bypass
    io.ren2dis->uop[i].src1_busy =
        busy_table_1[io.ren2dis->uop[i].src1_preg] && inst_r[i].uop.src1_en;
    io.ren2dis->uop[i].src2_busy =
        busy_table_1[io.ren2dis->uop[i].src2_preg] && inst_r[i].uop.src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < FETCH_WIDTH; i++) {
    for (int j = 0; j < i; j++) {
      if (!inst_r[j].valid || !inst_r[j].uop.dest_en)
        continue;

      if (inst_r[i].uop.src1_areg == inst_r[j].uop.dest_areg) {
        io.ren2dis->uop[i].src1_preg = io.ren2dis->uop[j].dest_preg;
        io.ren2dis->uop[i].src1_busy = true;
      }

      if (inst_r[i].uop.src2_areg == inst_r[j].uop.dest_areg) {
        io.ren2dis->uop[i].src2_preg = io.ren2dis->uop[j].dest_preg;
        io.ren2dis->uop[i].src2_busy = true;
      }

      if (inst_r[i].uop.dest_areg == inst_r[j].uop.dest_areg) {
        io.ren2dis->uop[i].old_dest_preg = io.ren2dis->uop[j].dest_preg;
      }
    }

    if (io.ren2dis->uop[i].dest_areg == 32) {
      io.ren2dis->uop[i].old_dest_preg = io.ren2dis->uop[i].dest_preg;
    }
  }

  // 特殊处理 临时使用的32号寄存器提交时可以直接回收物理寄存器
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2dis->uop[i].dest_areg == 32) {
      io.ren2dis->uop[i].old_dest_preg = io.ren2dis->uop[i].dest_preg;
    }
  }
}

void Rename::comb_fire() {
  // 分配寄存器
  for (int i = 0; i < FETCH_WIDTH; i++) {
    fire[i] = io.ren2dis->valid[i] && io.dis2ren->ready;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (fire[i] && io.ren2dis->uop[i].dest_en) {
      int dest_preg = io.ren2dis->uop[i].dest_preg;
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

  io.ren2dec->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dec->ready &= fire[i] || !inst_r[i].valid;
  }
}

void Rename::comb_branch() {
  // 分支处理
  if (io.dec_bcast->mispred && !io.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_mispred[i] = RAT_checkpoint[io.dec_bcast->br_tag][i];
    }

    // 恢复free_list
    // mispred和flush不会同时发生，可以不用考虑free_vec_1，直接用free_vec恢复
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_mispred[j] =
          free_vec[j] || alloc_checkpoint[io.dec_bcast->br_tag][j];
      spec_alloc_mispred[j] =
          spec_alloc[j] && !alloc_checkpoint[io.dec_bcast->br_tag][j];
    }
  }
}

void Rename ::comb_flush() {
  if (io.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_flush[i] = arch_RAT[i];
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
    if (io.rob_commit->commit_entry[i].valid) {
      perf.commit_num++;
      if (io.rob_commit->commit_entry[i].uop.dest_en) {

        // free_vec_1在异常指令提交时对应位不会置为true，不会释放dest_areg的原有映射的寄存器
        // spec_alloc_1在异常指令提交时对应位不会置为false，这样该指令的dest_preg才能正确在free_vec中被回收
        // 异常指令要看上去没有执行一样
        if (!io.rob_commit->commit_entry[i].uop.page_fault_load &&
            !io.rob_bcast->interrupt && !io.rob_bcast->illegal_inst) {
          free_vec_normal[io.rob_commit->commit_entry[i].uop.old_dest_preg] =
              true;
          spec_alloc_normal[io.rob_commit->commit_entry[i].uop.dest_preg] =
              false;
        }
      }

      if (LOG) {
        cout << "ROB commit PC 0x" << hex
             << io.rob_commit->commit_entry[i].uop.pc << " idx "
             << io.rob_commit->commit_entry[i].uop.inst_idx << endl;
      }
      back.difftest(&(io.rob_commit->commit_entry[i].uop));
    }
  }
}

void Rename ::comb_pipeline() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.rob_bcast->flush || io.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (io.ren2dec->ready) {
      inst_r_1[i].uop = io.dec2ren->uop[i];
      inst_r_1[i].valid = io.dec2ren->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !fire[i];
    }
  }

  if (io.rob_bcast->flush) {
    memcpy(spec_alloc_1, spec_alloc_flush, PRF_NUM);
    memcpy(free_vec_1, free_vec_flush, PRF_NUM);
    memcpy(spec_RAT_1, spec_RAT_flush, (ARF_NUM + 1) * sizeof(reg7_t));
  } else if (io.dec_bcast->mispred) {
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

  memcpy(free_vec, free_vec_1, PRF_NUM);
  memcpy(busy_table, busy_table_1, PRF_NUM);
  memcpy(spec_alloc, spec_alloc_1, PRF_NUM);

  for (int i = 0; i < MAX_BR_NUM; i++) {
    for (int j = 0; j < ARF_NUM + 1; j++) {
      RAT_checkpoint[i][j] = RAT_checkpoint_1[i][j];
    }

    for (int j = 0; j < PRF_NUM; j++) {
      alloc_checkpoint[i][j] = alloc_checkpoint_1[i][j];
    }
  }

  memcpy(spec_alloc_normal, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_mispred, spec_alloc, PRF_NUM);
  // memcpy(spec_alloc_flush, spec_alloc, PRF_NUM);

  memcpy(free_vec_normal, free_vec, PRF_NUM);
  // memcpy(free_vec_mispred, free_vec, PRF_NUM);
  // memcpy(free_vec_flush, free_vec, PRF_NUM);

  memcpy(spec_RAT_normal, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  // memcpy(spec_RAT_flush, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));
  // memcpy(spec_RAT_mispred, spec_RAT, (ARF_NUM + 1) * sizeof(reg7_t));

  // 监控是否产生寄存器泄露
  // if (sim_time % 10000000 == 0) {
  //   int free_vec_num = 0;
  //   for (int i = 0; i < PRF_NUM; i++) {
  //     if (free_vec[i])
  //       free_vec_num++;
  //   }
  //
  //   // assert(free_vec_num > 32);
  //   cout << "free_vec num: " << hex << free_vec_num << endl;
  // }
}
