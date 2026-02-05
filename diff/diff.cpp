#include "diff.h"
#include "RISCV.h"
#include "config.h"

#include <cstring>
#include <iostream>

CPU_state dut_cpu;
RefCpu ref_cpu;

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  ref_cpu.init(0);
  std::memcpy(ref_cpu.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
              img_size);
  ref_cpu.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  ref_cpu.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  ref_cpu.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  ref_cpu.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  ref_cpu.memory[uint32_t(0xc / 4)] = 0x00028067;
}

void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory) {
  std::cout << "Restore for ref cpu " << std::endl;
  ref_cpu.init(0);
  ref_cpu.state = ckpt_state;
  ref_cpu.privilege = RISCV_MODE_U;

  std::memcpy(ref_cpu.memory, ckpt_memory,
              (uint64_t)PHYSICAL_MEMORY_LENGTH * sizeof(uint32_t));

  uint32_t p_addr;
  Assert(ref_cpu.va2pa(p_addr, ref_cpu.state.pc, 0));
}

void get_state(CPU_state &dut_state, uint8_t &privilege, uint32_t *dut_memory) {
  dut_state = ref_cpu.state;
  privilege = ref_cpu.privilege;
  memcpy(dut_memory, ref_cpu.memory,
         (uint64_t)PHYSICAL_MEMORY_LENGTH * sizeof(uint32_t));
}

static void checkregs() {
  int i;

  if (ref_cpu.state.pc != dut_cpu.pc)
    goto fault;

  // 如果没有指令缺页异常，且指令不匹配，报错
  if (!ref_cpu.page_fault_inst && ref_cpu.Instruction != dut_cpu.instruction)
    goto fault;

  // 对比 Page Fault 状态
  if (ref_cpu.page_fault_inst != dut_cpu.page_fault_inst)
    goto fault;
  if (ref_cpu.page_fault_load != dut_cpu.page_fault_load)
    goto fault;
  if (ref_cpu.page_fault_store != dut_cpu.page_fault_store)
    goto fault;

  // 通用寄存器
  for (i = 0; i < 32; i++) {
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

  auto print_mismatch = [](const char *name, uint32_t ref, uint32_t dut) {
    printf("%10s:\t%08x\t%08x%s\n", name, ref, dut,
           (ref != dut ? "\t Error" : ""));
  };

  printf("        PC:\t%08x\t%08x\n", ref_cpu.state.pc, dut_cpu.pc);
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < 32; i++) {
    print_mismatch(reg_names[i].c_str(), ref_cpu.state.gpr[i], dut_cpu.gpr[i]);
  }

  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    print_mismatch(csr_names[i].c_str(), ref_cpu.state.csr[i], dut_cpu.csr[i]);
  }

  cout << endl;
  print_mismatch("store", ref_cpu.state.store, dut_cpu.store);
  print_mismatch("data", ref_cpu.state.store_data, dut_cpu.store_data);
  print_mismatch("addr", ref_cpu.state.store_addr, dut_cpu.store_addr);

  printf("Ref Inst: %08x\tDUT Inst: %08x\n", ref_cpu.Instruction,
         dut_cpu.instruction);

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
  if (check)
    checkregs();
}
