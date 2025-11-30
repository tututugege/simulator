#ifndef PREDECODE_H
#define PREDECODE_H

#include "front_IO.h"
#include <cstdint>
#include <RISCV.h>
#include <cvt.h>
#include <util.h>

enum PredecodeType {
  PREDECODE_NON_BRANCH,
  PREDECODE_DIRECT_JUMP_NO_JAL, // PC_relative
  PREDECODE_JALR, //jalr
  PREDECODE_JAL // jal
};

// enum Predecode_FIFO_TYPE{
//   PREDECODE_TAKEN,
//   PREDECODE_NOT_TAKEN,
//   PREDECODE_NOT_SURE
// };

struct PredecodeResult {
  PredecodeType type;
  uint32_t target_address; // target address (only valid for direct jump)
};

PredecodeResult predecode_instruction(uint32_t inst, uint32_t pc);

#endif // PREDECODE_H

