#include <Rename.h>
#include <config.h>
#include <cstdlib>
#include <cvt.h>

void Rename::init() {
  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = false;
    arch_RAT[i] = i;
    spec_RAT[i] = i;
  }

  for (int i = ARF_NUM; i < PRF_NUM; i += 2) {
    free_vec[i] = false;
  }
}

// 根据PRF使用情况以及后级流水线ready信号输出ready
void Rename::comb_0() {
  // 可用寄存器个数 大于INST_WAY时为INST_WAY
  int num = 0;
  for (int i = 0; i < PRF_NUM && num < INST_WAY; i++) {
    if (free_vec[i] == false) {
      alloc_reg[num] = i;
      num++;
    }
  }

  // 无有效指令或者无需分配寄存器ready都为1且不占用寄存器
  for (int i = 0; i < INST_WAY; i++) {
    if (!in.valid[i] || !in.inst[i].dest_en)
      out.ready[i] = true;
  }

  // 有效且需要寄存器的指令，寄存器不够则对应端口ready为false
  int alloc_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    out.inst[i] = in.inst[i];
    if (in.valid[i] && in.inst[i].dest_en) {
      // 分配寄存器
      if (alloc_num < num) {
        out.ready[i] = true;
      } else {
        out.ready[i] = false;
      }
    }
  }

  for (int i = 0; i < INST_WAY; i++) {
    // 如果输入有指令且寄存器够
    if (in.valid[i] && out.ready[i]) {
      out.to_iq_valid[i] = !iq_fire;
      out.to_rob_valid[i] = !rob_fire;
    } else {
      out.to_iq_valid[i] = false;
      out.to_rob_valid[i] = false;
    }
  }
}

void Rename::comb_1() {
  out.all_ready = out.ready[0];
  for (int i = 1; i < INST_WAY; i++) {
    out.all_ready = out.ready[i] && out.all_ready;
  }
  out.all_ready =
      out.all_ready && in.from_iq_all_ready && in.from_rob_all_ready;
}

void Rename::comb_2() {
  // 分配寄存器
  int alloc_num = 0;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i] && out.ready[i] && out.inst[i].dest_en) {
      out.inst[i].dest_preg = alloc_reg[alloc_num];
      free_vec_1[alloc_reg[alloc_num]] = false;
      busy_table_1[alloc_reg[alloc_num]] = true;
      for (int j = 0; j < MAX_BR_NUM; j++)
        alloc_checkpoint_1[j][alloc_reg[alloc_num]] = true;
    }
  }

  // 无waw raw的输出 读spec_RAT和busy_table
  for (int i = 0; i < INST_WAY; i++) {
    out.inst[i].old_dest_preg = spec_RAT[in.inst[i].dest_preg];
    out.inst[i].src1_preg = spec_RAT[in.inst[i].src1_areg];
    out.inst[i].src2_preg = spec_RAT[in.inst[i].src2_areg];
    out.inst[i].src1_busy =
        busy_table[out.inst[i].src1_preg] && in.inst[i].src1_en;
    out.inst[i].src2_busy =
        busy_table[out.inst[i].src1_preg] && in.inst[i].src2_en;
  }

  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit_valid[i]) {
      if (in.commit_inst[i].dest_en) {
        free_vec_1[in.commit_inst[i].old_dest_preg] = true;

#ifdef CONFIG_DIFFTEST
        arch_RAT[in.commit_inst[i].dest_areg] = in.commit_inst[i].dest_preg;
#endif
      }
    }
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
    for (int i = 0; i < MAX_BR_NUM; i++) {
      if (in.br.br_mask[i]) {
        for (int j = 0; j < ARF_NUM; j++) {
          free_vec_1[j] = free_vec_1[j] || alloc_checkpoint[j];
        }
      }
    }
  }

  if ((in.from_iq_all_ready || iq_fire) &&
      (in.from_rob_all_ready || rob_fire)) {
    iq_fire_1 = false;
    rob_fire_1 = false;
  } else {
    iq_fire_1 = in.from_iq_all_ready;
    rob_fire_1 = in.from_iq_all_ready;
  }
}

void Rename ::seq() {
  for (int i = 0; i < ARF_NUM; i++) {
    spec_RAT[i] = spec_RAT_1[i];
    for (int j = 0; i < MAX_BR_NUM; j++)
      RAT_checkpoint[j][i] = RAT_checkpoint_1[j][i];
  }

  for (int i = 0; i < ARF_NUM; i++) {
    free_vec[i] = free_vec_1[i];
    busy_table[i] = busy_table_1[i];
    for (int j = 0; i < MAX_BR_NUM; j++)
      alloc_checkpoint[j][i] = alloc_checkpoint_1[j][i];
  }

  rob_fire = rob_fire_1;
  iq_fire = iq_fire_1;
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
