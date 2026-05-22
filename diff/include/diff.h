#pragma once
#include "ref.h"
#include "refcpu_api.h"
#include <cstdint>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

extern CPU_state dut_cpu;
class SimContext;

void init_difftest(int);
void init_diff_ckpt(CPU_state ckpt_state, uint8_t privilege);
void get_state(CPU_state &dut_state, uint8_t &privilege);
void difftest_step(bool);
void difftest_skip();
bool difftest_ref_sim_end();
bool difftest_ref_last_inst_is_wfi();
void difftest_ref_set_uart_print(bool enable);
void difftest_ref_set_ref_only(bool enable);
uint64_t difftest_get_oracle_timer();
void difftest_dump_memory_line(const char *tag, uint32_t addr);
