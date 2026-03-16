#include "Idu.h"
#include "Csr.h"
#include "RISCV.h"
#include "config.h"
#include "ref.h"
#include "util.h"
#include <cstdint>
#include <cstdlib>

// 中间信号
static tag_t alloc_tag[DECODE_WIDTH]; // 分配的新 Tag

void Idu::init() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = true;
    tag_vec_1[i] = true;
    br_mask_cp[i] = 0;
    br_mask_cp_1[i] = 0;
  }
  tag_vec[0] = false;
  tag_vec_1[0] = false;
  now_br_mask = 0;
  now_br_mask_1 = 0;
  pending_free_mask = 0;
  pending_free_mask_1 = 0;
  br_latch = {};
}

// 译码并分配 Tag
void Idu::comb_decode() {
  wire<1> alloc_valid[DECODE_WIDTH];
  int alloc_num = 0;
  for (int i = 0; i < MAX_BR_NUM && alloc_num < max_br_per_cycle; i++) {
    if (tag_vec[i]) {
      alloc_tag[alloc_num] = i;
      alloc_valid[alloc_num] = true;
      alloc_num++;
    }
  }
  for (int i = alloc_num; i < DECODE_WIDTH; i++) {
    alloc_tag[i] = 0;
    alloc_valid[i] = false;
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    out.dec2ren->valid[i] = false;
    out.dec2ren->uop[i] = {};
  }

  Assert(in.issue != nullptr && "Idu::comb_decode: issue input is null");
  for (int i = 0; i < DECODE_WIDTH; i++) {
    const InstructionBufferEntry &entry = in.issue->entries[i];
    if (!entry.valid)
      continue;

    out.dec2ren->valid[i] = true;
    InstInfo decoded = {};
    if (entry.page_fault_inst) {
      decoded.diag_val = entry.inst;
      decoded.uop_num = 1;
      decoded.page_fault_inst = true;
      decoded.page_fault_load = false;
      decoded.page_fault_store = false;
      decoded.type = NOP;
      decoded.src1_en = false;
      decoded.src2_en = false;
      decoded.dest_en = false;
      decoded.dbg.instruction = entry.inst;
    } else {
      decode(decoded, entry.inst);
    }
    decoded.pc = entry.pc;
    decoded.ftq_idx = entry.ftq_idx;
    decoded.ftq_offset = entry.ftq_offset;
    decoded.ftq_is_last = entry.ftq_is_last;
    out.dec2ren->uop[i] = DecRenIO::DecRenInst::from_inst_info(decoded);
  }

  int br_num = 0;
#ifdef CONFIG_BPU
  auto needs_br_tag = [&](InstType t) { return is_branch(t); };
#else
  // Oracle mode: disable branch-tag resource pressure.
  auto needs_br_tag = [&](InstType) { return false; };
#endif
  // ID 阶段旁路清理：本拍已解析分支的 bit 不应继续传播到新译码指令。
  // clear_mask 来自上拍锁存的 BRU 解析结果（br_latch）。
  mask_t clear = br_latch.clear_mask;
  mask_t running_mask = now_br_mask & ~clear;
  bool stall = false;
  int i = 0;
  for (; i < DECODE_WIDTH; i++) {
    if (!out.dec2ren->valid[i]) {
      out.dec2ren->uop[i].br_id = 0;
      out.dec2ren->uop[i].br_mask = running_mask;
      continue;
    }

    if (needs_br_tag(out.dec2ren->uop[i].type)) {
      if (!alloc_valid[br_num]) {
#ifdef CONFIG_PERF_COUNTER
        ctx->perf.idu_tag_stall++;
        ctx->perf.stall_br_id_cycles++;
#endif
        stall = true;
        break;
      }
      tag_t new_tag = alloc_tag[br_num];
      out.dec2ren->uop[i].br_id = new_tag;
      // 分支自身不依赖自己；self bit 只作用于后续更年轻指令。
      out.dec2ren->uop[i].br_mask = running_mask;
      running_mask |= (mask_t(1) << new_tag);
      br_num++;
    } else {
      out.dec2ren->uop[i].br_id = 0;
      out.dec2ren->uop[i].br_mask = running_mask;
    }
  }

  if (stall) {
    for (; i < DECODE_WIDTH; i++) {
      out.dec2ren->valid[i] = false;
      out.dec2ren->uop[i].br_id = 0;
      out.dec2ren->uop[i].br_mask = 0;
    }
  }
}

void Idu::comb_branch() {
  // Init next state
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec_1[i] = tag_vec[i];
    br_mask_cp_1[i] = br_mask_cp[i];
  }
  now_br_mask_1 = now_br_mask;
  pending_free_mask_1 = pending_free_mask;

  // 0. 先应用上拍累积的释放请求（延迟一拍生效）
  mask_t matured_free = pending_free_mask;
  for (int i = 1; i < MAX_BR_NUM; i++) {
    if ((matured_free >> i) & 1) {
      tag_vec_1[i] = true;
      pending_free_mask_1 &= ~(mask_t(1) << i);
    }
  }

  // 1. 处理 clear_mask: 所有已解析的 branch 立即释放 (IDU 本地状态)
  mask_t clear = br_latch.clear_mask;
  for (int i = 1; i < MAX_BR_NUM; i++) {
    if ((clear >> i) & 1) {
      // 延迟到下一拍再真正释放 tag_vec，避免同拍复用
      pending_free_mask_1 |= (mask_t(1) << i);
      now_br_mask_1 &= ~(mask_t(1) << i);
    }
  }

  // 1.5. 全局更新 br_mask_cp：已解析分支的 bit 从所有快照中清除
  //      硬件实现：每个 br_mask_cp 寄存器加一个 AND 门，清除 clear_mask
  //      对应的位 这防止了 tag 被复用后，旧快照仍然"保护"新指令的问题
  if (clear != 0) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      br_mask_cp_1[i] &= ~clear;
    }
  }

  // 2. 处理误预测
  if (br_latch.mispred) {
    out.dec_bcast->mispred = true;
    out.dec_bcast->br_id = br_latch.br_id;
    out.dec_bcast->redirect_rob_idx = br_latch.redirect_rob_idx;
    out.dec_bcast->br_mask = 1ULL << br_latch.br_id;

    // 释放误预测分支之后分配的更年轻的 tag
    mask_t tags_to_free = now_br_mask & ~br_mask_cp[br_latch.br_id];
    now_br_mask_1 &= ~tags_to_free;
    // 同样延迟到下一拍释放空闲位图
    pending_free_mask_1 |= tags_to_free;
  } else {
    out.dec_bcast->br_mask = 0;
    out.dec_bcast->mispred = false;
    out.dec_bcast->br_id = 0;
  }

  // 广播 clear_mask（包含误预测分支的 bit）
  // 下游模块负责: 先 flush，再对存活条目清除 bit
  out.dec_bcast->clear_mask = clear;
}

void Idu::comb_flush() {
  Assert(in.rob_bcast != nullptr && "Idu::comb_flush: rob_bcast is null");
  if (in.rob_bcast->flush) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      tag_vec_1[i] = true;
    }
    tag_vec_1[0] = false;
    now_br_mask_1 = 0;
    pending_free_mask_1 = 0;
  }
}

void Idu::comb_fire() {
  Assert(in.ren2dec != nullptr && "Idu::comb_fire: ren2dec is null");
  Assert(in.rob_bcast != nullptr && "Idu::comb_fire: rob_bcast is null");
  if (br_latch.mispred || in.rob_bcast->flush) {
    for (int i = 0; i < DECODE_WIDTH; i++) {
      out.dec2ren->valid[i] = false;
    }
    return;
  }

  int br_num = 0;
#ifdef CONFIG_BPU
  auto needs_br_tag = [&](InstType t) { return is_branch(t); };
#else
  // Oracle mode: no branch-tag allocation in fire path.
  auto needs_br_tag = [&](InstType) { return false; };
#endif
  for (int i = 0; i < DECODE_WIDTH; i++) {
    wire<1> fire = out.dec2ren->valid[i] && in.ren2dec->ready;
    if (fire && needs_br_tag(out.dec2ren->uop[i].type)) {
      tag_t new_tag = alloc_tag[br_num];
      tag_vec_1[new_tag] = false;
      now_br_mask_1 |= (mask_t(1) << new_tag);
      br_mask_cp_1[new_tag] = now_br_mask_1;
      br_num++;
    }
  }

}

void Idu::seq() {
  now_br_mask = now_br_mask_1;
  pending_free_mask = pending_free_mask_1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    br_mask_cp[i] = br_mask_cp_1[i];
  }

  // Latch Exu Branch Result
  Assert(in.rob_bcast != nullptr && "Idu::seq: rob_bcast is null");
  Assert(in.exu2id != nullptr && "Idu::seq: exu2id is null");
  if (!in.rob_bcast->flush) {
    br_latch.mispred = in.exu2id->mispred;
    br_latch.redirect_pc = in.exu2id->redirect_pc;
    br_latch.redirect_rob_idx = in.exu2id->redirect_rob_idx;
    br_latch.br_id = in.exu2id->br_id;
    br_latch.ftq_idx = in.exu2id->ftq_idx;
    br_latch.clear_mask = in.exu2id->clear_mask;
  } else {
    br_latch = {};
  }
}

void Idu::decode(InstInfo &uop, uint32_t inst) {
  // 操作数来源以及type
  // uint32_t imm;
  int uop_num = 1;
  uop.dbg.instruction = inst;
  uop.dbg.difftest_skip = false;

  uint32_t opcode = BITS(inst, 6, 0);
  uint32_t number_funct3_unsigned = BITS(inst, 14, 12);
  uint32_t number_funct7_unsigned = BITS(inst, 31, 25);
  uint32_t reg_d_index = BITS(inst, 11, 7);
  uint32_t reg_a_index = BITS(inst, 19, 15);
  uint32_t reg_b_index = BITS(inst, 24, 20);
  uint32_t csr_idx = inst >> 20;

  // 准备立即数
  uop.diag_val = inst;
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
  uop.dbg.inst_idx = global_inst_idx++;

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
    // srli, srai, AND Zbb/Zbs imm extensions (clz, ctz, pcnt, sext, bseti,
    // bclri, binvi)
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = ADD;
    uop.imm = immI(inst);
    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
    // AND Zba/Zbb/Zbc/Zbs extensions (sh1add, clmul, xnor, pack, min, max, etc)
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
        uop.type = WFI;
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

  case number_12_opcode_float: {
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.src2_is_imm = false;
    uop.type = FP;
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
