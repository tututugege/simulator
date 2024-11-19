#include "config.h"
#include <IDU.h>
#include <RISCV.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>

Inst_info decode(bool inst_bit[]);

void IDU::init() {
  for (int i = 1; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
  }
  tag_vec[0] = false;
  tag_vec_1[0] = false;
  now_tag = 0;
  now_tag_1 = 0;

  tag_fifo[0] = 0;
  tag_fifo_1[0] = 0;
  enq_ptr = 1;
  enq_ptr_1 = 1;
}

// 译码并分配tag
void IDU::comb_dec() {
  int free_tag_num = 0;
  for (int i = 0; i < MAX_BR_NUM && free_tag_num < INST_WAY; i++) {
    if (tag_vec[i])
      alloc_tag[free_tag_num++] = i;
  }

  int new_tag_num = 0;

  int inst_tag = now_tag;
  bool stall = false;
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i] && !stall) {
      out.inst[i] = decode(in.instruction[i]);
      out.inst[i].tag = inst_tag;

      // 分配新tag
      if (is_branch(out.inst[i].op) && new_tag_num < free_tag_num) {
        out.inst[i].tag = inst_tag;
        inst_tag = alloc_tag[new_tag_num];
        out.valid[i] = true;
        out.ready[i] = true;
        new_tag_num++;
      } else if (!is_branch(out.inst[i].op)) {
        out.valid[i] = true;
        out.ready[i] = true;
      } else {
        stall = true;
        out.valid[i] = false;
        out.ready[i] = false;
      }
    } else {
      // 输入无效 或tag不够
      out.valid[i] = false;
      out.ready[i] = !stall;
    }
  }

  // 释放tag
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.free_valid[i]) {
      tag_vec_1[in.free_tag[i]] = true;
      deq_ptr_1 = (deq_ptr_1 + 1) % (MAX_BR_NUM);
    }
  }

  // 分支
  uint32_t br_mask = 0;
  if (in.br.mispred) {
    int idx = (enq_ptr_1 - 1 + MAX_BR_NUM) % MAX_BR_NUM;
    while (tag_fifo[idx] != in.br.br_tag) {
      br_mask = br_mask | (1 << tag_fifo[idx]);
      tag_vec_1[tag_fifo[idx]] = true;
      enq_ptr_1 = (enq_ptr_1 - 1 + MAX_BR_NUM) % MAX_BR_NUM;
      idx = (idx - 1 + MAX_BR_NUM) % MAX_BR_NUM;
    }

    idx = (idx + 1) % MAX_BR_NUM;
    enq_ptr_1 = (enq_ptr_1 + 1) % MAX_BR_NUM;
    tag_vec_1[tag_fifo[idx]] = false;
    now_tag_1 = tag_fifo[idx];

    out.br.mispred = true;
    out.br.br_tag = in.br.br_tag;
  } else {
    out.br.mispred = false;
  }

  for (int i = 0; i < MAX_BR_NUM; i++) {
    out.br.br_mask[i] = (bool)(br_mask & (1 << i));
  }
}

void IDU::comb_fire() {
  int new_tag_num = 0;

  for (int i = 0; i < INST_WAY; i++) {
    if (in.dis_fire[i] && (out.inst[i].op == BR || out.inst[i].op == JALR ||
                           out.inst[i].op == JAL)) {

      tag_fifo_1[enq_ptr_1] = alloc_tag[new_tag_num];
      tag_vec_1[alloc_tag[new_tag_num]] = false;
      enq_ptr_1 = (enq_ptr_1 + 1) % MAX_BR_NUM;
      now_tag_1 = alloc_tag[new_tag_num];
      new_tag_num++;
    }
  }
}

void IDU::seq() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    tag_fifo[i] = tag_fifo_1[i];
  }
  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
  now_tag = now_tag_1;
}

Inst_info decode(bool inst_bit[]) {
  // 操作数来源以及type
  bool dest_en, src1_en, src2_en;
  bool src2_is_imm;
  bool src2_is_4;
  Inst_op op;
  uint32_t imm;
  uint32_t csr_idx;
  uint32_t instruction = cvt_bit_to_number_unsigned(inst_bit, 32);

  // split instruction
  bool *bit_op_code = inst_bit + 25; // 25-31
  bool *rd_code = inst_bit + 20;     // 20-24
  bool *rs_a_code = inst_bit + 12;   // 12-16
  bool *rs_b_code = inst_bit + 7;    // 7-11
  bool *bit_csr_code = inst_bit + 0; // 0-11

  // 准备opcode、funct3、funct7
  uint32_t number_op_code_unsigned = cvt_bit_to_number_unsigned(bit_op_code, 7);
  bool *bit_funct3 = inst_bit + 17; // 3
  uint32_t number_funct3_unsigned = cvt_bit_to_number_unsigned(bit_funct3, 3);
  bool *bit_funct7 = inst_bit + 0; // 7
  uint32_t number_funct7_unsigned = cvt_bit_to_number_unsigned(bit_funct7, 7);

  // 准备立即数
  bool bit_immi_u_type[32]; // U-type
  bool bit_immi_j_type[21]; // J-type
  bool bit_immi_i_type[12]; // I-type
  bool bit_immi_b_type[13]; // B-type
  bool bit_immi_s_type[12]; // S-type
  init_indice(bit_immi_u_type, 0, 32);
  copy_indice(bit_immi_u_type, 0, inst_bit, 0, 20);
  init_indice(bit_immi_j_type, 0, 21);
  bit_immi_j_type[0] = (*(inst_bit + 0));
  copy_indice(bit_immi_j_type, 1, inst_bit, 12, 8);
  bit_immi_j_type[9] = (*(inst_bit + 11));
  copy_indice(bit_immi_j_type, 10, inst_bit, 1, 10);
  copy_indice(bit_immi_i_type, 0, inst_bit, 0, 12);
  init_indice(bit_immi_b_type, 0, 13);
  bit_immi_b_type[0] = (*(inst_bit + 0));
  bit_immi_b_type[1] = (*(inst_bit + 24));
  copy_indice(bit_immi_b_type, 2, inst_bit, 1, 6);
  copy_indice(bit_immi_b_type, 8, inst_bit, 20, 4);
  copy_indice(bit_immi_s_type, 0, inst_bit, 0, 7);
  copy_indice(bit_immi_s_type, 7, inst_bit, 20, 5);

  // 准备寄存器
  int reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
  int reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  int reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);

  src2_is_imm = true;

  switch (number_op_code_unsigned) {
  case number_0_opcode_lui: { // lui
    dest_en = true;
    src1_en = false;
    src2_en = false;
    op = LUI;
    imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    dest_en = true;
    src1_en = false;
    src2_en = false;
    op = AUIPC;
    imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_2_opcode_jal: { // jal
    dest_en = true;
    src1_en = false;
    src2_en = false;
    src2_is_imm = false;
    op = JAL;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_j_type, 21);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    break;
  }
  case number_3_opcode_jalr: { // jalr
    dest_en = true;
    src1_en = true;
    src2_en = false;
    op = JALR;
    src2_is_imm = false;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 21);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    dest_en = false;
    src1_en = true;
    src2_en = true;
    op = BR;
    src2_is_imm = false;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_b_type, 13);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    dest_en = true;
    src1_en = true;
    src2_en = false;
    op = LOAD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    dest_en = false;
    src1_en = true;
    src2_en = true;
    op = STORE;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_s_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai
    dest_en = true;
    src1_en = true;
    src2_en = false;
    op = ADD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
    dest_en = true;
    src1_en = true;
    src2_en = true;
    src2_is_imm = false;
    op = ADD;
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    dest_en = false;
    src1_en = false;
    src2_en = false;
    op = ADD;
    break;
  }
  case number_10_opcode_ecall: { // ecall, ebreak, csrrw, csrrs, csrrc, csrrwi,
                                 // csrrsi, csrrci
    src2_is_imm = bit_funct3[0] && (bit_funct3[2] || bit_funct3[1]);
    if (bit_funct3[2] || bit_funct3[1]) {
      op = CSR;
      dest_en = true;
      src1_en = true;
      src2_en = !src2_is_imm;
      imm = reg_a_index;
      csr_idx = instruction >> 20;
    } else {
      dest_en = false;
      src1_en = false;
      src2_en = false;

      if (instruction == INST_ECALL) {
        op = ECALL;
      } else if (instruction == INST_EBREAK) {
        op = EBREAK;
      } else if (instruction == INST_MRET) {
        op = MRET;
      } else {
        assert(0);
      }
    }
    break;
  }

  default: {
    if (LOG) {
      cerr << "*****************************************" << endl;
      cerr << "Error: unknown instruction: ";
      cerr << cvt_bit_to_number_unsigned(inst_bit, 32) << endl;
      cerr << "*****************************************" << endl;
    }
    /*assert(0);*/
    break;
  }
  }

  // 不写0寄存器
  if (reg_d_index == 0)
    dest_en = 0;

  Inst_info info = {.dest_areg = reg_d_index,
                    .src1_areg = reg_a_index,
                    .src2_areg = reg_b_index,
                    .dest_en = dest_en,
                    .src1_en = src1_en,
                    .src2_en = src2_en,
                    .op = op,
                    .src2_is_imm = src2_is_imm,
                    .func3 = number_funct3_unsigned,
                    .func7_5 = (bool)(number_funct7_unsigned >> 5),
                    .imm = imm,
                    .csr_idx = csr_idx};

  return info;
}
