#include "CSR.h"
#include "config.h"
#include <IDU.h>
#include <RISCV.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <util.h>

void decode(Inst_uop &uop, uint32_t instruction);

void IDU::init() {
  for (int i = 1; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
  }

  tag_vec_1[0] = false;
  now_tag_1 = now_tag = 0;
  enq_ptr_1 = enq_ptr = 1;
  deq_ptr_1 = deq_ptr = 0;
}

// 译码并分配tag
void IDU::comb_decode() {
  wire1_t no_tag = false;
  bool has_br = false; // 一周期只能解码一条分支指令
  bool stall = false;

  // 查找新的tag
  for (alloc_tag = 0; alloc_tag < MAX_BR_NUM; alloc_tag++) {
    if (tag_vec[alloc_tag])
      break;
  }

  // 无剩余的tag 相当于 tag_vec == 16'b0
  if (alloc_tag == MAX_BR_NUM) {
    no_tag = true;
    alloc_tag = 0;
  }

  int i;
  for (i = 0; i < FETCH_WIDTH; i++) {
    if (io.front2dec->valid[i]) {
      io.dec2ren->valid[i] = true;
      if (io.front2dec->page_fault_inst[i]) {
        io.dec2ren->uop[i].uop_num = 1;
        io.dec2ren->uop[i].page_fault_inst = true;
        io.dec2ren->uop[i].page_fault_load = false;
        io.dec2ren->uop[i].page_fault_store = false;
        io.dec2ren->uop[i].type = NONE;
        io.dec2ren->uop[i].src1_en = io.dec2ren->uop[i].src2_en =
            io.dec2ren->uop[i].dest_en = false;
      } else {
        decode(io.dec2ren->uop[i], io.front2dec->inst[i]);
      }
    } else {
      io.dec2ren->valid[i] = false;
      continue;
    }

    io.dec2ren->uop[i].tag = (has_br) ? alloc_tag : now_tag;
    io.dec2ren->uop[i].pc = io.front2dec->pc[i];
    io.dec2ren->uop[i].pred_br_taken = io.front2dec->predict_dir[i];
    io.dec2ren->uop[i].alt_pred = io.front2dec->alt_pred[i];
    io.dec2ren->uop[i].altpcpn = io.front2dec->altpcpn[i];
    io.dec2ren->uop[i].pcpn = io.front2dec->pcpn[i];
    io.dec2ren->uop[i].pred_br_pc = io.front2dec->predict_next_fetch_address[i];

    // for debug
    io.dec2ren->uop[i].pc_next = io.dec2ren->uop[i].pc + 4;

    if (io.front2dec->valid[i] &&
        (io.dec2ren->uop[i].type == BR || io.dec2ren->uop[i].type == JAL ||
         io.dec2ren->uop[i].type == JALR)) {
      if (!no_tag && !has_br) {
        has_br = true;
      } else {
        stall = true;
        break;
      }
    }
  }

  if (stall) {
    for (; i < FETCH_WIDTH; i++) {
      io.dec2ren->valid[i] = false;
    }
  }
}

void IDU::comb_branch() {
  // 如果一周期实现不方便，可以用状态机多周期实现
  if (io.prf2dec->mispred) {
    io.dec_bcast->mispred = true;
    io.dec_bcast->br_tag = io.prf2dec->br_tag;
    io.dec_bcast->redirect_rob_idx = io.prf2dec->redirect_rob_idx;

    LOOP_DEC(enq_ptr_1, MAX_BR_NUM);
    int enq_pre = (enq_ptr_1 + MAX_BR_NUM - 1) % MAX_BR_NUM;
    io.dec_bcast->br_mask = 0;
    while (tag_list[enq_pre] != io.prf2dec->br_tag) {
      io.dec_bcast->br_mask |= 1 << tag_list[enq_ptr_1];
      tag_vec_1[tag_list[enq_ptr_1]] = true;
      LOOP_DEC(enq_ptr_1, MAX_BR_NUM);
      LOOP_DEC(enq_pre, MAX_BR_NUM);
    }
    io.dec_bcast->br_mask |= 1 << tag_list[enq_ptr_1];
    now_tag_1 = tag_list[enq_ptr_1];
    LOOP_INC(enq_ptr_1, MAX_BR_NUM);
  } else {
    io.dec_bcast->br_mask = 0;
    io.dec_bcast->mispred = false;
  }
}

void IDU::comb_flush() {
  if (io.rob_bcast->flush) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      tag_vec_1[i] = true;
    }
    tag_vec_1[0] = false;
    now_tag_1 = 0;
    deq_ptr_1 = 0;
    enq_ptr_1 = 1;
    tag_list_1[0] = 0;
  }
}

void IDU::comb_fire() {
  io.dec2front->ready = io.ren2dec->ready && !io.prf2dec->mispred;

  if (io.prf2dec->mispred || io.rob_bcast->flush) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      io.dec2ren->valid[i] = false;
    }
  }

  for (int i = 0; i < FETCH_WIDTH; i++) {
    io.dec2front->fire[i] = io.dec2ren->valid[i] && io.ren2dec->ready;
    io.dec2front->ready = io.dec2front->ready &&
                          (!io.front2dec->valid[i] || io.dec2ren->valid[i]);

    if (io.dec2front->fire[i] && is_branch(io.dec2ren->uop[i].type)) {
      now_tag_1 = alloc_tag;
      tag_vec_1[alloc_tag] = false;
      tag_list_1[enq_ptr] = alloc_tag;
      LOOP_INC(enq_ptr_1, MAX_BR_NUM);
    }
  }
}

void IDU::comb_release_tag() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (io.commit->commit_entry[i].valid &&
        is_branch(io.commit->commit_entry[i].uop.type)) {
      tag_vec_1[io.commit->commit_entry[i].uop.tag] = true;
      LOOP_INC(deq_ptr_1, MAX_BR_NUM);
    }
  }
}

void IDU::seq() {
  now_tag = now_tag_1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    tag_list[i] = tag_list_1[i];
  }
  enq_ptr = enq_ptr_1;
  deq_ptr = deq_ptr_1;
}

void decode(Inst_uop &uop, uint32_t inst) {
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

  extern long long sim_time;

  uop.instruction = inst;
  uop.dest_areg = reg_d_index;
  uop.src1_areg = reg_a_index;
  uop.src2_areg = reg_b_index;
  uop.src1_is_pc = false;
  uop.src2_is_imm = true;
  uop.func3 = number_funct3_unsigned;
  uop.func7_5 = (bool)(number_funct7_unsigned >> 5);
  uop.csr_idx = csr_idx;
  uop.page_fault_inst = false;
  uop.page_fault_load = false;
  uop.page_fault_store = false;
  uop.illegal_inst = false;
  uop.type = NONE;
  uop.amoop = AMONONE;
  uop.inst_idx = sim_time;

  switch (number_op_code_unsigned) {
  case number_0_opcode_lui: { // lui
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src1_areg = 0;
    uop.src2_en = false;
    uop.type = ADD;
    uop.func3 = 0;
    uop.func7_5 = 0;
    uop.imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.type = ADD;
    uop.func3 = 0;
    uop.func7_5 = 0;
    uop.imm = cvt_bit_to_number_unsigned(bit_immi_u_type, 32);
    break;
  }
  case number_2_opcode_jal: { // jal
    uop_num = 2;
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.src2_is_imm = true;
    uop.func3 = 0;
    uop.func7_5 = 0;
    uop.type = JAL;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_j_type, 21);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    break;
  }
  case number_3_opcode_jalr: { // jalr
    uop_num = 2;
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.src2_is_imm = true;
    uop.func3 = 0;
    uop.func7_5 = 0;
    uop.type = JALR;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    imm = cvt_bit_to_number_unsigned(bit_temp, 32);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = BR;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_b_type, 13);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = LOAD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    uop_num = 2;

    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = STORE;
    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_s_type, 12);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = ADD;

    bool bit_temp[32];
    sign_extend(bit_temp, 32, bit_immi_i_type, 12);
    uop.imm = cvt_bit_to_number_unsigned(bit_temp, 32);

    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.src2_is_imm = false;
    if (number_funct7_unsigned == 1) { // mul div
      if (number_funct3_unsigned & 0b100) {
        uop.type = DIV;
      } else {
        uop.type = MUL;
      }
    } else {
      uop.type = ADD;
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    uop.dest_en = false;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.type = NONE;
    break;
  }
  case number_10_opcode_ecall: { // ecall, ebreak, csrrw, csrrs, csrrc, csrrwi,
                                 // csrrsi, csrrci
    uop.src2_is_imm = bit_funct3[0] && (bit_funct3[2] || bit_funct3[1]);

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
          csr_idx != number_misa) {
        uop.type = NONE;
        uop.dest_en = false;
        uop.src1_en = false;
        uop.src2_en = false;

        if (csr_idx == number_time || csr_idx == number_timeh)
          uop.illegal_inst = true;

      } else {
        uop.type = CSR;
        uop.dest_en = true;
        uop.src1_en = true;
        uop.src2_en = !uop.src2_is_imm;
        uop.imm = reg_a_index;
      }
    } else {
      uop.dest_en = false;
      uop.src1_en = false;
      uop.src2_en = false;

      if (inst == INST_ECALL) {
        uop.type = ECALL;
      } else if (inst == INST_EBREAK) {
        uop.type = EBREAK;
      } else if (inst == INST_MRET) {
        uop.type = MRET;
      } else if (inst == INST_WFI) {
        uop.type = NONE;
      } else if (inst == INST_SRET) {
        uop.type = SRET;
      } else if (number_funct7_unsigned == 0b0001001 &&
                 number_funct3_unsigned == 0 && reg_d_index == 0) {
        uop.type = SFENCE_VMA;
      } else {
        uop.type = NONE;
        /*uop[0].illegal_inst = true;*/
        /*cout << hex << inst << endl;*/
        /*assert(0);*/
      }
    }
    break;
  }

  case number_11_opcode_lrw: {
    uop_num = 3;
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.imm = 0;
    uop.type = AMO;

    switch (number_funct7_unsigned >> 2) {
    case 0: { // amoadd.w
      uop.amoop = AMOADD;
      break;
    }
    case 1: { // amoswap.w
      uop.amoop = AMOSWAP;
      break;
    }
    case 2: { // lr.w
      uop_num = 1;
      uop.src2_en = false;
      uop.amoop = LR;
      break;
    }
    case 3: { // sc.w
      uop.amoop = SC;
      break;
    }
    case 4: { // amoxor.w
      uop.amoop = AMOXOR;
      break;
    }
    case 8: { // amoor.w
      uop.amoop = AMOOR;
      break;
    }
    case 12: { // amoand.w
      uop.amoop = AMOAND;
      break;
    }
    case 16: { // amomin.w
      uop.amoop = AMOMIN;
      break;
    }
    case 20: { // amomax.w
      uop.amoop = AMOMAX;
      break;
    }
    case 24: { // amominu.w
      uop.amoop = AMOMINU;
      break;
    }
    case 28: { // amomaxu.w
      uop.amoop = AMOMAXU;
      break;
    }
    }

    break;
  }

  default: {
    uop.dest_en = false;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.type = NONE;
    uop.illegal_inst = true;
    break;
  }
  }

  uop.uop_num = uop_num;

  // amo 指令dest为0时特殊处理
  if (uop.type == AMO && uop.dest_areg == 0 && uop.amoop != LR &&
      uop.amoop != SC) {
    uop.dest_areg = 32;
  }

  // 不写0寄存器
  if (uop.dest_areg == 0)
    uop.dest_en = false;
}
