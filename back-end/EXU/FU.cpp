#include "TOP.h"
#include "config.h"
#include <cstdint>

extern Back_Top back;

#define SUB 0b000
#define SLL 0b001
#define SLT 0b010
#define SLTU 0b011
#define XOR 0b100
#define SRL 0b101
#define OR 0b110
#define AND 0b111

#define BEQ 0b000
#define BNE 0b001
#define BLT 0b100
#define BGE 0b101
#define BLTU 0b110
#define BGEU 0b111

void alu(Inst_info *inst) {
  uint32_t operand1, operand2;
  if (inst->op == AUIPC || inst->op == JAL || inst->op == JALR)
    operand1 = inst->pc;
  else if (inst->op == LUI)
    operand1 = 0;
  else
    operand1 = inst->src1_rdata;

  if (inst->src2_is_imm) {
    operand2 = inst->imm;
  } else if (inst->op == JALR || inst->op == JAL) {
    operand2 = 4;
  } else {
    operand2 = inst->src2_rdata;
  }

  switch (inst->op) {
  case ADD: {
    switch (inst->func3) {
    case SUB:
      if (inst->func7_5 && !inst->src2_is_imm)
        inst->result = operand1 - operand2;
      else
        inst->result = operand1 + operand2;
      break;
    case SLL:
      inst->result = operand1 << operand2;
      break;
    case SLT:
      inst->result = ((signed)operand1 < (signed)operand2);
      break;
    case SLTU:
      inst->result = ((unsigned)operand1 < (unsigned)operand2);
      break;
    case XOR:
      inst->result = (operand1 ^ operand2);
      break;
    case SRL:
      if (inst->func7_5)
        inst->result = ((signed)operand1 >> operand2);
      else
        inst->result = ((unsigned)operand1 >> operand2);
      break;
    case OR:
      inst->result = (operand1 | operand2);
      break;
    case AND:
      inst->result = (operand1 & operand2);
      break;
    default:
      assert(0);
    }
    break;
  }
  default: {
    inst->result = operand1 + operand2;
    break;
  }
  }

  if (is_branch(inst->op)) {
    uint32_t pc_br = inst->pc + inst->imm;
    bool br_taken = true;
    uint32_t operand1, operand2;

    assert(is_branch(inst->op));
    if (inst->op == BR) {
      switch (inst->func3) {
      case BEQ:
        inst->result = (operand1 == operand2);
        break;
      case BNE:
        inst->result = (operand1 != operand2);
        break;
      case BGE:
        inst->result = ((signed)operand1 >= (signed)operand2);
        break;
      case BLT:
        inst->result = ((signed)operand1 < (signed)operand2);
        break;
      case BGEU:
        inst->result = ((unsigned)operand1 >= (unsigned)operand2);
        break;
      case BLTU:
        inst->result = ((unsigned)operand1 < (unsigned)operand2);
        break;
      }
    }

    switch (inst->op) {
    case BR:
      br_taken = inst->result;
      break;
    case JAL:
      br_taken = true;
      break;
    case JALR:
      br_taken = true;
      pc_br = (operand1 + inst->imm) & (~0x1);
      break;
    default:
      br_taken = false;
    }

    if (br_taken && inst->pred_br_taken && inst->pred_br_pc == pc_br ||
        !br_taken && !inst->pred_br_taken) {
      inst->mispred = false;
    } else {
      inst->mispred = true;
    }

    if (br_taken)
      inst->pc_next = pc_br;
  }
}

void ldu(Inst_info *inst) {
  enum STATE { IDLE, REQ, RECV } state;
  int addr = inst->src1_rdata + inst->imm;

  if (state == IDLE) {

    back.out.araddr = addr;
    back.out.arvalid = true;
    state = REQ;
  } else if (state == REQ) {
    if (back.in.arready && back.out.arvalid) {
      back.out.rready = true;
      state = RECV;
    }
  } else if (state == RECV) {
    int size = inst->func3;
    int offset = addr & 0b11;
    int data;

    if (back.out.rvalid && back.out.rvalid) {
      data = back.in.rdata;
      state = IDLE;
    } else {
      return;
    }
    uint32_t mask = 0;
    uint32_t sign = 0;

    data = data >> (offset * 8);
    if (size == 0) {
      mask = 0xFF;
      if (data & 0x80)
        sign = 0xFFFFFF00;
    } else if (size == 0b01) {
      mask = 0xFFFF;
      if (data & 0x8000)
        sign = 0xFFFF0000;
    } else {
      mask = 0xFFFFFFFF;
    }

    data = data & mask;

    // 有符号数
    if (!(inst->func3 & 0b100)) {
      data = data | sign;
    }

    inst->result = data;
  }
}

void stu(Inst_info *inst) { inst->result = inst->src1_rdata + inst->imm; }
