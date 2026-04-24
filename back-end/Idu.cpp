#include "Idu.h"
#include "Csr.h"
#include "RISCV.h"
#include "config.h"
#include "ref.h"
#include "util.h"
#include <cstdint>
#include <cstdlib>

// 中间信号
#ifdef CONFIG_BPU
static wire<BR_TAG_WIDTH> alloc_tag[DECODE_WIDTH]; // 分配的新 Tag
#endif

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
  br_latch_1 = {};
}

/*
 * comb_begin
 * 功能: 组合阶段开始时，将时序态镜像到 *_1 工作副本，作为本拍组合逻辑的可写基线。
 * 输入依赖: tag_vec, br_mask_cp, now_br_mask, pending_free_mask, br_latch。
 * 输出更新: tag_vec_1, br_mask_cp_1, now_br_mask_1, pending_free_mask_1, br_latch_1。
 * 约束: 仅做状态复制，不分配/释放分支 Tag，不驱动对外接口信号。
 */
void Idu::comb_begin() {
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec_1[i] = tag_vec[i];
    br_mask_cp_1[i] = br_mask_cp[i];
  }
  now_br_mask_1 = now_br_mask;
  pending_free_mask_1 = pending_free_mask;
  br_latch_1 = br_latch;
}

/*
 * comb_decode
 * 功能: 译码 in.issue 指令并生成 dec2ren uop，同时为分支指令预分配 br_id/br_mask（遇 Tag 不足时截断）。
 * 输入依赖: in.issue->entries, br_latch.clear_mask, now_br_mask, tag_vec, max_br_per_cycle。
 * 输出更新: out.dec2ren->valid/uop, alloc_tag（供 comb_fire 在 fire 时提交分配）。
 * 约束: 每拍最多分配 max_br_per_cycle 个分支 Tag, Tag 不足时后续槽位 valid 置 0。
 */
void Idu::comb_decode() {
#ifndef CONFIG_BPU
  for (int i = 0; i < DECODE_WIDTH; i++) {
    out.dec2ren->valid[i] = false;
    out.dec2ren->uop[i] = {};
  }

  for (int i = 0; i < DECODE_WIDTH; i++) {
    const InstructionBufferEntry &entry = in.issue->entries[i];
    if (!entry.valid)
      continue;

    out.dec2ren->valid[i] = true;
    auto &decoded = out.dec2ren->uop[i];
    decoded = {};
    if (entry.page_fault_inst) {
      decoded.diag_val = entry.inst;
      decoded.page_fault_inst = true;
      decoded.type = encode_inst_type(NOP);
      decoded.src1_en = false;
      decoded.src2_en = false;
      decoded.dest_en = false;
      decoded.dbg.instruction = entry.inst;
    } else {
      decode(decoded, entry.inst);
    }
    decoded.dbg.pc = entry.pc;
    decoded.ftq_idx = entry.ftq_idx;
    decoded.ftq_offset = entry.ftq_offset;
    decoded.ftq_is_last = entry.ftq_is_last;
    decoded.br_id = 0;
    decoded.br_mask = 0;
  }
  return;
#else
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

  for (int i = 0; i < DECODE_WIDTH; i++) {
    const InstructionBufferEntry &entry = in.issue->entries[i];
    if (!entry.valid)
      continue;

    out.dec2ren->valid[i] = true;
    auto &decoded = out.dec2ren->uop[i];
    decoded = {};
    if (entry.page_fault_inst) {
      decoded.diag_val = entry.inst;
      decoded.page_fault_inst = true;
      decoded.type = encode_inst_type(NOP);
      decoded.src1_en = false;
      decoded.src2_en = false;
      decoded.dest_en = false;
      decoded.dbg.instruction = entry.inst;
    } else {
      decode(decoded, entry.inst);
    }
    decoded.dbg.pc = entry.pc;
    decoded.ftq_idx = entry.ftq_idx;
    decoded.ftq_offset = entry.ftq_offset;
    decoded.ftq_is_last = entry.ftq_is_last;
  }

  int br_num = 0;
  // ID 阶段旁路清理：本拍已解析分支的 bit 不应继续传播到新译码指令。
  // clear_mask 来自上拍锁存的 BRU 解析结果（br_latch）。
  wire<BR_MASK_WIDTH> clear = br_latch.clear_mask;
  wire<BR_MASK_WIDTH> running_mask = now_br_mask & ~clear;
  bool stall = false;
  int i = 0;
  for (; i < DECODE_WIDTH; i++) {
    if (!out.dec2ren->valid[i]) {
      out.dec2ren->uop[i].br_id = 0;
      out.dec2ren->uop[i].br_mask = running_mask;
      continue;
    }

    if (is_branch(out.dec2ren->uop[i].type)) {
      if (!alloc_valid[br_num]) {
#ifdef CONFIG_PERF_COUNTER
        ctx->perf.idu_tag_stall++;
        ctx->perf.stall_br_id_cycles++;
#endif
        stall = true;
        break;
      }
      wire<BR_TAG_WIDTH> new_tag = alloc_tag[br_num];
      out.dec2ren->uop[i].br_id = new_tag;
      // 分支自身不依赖自己；self bit 只作用于后续更年轻指令。
      out.dec2ren->uop[i].br_mask = running_mask;
      running_mask |= (wire<BR_MASK_WIDTH>(1) << new_tag);
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
#endif
}

/*
 * comb_branch
 * 功能: 将锁存的分支解析结果打包并广播到 dec_bcast（含 mispred 与 clear_mask）。
 * 输入依赖: br_latch.{mispred, br_id, redirect_rob_idx, clear_mask}。
 * 输出更新: out.dec_bcast->mispred, br_id, redirect_rob_idx, br_mask, clear_mask。
 * 约束: 本函数仅广播，不修改 IDU 本地 Tag/Mask 状态；本地状态更新统一由 comb_fire 处理。
 */
void Idu::comb_branch() {
  // comb_branch 仅负责广播分支结果；IDU本地状态更新统一在 comb_fire 处理。
  wire<BR_MASK_WIDTH> clear = br_latch.clear_mask;
  if (br_latch.mispred) {
    out.dec_bcast->mispred = true;
    out.dec_bcast->br_id = br_latch.br_id;
    out.dec_bcast->redirect_rob_idx = br_latch.redirect_rob_idx;
    out.dec_bcast->br_mask = 1ULL << br_latch.br_id;
  } else {
    out.dec_bcast->br_mask = 0;
    out.dec_bcast->mispred = false;
    out.dec_bcast->br_id = 0;
  }

  // 广播 clear_mask（包含误预测分支的 bit）
  // 下游模块负责: 先 flush，再对存活条目清除 bit
  out.dec_bcast->clear_mask = clear;
}

/*
 * comb_fire
 * 功能: 在 ready/flush/分支解析条件下推进 IDU 分支状态机（Tag 释放、误预测回收、新分支提交分配）。
 * 输入依赖: in.rob_bcast->flush, in.ren2dec->ready, out.dec2ren->valid/uop, alloc_tag, br_latch, now_br_mask, br_mask_cp, pending_free_mask。
 * 输出更新: tag_vec_1, now_br_mask_1, br_mask_cp_1, pending_free_mask_1, br_latch_1。
 * 约束: flush 最高优先级并直接返回；mispred 路径不进行新分支分配；仅对 fire 且为分支的槽位提交 Tag 占用。
 */
void Idu::comb_fire() {
  for (int i = 0; i < DECODE_WIDTH; i++) {
    out.idu_consume->fire[i] = false;
  }

  if (!in.rob_bcast->flush) {
    br_latch_1 = *in.exu2id;
  } else {
    br_latch_1 = {};
  }

  // 0. flush 最高优先级：清空本地分支状态。
  if (in.rob_bcast->flush) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      tag_vec_1[i] = true;
    }
    tag_vec_1[0] = false;
    now_br_mask_1 = 0;
    pending_free_mask_1 = 0;
    return;
  }

  // 1. 先应用上拍累积的释放请求（延迟一拍生效）
  wire<BR_MASK_WIDTH> matured_free = pending_free_mask;
  for (int i = 1; i < MAX_BR_NUM; i++) {
    if ((matured_free >> i) & 1) {
      tag_vec_1[i] = true;
      pending_free_mask_1 &= ~(wire<BR_MASK_WIDTH>(1) << i);
    }
  }

  // 2. clear_mask 生效：已解析分支从当前运行集合清除，并延迟释放空闲位图。
  wire<BR_MASK_WIDTH> clear = br_latch.clear_mask;
  for (int i = 1; i < MAX_BR_NUM; i++) {
    if ((clear >> i) & 1) {
      pending_free_mask_1 |= (wire<BR_MASK_WIDTH>(1) << i);
      now_br_mask_1 &= ~(wire<BR_MASK_WIDTH>(1) << i);
    }
  }

  // 3. 清理所有 checkpoint 中对应 clear_mask 的 bit，避免 tag 复用污染。
  if (clear != 0) {
    for (int i = 1; i < MAX_BR_NUM; i++) {
      br_mask_cp_1[i] &= ~clear;
    }
  }

  // 4. 误预测：回收更年轻 tag；本拍不再进行新分支分配。
  if (br_latch.mispred) {
    wire<BR_MASK_WIDTH> tags_to_free =
        now_br_mask & ~br_mask_cp[br_latch.br_id];
    now_br_mask_1 &= ~tags_to_free;
    pending_free_mask_1 |= tags_to_free;
    return;
  }

  // 5. 正常发射路径：所有成功握手的槽位都要通知 PreIduQueue 出队；
  // 分支 tag 的推进仅在启用 BPU 时生效。
#ifdef CONFIG_BPU
  int br_num = 0;
#endif
  for (int i = 0; i < DECODE_WIDTH; i++) {
    wire<1> fire = out.dec2ren->valid[i] && in.ren2dec->ready;
    out.idu_consume->fire[i] = fire;
#ifdef CONFIG_BPU
    if (fire && is_branch(out.dec2ren->uop[i].type)) {
      wire<BR_TAG_WIDTH> new_tag = alloc_tag[br_num];
      tag_vec_1[new_tag] = false;
      now_br_mask_1 |= (wire<BR_MASK_WIDTH>(1) << new_tag);
      br_mask_cp_1[new_tag] = now_br_mask_1;
      br_num++;
    }
#endif
  }
}

void Idu::seq() {
  now_br_mask = now_br_mask_1;
  pending_free_mask = pending_free_mask_1;
  for (int i = 0; i < MAX_BR_NUM; i++) {
    tag_vec[i] = tag_vec_1[i];
    br_mask_cp[i] = br_mask_cp_1[i];
  }
  br_latch = br_latch_1;
}

void Idu::decode(DecRenIO::DecRenInst &uop, uint32_t inst) {
  // 操作数来源以及type
  // uint32_t imm;
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
  uop.illegal_inst = false;
  uop.type = encode_inst_type(NOP);
  uop.tma.is_cache_miss = false;
  uop.tma.is_ret = false;
  uop.tma.mem_commit_is_load = false;
  uop.tma.mem_commit_is_store = false;
  uop.dbg.mem_align_mask = 0;
  static uint64_t global_inst_idx = 0;
  uop.dbg.inst_idx = global_inst_idx++;

  switch (opcode) {
  case number_0_opcode_lui: { // lui
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src1_areg = 0;
    uop.src2_en = false;
    uop.type = encode_inst_type(ADD);
    uop.func3 = 0;
    uop.imm = immU(inst);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.type = encode_inst_type(ADD);
    uop.func3 = 0;
    uop.imm = immU(inst);
    break;
  }
  case number_2_opcode_jal: { // jal
    uop.dest_en = true;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.src2_is_imm = true;
    uop.func3 = 0;
    uop.type = encode_inst_type(JAL);
    uop.imm = immJ(inst);
    break;
  }
  case number_3_opcode_jalr: { // jalr
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.src1_is_pc = true;
    uop.src2_is_imm = true;
    uop.func3 = 0;
    uop.type = encode_inst_type(JALR);
    uop.imm = immI(inst);

    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = encode_inst_type(BR);
    uop.imm = immB(inst);
    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = encode_inst_type(LOAD);
    uop.imm = immI(inst);
    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    uop.dest_en = false;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.type = encode_inst_type(STORE);
    uop.imm = immS(inst);
    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
    // srli, srai, AND Zbb/Zbs imm extensions (clz, ctz, pcnt, sext, bseti,
    // bclri, binvi)
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = false;
    uop.type = encode_inst_type(ADD);
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
        uop.type = encode_inst_type(DIV);
      } else {
        uop.type = encode_inst_type(MUL);
      }
    } else {
      uop.type = encode_inst_type(ADD);
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    uop.dest_en = false;
    uop.src1_en = false;
    uop.src2_en = false;

    // Check funct3 for FENCE.I (001)
    if (number_funct3_unsigned == 0b001) {
      uop.type = encode_inst_type(FENCE_I); // Strict separation
    } else {
      uop.type = encode_inst_type(FENCE);
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
        uop.type = encode_inst_type(NOP);
        uop.dest_en = false;
        uop.src1_en = false;
        uop.src2_en = false;

        if (csr_idx == number_time || csr_idx == number_timeh)
          uop.illegal_inst = true;

      } else {
        uop.type = encode_inst_type(CSR);
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
        uop.type = encode_inst_type(ECALL);
      } else if (inst == INST_EBREAK) {
        uop.type = encode_inst_type(EBREAK);
      } else if (inst == INST_MRET) {
        uop.type = encode_inst_type(MRET);
      } else if (inst == INST_WFI) {
        uop.type = encode_inst_type(WFI);
      } else if (inst == INST_SRET) {
        uop.type = encode_inst_type(SRET);
      } else if (number_funct7_unsigned == 0b0001001 &&
                 number_funct3_unsigned == 0 && reg_d_index == 0) {
        uop.type = encode_inst_type(SFENCE_VMA);
        uop.src1_en = true;
        uop.src2_en = true;
      } else {
        uop.type = encode_inst_type(NOP);
        /*uop[0].illegal_inst = true;*/
        /*cout << hex << inst << endl;*/
        /*Assert(0);*/
      }
    }
    break;
  }

  case number_11_opcode_lrw: {
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.imm = 0;
    uop.type = encode_inst_type(AMO);
    uop.is_atomic = true;

    if ((number_funct7_unsigned >> 2) == AmoOp::LR) {
      uop.src2_en = false;
    }

    break;
  }

  case number_12_opcode_float: {
    uop.dest_en = true;
    uop.src1_en = true;
    uop.src2_en = true;
    uop.src2_is_imm = false;
    // Preserve encoded rs2 (fmt selector) for FCVT/other unary FP ops.
    uop.imm = reg_b_index;
    if (number_funct7_unsigned == 0x60 || number_funct7_unsigned == 0x68 ||
        number_funct7_unsigned == 0x2C || number_funct7_unsigned == 0x70) {
      uop.src2_en = false;
    }
    uop.type = encode_inst_type(FP);
    break;
  }

  default: {
    uop.dest_en = false;
    uop.src1_en = false;
    uop.src2_en = false;
    uop.type = encode_inst_type(NOP);
    uop.illegal_inst = true;
    break;
  }
  }

  InstType inst_type = decode_inst_type(uop.type);
  uop.tma.is_ret = (inst_type == JALR && uop.src1_areg == 1 &&
                    uop.dest_areg == 0 && uop.imm == 0);
  uop.tma.mem_commit_is_load =
      (inst_type == LOAD ||
       (inst_type == AMO && (uop.func7 >> 2) != AmoOp::SC));
  uop.tma.mem_commit_is_store =
      (inst_type == STORE ||
       (inst_type == AMO && (uop.func7 >> 2) != AmoOp::LR));
  if (uop.tma.mem_commit_is_load) {
    uop.dbg.mem_align_mask = (uop.func3 & 0x3) == 0   ? 0
                             : (uop.func3 & 0x3) == 1 ? 1
                                                      : 3;
  }

  if (inst_type == AMO && uop.dest_areg == 0 && (uop.func7 >> 2) != AmoOp::LR &&
      (uop.func7 >> 2) != AmoOp::SC) {
    uop.dest_areg = 32;
  }

  if (uop.dest_areg == 0)
    uop.dest_en = false;
}
