#pragma once

#include "config.h"

class Back_Top;

struct mmu_slot_t {
  wire2_t idx = 0;
  wire1_t valid = false;
};

enum FuOp {
  OP_INT = 1 << 0,
  OP_MUL = 1 << 1,
  OP_DIV = 1 << 2,
  OP_LD = 1 << 3,
  OP_STA = 1 << 4,
  OP_STD = 1 << 5,
  OP_BR = 1 << 6
};

class FU {
public:
  void exec(Inst_uop &);
  int latency = 0;
  int cycle = 0;
  uint32_t support_ops = 0;
  bool complete = false;

  // only for load/store functional unit
  mmu_slot_t mmu_lsu_slot_r = {};
  mmu_slot_t mmu_lsu_slot_r_1 = {};
};
