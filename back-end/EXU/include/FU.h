#pragma once

#include "Dcache.h"
#include "config.h"
#include <cmath>

class SRT4DividerDynamic {
  const int width;
  const int overhead_cycles; // Setup + Post processing

public:
  SRT4DividerDynamic(int w = 32, int overhead = 2)
      : width(w), overhead_cycles(overhead) {}

  int calculate_latency(uint32_t dividend, uint32_t divisor) {
    // 1. 处理除以0异常 (通常也是固定周期的 Trap)
    if (divisor == 0)
      return overhead_cycles;

    // 2. 处理除数大于被除数 (结果为0，余数为被除数)
    if (dividend < divisor)
      return overhead_cycles;

    // 3. 计算前导零 (Leading Zeros)
    int clz_dividend = __builtin_clz(dividend);
    int clz_divisor = __builtin_clz(divisor);

    // 4. 计算有效位宽位置 (MSB 位置)
    // 例如：32位宽，clz=31 -> MSB index=0 (即数值1)
    int pos_dividend = width - clz_dividend;
    int pos_divisor = width - clz_divisor;

    // 5. 计算有效迭代所需的位差
    int delta = pos_dividend - pos_divisor;

    // Radix-4 每次消化 2 bit，向上取整
    // 注意：至少需要跑一次迭代或由 setup 覆盖，这里假设 delta=0 也要 1 次
    int iterations = (delta > 0) ? std::ceil((double)delta / 2.0) : 1;

    return overhead_cycles + iterations;
  }
};

struct mmu_slot_t {
  wire2_t idx = 0;
  wire1_t valid = false;
};

class FU {
public:
#if defined(CONFIG_CACHE)
  void exec(Inst_uop &inst, Mem_REQ *&in, bool mispred);
#else
  void exec(Inst_uop &inst);
#endif
  int latency = 0;
  int cycle = 0;
  bool complete = false;
  // only for load/store functional unit
  mmu_slot_t mmu_lsu_slot_r = {};
  mmu_slot_t mmu_lsu_slot_r_1 = {};
};
