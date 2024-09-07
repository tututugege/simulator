#include <config.h>
#include <cstdint>
#include <cstdio>
#include <cvt.h>
#include <iostream>

bool check_branch(Inst_op op, uint32_t operand1, uint32_t operand2) {

  return true;
}

Inst_res execute(bool *input_data, Inst_info inst, uint32_t pc) {
  uint32_t operand1, operand2, result;
  uint32_t pc_operand1, pc_operand2, pc_next;

  operand1 = cvt_bit_to_number_unsigned(input_data + 32 * inst.src1_idx, 32);
  if (inst.type == ITYPE) {
    operand2 = inst.imm;
  } else {
    operand2 = cvt_bit_to_number_unsigned(input_data + 32 * inst.src2_idx, 32);
  }

  pc_operand1 = pc;
  pc_operand2 = 4;

  switch (inst.op) {
  case LUI: { // lui
    result = inst.imm;
    break;
  }
  case AUIPC: { // auipc
    result = pc + (signed)inst.imm;
    break;
  }
  case JAL: { // jal
    result = pc + 4;
    pc_operand2 = (signed)inst.imm;
    break;
  }
  case JALR: { // jalr
    result = pc + 4;
    pc_operand1 = operand1;
    pc_operand2 = (signed)inst.imm;
    break;
  }
  case BEQ:
  case BNE:
  case BLT:
  case BGE:
  case BLTU: {
    if (check_branch(inst.op, operand1, operand2))
      pc_operand2 = (signed)inst.imm;
    break;
  }
  case LB:
  case LH:
  case LW:
  case LBU:
  case LHU:
  case SB:
  case SH:
  case SW: { // sb, sh, sw
    result = (signed)operand1 + (signed)operand2;
    break;
  }
  case ADD: // add, sub, sll, slt, sltu, xor, srl, sra, or,
    result = operand1 + operand2;
    break;
  case SUB:
    result = operand1 - operand2;
    break;
  case SLL:
    result = operand1 << operand2;
    break;
  case SLT:
    result = ((signed)operand1 < (signed)operand2);
    break;
  case SLTU:
    result = ((unsigned)operand1 < (unsigned)operand2);
    break;
  case XOR:
    result = operand1 ^ operand2;
    break;
  case SRL:
    result = (unsigned)operand1 >> operand2;
    break;
  case SRA:
    result = (signed)operand1 >> operand2;
    break;
  case OR:
    result = operand1 | operand2;
    break;

  /*case FENCE: { // fence, fence.i*/
  /*  break;*/
  /*}*/
  default: {
    cerr << "error" << endl;
    assert(0);
    exit(-1);
    break;
  }
  }

  Inst_res res;
  res.result = result;
  res.pc_next = pc_operand1 + pc_operand2;
  if (res.pc_next != pc + 4)
    res.branch = true;
  else
    res.branch = false;

  return res;
}
