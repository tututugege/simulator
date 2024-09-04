#include "RISCV.h"
#include "back-end/config.h"
#include "cvt.h"
#include <cassert>
#include <cstdint>
Inst_info decode(bool inst_bit[]);

void RISCV_32I(bool input_data[BIT_WIDTH], bool *output_data) {
  // get input data
  bool *general_regs = input_data;            // 1024
  bool *reg_csrs = input_data + 32 * PRF_NUM; // 32*21

  bool *instruction[WAY];
  bool *bit_this_pc[WAY]; // 32
  uint32_t number_pc_unsigned[WAY];

  for (int i = 0; i < WAY; i++) {
    instruction[i] = input_data + POS_IN_INST + 32 * i;
    bit_this_pc[i] = input_data + POS_IN_PC + 32 * i;
    number_pc_unsigned[i] = cvt_bit_to_number_unsigned(bit_this_pc[i], 32);
  }

  bool *bit_load_data = input_data + POS_IN_LOAD_DATA;   // 32
  bool *this_priviledge = input_data + POS_IN_PRIVILEGE; // 2

  bool asy = input_data[POS_IN_ASY];
  bool page_fault_inst = input_data[POS_PAGE_FAULT_INST];
  bool page_fault_load = input_data[POS_PAGE_FAULT_LOAD];
  bool page_fault_store = input_data[POS_PAGE_FAULT_STORE];

  // initialize output data
  bool bit_result_tensor[32];
  init_indice(bit_result_tensor, 0, 32);
  bool next_priviledge[2];
  copy_indice(next_priviledge, 0, this_priviledge, 0, 2);

  /*bool *next_general_regs = output_data + 0; // 1024*/
  /*bool *bit_load_address = output_data + POS_OUT_LOAD_ADDR;*/
  /*bool *bit_store_data = output_data + POS_OUT_STORE_DATA;*/
  /*bool *bit_store_address = output_data + POS_OUT_STORE_ADDR;*/

  /*bool *bit_reg_data_a = general_regs; // 32*/
  /*bool *bit_reg_data_b = general_regs; // 32*/
  /**/

  Inst_info inst[WAY];
  // decode

  for (int i = 0; i < WAY; i++) {
    if (*(input_data + POS_IN_INST_VALID + i)) {
      back.in.inst[i] = decode(instruction[i]);
      back.in.PC[i] = number_pc_unsigned[i];
    } else {
      back.in.inst[i].type = NOP;
    }
  }
  // rename -> execute -> write back
  back.Back_cycle(input_data, output_data);

  // output data
  /*init_indice(next_general_regs, 0, 32);*/
  // copy_indice(output_data, 0, next_general_regs, 0, 1024);
  // copy_indice(output_data, 1024, next_reg_csrs, 0, 32*21);
  // copy_indice(output_data, 1696, bit_next_pc, 0, 32);
  // copy_indice(output_data, 1728, bit_load_address, 0, 32);
  // copy_indice(output_data, 1760, bit_store_data, 0, 32);
  // copy_indice(output_data, 1792, bit_store_address, 0, 32);
  // copy_indice(output_data, 1824, next_priviledge, 0, 2);
}

Inst_info decode(bool inst_bit[]) {
  // 操作数来源以及type
  bool dest_en, src1_en, src2_en;
  Inst_type type;
  Inst_op op;
  uint32_t imm;

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

  switch (number_op_code_unsigned) {
  case number_0_opcode_lui: { // lui
    dest_en = true;
    src1_en = false;
    src2_en = false;
    type = UTYPE;
    op = LUI;
    imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    dest_en = true;
    src1_en = false;
    src2_en = false;
    type = UTYPE;
    op = AUIPC;
    imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_2_opcode_jal: { // jal
    dest_en = true;
    src1_en = false;
    src2_en = false;
    type = JTYPE;
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
    type = ITYPE;
    op = JALR;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 21);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    dest_en = false;
    src1_en = true;
    src2_en = true;
    type = BTYPE;
    switch (number_funct3_unsigned) {
    case 0: // beq
      op = BEQ;
      break;
    case 1: // bne
      op = BNE;
      break;
    case 4: // blt
      op = BLT;
      break;
    case 5: // bge
      op = BGE;
      break;
    case 6: // bltu
      op = BLTU;
      break;
    case 7: // bgeu
      op = BGEU;
      break;
    default:
      assert(0);
    }
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_b_type, 13);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    dest_en = true;
    src1_en = true;
    src2_en = false;
    type = ITYPE;

    switch (number_funct3_unsigned) {
    case 0: // lb
      op = LB;
      break;
    case 1: // lh
      op = LH;
      break;
    case 2: // lw
      op = LW;
      break;
    case 4: // lbu
      op = LBU;
      break;
    case 5: // lhu
      op = LHU;
      break;
    default:
      assert(0);
    }
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    dest_en = false;
    src1_en = true;
    src2_en = true;

    switch (number_funct3_unsigned) {
    case 0: // sb
      op = SB;
      break;
    case 1: // sh
      op = SH;
      break;
    case 2: // sw
      op = SW;
      break;
    }
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_s_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    type = STYPE;

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai
    dest_en = true;
    src1_en = true;
    src2_en = false;

    switch (number_funct3_unsigned) {
    case 0: // addi
      op = ADD;
      break;
    case 2: // slti
      op = SLT;
      break;
    case 3: // sltiu
      op = SLTU;
      break;
    case 4: // xori
      op = XOR;
      break;
    case 6: // ori
      op = OR;
      break;
    case 7: // andi
      op = AND;
      break;
    case 1: // slli
      op = SLL;
      break;
    case 5: { // srli, srai
      switch (number_funct7_unsigned) {
      case 0: // srli
        op = SRL;
        break;
      case 32: // srai
        op = SRA;
        break;
      default:
        assert(0);
      }
      break;
    }
    }

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    type = ITYPE;

    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
    dest_en = true;
    src1_en = true;
    src2_en = true;
    type = RTYPE;

    switch (number_funct3_unsigned) {
    case 0: { // add, sub
      switch (number_funct7_unsigned) {
      case 0: // add
        op = ADD;
        break;
      case 32: // sub
        op = SUB;
        break;
      default:
        assert(0);
      }
      break;
    }
    case 1: // sll
      op = SLL;
      break;
    case 2: // slt
      op = SLT;
      break;
    case 3: // sltu
      op = SLTU;
      break;
    case 4: // xor
      op = XOR;
      break;
    case 5: { // srl, sra
      switch (number_funct7_unsigned) {
      case 0: // srl
        op = SRL;
        break;
      case 32: // sra
        op = SRA;
        break;
      default:
        assert(0);
      }
      break;
    }
    case 6: // or
      op = OR;
      break;
    case 7: // and
      op = AND;
      break;
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    dest_en = false;
    src1_en = false;
    src2_en = false;

    break;
  }
  default: {
    cerr << "error" << endl;
    exit(-1);
    break;
  }
  }

  // 不写0寄存器
  if (reg_d_index == 0)
    dest_en = 0;

  Inst_info info = {.dest_idx = reg_d_index,
                    .src1_idx = reg_a_index,
                    .src2_idx = reg_b_index,
                    .dest_en = dest_en,
                    .src1_en = src1_en,
                    .src2_en = src2_en,
                    .type = type,
                    .op = op,
                    .imm = imm};

  return info;
}
