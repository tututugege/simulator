#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <iomanip>
#include <ref.h>

CPU_state dut_cpu;
static Ref_cpu ref;

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  ref.init(0);
  memcpy(ref.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
         img_size * sizeof(uint32_t));
  ref.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  ref.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  ref.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  ref.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  ref.memory[uint32_t(0xc / 4)] = 0x00028067;
  ref.memory[uint32_t(0x00001000 / 4)] = 0x00000297; // auipc           t0,0
  ref.memory[uint32_t(0x00001004 / 4)] = 0x02828613; // addi a2,t0,40
  ref.memory[uint32_t(0x00001008 / 4)] = 0xf1402573; // csrrs a0,mhartid,zero
  ref.memory[uint32_t(0x0000100c / 4)] = 0x0202a583; // lw a1,32(t0)
  ref.memory[uint32_t(0x00001010 / 4)] = 0x0182a283; // lw t0,24(t0)
  ref.memory[uint32_t(0x00001014 / 4)] = 0x00028067; // jr              t0
  ref.memory[uint32_t(0x00001018 / 4)] = 0x80000000;
  ref.memory[uint32_t(0x00001020 / 4)] = 0x8fe00000;
}

static void checkregs() {
  int i;

  if (ref.state.pc != dut_cpu.pc)
    goto fault;

  // 通用寄存器
  for (i = 0; i < ARF_NUM; i++) {
    if (ref.state.gpr[i] != dut_cpu.gpr[i])
      goto fault;
  }

  // csr
  for (i = 0; i < CSR_NUM; i++) {
    if (ref.state.csr[i] != dut_cpu.csr[i])
      goto fault;
  }

  if (dut_cpu.store) {
    if (dut_cpu.store != ref.state.store)
      goto fault;

    if (dut_cpu.store_data != ref.state.store_data)
      goto fault;

    if (dut_cpu.store_addr != ref.state.store_addr)
      goto fault;
  }

  return;

fault:
  cout << "Difftest: error" << endl;
  cout << "cycle: " << dec << sim_time << endl;
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < ARF_NUM; i++) {
    cout << setw(10) << reg_names[i] << ":\t";
    printf("%08x\t%08x", ref.state.gpr[i], dut_cpu.gpr[i]);
    if (ref.state.gpr[i] != dut_cpu.gpr[i])
      printf("\t Error");
    putchar('\n');
  }

  printf("        PC:\t%08x\t%08x\n", ref.state.pc, dut_cpu.pc);
  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    cout << setw(10) << csr_names[i] << ":\t";
    printf("%08x\t%08x", ref.state.csr[i], dut_cpu.csr[i]);
    if (ref.state.csr[i] != dut_cpu.csr[i])
      printf("\t Error");
    putchar('\n');
  }

  cout << endl << setw(10) << "store" << ":\t";
  printf("%8x\t%8x\n", ref.state.store, dut_cpu.store);
  cout << setw(10) << "data" << ":\t";
  printf("%08x\t%08x\n", ref.state.store_data, dut_cpu.store_data);
  cout << setw(10) << "addr" << ":\t";
  printf("%08x\t%08x\n", ref.state.store_addr, dut_cpu.store_addr);

  printf("%08x\n", ref.Instruction);

  exit(1);
}

void difftest_skip() {
  ref.exec();
  for (int i = 0; i < 32; i++) {
    ref.state.gpr[i] = dut_cpu.gpr[i];
  }
}

void difftest_step() {
  ref.exec();
  checkregs();
}
