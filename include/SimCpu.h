#pragma once
#include "MMU.h"
#include "TOP.h"
#include "config.h"
#include <front_IO.h>

class SimCpu {
  // 性能计数器
public:
  SimCpu() : back(&this->ctx){};
  Back_Top back;
  MMU mmu;
  front_top_out front_out;
  front_top_in front_in;
  SimContext ctx;

  void init();
  void cycle();
  void front_cycle();
  void back2front_comb();
  void back2mmu_comb();
};

extern SimCpu cpu;
