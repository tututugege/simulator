#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cvt.h>

void Rename::init() {
  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = false;
    arch_RAT[i] = i;
    spec_RAT[i] = i;

    free_vec_1[i] = false;
    spec_RAT_1[i] = i;
  }

  for (int i = ARF_NUM; i < PRF_NUM; i++) {
    free_vec[i] = true;
    free_vec_1[i] = true;
  }
}

// valid
void Rename::comb_alloc() {
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
    out.inst[i] = in.inst[i];
    if (in.valid[i] && in.inst[i].dest_en && !stall) {
      // 分配寄存器
      if (alloc_num < num) {
        out.valid[i] = true;
        out.ready[i] = true;
        out.inst[i].dest_preg = alloc_reg[alloc_num];
        alloc_num++;
      } else {
        stall = true;
        out.valid[i] = false;
        out.ready[i] = false;
      }
    } else {
      out.valid[i] = in.valid[i] && !stall;
      out.ready[i] = !stall;
    }
  }

  // busy_table wake up
  for (int i = 0; i < ALU_NUM + 1; i++) {
    if (in.wake[i].valid) {
      busy_table_1[in.wake[i].preg] = false;
    }
  }

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < INST_WAY; i++) {
    out.inst[i].old_dest_preg = spec_RAT[in.inst[i].dest_areg];
    out.inst[i].src1_preg = spec_RAT[in.inst[i].src1_areg];
    out.inst[i].src2_preg = spec_RAT[in.inst[i].src2_areg];
    out.inst[i].src1_busy =
        busy_table_1[out.inst[i].src1_preg] && in.inst[i].src1_en;
    out.inst[i].src2_busy =
        busy_table_1[out.inst[i].src2_preg] && in.inst[i].src2_en;
  }

  // 针对RAT 和busy_table的raw的bypass
  for (int i = 1; i < INST_WAY; i++) {
    for (int j = 0; j < i; j++) {
      if (!in.valid[j] || !in.inst[j].dest_en)
        continue;

      if (in.inst[i].src1_areg == in.inst[j].dest_areg) {
        out.inst[i].src1_preg = out.inst[j].dest_preg;
        out.inst[i].src1_busy = true;
      }

      if (in.inst[i].src2_areg == in.inst[j].dest_areg) {
        out.inst[i].src2_preg = out.inst[j].dest_preg;
        out.inst[i].src2_busy = true;
      }

      if (in.inst[i].dest_areg == in.inst[j].dest_areg) {
        out.inst[i].old_dest_preg = out.inst[j].dest_preg;
      }
    }
  }

  // 分支处理
  if (in.br.br_taken) {
    // 恢复重命名表
    for (int i = 0; i < ARF_NUM; i++) {
      spec_RAT_1[i] = RAT_checkpoint[in.br.br_tag][i];
    }

    // 恢复free_list
    for (int j = 0; j < ARF_NUM; j++) {
      free_vec_1[j] = free_vec_1[j] || alloc_checkpoint[in.br.br_tag][j];
    }
  }
}

/*void Rename::comb_fire() {*/
/*  for (int i = 0; i < INST_WAY; i++) {*/
/*    // 无有效输入或者本级即将流入下一级*/
/*    out.to_if_ready[i] = !in.valid[i] || ((in.from_iq_all_ready || iq_fire)
 * &&*/
/*                                          (in.from_rob_all_ready ||
 * rob_fire));*/
/*  }*/
/**/
/*  out.to_if_all_ready = out.to_if_ready[0];*/
/*  for (int i = 1; i < INST_WAY; i++) {*/
/*    out.to_if_all_ready = out.to_if_ready[i] && out.to_if_all_ready;*/
/*  }*/
/*  out.to_if_all_ready =*/
/*      out.to_if_all_ready && in.from_iq_all_ready && in.from_rob_all_ready;*/
/*}*/

void Rename::comb_fire() {
  // 分配寄存器
  int alloc_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i] && out.inst[i].dest_en) {
      free_vec_1[alloc_reg[alloc_num]] = false;
      busy_table_1[alloc_reg[alloc_num]] = true;
      spec_RAT_1[in.inst[i].dest_areg] = alloc_reg[alloc_num];
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][alloc_reg[alloc_num]] = true;

      alloc_num++;
    }

    // 保存checkpoint
    if (is_branch(in.inst[i].op) && in.dis_fire[i]) {
      for (int j = 0; j < ARF_NUM; j++) {
        RAT_checkpoint_1[in.inst[i].tag][j] = spec_RAT_1[j];
      }

      for (int j = 0; j < PRF_NUM; j++) {
        alloc_checkpoint_1[in.inst[i].tag][j] = false;
      }
    }
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit_valid[i]) {
      if (in.commit_inst[i].dest_en) {
        free_vec_1[in.commit_inst[i].old_dest_preg] = true;
      }
    }
  }
}

void Rename ::seq() {
  for (int i = 0; i < ARF_NUM; i++) {
    spec_RAT[i] = spec_RAT_1[i];
    for (int j = 0; j < MAX_BR_NUM; j++)
      RAT_checkpoint[j][i] = RAT_checkpoint_1[j][i];
  }

  for (int i = 0; i < PRF_NUM; i++) {
    free_vec[i] = free_vec_1[i];
    busy_table[i] = busy_table_1[i];
    for (int j = 0; j < MAX_BR_NUM; j++)
      alloc_checkpoint[j][i] = alloc_checkpoint_1[j][i];
  }
}

/*void Rename::print_reg() {*/
/*  int preg_idx;*/
/*  for (int i = 0; i < ARF_NUM; i++) {*/
/*    preg_idx = arch_RAT[i];*/
/*    uint32_t data = cvt_bit_to_number_unsigned(preg_base + preg_idx * 32,
 * 32);*/
/**/
/*    cout << reg_names[i] << ": " << hex << data << " ";*/
/**/
/*    if (i % 8 == 0)*/
/*      cout << endl;*/
/*  }*/
/*  cout << endl;*/
/*}*/
/**/
/*uint32_t Rename::reg(int idx) {*/
/*  int preg_idx = arch_RAT[idx];*/
/*  return cvt_bit_to_number_unsigned(preg_base + preg_idx * 32, 32);*/
/*}*/
/**/
/*void Rename::print_RAT() {*/
/*  for (int i = 0; i < ARF_NUM; i++) {*/
/*    cout << dec << i << ":" << dec << arch_RAT[i] << " ";*/
/**/
/*    if (i % 8 == 0)*/
/*      cout << endl;*/
/*  }*/
/*  cout << endl;*/
/*}*/
