#include "TOP.h"
#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cstring>
#include <cvt.h>
#include <util.h>

int reg_count[32];
int freq_reg[FREQ_REG_NUM];
void update_freq_reg();

extern Back_Top back;
extern int commit_num;
extern int commit_uop_num;

void alu(Inst_uop &inst);

int ren_stall_reg = 0;
int ren_stall_csr = 0;

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

  for (int i = 0; i < DECODE_WIDTH; i++) {
    inst_r[i].valid = false;
  }
}

int read_br_conf(uint32_t pc);

void Rename::comb_alloc() {
  // valid初始化为0
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2rob->valid[i] = false;
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
    io.ren2rob->uop[i] = inst_r[i].uop;

    if (is_branch(inst_r[i].uop.op)) {
      io.ren2rob->uop[i].br_conf = read_br_conf(inst_r[i].uop.pc);
    }

    if (inst_r[i].valid) {
      // 分配stq_idx 和 rob_idx
      if (is_sta(io.ren2rob->uop[i].op) || is_std(io.ren2rob->uop[i].op)) {
        io.ren2rob->uop[i].stq_idx = stq_idx;
        if (is_std(io.ren2rob->uop[i].op)) {
          LOOP_INC(stq_idx, STQ_NUM);
        }
      }

      io.ren2rob->uop[i].rob_idx = rob_idx;
      LOOP_INC(rob_idx, ROB_NUM);
    }

    if (inst_r[i].valid && inst_r[i].uop.dest_en && !stall) {
      // 分配寄存器
      if (alloc_num < num) {
        io.ren2rob->valid[i] = true;
        io.ren2rob->uop[i].dest_preg = alloc_reg[alloc_num];
        alloc_num++;
      } else {
        stall = true;
      }
    } else if (inst_r[i].valid && !inst_r[i].uop.dest_en) {
      io.ren2rob->valid[i] = !stall;
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->valid[i] = inst_r[i].valid;
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
    io.ren2rob->uop[i].old_dest_preg = spec_RAT[inst_r[i].uop.dest_areg];
    io.ren2rob->uop[i].src1_preg = spec_RAT[inst_r[i].uop.src1_areg];
    io.ren2rob->uop[i].src2_preg = spec_RAT[inst_r[i].uop.src2_areg];
    // 唤醒的bypass
    io.ren2rob->uop[i].src1_busy =
        busy_table_1[io.ren2rob->uop[i].src1_preg] && inst_r[i].uop.src1_en;
    io.ren2rob->uop[i].src2_busy =
        busy_table_1[io.ren2rob->uop[i].src2_preg] && inst_r[i].uop.src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < DECODE_WIDTH; i++) {
    for (int j = 0; j < i; j++) {
      if (!inst_r[j].valid || !inst_r[j].uop.dest_en)
        continue;

      // 拆分的指令 有的不需要前递
      if (inst_r[i].uop.src1_areg == inst_r[j].uop.dest_areg) {

        if (!((io.ren2rob->uop[i].op == JUMP ||
               io.ren2rob->uop[i].op == STA &&
                   io.ren2rob->uop[i].amoop != AMONONE &&
                   io.ren2rob->uop[i].amoop != SC) &&
              j == i - 1)) {
          io.ren2rob->uop[i].src1_preg = io.ren2rob->uop[j].dest_preg;

          if (io.ren2rob->uop[j].vp_valid)
            io.ren2rob->uop[i].src1_busy = false;
          else
            io.ren2rob->uop[i].src1_busy = true;
        }
      }

      if (inst_r[i].uop.src2_areg == inst_r[j].uop.dest_areg) {
        if (!(io.ren2rob->uop[i].op == STD &&
              io.ren2rob->uop[i].amoop != AMONONE &&
              io.ren2rob->uop[i].amoop != SC && j == i - 2)) {
          io.ren2rob->uop[i].src2_preg = io.ren2rob->uop[j].dest_preg;
          if (io.ren2rob->uop[j].vp_valid)
            io.ren2rob->uop[i].src2_busy = false;
          else
            io.ren2rob->uop[i].src2_busy = true;
        }
      }

      if (inst_r[i].uop.dest_areg == inst_r[j].uop.dest_areg) {
        io.ren2rob->uop[i].old_dest_preg = io.ren2rob->uop[j].dest_preg;
      }
    }

    if (io.ren2rob->uop[i].dest_areg == 32) {
      io.ren2rob->uop[i].old_dest_preg = io.ren2rob->uop[i].dest_preg;
    }
  }

  // 特殊处理 临时使用的32号寄存器提交时可以直接回收物理寄存器
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.ren2rob->uop[i].dest_areg == 32) {
      io.ren2rob->uop[i].old_dest_preg = io.ren2rob->uop[i].dest_preg;
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->uop[i] = io.ren2rob->uop[i];
    io.ren2stq->tag[i] = io.ren2rob->uop[i].tag;
    io.ren2stq->valid[i] = inst_r[i].valid && (is_std(io.ren2rob->uop[i].op) ||
                                               is_sta(io.ren2rob->uop[i].op));
    io.ren2stq->is_std[i] = is_std(io.ren2rob->uop[i].op);
  }
}

void Rename::comb_fire() {
  // 分配寄存器
  bool pre_stall = false;
  bool pre_fire = false; // csr指令需要等前面的指令都发射执行完毕
  bool csr_stall = false; // csr指令后面的指令都要阻塞

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2rob->dis_fire[i] =
        (io.ren2rob->uop[i].vp_valid ||
         io.ren2iss->valid[i] && io.iss2ren->ready[i]) &&
        (!is_std(io.ren2iss->uop[i].op) && !is_sta(io.ren2iss->uop[i].op) ||
         io.ren2stq->valid[i] && io.stq2ren->ready[i]) &&
        (io.ren2rob->valid[i] && io.rob2ren->ready[i]) && !pre_stall &&
        !io.dec_bcast->mispred && !io.rob_bcast->flush && !csr_stall &&
        (!is_CSR(io.ren2iss->uop[i].op) || io.rob2ren->empty && !pre_fire) &&
        !io.rob2ren->stall;

    pre_stall = inst_r[i].valid && !io.ren2rob->dis_fire[i];
    pre_fire = io.ren2rob->dis_fire[i];

    // 异常相关指令需要单独执行
    if (io.ren2rob->valid[i] && is_CSR(io.ren2iss->uop[i].op)) {
      csr_stall = true;
    }
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->dis_fire[i] = io.ren2rob->dis_fire[i] && io.ren2iss->valid[i];
    io.ren2stq->dis_fire[i] = io.ren2rob->dis_fire[i];

    if (io.ren2rob->dis_fire[i] && io.ren2rob->uop[i].dest_en) {
      int dest_preg = io.ren2rob->uop[i].dest_preg;
      spec_alloc_1[dest_preg] = true;
      free_vec_1[dest_preg] = false;
      spec_RAT_1[inst_r[i].uop.dest_areg] = dest_preg;
      if (io.ren2rob->uop[i].vp_valid)
        busy_table_1[dest_preg] = false;
      else
        busy_table_1[dest_preg] = true;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 保存checkpoint
    if (io.ren2rob->dis_fire[i] && is_branch(inst_r[i].uop.op)) {
      for (int j = 0; j < ARF_NUM + 1; j++) {
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_1[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }
  }

  io.ren2dec->ready = true;
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2dec->ready &= io.ren2rob->dis_fire[i] || !inst_r[i].valid;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2prf->valid[i] = io.ren2rob->dis_fire[i] && inst_r[i].uop.vp_valid &&
                           inst_r[i].uop.dest_en;
  }

  // for (int i = 0; i < DECODE_WIDTH; i++) {
  //   if (io.ren2rob->dis_fire[i]) {
  //     if (io.ren2rob->uop[i].src1_en && io.ren2rob->uop[i].src1_areg != 0)
  //       reg_count[io.ren2rob->uop[i].src1_areg]++;
  //     if (io.ren2rob->uop[i].src2_en && io.ren2rob->uop[i].src2_areg != 0)
  //       reg_count[io.ren2rob->uop[i].src2_areg]++;
  //     if (io.ren2rob->uop[i].dest_en)
  //       reg_count[io.ren2rob->uop[i].dest_areg]++;
  //   }
  // }
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

    // update_freq_reg();
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

    // update_freq_reg();
  }
}

void Rename ::comb_vp_exec() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.vp_valid) {
      io.ren2iss->valid[i] = false;
      io.ren2prf->dest_preg[i] = io.ren2rob->uop[i].dest_preg;
      alu(inst_r[i].uop);
      io.ren2prf->reg_wdata[i] = inst_r[i].uop.result;
      io.ren2rob->uop[i].result = inst_r[i].uop.result;
    } else {
      io.ren2prf->valid[i] = false;
    }
  }
}

void Rename ::comb_commit() {
  // 提交指令修改RAT
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.rob_commit->commit_entry[i].valid) {

      Inst_uop *uop = &io.rob_commit->commit_entry[i].uop;
      if (uop->src1_en && uop->src1_areg != 0)
        reg_count[uop->src1_areg]++;
      if (uop->src2_en && uop->src2_areg != 0)
        reg_count[uop->src2_areg]++;
      if (uop->dest_en && uop->dest_areg != 0)
        reg_count[uop->dest_areg]++;

      if (io.rob_commit->commit_entry[i].uop.dest_en &&
          !io.rob_commit->commit_entry[i].uop.page_fault_load &&
          !(io.rob_commit->commit_entry[i].uop.vp_valid &&
            io.rob_commit->commit_entry[i].uop.vp_mispred)) {
        free_vec_1[io.rob_commit->commit_entry[i].uop.old_dest_preg] = true;
        spec_alloc_1[io.rob_commit->commit_entry[i].uop.dest_preg] = false;
      }
      commit_uop_num++;
      if (io.rob_commit->commit_entry[i].uop.is_last_uop)
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
    if (io.rob_bcast->flush || io.dec_bcast->mispred) {
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

  static int time = 0;
  if (time % 100 == 0) {
    update_freq_reg();
  }
  time++;

  for (int i = 0; i < DECODE_WIDTH; i++) {
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
}

void update_freq_reg() {
  for (int i = 0; i < FREQ_REG_NUM; i++) {
    int max = 0;
    int max_idx = 0;

    for (int j = 0; j < 32; j++) {
      if (max < reg_count[j]) {
        max = reg_count[j];
        max_idx = j;
      }
    }

    freq_reg[i] = max_idx;
    reg_count[max_idx] = 0;
  }

  for (int i = 0; i < 32; i++) {
    reg_count[i] = 0;
  }
  // for (int i = 0; i < FREQ_REG_NUM; i++) {
  //   freq_reg[i] = 10 + i;
  // }

  // for (int i = 0; i < FREQ_REG_NUM; i++) {
  //   cout << freq_reg[i] << " ";
  // }
  // cout << endl;
}
