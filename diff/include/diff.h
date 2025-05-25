/*#include <top_config.h>*/
#include <config.h>
#include <cstdint>
#include <string>

#define USE_ENLIGHTENMENT_V1 // 是否使用 ENLIGHTENMENT_V1
                             // 作为参考设计；注释后，则使用 NEMU 作为参考设计

typedef struct CPU_state {
  uint32_t gpr[32];
#ifdef USE_ENLIGHTENMENT_V1
  uint32_t csr[21];
#endif
  uint32_t pc;
} CPU_state;

extern CPU_state ref;
extern CPU_state dut;

extern uint32_t *p_memory;
extern uint32_t *ref_memory; // memory for dut

// difftest using ENLIGHTENMENT_V1 as reference design
void init_difftest(int);
void difftest_step();

void v1_difftest_init(uint32_t pc_start);
void v1_difftest_exec();
void v1_difftest_regcpy(CPU_state *ref, bool direction);
