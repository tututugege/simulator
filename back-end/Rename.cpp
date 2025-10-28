#include "TOP.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <cvt.h>
#include <util.h>

extern Back_Top back;
extern int commit_num;

void alu(Inst_uop &inst);

Rename::Rename() {
  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = false;
    arch_RAT[i] = i;
    spec_RAT[i] = i;
    spec_alloc[i] = false;

    free_vec_1[i] = false;
    spec_RAT_1[i] = i;
    spec_alloc_1[i] = false;
  }

  for (int i = ARF_NUM + 1; i < PRF_NUM; i++) {
    spec_alloc[i] = false;
    free_vec[i] = true;
    spec_alloc_1[i] = false;
    free_vec_1[i] = true;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i].valid = false;
  }
}

void Rename::comb_alloc() {
  // 可用寄存器个数 每周期最多使用FETCH_WIDTH个
  int alloc_reg[FETCH_WIDTH];
  int alloc_valid[FETCH_WIDTH];
  int alloc_num = 0;
  for (int i = 0; i < PRF_NUM && alloc_num < FETCH_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_reg[alloc_num] = i;
      alloc_num++;
    }
  }

  for (int i = alloc_num; i < FETCH_WIDTH; i++) {
    alloc_valid[i] = false;
  }

  // 有效且需要寄存器的指令，寄存器不够则对应端口ready为false
  bool stall = false;

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
  bool pre_stall = false;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dis->fire[i] = (io.ren2dis->valid[i] && io.dis2ren->ready) &&
                          !pre_stall && !io.dec_bcast->mispred &&
                          !io.rob_bcast->flush;
    pre_stall = inst_r[i].valid && !io.ren2dis->fire[i];
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2dis->fire[i] && io.ren2dis->uop[i].dest_en) {
      int dest_preg = io.ren2dis->uop[i].dest_preg;
      spec_alloc_1[dest_preg] = true;
      free_vec_1[dest_preg] = false;
      spec_RAT_1[inst_r[i].uop.dest_areg] = dest_preg;
      busy_table_1[dest_preg] = true;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 保存checkpoint
    if (io.ren2dis->fire[i] && is_branch(inst_r[i].uop.type)) {
      for (int j = 0; j < ARF_NUM + 1; j++) {
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_1[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }
  }

  io.ren2dec->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dec->ready &= io.ren2dis->fire[i] || !inst_r[i].valid;
  }
}

void Rename::comb_branch() {
  // 分支处理
  if (io.dec_bcast->mispred && !io.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_1[i] = RAT_checkpoint[io.dec_bcast->br_tag][i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_1[j] = free_vec[j] || alloc_checkpoint[io.dec_bcast->br_tag][j];
      spec_alloc_1[j] =
          spec_alloc[j] && !alloc_checkpoint[io.dec_bcast->br_tag][j];
    }
  }
}

void Rename ::comb_flush() {
  if (io.rob_bcast->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM + 1; i++) {
      spec_RAT_1[i] = arch_RAT[i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_1[j] = free_vec_1[j] || spec_alloc_1[j];
      spec_alloc_1[j] = false;
    }
  }
}

void Rename ::comb_commit() {
  // 提交指令修改RAT
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.rob_commit->commit_entry[i].valid) {
      if (io.rob_commit->commit_entry[i].uop.dest_en &&
          !io.rob_commit->commit_entry[i].uop.page_fault_load) {
        free_vec_1[io.rob_commit->commit_entry[i].uop.old_dest_preg] = true;
        spec_alloc_1[io.rob_commit->commit_entry[i].uop.dest_preg] = false;
      }
      commit_num++;
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
      inst_r_1[i].valid = inst_r[i].valid && !io.ren2dis->fire[i];
    }
  }
}

void Rename ::seq() {

  for (int i = 0; i < FETCH_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }

  for (int i = 0; i < ARF_NUM + 1; i++) {
    spec_RAT[i] = spec_RAT_1[i];
  }

  for (int i = 0; i < PRF_NUM; i++) {
    free_vec[i] = free_vec_1[i];
    busy_table[i] = busy_table_1[i];
    spec_alloc[i] = spec_alloc_1[i];
  }

  for (int i = 0; i < MAX_BR_NUM; i++) {
    for (int j = 0; j < ARF_NUM + 1; j++) {
      RAT_checkpoint[i][j] = RAT_checkpoint_1[i][j];
    }

    for (int j = 0; j < PRF_NUM; j++) {
      alloc_checkpoint[i][j] = alloc_checkpoint_1[i][j];
    }
  }

  /*static int count = 0;*/
  /**/
  /*count++;*/
  /*if (count % 10000 == 0) {*/
  /*  for (int i = 0; i < PRF_NUM; i++) {*/
  /*    cout << free_vec[i];*/

  /*if (i % 32 == 0)*/
  /*  cout << endl;*/
  /*}*/
  /*cout << endl;*/
  /*}*/
}
