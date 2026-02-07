#include "Csr.h"
#include "config.h"
#include "ref.h"
#include <Idu.h>
#include <RISCV.h>
#include <cstdint>
#include <cstdlib>
#include <util.h>

// 中间信号
static tag_t alloc_tag[FETCH_WIDTH]; // 分配的新 Tag

void decode(InstUop &uop, uint32_t instructinn);

void Idu::init() {
  for (int i = 1; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
  }

  tag_vec_1[0] = false;
  now_tag_1 = now_tag = 0;
  enq_ptr_1 = enq_ptr = 1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_list[i] = 0;
    tag_list_1[i] = 0;
  }
}

// 译码并分配 Tag
void Idu::comb_decode() {

  wire<1> alloc_valid[FETCH_WIDTH];
  int alloc_num = 0;
  int i;
  for (i = 0; i < MAX_BR_NUM && alloc_num < max_br_per_cycle; i++) {
    if (tag_vec[i]) {
      alloc_tag[alloc_num] = i;
      alloc_valid[alloc_num] = true;
      alloc_num++;
    }
  }

  for (int i = alloc_num; i < FETCH_WIDTH; i++) {
    alloc_tag[i] = 0;
    alloc_valid[i] = false;
  }

  for (i = 0; i < FETCH_WIDTH; i++) {
    if (in.front2dec->valid[i]) {
      out.dec2ren->valid[i] = true;
      if (in.front2dec->page_fault_inst[i]) {
        out.dec2ren->uop[i].uop_num = 1;
        out.dec2ren->uop[i].page_fault_inst = true;
        out.dec2ren->uop[i].page_fault_load = false;
        out.dec2ren->uop[i].page_fault_store = false;
        out.dec2ren->uop[i].type = NOP;
        out.dec2ren->uop[i].src1_en = out.dec2ren->uop[i].src2_en =
            out.dec2ren->uop[i].dest_en = false;
      } else {
        // 实际电路中4个译码电路每周期无论是否valid都会运行
        decode(out.dec2ren->uop[i], in.front2dec->inst[i]);
      }
    } else {
      out.dec2ren->valid[i] = false;
      continue;
    }

    out.dec2ren->uop[i].pc = in.front2dec->pc[i];
    out.dec2ren->uop[i].pred_br_taken = in.front2dec->predict_dir[i];
    out.dec2ren->uop[i].alt_pred = in.front2dec->alt_pred[i];
    out.dec2ren->uop[i].altpcpn = in.front2dec->altpcpn[i];
    out.dec2ren->uop[i].pcpn = in.front2dec->pcpn[i];
    for (int j = 0; j < 4; j++) { // TN_MAX = 4
      out.dec2ren->uop[i].tage_idx[j] = in.front2dec->tage_idx[i][j];
    }

    out.dec2ren->uop[i].pred_br_pc =
        in.front2dec->predict_next_fetch_address[i];

    // for debug
    if (is_branch(out.dec2ren->uop[i].type)) {
      out.dec2ren->uop[i].pc_next = out.dec2ren->uop[i].pred_br_pc;
    } else {
      out.dec2ren->uop[i].pc_next = out.dec2ren->uop[i].pc + 4;
    }
  }

  int br_num = 0;
  bool stall = false;
  for (i = 0; i < FETCH_WIDTH; i++) {
    out.dec2ren->uop[i].tag = (br_num == 0) ? now_tag : alloc_tag[br_num - 1];
    if (in.front2dec->valid[i] && is_branch(out.dec2ren->uop[i].type)) {
      if (!alloc_valid[br_num]) {
#ifdef CONFIG_PERF_COUNTER
        ctx->perf.idu_tag_stall++;
#endif
        stall = true;
        break;
      } else {
        br_num++;
      }
    }
  }

  if (stall) {
    for (; i < FETCH_WIDTH; i++) {
      out.dec2ren->valid[i] = false;
      out.dec2ren->uop[i].tag = 0;
    }
  }
}

void Idu::comb_branch() {
  // Init next state
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec_1[i] = tag_vec[i];
    tag_list_1[i] = tag_list[i];
  }
  enq_ptr_1 = enq_ptr;
  now_tag_1 = now_tag;

  // 如果一周期实现不方便，可以用状态机多周期实现
  if (in.prf2dec->mispred) {
    out.dec_bcast->mispred = true;
    out.dec_bcast->br_tag = in.prf2dec->br_tag;
    out.dec_bcast->redirect_rob_idx = in.prf2dec->redirect_rob_idx;

    LOOP_DEC(enq_ptr_1, MAX_BR_NUM);
    int enq_pre = (enq_ptr_1 + MAX_BR_NUM - 1) % MAX_BR_NUM;
    out.dec_bcast->br_mask = 0;
    // TODO: 换while
    while (tag_list[enq_pre] != in.prf2dec->br_tag) {
      out.dec_bcast->br_mask |= 1ULL << tag_list[enq_ptr_1];
      tag_vec_1[tag_list[enq_ptr_1]] = true;
      LOOP_DEC(enq_ptr_1, MAX_BR_NUM);
      LOOP_DEC(enq_pre, MAX_BR_NUM);
    }
    out.dec_bcast->br_mask |= 1 << tag_list[enq_ptr_1];
    now_tag_1 = tag_list[enq_ptr_1];
    LOOP_INC(enq_ptr_1, MAX_BR_NUM);
  } else {
    out.dec_bcast->br_mask = 0;
    out.dec_bcast->mispred = false;
  }
}

void Idu::comb_flush() {
  if (in.rob_bcast->flush) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      tag_vec_1[i] = true;
    }
    tag_vec_1[0] = false;
    now_tag_1 = 0;
    enq_ptr_1 = 1;
    tag_list_1[0] = 0;
  }
}

void Idu::comb_fire() {
  out.dec2front->ready = in.ren2dec->ready && !in.prf2dec->mispred;

  if (in.prf2dec->mispred || in.rob_bcast->flush) {
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out.dec2ren->valid[i] = false;
    }
  }

  int br_num = 0;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out.dec2front->fire[i] = out.dec2ren->valid[i] && in.ren2dec->ready;
    out.dec2front->ready = out.dec2front->ready &&
                           (!in.front2dec->valid[i] || out.dec2ren->valid[i]);

    if (out.dec2front->fire[i] && is_branch(out.dec2ren->uop[i].type)) {
      now_tag_1 = alloc_tag[br_num];
      tag_vec_1[alloc_tag[br_num]] = false;
      tag_list_1[enq_ptr_1] = alloc_tag[br_num];
      LOOP_INC(enq_ptr_1, MAX_BR_NUM);
      br_num++;
    }
  }
}

void Idu::comb_release_tag() {
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    if (in.commit->commit_entry[i].valid &&
        is_branch(in.commit->commit_entry[i].uop.type)) {
      tag_vec_1[in.commit->commit_entry[i].uop.tag] = true;
    }
  }
}

void Idu::seq() {
  now_tag = now_tag_1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    tag_list[i] = tag_list_1[i];
  }
  enq_ptr = enq_ptr_1;
}

void Idu::decode(InstUop &uop, uint32_t inst) {
  // 操作数来源以及type
  // uint32_t imm;
  int uop_num = 1;
  uop.difftest_skip = false;

  uint32_t opcode = BITS(inst, 6, 0);
  uint32_t number_funct3_unsigned = BITS(inst, 14, 12);
  uint32_t number_funct7_unsigned = BITS(inst, 31, 25);
  uint32_t reg_d_index = BITS(inst, 11, 7);
  uint32_t reg_a_index = BITS(inst, 19, 15);
  uint32_t reg_b_index = BITS(inst, 24, 20);
  uint32_t csr_idx = inst >> 20;

  // 准备立即数
  uop.instruction = inst;
  uop.dest_areg = reg_d_index;
  uop.src1_areg = reg_a_index;
  uop.src2_areg = reg_b_index;
  uop.src1_is_pc = false;
  uop.src2_is_imm = true;
  uop.func3 = number_funct3_unsigned;
  uop.func7 = number_funct7_unsigned;
  uop.csr_idx = csr_idx;
  uop.page_fault_inst = false;
  uop.page_fault_load = false;
  uop.page_fault_store = false;
  uop.illegal_inst = false;
  uop.type = NOP;
  static uint64_t global_inst_idx = 0;
  uop.inst_idx = global_inst_idx++;

  switch (opcode) {
  case number_0_opcode_lui: { // lui
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src1_areg = 0;
    uop.src2_en = false;
    uop.type = ADD;
    uop.func3 = 0;
    uop.imm = immU(inst);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.type = ADD;
    uop.func3 = 0;
    uop.imm = immU(inst);
    break;
  }
  case number_2_opcode_jal: { // jal
    uop_num = 2;              // 前端pre-decode预先解决jal
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.src2_is_imm = true;
    uop.func3 = 0;
    uop.type = JAL;
    uop.imm = immJ(inst);
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
    uop.type = JALR;
    uop.imm = immI(inst);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = BR;
    uop.imm = immB(inst);
    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = LOAD;
    uop.imm = immI(inst);
    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    uop_num = 2;
    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = STORE;
    uop.imm = immS(inst);
    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = ADD;
    uop.imm = immI(inst);
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

    // Check funct3 for FENCE.I (001)
    if (number_funct3_unsigned == 0b001) {
      uop.type = FENCE_I; // Strict separation
    } else {
      uop.type = NOP; // Ordinary FENCE is NOP
    }
    break;
  }
  case number_10_opcode_ecall: { // ecall, ebreak, csrrw, csrrs, csrrc,
                                 // csrrwi, csrrsi, csrrci
    uop.src2_is_imm =
        number_funct3_unsigned & 0b100 &&
        (number_funct3_unsigned & 0b001 || number_funct3_unsigned & 0b010);

    if (number_funct3_unsigned & 0b001 || number_funct3_unsigned & 0b010) {
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
        uop.type = NOP;
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
        uop.type = NOP;
      } else if (inst == INST_SRET) {
        uop.type = SRET;
      } else if (number_funct7_unsigned == 0b0001001 &&
                 number_funct3_unsigned == 0 && reg_d_index == 0) {
        uop.type = SFENCE_VMA;
        uop.src1_en = true;
        uop.src2_en = true;
      } else {
        uop.type = NOP;
        /*uop[0].illegal_inst = true;*/
        /*cout << hex << inst << endl;*/
        /*Assert(0);*/
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

    if ((number_funct7_unsigned >> 2) == AmoOp::LR) {
      uop_num = 1;
      uop.src2_en = false;
    }

    break;
  }

  default: {
    uop.dest_en = false;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.type = NOP;
    uop.illegal_inst = true;
    break;
  }
  }

  uop.uop_num = uop_num;

  if (uop.type == AMO && uop.dest_areg == 0 && (uop.func7 >> 2) != AmoOp::LR &&
      (uop.func7 >> 2) != AmoOp::SC) {
    uop.dest_areg = 32;
  }

  if (uop.dest_areg == 0)
    uop.dest_en = false;
}

IduIO Idu::get_hardware_io() {
  IduIO hardware;

  // --- Inputs ---
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.from_front.valid[i] = in.front2dec->valid[i];
    hardware.from_front.inst[i] = in.front2dec->inst[i];
  }
  hardware.from_ren.ready = in.ren2dec->ready;

  hardware.from_back.flush = in.rob_bcast->flush;
  hardware.from_back.mispred = in.prf2dec->mispred;
  hardware.from_back.br_tag = in.prf2dec->br_tag;

  // --- Outputs ---
  hardware.to_front.ready = out.dec2front->ready;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    hardware.to_front.fire[i] = out.dec2front->fire[i];

    hardware.to_ren.valid[i] = out.dec2ren->valid[i];
    hardware.to_ren.uop[i] = DecRenUop::filter(out.dec2ren->uop[i]);
  }

  hardware.to_back.mispred = out.dec_bcast->mispred;
  hardware.to_back.br_mask = out.dec_bcast->br_mask;
  hardware.to_back.br_tag = out.dec_bcast->br_tag;

  return hardware;
}
