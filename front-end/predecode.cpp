#include "predecode.h"
#include <cstdint>
#include <RISCV.h>
#include <cvt.h>

PredecodeResult predecode_instruction(uint32_t inst, uint32_t pc) {
  PredecodeResult result;
  result.type = PREDECODE_NON_BRANCH;
  result.target_address = 0;

  if (inst == 0 || inst == INST_NOP) {
    result.type = PREDECODE_NON_BRANCH;
    result.target_address = 0;
    return result;
  }

  // cout << "pc" << std::hex << pc << " inst" << std::hex << inst << endl;

  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, inst, 32);

  bool *bit_op_code = inst_bit + 25; // 25-31
  uint32_t number_op_code_unsigned = cvt_bit_to_number_unsigned(bit_op_code, 7);

  switch (number_op_code_unsigned) {
    case number_2_opcode_jal: { // jal
      result.type = PREDECODE_JAL;
      
      bool bit_immi_j_type[21];
      init_indice(bit_immi_j_type, 0, 21);
      bit_immi_j_type[0] = inst_bit[0];
      copy_indice(bit_immi_j_type, 1, inst_bit, 12, 8);
      bit_immi_j_type[9] = inst_bit[11];
      copy_indice(bit_immi_j_type, 10, inst_bit, 1, 10);
      
      bool bit_temp[32];
      sign_extend(bit_temp, 32, bit_immi_j_type, 21);
      uint32_t imm = cvt_bit_to_number_unsigned(bit_temp, 32);
      
      result.target_address = pc + imm;
      break;
    }
    
    case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
      result.type = PREDECODE_DIRECT_JUMP_NO_JAL;
      
      bool bit_immi_b_type[13];
      init_indice(bit_immi_b_type, 0, 13);
      bit_immi_b_type[0] = inst_bit[0];
      bit_immi_b_type[1] = inst_bit[24];
      copy_indice(bit_immi_b_type, 2, inst_bit, 1, 6);
      copy_indice(bit_immi_b_type, 8, inst_bit, 20, 4);
      
      bool bit_temp[32];
      sign_extend(bit_temp, 32, bit_immi_b_type, 13);
      uint32_t imm = cvt_bit_to_number_unsigned(bit_temp, 32);
      
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

