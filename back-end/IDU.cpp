#include "config.h"
#include "frontend.h"
#include <IDU.h>
#include <RISCV.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <ios>
#include <util.h>

int decode(Inst_uop uop[2], uint32_t instruction);

void IDU::init() {
  for (int i = 1; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
  }

  tag_vec_1[0] = false;
  now_tag = 0;
  tag_list.push_back(0);
}

void IDU::default_val() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    dec_valid[i] = false;
  }
  dec2ren_port = 0;
}

// 分配mem_tag
int IDU::mem_tag_alloc() {
  return -1;
}

int IDU::bra_tag_alloc() {
  for (int alloc_bra_tag = 0; alloc_bra_tag < MAX_BR_NUM; alloc_bra_tag++) {
    if (tag_vec[alloc_bra_tag])
      return alloc_bra_tag;
  }
  return -1;
}

// 完成指令包译码，直到该指令包的全部uop流入rename级，才译码下一个指令包
void IDU::comb_decode() {
  if (stall)
    return;
  else {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (io.front2dec->valid[i]) {
        dec_uop_num[i] = decode(dec_uop[i], io.front2dec->inst[i]);
        for (int j = 0; j < 2; j++) {
          dec_uop[i][j].pc = io.front2dec->pc[i];
          dec_uop[i][j].pred_br_taken = io.front2dec->predict_dir[i];
          dec_uop[i][j].alt_pred = io.front2dec->alt_pred[i];
          dec_uop[i][j].altpcpn = io.front2dec->altpcpn[i];
          dec_uop[i][j].pcpn = io.front2dec->pcpn[i];
          dec_uop[i][j].pred_br_pc = io.front2dec->predict_next_fetch_address[i];
          dec_uop[i][j].pc_next = dec_uop[i][j].pc + 4;
        }
      }
    }
  }
}

// 分配访存tag 分配分支tag
void IDU::comb_ren_req() {
  bool no_tag = false;
  bool has_br = false;
  if ((alloc_bra_tag = bra_tag_alloc()) == -1)
    no_tag = true;

  for (int i = 0; !stall && i < FETCH_WIDTH; i++) {
    if (io.front2dec->valid[i]) {
      // decode-->rename端口不够
      if (dec2ren_port + dec_uop_num[i] > DECODE_WIDTH) {
        port_stall = true;
      }
      // store指令尝试分配访存tag
      if (dec_uop[i][0].op == STORE) {
        if ((alloc_mem_tag = mem_tag_alloc()) == -1)
          mem_tag_stall = true;
      }
      // 分支指令尝试分配分支tag
      if (dec_uop[i][0].op == BR || dec_uop[i][1].op == JUMP) {
        if (!no_tag && !has_br) {
          has_br = true;
        } else {
          bra_tag_stall = true;
          break;
        }
      }

      // 发起decode-->rename请求
      stall = port_stall || mem_tag_stall || bra_tag_stall;
      if (!stall) {
        dec_valid[i] = true;
        for (int j = 0; j < dec_uop_num[i]; j++) {
          dec_uop[i][j].bra_tag = has_br ? alloc_bra_tag : now_tag;
          dec_uop[i][j].mem_tag = alloc_mem_tag;
          io.dec2ren->uop[dec2ren_port] = dec_uop[i][j];
          io.dec2ren->valid[dec2ren_port] = true;
          dec2ren_port++;
        }
      }
    }
  }
}

void IDU::comb_branch() {
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
  } else {
    pop = false;
    io.dec_bcast->br_mask = 0;
    io.dec_bcast->mispred = false;
  }
}

void IDU::comb_rollback() {
  if (io.rob_bc->rollback) {
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
  io.dec2front->ready = io.ren2dec->ready && !io.prf2dec->mispred;

  if (io.prf2dec->mispred) {
    for (int i = 0; i < DECODE_WIDTH; i++) {
      io.dec2ren->valid[i] = false;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dec2front->fire[i] = dec_valid[i] && io.ren2dec->ready;
    io.dec2front->ready =
        io.dec2front->ready && (!io.front2dec->valid[i] || dec_valid[i]);
    if (io.dec2front->fire[i] &&
        (uop_valid[i][0] && is_branch(dec_uop[i][0].op) ||
         uop_valid[i][1] && is_branch(dec_uop[i][1].op))) {
      push = true;
      now_tag_1 = alloc_tag;
      tag_vec_1[alloc_tag] = false;
    }
  }
}

void IDU::comb_release_tag() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.commit->commit_entry[i].valid &&
        is_branch(io.commit->commit_entry[i].uop.op)) {
      tag_vec_1[io.commit->commit_entry[i].uop.tag] = true;
    }
  }
}

void IDU::seq() {
  now_tag = now_tag_1;
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
}

int decode(Inst_uop uop[2], uint32_t inst) {
  // 操作数来源以及type
  uint32_t imm;
  uint32_t csr_idx;
  int uop_num = 1;

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

  csr_idx = inst >> 20;

  uop[0] = {.dest_areg = reg_d_index,
            .src1_areg = reg_a_index,
            .src2_areg = reg_b_index,
            .src1_is_pc = false,
            .src2_is_imm = true,
            .func3 = number_funct3_unsigned,
            .func7_5 = (bool)(number_funct7_unsigned >> 5),
            .csr_idx = csr_idx,
            .is_last_uop = true,
            .amoop = AMONONE};

  uop[1] = {.dest_areg = reg_d_index,
            .src1_areg = reg_a_index,
            .src2_areg = reg_b_index,
            .src1_is_pc = false,
            .src2_is_imm = true,
            .func3 = number_funct3_unsigned,
            .func7_5 = (bool)(number_funct7_unsigned >> 5),
            .csr_idx = csr_idx,
            .is_last_uop = true,
            .amoop = AMONONE};

  uop[1].op = NONE;

  switch (number_op_code_unsigned) {
  case number_0_opcode_lui: { // lui
    uop[0].dest_en = true;
    uop[0].src1_en = true;
    uop[0].src1_areg = 0;
    uop[0].src2_en = false;
    uop[0].op = ADD;
    uop[0].func3 = 0;
    uop[0].func7_5 = 0;
    uop[0].imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    uop[0].dest_en = true;
    uop[0].src1_en = false;
    uop[0].src2_en = false;
    uop[0].src1_is_pc = true;
    uop[0].op = ADD;
    uop[0].func3 = 0;
    uop[0].func7_5 = 0;
    uop[0].imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_2_opcode_jal: { // jal
    uop_num = 2;
    uop[0].dest_en = true;
    uop[0].src1_en = false;
    uop[0].src2_en = false;
    uop[0].src1_is_pc = true;
    uop[0].src2_is_imm = true;
    uop[0].is_last_uop = false;
    uop[0].func3 = 0;
    uop[0].func7_5 = 0;
    uop[0].imm = 4;
    uop[0].op = ADD;

    uop[1].dest_en = false;
    uop[1].src1_en = false;
    uop[1].src2_en = false;
    uop[1].op = JUMP;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_j_type, 21);
    uop[1].imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    break;
  }
  case number_3_opcode_jalr: { // jalr
    uop_num = 2;
    uop[0].dest_en = true;
    uop[0].src1_en = false;
    uop[0].src2_en = false;
    uop[0].src1_is_pc = true;
    uop[0].src2_is_imm = true;
    uop[0].is_last_uop = false;
    uop[0].imm = 4;
    uop[0].func3 = 0;
    uop[0].func7_5 = 0;
    uop[0].op = ADD;

    uop[1].dest_en = false;
    uop[1].src1_en = true;
    uop[1].src2_en = false;
    uop[1].op = JUMP;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    uop[1].imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    uop[0].dest_en = false;
    uop[0].src1_en = true;
    uop[0].src2_en = true;
    uop[0].op = BR;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_b_type, 13);
    uop[0].imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uop[0].dest_en = true;
    uop[0].src1_en = true;
    uop[0].src2_en = false;
    uop[0].op = LOAD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    uop[0].imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    uop[0].dest_en = false;
    uop[0].src1_en = true;
    uop[0].src2_en = true;
    uop[0].op = STORE;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_s_type, 12);
    uop[0].imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai
    uop[0].dest_en = true;
    uop[0].src1_en = true;
    uop[0].src2_en = false;
    uop[0].op = ADD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    uop[0].imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
    uop[0].dest_en = true;
    uop[0].src1_en = true;
    uop[0].src2_en = true;
    uop[0].src2_is_imm = false;
    if (number_funct7_unsigned == 1) { // mul div
      if (number_funct3_unsigned & 0b100) {
        uop[0].op = DIV;
      } else {
        uop[0].op = MUL;
      }
    } else {
      uop[0].op = ADD;
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    uop[0].dest_en = false;
    uop[0].src1_en = false;
    uop[0].src2_en = false;
    uop[0].op = NONE;
    break;
  }
  case number_10_opcode_ecall: { // ecall, ebreak, csrrw, csrrs, csrrc, csrrwi,
                                 // csrrsi, csrrci
    uop[0].src2_is_imm = bit_funct3[0] && (bit_funct3[2] || bit_funct3[1]);

    if (bit_funct3[2] || bit_funct3[1]) {
      if (csr_idx != number_mtvec && csr_idx != number_mepc &&
          csr_idx != number_mcause && csr_idx != number_mie &&
          csr_idx != number_mip && csr_idx != number_mtval &&
          csr_idx != number_mscratch && csr_idx != number_mstatus &&
          csr_idx != number_mideleg && csr_idx != number_medeleg &&
          csr_idx != number_sepc && csr_idx != number_stvec &&
          csr_idx != number_scause && csr_idx != number_sscratch &&
          csr_idx != number_stval && csr_idx != number_sstatus &&
          csr_idx != number_sie && csr_idx != number_sip &&
          csr_idx != number_satp && csr_idx != number_mhartid &&
          csr_idx != number_misa && csr_idx != number_time &&
          csr_idx != number_timeh) {
        uop[0].op = NONE;
        uop[0].dest_en = false;
        uop[0].src1_en = false;
        uop[0].src2_en = false;

      } else {
        uop[0].op = CSR;
        uop[0].dest_en = true;
        uop[0].src1_en = true;
        uop[0].src2_en = !uop[0].src2_is_imm;
        uop[0].imm = reg_a_index;
      }
    } else {
      uop[0].dest_en = false;
      uop[0].src1_en = false;
      uop[0].src2_en = false;

      if (inst == INST_ECALL) {
        uop[0].op = ECALL;
      } else if (inst == INST_EBREAK) {
        uop[0].op = EBREAK;
      } else if (inst == INST_MRET) {
        uop[0].op = MRET;
      } else if (inst == INST_WFI) {
        uop[0].op = NONE;
      } else {
        cout << hex << inst << endl;
        /*assert(0);*/
      }
    }
    break;
  }

  case number_11_opcode_lrw: {
    uop_num = 2;
    uop[0].dest_en = true;
    uop[0].src1_en = true;
    uop[0].src2_en = false;
    uop[0].imm = 0;
    uop[0].op = LOAD;
    uop[0].is_last_uop = false;

    uop[1].dest_en = false;
    uop[1].src1_en = true;
    uop[1].src2_en = true;
    uop[1].imm = 0;
    uop[1].op = STORE;

    switch (number_funct7_unsigned >> 2) {
    case 0: { // amoadd.w
      uop[0].amoop = AMOADD;
      break;
    }
    case 1: { // amoswap.w
      uop[0].amoop = AMOADD;

      break;
    }
    case 2: { // lr.w
      uop_num = 1;
      uop[0].src2_en = false;
      uop[0].op = LOAD;
      uop[0].amoop = LR;
      uop[0].is_last_uop = true;

      uop[1].op = NONE;
      break;
    }
    case 3: { // sc.w
      uop[0].op = STORE;
      uop[0].amoop = SC;
      uop[1].op = ADD;
      uop[1].dest_en = true;
      break;
    }
    case 4: { // amoxor.w
      uop[0].amoop = AMOXOR;
      break;
    }
    case 8: { // amoor.w
      uop[0].amoop = AMOOR;
      break;
    }
    case 12: { // amoand.w
      uop[0].amoop = AMOAND;
      break;
    }
    case 16: { // amomin.w
      uop[0].amoop = AMOMIN;
      break;
    }
    case 20: { // amomax.w
      uop[0].amoop = AMOMAX;
      break;
    }
    case 24: { // amominu.w
      uop[0].amoop = AMOMINU;
      break;
    }
    case 28: { // amomaxu.w
      uop[0].amoop = AMOMAXU;
      break;
    }
    }

    uop[1].amoop = uop[0].amoop;
    break;
  }

  default: {
    /*if (LOG) {*/
    /*  cerr << "Error: unknown instruction: ";*/
    /*  cerr << cvt_bit_to_number_unsigned(inst_bit, 32) << endl;*/
    /*}*/
    uop[0].dest_en = false;
    uop[0].src1_en = false;
    uop[0].src2_en = false;
    uop[0].op = NONE;

    break;
  }
  }

  // 不写0寄存器
  if (reg_d_index == 0) {
    uop[0].dest_en = 0;
    uop[1].dest_en = 0;
  }

  for (int i = 0; i < uop_num; i++) {
    if (is_branch(uop[i].op)) {
      uop[i].iq_type = IQ_BR;
    } else if (is_load(uop[i].op) || is_store(uop[i].op)) {
      uop[i].iq_type = IQ_LS;
    } else if (uop[i].op == MUL) {
      uop[i].iq_type = IQ_INTM;
    } else if (uop[i].op == DIV) {
      uop[i].iq_type = IQ_INTD;
    } else if (is_CSR(uop[i].op)) {
      uop[i].iq_type = IQ_INTM;
    } else {
      if (rand() % 2) {
        uop[i].iq_type = IQ_INTD;
      } else {
        uop[i].iq_type = IQ_INTM;
      }
    }
  }

  return uop_num;
}
