#include "TOP.h"
#include "frontend.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cvt.h>
#include <util.h>

extern Back_Top back;
extern int commit_num;

void Rename::init() {
  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = false;
    arch_RAT[i] = i;
    spec_RAT[i] = i;
    spec_alloc[i] = false;

    free_vec_1[i] = false;
    spec_RAT_1[i] = i;
    spec_alloc_1[i] = false;
  }

  for (int i = ARF_NUM; i < PRF_NUM; i++) {
    spec_alloc[i] = false;
    free_vec[i] = true;
    spec_alloc_1[i] = false;
    free_vec_1[i] = true;
  }
}

void Rename::comb_alloc() {
  // valid初始化为0
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->valid[i] = false;
  }

  // 可用寄存器个数 大于DECODE_WIDTH时为DECODE_WIDTH
  int alloc_reg[DECODE_WIDTH];
  int num = 0;
  for (int i = 0; i < PRF_NUM && num < DECODE_WIDTH; i++) {
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

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->uop[i] = inst_r[i].uop;

    if (inst_r[i].valid) {
      // 分配stq_idx 和 rob_idx
      if (is_store(io.ren2iss->uop[i].op) ||
          (io.ren2iss->uop[i].op == LOAD &&
           io.ren2iss->uop[i].amoop != AMONONE)) {
        io.ren2iss->uop[i].stq_idx = stq_idx;

        if (io.ren2iss->uop[i].op == STORE) {
          LOOP_INC(stq_idx, STQ_NUM);
        }
      }

      io.ren2iss->uop[i].rob_idx = rob_idx;
      LOOP_INC(rob_idx, ROB_NUM);
    }

    if (inst_r[i].valid && inst_r[i].uop.dest_en && !stall) {
      // 分配寄存器
      if (alloc_num < num) {
        io.ren2iss->valid[i] = true;
        io.ren2iss->uop[i].dest_preg = alloc_reg[alloc_num];
        alloc_num++;
      } else {
        stall = true;
      }
    } else if (inst_r[i].valid && !inst_r[i].uop.dest_en) {
      io.ren2iss->valid[i] = !stall;
    }
  }
}

void Rename::comb_wake() {
  // busy_table wake up
  if (io.awake->wake.valid) {
    busy_table_1[io.awake->wake.preg] = false;
  }

  // TODO: Magic Number
  for (int i = 0; i < ALU_NUM; i++) {
    if (io.iss2ren->wake[i].valid) {
      busy_table_1[io.iss2ren->wake[i].preg] = false;
    }
  }
}

void Rename::comb_rename() {

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->uop[i].old_dest_preg = spec_RAT[inst_r[i].uop.dest_areg];
    io.ren2iss->uop[i].src1_preg = spec_RAT[inst_r[i].uop.src1_areg];
    io.ren2iss->uop[i].src2_preg = spec_RAT[inst_r[i].uop.src2_areg];
    // 唤醒的bypass
    io.ren2iss->uop[i].src1_busy =
        busy_table_1[io.ren2iss->uop[i].src1_preg] && inst_r[i].uop.src1_en;
    io.ren2iss->uop[i].src2_busy =
        busy_table_1[io.ren2iss->uop[i].src2_preg] && inst_r[i].uop.src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < DECODE_WIDTH; i++) {
    for (int j = 0; j < i; j++) {
      if (!inst_r[j].valid || !inst_r[j].uop.dest_en)
        continue;

      if (inst_r[i].uop.src1_areg == inst_r[j].uop.dest_areg) {
        if (!((io.ren2iss->uop[i].op == JUMP ||
               io.ren2iss->uop[i].op == STORE &&
                   io.ren2iss->uop[i].amoop != AMONONE &&
                   io.ren2iss->uop[i].amoop != SC) &&
              j == i - 1)) {
          io.ren2iss->uop[i].src1_preg = io.ren2iss->uop[j].dest_preg;
          io.ren2iss->uop[i].src1_busy = true;
        }
      }

      if (inst_r[i].uop.src2_areg == inst_r[j].uop.dest_areg) {
        if (!(io.ren2iss->uop[i].op == STORE &&
              io.ren2iss->uop[i].amoop != AMONONE &&
              io.ren2iss->uop[i].amoop != SC && j == i - 1)) {
          io.ren2iss->uop[i].src2_preg = io.ren2iss->uop[j].dest_preg;
          io.ren2iss->uop[i].src2_busy = true;
        }
      }

      if (inst_r[i].uop.dest_areg == inst_r[j].uop.dest_areg) {
        io.ren2iss->uop[i].old_dest_preg = io.ren2iss->uop[j].dest_preg;
      }
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2rob->uop[i] = io.ren2iss->uop[i];
    io.ren2rob->valid[i] = inst_r[i].valid;

    io.ren2stq->tag[i] = io.ren2iss->uop[i].tag;
    io.ren2stq->valid[i] = inst_r[i].valid && is_store(io.ren2iss->uop[i].op);
  }
}

void Rename::comb_store() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->valid[i] && is_load(io.ren2iss->uop[i].op)) {
      for (int j = 0; j < STQ_NUM; j++) {
        io.ren2iss->uop[i].pre_store[j] = io.stq2ren->stq_valid[j];
      }

      // 同一组store 和load
      for (int j = 0; j < i; j++) {
        if (io.ren2iss->valid[j] && is_store(io.ren2iss->uop[j].op)) {
          int idx = io.ren2iss->uop[j].stq_idx;
          io.ren2iss->uop[i].pre_store[idx] = true;
        }
      }
    }
  }
}

void Rename::comb_fire() {
  // 分配寄存器
  bool pre_stall = false;
  bool pre_fire = false; // csr指令需要等前面的指令都发射执行完毕
  bool csr_stall = false; // csr指令后面的指令都要阻塞

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->dis_fire[i] =
        io.ren2iss->valid[i] && io.iss2ren->ready[i] &&
        (!is_store(io.ren2iss->uop[i].op) ||
         io.ren2stq->valid[i] && io.stq2ren->ready[i]) &&
        (io.ren2rob->valid[i] && io.rob2ren->ready[i]) && !pre_stall &&
        !io.dec_bcast->mispred && !io.rob_bc->flush && !csr_stall &&
        (!is_CSR(io.ren2iss->uop[i].op) || io.rob2ren->empty && !pre_fire) &&
        !io.rob2ren->stall;

    pre_stall = inst_r[i].valid && !io.ren2iss->dis_fire[i];
    pre_fire = io.ren2iss->dis_fire[i];

    // 异常相关指令需要单独执行
    if (io.ren2rob->valid[i] && is_CSR(io.ren2iss->uop[i].op)) {
      csr_stall = true;
    }
  }

  // split的指令需要同时dispatch
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2iss->valid[i] && !io.ren2iss->uop[i].is_last_uop) {
      if (io.ren2iss->dis_fire[i] && !io.ren2iss->dis_fire[i + 1]) {
        for (int j = i; j < DECODE_WIDTH; j++) {
          io.ren2iss->dis_fire[j] = false;
        }
        break;
      }
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2rob->dis_fire[i] = io.ren2iss->dis_fire[i];
    io.ren2stq->dis_fire[i] = io.ren2iss->dis_fire[i];

    if (io.ren2iss->dis_fire[i] && io.ren2iss->uop[i].dest_en) {
      int dest_preg = io.ren2iss->uop[i].dest_preg;
      spec_alloc_1[dest_preg] = true;
      free_vec_1[dest_preg] = false;
      busy_table_1[dest_preg] = true;
      spec_RAT_1[inst_r[i].uop.dest_areg] = dest_preg;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 保存checkpoint
    if (io.ren2iss->dis_fire[i] && is_branch(inst_r[i].uop.op)) {
      for (int j = 0; j < ARF_NUM; j++) {
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_1[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }
  }

  io.ren2dec->ready = true;
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2dec->ready &= io.ren2iss->dis_fire[i] || !inst_r[i].valid;
  }

  /*for (int i = 0; i < DECODE_WIDTH; i++) {*/
  /*  if (io.ren2iss->valid[i] && !io.ren2iss->uop[i].is_last_uop) {*/
  /*    if ((io.ren2iss->dis_fire[i] && !io.ren2iss->dis_fire[i + 1]))*/
  /*      cout << "error" << endl;*/
  /*  }*/
  /*}*/
}

void Rename::comb_branch() {
  // 分支处理
  if (io.dec_bcast->mispred && !io.rob_bc->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
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
  if (io.rob_bc->flush) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
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
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.rob_bc->flush || io.dec_bcast->mispred) {
      inst_r_1[i].valid = false;
    } else if (io.ren2dec->ready) {
      inst_r_1[i].uop = io.dec2ren->uop[i];
      inst_r_1[i].valid = io.dec2ren->valid[i];
    } else {
      inst_r_1[i].valid = inst_r[i].valid && !io.ren2rob->dis_fire[i];
    }
  }
}

void Rename ::seq() {

  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }

  for (int i = 0; i < ARF_NUM; i++) {
    spec_RAT[i] = spec_RAT_1[i];
  }

  for (int i = 0; i < PRF_NUM; i++) {
    free_vec[i] = free_vec_1[i];
    busy_table[i] = busy_table_1[i];
    spec_alloc[i] = spec_alloc_1[i];
  }

  for (int i = 0; i < MAX_BR_NUM; i++) {
    for (int j = 0; j < ARF_NUM; j++) {
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
