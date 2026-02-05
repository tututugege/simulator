#include "predecode.h"
#include "ref.h"
#include <RISCV.h>
#include <cstdint>

PredecodeResult predecode_instruction(uint32_t inst, uint32_t pc) {
  PredecodeResult result;
  result.type = PREDECODE_NON_BRANCH;
  result.target_address = 0;

  if (inst == 0 || inst == INST_NOP) {
    result.type = PREDECODE_NON_BRANCH;
    result.target_address = 0;
    return result;
  }

  uint32_t opcode = BITS(inst, 6, 0);
  switch (opcode) {
  case number_2_opcode_jal: { // jal
    result.type = PREDECODE_JAL;

    uint32_t imm = immJ(inst);

    result.target_address = pc + imm;
    break;
  }

  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    result.type = PREDECODE_DIRECT_JUMP_NO_JAL;
    uint32_t imm = immB(inst);

    result.target_address = pc + imm;
    break;
  }

  case number_3_opcode_jalr: { // jalr
    // cout << "pc" << std::hex << pc << " inst" << std::hex << inst << endl;
    result.type = PREDECODE_JALR;
    result.target_address = 0;
    break;
  }

  default: {
    result.type = PREDECODE_NON_BRANCH;
    result.target_address = 0;
    break;
  }
  }

  return result;
}
