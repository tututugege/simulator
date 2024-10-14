#include <EXU.h>
#include <cassert>
#include <config.h>
#include <cvt.h>

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

void ALU::cycle() {

  switch (in.alu_op.op) {
  case ADD: {
    switch (in.alu_op.func3) {
    case SUB:
      if (in.alu_op.func7_5 && !in.alu_op.src2_is_imm)
        out.res = in.src1 - in.src2;
      else
        out.res = in.src1 + in.src2;
      break;
    case SLL:
      out.res = in.src1 << in.src2;
      break;
    case SLT:
      out.res = ((signed)in.src1 < (signed)in.src2);
      break;
    case SLTU:
      out.res = ((unsigned)in.src1 < (unsigned)in.src2);
      break;
    case XOR:
      out.res = (in.src1 ^ in.src2);
      break;
    case SRL:
      if (in.alu_op.func7_5)
        out.res = ((signed)in.src1 >> in.src2);
      else
        out.res = ((unsigned)in.src1 >> in.src2);
      break;
    case OR:
      out.res = (in.src1 | in.src2);
      break;
    case AND:
      out.res = (in.src1 & in.src2);
      break;
    default:
      assert(0);
    }
    break;
  }
  case BR:
    switch (in.alu_op.func3) {
    case BEQ:
      out.res = (in.src1 == in.src2);
      break;
    case BNE:
      out.res = (in.src1 != in.src2);
      break;
    case BGE:
      out.res = ((signed)in.src1 >= (signed)in.src2);
      break;
    case BLT:
      out.res = ((signed)in.src1 < (signed)in.src2);
      break;
    case BGEU:
      out.res = ((unsigned)in.src1 >= (unsigned)in.src2);
      break;
    case BLTU:
      out.res = ((unsigned)in.src1 < (unsigned)in.src2);
      break;
    }

    break;

  default: {
    out.res = in.src1 + in.src2;
    break;
  }
  }
}

void AGU::cycle() { out.addr = in.base + in.off; }

void BRU::cycle() {
  uint32_t pc_br = in.pc + in.off;

  switch (in.op) {
  case BR:
    out.br_taken = in.alu_out;
    break;
  case JAL:
    out.br_taken = true;
    break;
  case JALR:
    out.br_taken = true;
    pc_br = (in.src1 + in.off) & (~0x1);
    break;
  default:
    out.br_taken = false;
  }

  if (out.br_taken)
    out.pc_next = pc_br;
  else
    out.pc_next = in.pc + 4;
}
