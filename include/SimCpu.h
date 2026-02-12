#pragma once
#include "BackTop.h"
#include "MMU.h"
#include <front_IO.h>

class SimCpu {
  // 性能计数器
public:
  SimCpu() : back(&this->ctx) {};
  BackTop back;
  MMU mmu;
  front_top_out front_out;
  front_top_in front_in;
  SimContext ctx;

  void init();
  void restore_pc(uint32_t pc);
  void cycle();
  void front_cycle();
  void back2front_comb();
  void back2mmu_comb();
  uint32_t get_reg(uint8_t arch_idx) { return back.get_reg(arch_idx); }
};

extern SimCpu cpu;
