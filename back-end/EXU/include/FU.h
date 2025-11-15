#pragma once

#include "config.h"

struct mmu_slot_t {
  wire2_t idx = 0;
  wire1_t valid = false;
};

class FU {
public:
  void exec(Inst_uop &);
  int latency = 0;
  int cycle = 0;
  bool complete = false;
  // only for load/store functional unit
  mmu_slot_t mmu_lsu_slot_r = {};
  mmu_slot_t mmu_lsu_slot_r_1 = {};
};
