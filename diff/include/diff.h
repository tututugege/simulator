#include <config.h>
typedef struct CPU_state {
  uint32_t gpr[32];
  uint32_t pc;
} CPU_state;

extern CPU_state ref;
extern CPU_state dut;

void difftest_skip_ref();
void difftest_step();
void init_difftest(const char *ref_so_file, long img_size);
