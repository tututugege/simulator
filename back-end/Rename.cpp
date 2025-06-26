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

void Rename::default_val() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->valid[i] = false;
    io.ren2rob->valid[i] = false;
    io.ren2rob->uop[i] = io.ren2iss->uop[i] = inst_r[i].uop;
  }
  io.ren2dec->ready = true;
}

void Rename::comb_fire_forepart() {
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
  int  alloc_num = 0;
  bool preg_stall = false;
  int rob_idx = io.rob2ren->enq_idx;

  for (int i = 0; !preg_stall && i < DECODE_WIDTH; i++) {
    if (inst_r[i].valid) {
      // TODO:风格不统一，派遣至stq和rob风格可以保持一致
      io.ren2iss->uop[i].rob_idx = rob_idx;
      LOOP_INC(rob_idx, ROB_NUM);

      if (inst_r[i].uop.dest_en) {
        if (alloc_num < num) {
          // 分配新的物理寄存器
          io.ren2iss->valid[i] = true;
          io.ren2iss->uop[i].dest_preg = alloc_reg[alloc_num];
          alloc_num++;
          // 派遣至ROB请求
          io.ren2rob->valid[i] = inst_r[i].valid;
        }
        else {
          preg_stall = true;
        }
      }
      else {
        io.ren2iss->valid[i] = !preg_stall;
      }

      if (!preg_stall) {
        if (is_load(inst_r[i].uop.op))
          io.ren2ldq->valid[i] = true;
        else if (is_store(inst_r[i].uop.op))
          io.ren2stq->valid[i]   = true;
      }
    }
  }
}

void Rename::comb_fire_backpart() {
  bool pre_stall = false;
  bool pre_fire = false; // csr指令需要等前面的指令都发射执行完毕
  bool csr_stall = false; // csr指令后面的指令都要阻塞

  for (int i = 0; i < DECODE_WIDTH; i++) {
    io.ren2iss->dis_fire[i] =
        io.ren2iss->valid[i] && io.iss2ren->ready[i] &&
        (!is_store(io.ren2iss->uop[i].op) ||
         io.ren2stq->valid[i] && io.stq2ren->ready[i]) &&
        (!is_load(io.ren2iss->uop[i].op) ||
         io.ren2ldq->valid[i] && io.ldq2ren->ready[i]) && 
        (io.ren2rob->valid[i] && io.rob2ren->ready[i]) && !pre_stall &&
        !io.dec_bcast->mispred && !csr_stall &&
        (!is_CSR(io.ren2iss->uop[i].op) || io.rob2ren->empty && !pre_fire) &&
        !io.rob2ren->stall;

    io.ren2stq->dis_fire[i] = io.ren2ldq->dis_fire[i] = io.ren2rob->dis_fire[i] = io.ren2iss->dis_fire[i];
    pre_stall = inst_r[i].valid && !io.ren2iss->dis_fire[i];
    pre_fire = io.ren2iss->dis_fire[i];

    // 异常相关指令需要单独执行
    if (io.ren2rob->valid[i] && is_CSR(io.ren2iss->uop[i].op)) {
      csr_stall = true;
    }

    if (io.ren2iss->dis_fire[i] && io.ren2iss->uop[i].dest_en) {
      int dest_preg = io.ren2iss->uop[i].dest_preg;
      // 重命名
      io.ren2iss->uop[i].old_dest_preg = spec_RAT_1[inst_r[i].uop.dest_areg];
      io.ren2iss->uop[i].src1_preg     = spec_RAT_1[inst_r[i].uop.src1_areg];
      io.ren2iss->uop[i].src2_preg     = spec_RAT_1[inst_r[i].uop.src2_areg];
      spec_RAT_1[inst_r[i].uop.dest_areg] = dest_preg;
      // busy信息
      io.ren2iss->uop[i].src1_busy =
        busy_table_1[io.ren2iss->uop[i].src1_preg] && inst_r[i].uop.src1_en;
      io.ren2iss->uop[i].src2_busy =
        busy_table_1[io.ren2iss->uop[i].src2_preg] && inst_r[i].uop.src2_en;
      busy_table_1[dest_preg] = true;
      // 空闲链表信息
      free_vec_1[dest_preg] = false;
      // 推测执行分配的物理寄存器
      spec_alloc_1[dest_preg] = true;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][dest_preg] = true;
    }

    // 分支指令分配GC，初始化alloc_checkpoint
    if (io.ren2iss->dis_fire[i] && is_branch(inst_r[i].uop.op)) {
      for (int j = 0; j < ARF_NUM; j++) {
        RAT_checkpoint_1[inst_r[i].uop.tag][j] = spec_RAT_1[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[inst_r[i].uop.tag][j] = false;
      }
    }

    if (io.ren2iss->dis_fire[i] && is_load(inst_r[i].uop.op)) {
      // 派遣写入ldq的信息
      io.ren2ldq->mem_tag[i]     = inst_r[i].uop.mem_tag;
      io.ren2stq->mem_tag_bit[i] = inst_r[i].uop.mem_tag_bit;
      io.ren2ldq->mem_sz[i]      = ;
      io.ren2ldq->dst_reg[i]     = io.ren2iss->uop[i].dest_preg;
      io.ren2ldq->sign[i]        = ;

      // load指令发射前的pend位向量，保证前面的store全部发射
      for (int j = 0; j < STQ_NUM; j++) {
        io.ren2iss->st_issue_pend[j] = st_pend_1[j];
      }

      // load指令在ldq中的索引，写入ISU
      io.ren2iss->uop[i].ldq_idx = io.ldq2ren->ldq_idx[i];
    }

    // store指令修改pend位向量
    if (io.ren2iss->dis_fire[i] && is_store(inst_r[i].uop.op)) {
      st_pend_1[io.stq2ren->stq_idx] = true;

      // 派遣写入stq的信息
      io.ren2stq->mem_tag[i]     = inst_r[i].uop.mem_tag;
      io.ren2stq->mem_tag_bit[i] = inst_r[i].uop.mem_tag_bit;
      io.ren2stq->mem_sz[i]      = ;

      // store指令在ldq中的索引，写入ISU
      io.ren2iss->uop[i].stq_idx = io.stq2ren->stq_idx[i];
    }

    io.ren2dec->ready &= io.ren2iss->dis_fire[i] || !inst_r[i].valid;
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

void Rename::comb_st_issue() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.iss2ren->st_issue[i].valid) {
      st_pend_1[io.iss2ren->st_issue[i].stq_idx] = false;
    }
  }
}

void Rename::comb_branch() {
  // 分支处理
  if (io.dec_bcast->mispred) {
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

  if (io.rob_bc->rollback) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
      spec_RAT_1[i] = arch_RAT[i];
    }

    // 恢复free_list
    for (int j = 0; j < PRF_NUM; j++) {
      free_vec_1[j] = free_vec[j] || spec_alloc[j];
      spec_alloc_1[j] = false;
    }
  }
}

void Rename ::comb_commit() {
  // 提交指令修改RAT
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.rob_commit->commit_entry[i].valid) {
      if (io.rob_commit->commit_entry[i].uop.dest_en) {
        free_vec_1[io.rob_commit->commit_entry[i].uop.old_dest_preg] = true;
        spec_alloc_1[io.rob_commit->commit_entry[i].uop.dest_preg] = false;
      }
      commit_num++;
      if (LOG) {
        cout << "ROB commit PC 0x" << hex
             << io.rob_commit->commit_entry[i].uop.pc << " idx "
             << io.rob_commit->commit_entry[i].uop.inst_idx << endl;
      }
#ifdef CONFIG_DIFFTEST
      back.difftest(&(io.rob_commit->commit_entry[i].uop));
#endif // CONFIG_DIFFTSET
    }
  }
}

void Rename ::comb_pipeline() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    if (io.rob_bc->rollback || io.dec_bcast->mispred) {
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
}
