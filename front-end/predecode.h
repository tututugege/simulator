#ifndef PREDECODE_H
#define PREDECODE_H

#include "front_IO.h"
#include <cstdint>
#include <RISCV.h>

static constexpr predecode_type_t PREDECODE_NON_BRANCH = 0;
static constexpr predecode_type_t PREDECODE_DIRECT_JUMP_NO_JAL = 1;
static constexpr predecode_type_t PREDECODE_JALR = 2;
static constexpr predecode_type_t PREDECODE_JAL = 3;

struct PredecodeResult {
  predecode_type_t type;
  target_addr_t target_address;
};

struct predecode_in {
  inst_word_t inst;
  pc_t pc;
};

struct predecode_read_data {
  inst_word_t inst;
  pc_t pc;
};

void predecode_seq_read(const struct predecode_in *in,
                        struct predecode_read_data *rd);
void predecode_comb(const predecode_read_data &input, PredecodeResult &output);
void predecode_seq_write();

#endif
