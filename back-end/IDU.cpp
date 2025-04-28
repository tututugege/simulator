#include "config.h"
#include <IDU.h>
#include <RISCV.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <util.h>

Inst_info decode(uint32_t);
void IDU::init() {
  state = 0;
  state_1 = 0;
  for (int i = 1; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
  }
  tag_vec_1[0] = false;
  now_tag = 0;
  tag_list.push_back(0);
}

// 译码并分配tag
void IDU::comb_decode() {
  bool has_br = false; // 一周期只能解码一条分支指令
  bool stall = false;
  bool no_tag = false;

  // 查找新的tag
  for (alloc_tag = 0; alloc_tag < MAX_BR_NUM; alloc_tag++) {
    if (tag_vec[alloc_tag])
      break;
  }

  // 无剩余的tag
  if (alloc_tag == MAX_BR_NUM) {
    no_tag = true;
    alloc_tag = 0;
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dec2ren->inst[i] = decode(io.front2dec->inst[i]);
    io.dec2ren->inst[i].tag = (has_br) ? alloc_tag : now_tag;
    io.dec2ren->inst[i].pc = io.front2dec->pc[i];
    io.dec2ren->inst[i].pred_br_taken = io.front2dec->predict_dir[i];
    io.dec2ren->inst[i].alt_pred = io.front2dec->alt_pred[i];
    io.dec2ren->inst[i].altpcpn = io.front2dec->altpcpn[i];
    io.dec2ren->inst[i].pcpn = io.front2dec->pcpn[i];
    io.dec2ren->inst[i].pred_br_pc =
        io.front2dec->predict_next_fetch_address[i];

    if (io.front2dec->valid[i] && !stall) {
      if (!is_branch(io.dec2ren->inst[i].op))
        io.dec2ren->inst[i].pc_next = io.front2dec->pc[i] + 4;

      // 分配新tag 一组指令只能有一个分支指令
      if (is_branch(io.dec2ren->inst[i].op)) {
        if (!no_tag && !has_br) {
          io.dec2ren->valid[i] = true;
          has_br = true;
        } else {
          io.dec2ren->valid[i] = false;
          stall = true;
        }
      } else {
        io.dec2ren->valid[i] = true;
      }
    } else {
      io.dec2ren->valid[i] = false;
    }
  }
}

void IDU::comb_branch() {
  /*if (io.prf2dec->mispred) {*/
  /*  br_tag_1 = io.prf2dec->br_tag;*/
  /*}*/

  /*if (state == MISPRED || io.prf2dec->mispred) {*/
  if (io.prf2dec->mispred) {
    io.dec_bcast->mispred = true;
    io.dec_bcast->br_tag = io.prf2dec->br_tag;

    auto it = tag_list.end();
    auto it_prev = tag_list.end();
    it--;
    it_prev--;
    it_prev--;
    pop = 0;
    io.dec_bcast->br_mask = 0;
    while (*it_prev != io.prf2dec->br_tag) {
      io.dec_bcast->br_mask |= 1 << *it;
      tag_vec_1[*it] = true;
      pop++;
      it--;
      it_prev--;
    }
    io.dec_bcast->br_mask |= 1 << *it;
    now_tag_1 = *it;

    state_1 = NORMAL;
  } else {
    pop = false;
    io.dec_bcast->br_mask = 0;
    io.dec_bcast->mispred = false;
    state_1 = NORMAL;
  }
}

void IDU::comb_rollback() {
  if (io.rob_bc->rollback) {
    state_1 = 0;
    for (int i = 1; i < MAX_BR_NUM; i++) {
      tag_vec_1[i] = true;
    }
    tag_vec_1[0] = false;
    now_tag_1 = 0;
    tag_list.clear();
    tag_list.push_back(0);
  }
}

void IDU::comb_fire() {
  push = false;
  io.dec2front->ready =
      (state == NORMAL) && io.ren2dec->ready && !io.prf2dec->mispred;

  if (state == MISPRED || io.prf2dec->mispred) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      io.dec2ren->valid[i] = false;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dec2front->fire[i] = io.dec2ren->valid[i] && io.ren2dec->ready;
    io.dec2front->ready = io.dec2front->ready &&
                          (!io.front2dec->valid[i] || io.dec2ren->valid[i]);
    if (io.dec2front->fire[i] && is_branch(io.dec2ren->inst[i].op)) {
      push = true;
      now_tag_1 = alloc_tag;
      tag_vec_1[alloc_tag] = false;
    }
  }
}

void IDU::comb_release_tag() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.commit->commit_entry[i].valid &&
        is_branch(io.commit->commit_entry[i].inst.op)) {
      tag_vec_1[io.commit->commit_entry[i].inst.tag] = true;
    }
  }
}

void IDU::seq() {
  now_tag = now_tag_1;
  state = state_1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
  }

  while (pop > 0) {
    tag_list.pop_back();
    pop--;
  }

  if (push) {
    tag_list.push_back(alloc_tag);
  }

  while (tag_list.size() > MAX_BR_NUM) {
    tag_list.pop_front();
  }

  /*static int idx = 0;*/
  /*cout << idx++ << " ";*/
  /*for (int i = 0; i < MAX_BR_NUM; i++) {*/
  /*  cout << tag_vec[i];*/
  /*}*/
  /**/
  /*cout << endl;*/
}

Inst_info decode(uint32_t inst) {
  // 操作数来源以及type
  bool dest_en, src1_en, src2_en;
  bool src2_is_imm;
  bool src2_is_4;
  Inst_op op;
  uint32_t imm;
  uint32_t csr_idx;
  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, inst, 32);

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
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
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
      csr_idx = inst >> 20;
    } else {
      dest_en = false;
      src1_en = false;
      src2_en = false;

      if (inst == INST_ECALL) {
        op = ECALL;
      } else if (inst == INST_EBREAK) {
        op = EBREAK;
      } else if (inst == INST_MRET) {
        op = MRET;
      } else {
        cout << hex << inst << endl;
        assert(0);
      }
    }
    break;
  }

  default: {
    /*if (LOG) {*/
    /*  cerr << "Error: unknown instruction: ";*/
    /*  cerr << cvt_bit_to_number_unsigned(inst_bit, 32) << endl;*/
    /*}*/
    dest_en = false;
    src1_en = false;
    src2_en = false;

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

  if (info.op == LOAD) {
    info.iq_type = IQ_LD;
  } else if (info.op == STORE) {
    info.iq_type = IQ_ST;
  } else if (is_branch(info.op)) {
    info.iq_type = IQ_BR;
  } else if (is_CSR(op)) {
    info.iq_type = IQ_CSR;
  } else {
    info.iq_type = IQ_INT;
  }

  return info;
}
