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
  // 可用寄存器个数 大于FETCH_WIDTH时为FETCH_WIDTH
  int num = 0;
  for (int i = 0; i < PRF_NUM && num < FETCH_WIDTH; i++) {
    if (free_vec[i]) {
      alloc_reg[num] = i;
      num++;
    }
  }

  // 有效且需要寄存器的指令，寄存器不够则对应端口ready为false
  int alloc_num = 0;
  bool stall = false;
  int rob_idx = io.rob2ren->enq_idx;
  int stq_idx = io.stq2ren->stq_idx;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2iss->inst[i] = dec_ren_r[i].inst;

    if (dec_ren_r[i].valid) {
      // 分配stq_idx 和 rob_idx
      if (io.ren2iss->inst[i].op == STORE) {
        io.ren2iss->inst[i].stq_idx = stq_idx;
        LOOP_INC(stq_idx, STQ_NUM);
      }

      io.ren2iss->inst[i].rob_idx = rob_idx;
      LOOP_INC(rob_idx, ROB_NUM);
    }

    if (dec_ren_r[i].valid && dec_ren_r[i].inst.dest_en && !stall) {
      // 分配寄存器
      if (alloc_num < num) {
        io.ren2iss->valid[i] = true;
        io.ren2iss->inst[i].dest_preg = alloc_reg[alloc_num];
        alloc_num++;
      } else {
        stall = true;
        io.ren2iss->valid[i] = false;
      }
    } else if (!dec_ren_r[i].valid) {
      io.ren2iss->valid[i] = false;
    } else {
      io.ren2iss->valid[i] = !stall;
    }
  }

  // busy_table wake up
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.awake->wake[i].valid) {
      busy_table[io.awake->wake[i].preg] = false;
    }
  }

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2iss->inst[i].old_dest_preg = spec_RAT[dec_ren_r[i].inst.dest_areg];
    io.ren2iss->inst[i].src1_preg = spec_RAT[dec_ren_r[i].inst.src1_areg];
    io.ren2iss->inst[i].src2_preg = spec_RAT[dec_ren_r[i].inst.src2_areg];
    io.ren2iss->inst[i].src1_busy =
        busy_table[io.ren2iss->inst[i].src1_preg] && dec_ren_r[i].inst.src1_en;
    io.ren2iss->inst[i].src2_busy =
        busy_table[io.ren2iss->inst[i].src2_preg] && dec_ren_r[i].inst.src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < FETCH_WIDTH; i++) {
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

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2rob->inst[i] = io.ren2iss->inst[i];
    io.ren2rob->valid[i] = io.ren2iss->valid[i];

    io.ren2stq->tag[i] = io.ren2iss->inst[i].tag;
    io.ren2stq->valid[i] =
        io.ren2iss->valid[i] && io.ren2iss->inst[i].op == STORE;
  }
}

void Rename ::seq() {
  // 分配寄存器
  int alloc_num = 0;
  bool pre_stall = false;

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.ren2iss->valid[i] && io.ren2iss->inst[i].op == LOAD) {
      for (int j = 0; j < STQ_NUM; j++) {
        io.ren2iss->inst[i].pre_store[j] = io.stq2ren->stq_valid[j];
      }

      // 同一组store 和load
      for (int j = 0; j < i; j++) {
        if (io.ren2iss->valid[j] && io.ren2iss->inst[j].op == STORE) {
          int idx = io.ren2iss->inst[j].stq_idx;
          io.ren2iss->inst[i].pre_store[idx] = true;
        }
      }
    }

    io.ren2iss->dis_fire[i] = (io.ren2iss->valid[i] && io.iss2ren->ready[i]) &&
                              (io.ren2iss->inst[i].op != STORE ||
                               io.ren2stq->valid[i] && io.stq2ren->ready[i]) &&
                              (io.ren2rob->valid[i] && io.rob2ren->ready[i]) &&
                              !pre_stall && !io.id_bc->mispred;

    io.ren2rob->dis_fire[i] = io.ren2iss->dis_fire[i];
    io.ren2stq->dis_fire[i] = io.ren2iss->dis_fire[i];
    pre_stall = dec_ren_r[i].valid && !io.ren2iss->dis_fire[i];

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
      }
      if (LOG) {
        cout << "ROB commit PC 0x" << hex
             << io.rob_commit->commit_entry[i].inst.pc << " idx "
             << io.rob_commit->commit_entry[i].inst.inst_idx << endl;
      }
      back.difftest(&(io.rob_commit->commit_entry[i].inst));
    }
  }

  // 分支处理
  if (io.id_bc->mispred) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
      spec_RAT[i] = RAT_checkpoint[io.id_bc->br_tag][i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec[j] = free_vec[j] || alloc_checkpoint[io.id_bc->br_tag][j];
      spec_alloc[j] = spec_alloc[j] && !alloc_checkpoint[io.id_bc->br_tag][j];
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

  io.ren2dec->ready = true;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.ren2dec->ready &= io.ren2iss->dis_fire[i] || !dec_ren_r[i].valid;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (io.rob_bc->rollback || io.id_bc->mispred) {
      dec_ren_r[i].valid = false;
    } else if (io.ren2dec->ready) {
      dec_ren_r[i].inst = io.dec2ren->inst[i];
      dec_ren_r[i].valid = io.dec2ren->valid[i];
    } else {
      dec_ren_r[i].valid = dec_ren_r[i].valid && !io.ren2iss->dis_fire[i];
    }
  }
}
