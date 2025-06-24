#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <iomanip>

bool USE_LINUX_SIMU = 1; // 是否为启动 Linux 模式

enum { DIFFTEST, BRANCHCHECK };

CPU_state dut_cpu, ref_cpu;
extern uint32_t *p_memory;
extern uint32_t next_PC[FETCH_WIDTH];
extern uint32_t fetch_PC[FETCH_WIDTH];
static uint32_t last_pc = 0;

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  // 1. memory copy
  memcpy(ref_memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
         img_size * sizeof(uint32_t));

  // 2. init the ref_cpu and dut_cpu
  for (int i = 0; i < ARF_NUM; i++) {
    ref_cpu.gpr[i] = 0;
    dut_cpu.gpr[i] = 0;
  }
  ref_cpu.pc = 0x00000000;
  dut_cpu.pc = 0x00000000;
  v1_difftest_init(ref_cpu.pc);
}

static void checkregs() {
  int i;

  if (ref_cpu.pc != dut_cpu.pc)
    goto fault;

  // 通用寄存器
  for (i = 0; i < ARF_NUM; i++) {
    if (ref_cpu.gpr[i] != dut_cpu.gpr[i])
      goto fault;
  }

  // csr
  for (i = 0; i < CSR_NUM; i++) {
    if (ref_cpu.csr[i] != dut_cpu.csr[i])
      goto fault;
  }

  if (dut_cpu.store) {
    if (dut_cpu.store_data != ref_cpu.store_data)
      goto fault;

    if (dut_cpu.store_addr != ref_cpu.store_addr)
      goto fault;
  }

  return;

  /*if (i == ARF_NUM) {*/
  /*  static int commit_idx = 0;*/
  /*cout << "- idx: " << dec << commit_idx++ << " commit pc: " << hex <<
   * last_pc*/
  /*     << endl; // debug for linux simu*/
  /*  return;*/
  /*}*/

fault:
  cout << "Difftest: error" << endl;
  cout << "cycle: " << dec << sim_time << endl;
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < ARF_NUM; i++) {
    cout << setw(10) << reg_names[i] << ":\t";
    printf("%08x\t%08x", ref_cpu.gpr[i], dut_cpu.gpr[i]);
    if (ref_cpu.gpr[i] != dut_cpu.gpr[i])
      printf("\t Error");
    putchar('\n');
  }

  printf("        PC:\t%08x\t%08x\n", ref_cpu.pc, dut_cpu.pc);
  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    cout << setw(10) << csr_names[i] << ":\t";
    printf("%08x\t%08x", ref_cpu.csr[i], dut_cpu.csr[i]);
    if (ref_cpu.csr[i] != dut_cpu.csr[i])
      printf("\t Error");
    putchar('\n');
  }

  cout << endl << setw(10) << "store" << ":\t";
  printf("%8x\t%8x\n", ref_cpu.store, dut_cpu.store);
  cout << setw(10) << "data" << ":\t";
  printf("%08x\t%08x\n", ref_cpu.store_data, dut_cpu.store_data);
  cout << setw(10) << "addr" << ":\t";
  printf("%08x\t%08x\n", ref_cpu.store_addr, dut_cpu.store_addr);

  extern int commit_num;
  /*printf("commit_num (dec): %d\n", commit_num);*/
  /*cout << "last_pc: " << hex << last_pc << endl;*/
  /*cout << "p_memory[last_pc]: " << hex << p_memory[last_pc / 4] << endl;*/
  /*cout << "ref_memory[last_pc]: " << hex << ref_memory[last_pc / 4] << endl;*/
  //
  // 仍然使用 ENLIGHTENMENT_V1 继续执行
  //
  /*for (;;) {*/
  /*  v1_difftest_exec();*/
  /*}*/
  exit(1);
}

void difftest_step() {
  last_pc = ref_cpu.pc;
  // printf("current check_type: %u\n", check_type);
  // using ENLIGHTENMENT_V1 as reference design
  static int v1_commit_num = 0;
  // cout << "v1_commit_num: " << dec << v1_commit_num << endl;
  v1_difftest_exec();
  // cout << "v1 regcpy start..." << endl;
  v1_difftest_regcpy(&ref_cpu, DIFFTEST_TO_REF);
  checkregs();
  // cout << "pass v1 checkregs!!!" << endl;
  v1_commit_num++;
}

/*void branch_check() {*/
/*  for (int i = 0; i < FETCH_WIDTH; i++) {*/
/*    ref_difftest_exec[BRANCHCHECK](1);*/
/*    ref_difftest_regcpy[BRANCHCHECK](&ref_cpu, DIFFTEST_TO_DUT);*/
/*    next_PC[i] = ref_cpu.pc;*/
/*  }*/
/*}*/
