#include "TOP.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cvt.h>
#include <util.h>

extern Back_Top back;

void Rename::init() {
  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = false;
    arch_RAT[i] = i;
    spec_RAT[i] = i;
    spec_alloc[i] = false;
  }

  for (int i = ARF_NUM; i < PRF_NUM; i++) {
    spec_alloc[i] = false;
    free_vec[i] = true;
  }
}

void Rename::comb() {
  // 可用寄存器个数 大于INST_WAY时为INST_WAY
  int num = 0;
  for (int i = 0; i < PRF_NUM && num < INST_WAY; i++) {
    if (free_vec[i]) {
      alloc_reg[num] = i;
      num++;
    }
  }

  // 有效且需要寄存器的指令，寄存器不够则对应端口ready为false
  int alloc_num = 0;
  bool stall = false;
  for (int i = 0; i < INST_WAY; i++) {
    io.ren2iss->inst[i] = dec_ren_r[i].inst;
    if (dec_ren_r[i].valid && dec_ren_r[i].inst.dest_en && !stall) {
      // 分配寄存器
      if (alloc_num < num) {
        io.ren2iss->valid[i] = true;
        io.ren2rob->valid[i] = true;
        io.ren2dec->ready[i] = true;
        io.ren2iss->inst[i].dest_preg = alloc_reg[alloc_num];
        io.ren2rob->inst[i] = io.ren2iss->inst[i];
        alloc_num++;
      } else {
        stall = true;
        io.ren2iss->valid[i] = false;
        io.ren2rob->valid[i] = false;
        io.ren2dec->ready[i] = false;
      }
    } else if (!dec_ren_r[i].valid) {
      io.ren2dec->ready[i] = true;
      io.ren2rob->valid[i] = false;
      io.ren2iss->valid[i] = false;
    } else {
      io.ren2iss->valid[i] = !stall;
      io.ren2rob->valid[i] = !stall;
    }
  }

  // busy_table wake up
  for (int i = 0; i < ALU_NUM + 1; i++) {
    if (io.exe2ren->wake[i].valid) {
      busy_table[io.exe2ren->wake[i].preg] = false;
    }
  }

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < INST_WAY; i++) {
    io.ren2iss->inst[i].old_dest_preg = spec_RAT[dec_ren_r[i].inst.dest_areg];
    io.ren2iss->inst[i].src1_preg = spec_RAT[dec_ren_r[i].inst.src1_areg];
    io.ren2iss->inst[i].src2_preg = spec_RAT[dec_ren_r[i].inst.src2_areg];
    io.ren2iss->inst[i].src1_busy =
        busy_table[io.ren2iss->inst[i].src1_preg] && dec_ren_r[i].inst.src1_en;
    io.ren2iss->inst[i].src2_busy =
        busy_table[dec_ren_r[i].inst.src2_preg] && dec_ren_r[i].inst.src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < INST_WAY; i++) {
    for (int j = 0; j < i; j++) {
      if (!dec_ren_r[j].valid || !dec_ren_r[j].inst.dest_en)
        continue;

      if (dec_ren_r[i].inst.src1_areg == dec_ren_r[j].inst.dest_areg) {
        io.ren2iss->inst[i].src1_preg = io.ren2iss->inst[j].dest_preg;
        io.ren2iss->inst[i].src1_busy = true;
      }

      if (dec_ren_r[i].inst.src2_areg == dec_ren_r[j].inst.dest_areg) {
        io.ren2iss->inst[i].src2_preg = io.ren2iss->inst[j].dest_preg;
        io.ren2iss->inst[i].src2_busy = true;
      }

      if (dec_ren_r[i].inst.dest_areg == dec_ren_r[j].inst.dest_areg) {
        io.ren2iss->inst[i].old_dest_preg = io.ren2iss->inst[j].dest_preg;
      }
    }
  }
}

void Rename ::seq() {
  // 分配寄存器
  int alloc_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    io.ren2iss->dis_fire[i] = (io.ren2iss->valid[i] && io.iss2ren->ready[i]) &&
                              (io.ren2rob->valid[i] && io.rob2ren->ready[i]);
    io.ren2rob->dis_fire[i] = io.ren2iss->dis_fire[i];
    if (io.ren2iss->dis_fire[i] && io.ren2iss->inst[i].dest_en) {
      spec_alloc[alloc_reg[alloc_num]] = true;
      free_vec[alloc_reg[alloc_num]] = false;
      busy_table[alloc_reg[alloc_num]] = true;
      spec_RAT[dec_ren_r[i].inst.dest_areg] = alloc_reg[alloc_num];
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint[j][alloc_reg[alloc_num]] = true;

      alloc_num++;
    }

    // 保存checkpoint
    if (is_branch(dec_ren_r[i].inst.op) && io.ren2iss->dis_fire[i]) {
      for (int j = 0; j < ARF_NUM; j++) {
        RAT_checkpoint[dec_ren_r[i].inst.tag][j] = spec_RAT[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint[dec_ren_r[i].inst.tag][j] = false;
      }
    }
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.rob_commit->commit_entry[i].valid) {
      if (io.rob_commit->commit_entry[i].inst.dest_en) {
        free_vec[io.rob_commit->commit_entry[i].inst.old_dest_preg] = true;
        spec_alloc[io.rob_commit->commit_entry[i].inst.dest_preg] = false;
        back.difftest(&(io.rob_commit->commit_entry[i].inst));
      }
    }
  }

  // 分支处理
  if (io.exe_bc->mispred) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
      spec_RAT[i] = RAT_checkpoint[io.exe_bc->br_tag][i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec[j] = free_vec[j] || alloc_checkpoint[io.exe_bc->br_tag][j];
      spec_alloc[j] = spec_alloc[j] && !alloc_checkpoint[io.exe_bc->br_tag][j];
    }
  }

  if (io.rob_bc->rollback) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
      spec_RAT[i] = arch_RAT[i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec[j] = free_vec[j] || spec_alloc[j];
      spec_alloc[j] = false;
    }
  }

  bool ready = true;
  for (int i = 0; i < INST_WAY; i++) {
    ready = ready && io.ren2dec->ready[i];
  }

  for (int i = 0; i < INST_WAY; i++) {
    if (io.rob_bc->rollback) {
      dec_ren_r[i].valid = false;
    } else if (ready) {
      dec_ren_r[i].inst = io.dec2ren->inst[i];
      dec_ren_r[i].valid = io.dec2ren->valid[i];
    } else {
      dec_ren_r[i].valid = dec_ren_r[i].valid && !io.ren2dec->dec_fire[i];
    }
  }

  /*for (int i = 0; i < PRF_NUM; i++) {*/
  /*  cout << dec << free_vec[i];*/
  /*}*/
  /*cout << endl;*/
}
