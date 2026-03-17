#pragma once
#include "ref.h"
#include "config.h"
#include <cstdint>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

extern CPU_state dut_cpu;
extern uint32_t *p_memory;
class SimContext;

struct DifftestPageFaultWarning {
  bool valid = false;
  uint64_t cycle = 0;
  uint8_t access_type = 0;
  uint32_t dut_pc = 0;
  uint32_t dut_commit_pc = 0;
  uint32_t dut_inst = 0;
};

void init_difftest(int);
void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory);
void get_state(CPU_state &dut_state, uint8_t &privilege, uint32_t *dut_memory);
void difftest_step(bool);
void difftest_skip();
DifftestPageFaultWarning difftest_get_last_pf_warning();
