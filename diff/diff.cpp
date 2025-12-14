#include "RISCV.h"
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <iomanip>
#include <ref.h>

CPU_state dut_cpu;
static Ref_cpu ref_cpu;

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  ref_cpu.init(0);
  memcpy(ref_cpu.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
         img_size * sizeof(uint32_t));
  ref_cpu.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  ref_cpu.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  ref_cpu.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  ref_cpu.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  ref_cpu.memory[uint32_t(0xc / 4)] = 0x00028067;
}

void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory) {
  ref_cpu.init(0);
  ref_cpu.state = ckpt_state;
  ref_cpu.privilege = RISCV_MODE_U;
  memcpy(ref_cpu.memory, ckpt_memory, (uint64_t)PHYSICAL_MEMORY_LENGTH * 4);

  uint32_t p_addr;
  assert(ref_cpu.va2pa(p_addr, ref_cpu.state.pc, 0));
}

static void checkregs() {
  int i;

  if (ref_cpu.state.pc != dut_cpu.pc)
    goto fault;

  // 通用寄存器
  for (i = 0; i < ARF_NUM; i++) {
    if (ref_cpu.state.gpr[i] != dut_cpu.gpr[i])
      goto fault;
  }

  // csr
  for (i = 0; i < CSR_NUM; i++) {
    if (ref_cpu.state.csr[i] != dut_cpu.csr[i])
      goto fault;
  }

  if (ref_cpu.state.store) {
    if (dut_cpu.store != ref_cpu.state.store)
      goto fault;

    if (dut_cpu.store_data != ref_cpu.state.store_data)
      goto fault;

    if (dut_cpu.store_addr != ref_cpu.state.store_addr)
      goto fault;
  }

  return;

fault:
  cout << "Difftest: error" << endl;
  cout << "cycle: " << dec << sim_time << endl;
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < ARF_NUM; i++) {
    cout << setw(10) << reg_names[i] << ":\t";
    printf("%08x\t%08x", ref_cpu.state.gpr[i], dut_cpu.gpr[i]);
    if (ref_cpu.state.gpr[i] != dut_cpu.gpr[i])
      printf("\t Error");
    putchar('\n');
  }

  printf("        PC:\t%08x\t%08x\n", ref_cpu.state.pc, dut_cpu.pc);
  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    cout << setw(10) << csr_names[i] << ":\t";
    printf("%08x\t%08x", ref_cpu.state.csr[i], dut_cpu.csr[i]);
    if (ref_cpu.state.csr[i] != dut_cpu.csr[i])
      printf("\t Error");
    putchar('\n');
  }

  cout << endl << setw(10) << "store" << ":\t";
  printf("%8x\t%8x\n", ref_cpu.state.store, dut_cpu.store);
  cout << setw(10) << "data" << ":\t";
  printf("%08x\t%08x\n", ref_cpu.state.store_data, dut_cpu.store_data);
  cout << setw(10) << "addr" << ":\t";
  printf("%08x\t%08x\n", ref_cpu.state.store_addr, dut_cpu.store_addr);

  printf("%08x\n", ref_cpu.Instruction);

  // while (1) {
  // }
  exit(1);
}

void difftest_skip() {
  ref_cpu.exec();
  for (int i = 0; i < 32; i++) {
    ref_cpu.state.gpr[i] = dut_cpu.gpr[i];
  }
}

void difftest_step(bool check) {
  ref_cpu.exec();
#ifndef CONFIG_RUN_REF
  if (check)
    checkregs();
#endif // !CONFIG
}
