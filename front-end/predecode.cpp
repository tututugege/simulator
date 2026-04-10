#include "predecode.h"

#include <cstring>

static inline uint32_t sign_extend_u32(uint32_t value, int bits) {
  const uint32_t mask = 1u << (bits - 1);
  return (value ^ mask) - mask;
}

void predecode_seq_read(const struct predecode_in *in,
                        struct predecode_read_data *rd) {
  rd->inst = in->inst;
  rd->pc = in->pc;
}

void predecode_comb(const predecode_read_data &input, PredecodeResult &output) {
  std::memset(&output, 0, sizeof(PredecodeResult));
  output.type = PREDECODE_NON_BRANCH;
  output.target_address = 0;

  const uint32_t inst = input.inst;
  const uint32_t pc = input.pc;
  if (inst == 0 || inst == INST_NOP) {
    return;
  }

  const uint32_t opcode = inst & 0x7f;
  switch (opcode) {
  case number_2_opcode_jal: {
    output.type = PREDECODE_JAL;
    const uint32_t imm20 = (inst >> 31) & 0x1;
    const uint32_t imm10_1 = (inst >> 21) & 0x3ff;
    const uint32_t imm11 = (inst >> 20) & 0x1;
    const uint32_t imm19_12 = (inst >> 12) & 0xff;
    const uint32_t imm_raw =
        (imm20 << 20) | (imm19_12 << 12) | (imm11 << 11) | (imm10_1 << 1);
    output.target_address = pc + sign_extend_u32(imm_raw, 21);
    break;
  }
  case number_4_opcode_beq: {
    output.type = PREDECODE_DIRECT_JUMP_NO_JAL;
    const uint32_t imm12 = (inst >> 31) & 0x1;
    const uint32_t imm10_5 = (inst >> 25) & 0x3f;
    const uint32_t imm4_1 = (inst >> 8) & 0xf;
    const uint32_t imm11 = (inst >> 7) & 0x1;
    const uint32_t imm_raw =
        (imm12 << 12) | (imm11 << 11) | (imm10_5 << 5) | (imm4_1 << 1);
    output.target_address = pc + sign_extend_u32(imm_raw, 13);
    break;
  }
  case number_3_opcode_jalr:
    output.type = PREDECODE_JALR;
    output.target_address = 0;
    break;
  default:
    break;
  }
}

void predecode_seq_write() {}
