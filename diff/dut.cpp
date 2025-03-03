#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };
enum { DIFFTEST, BRANCHCHECK };

CPU_state dut, ref;
bool difftest_skip = false;
extern uint32_t *p_memory;
extern uint32_t next_PC[FETCH_WIDTH];
extern uint32_t fetch_PC[FETCH_WIDTH];

void (*ref_difftest_memcpy[2])(uint32_t addr, void *buf, size_t n,
                               bool direction);

void (*ref_difftest_regcpy[2])(void *dut, bool direction);
void (*ref_difftest_exec[2])(uint64_t n);

void isa_reg_display();
void *handle[2];

void init_difftest(const char *ref_so_file, long img_size) {

  assert(ref_so_file != NULL);

#ifdef CONFIG_BRANCHCHECK
  for (int i = 0; i < 2; i++) {
#else
  for (int i = 0; i < 1; i++) {
#endif
    if (i == 0)
      handle[i] = dlopen(ref_so_file, RTLD_LAZY);
    else
      handle[i] =
          dlopen("./nemu/build/riscv32-nemu-interpreter-so2", RTLD_LAZY);
    assert(handle[i]);

    ref_difftest_memcpy[i] = (void (*)(uint32_t, void *, size_t, bool))dlsym(
        handle[i], "difftest_memcpy");
    assert(ref_difftest_memcpy[i]);

    ref_difftest_regcpy[i] =
        (void (*)(void *, bool))dlsym(handle[i], "difftest_regcpy");
    assert(ref_difftest_regcpy[i]);

    ref_difftest_exec[i] =
        (void (*)(uint64_t))dlsym(handle[i], "difftest_exec");
    assert(ref_difftest_exec[i]);

    void (*ref_difftest_init)() = (void (*)())dlsym(handle[i], "difftest_init");
    assert(ref_difftest_init);

    ref_difftest_init();
    ref_difftest_memcpy[i](0x80000000, p_memory + 0x80000000 / 4, img_size,
                           DIFFTEST_TO_REF);

    for (int i = 0; i < ARF_NUM; i++) {
      ref.gpr[i] = 0;
      dut.gpr[i] = 0;
    }
    ref.pc = 0x80000000;
    dut.pc = 0x80000000;

    ref_difftest_regcpy[i](&ref, DIFFTEST_TO_REF);
  }

#ifdef CONFIG_BRANCHCHECK

  fetch_PC[0] = 0x80000000;
  for (int i = 0; i < FETCH_WIDTH - 1; i++) {
    ref_difftest_exec[BRANCHCHECK](1);
    ref_difftest_regcpy[BRANCHCHECK](&ref, DIFFTEST_TO_DUT);
    fetch_PC[i + 1] = ref.pc;
  }
#endif
}

static void checkregs() {
  int i;

  if (ref.pc != dut.pc)
    goto fault;

  for (i = 0; i < ARF_NUM; i++) {
    if (ref.gpr[i] != dut.gpr[i])
      break;
  }

  if (i == ARF_NUM)
    return;

fault:

  cout << "Difftest: error" << endl;
  cout << "\tReference\tDut" << endl;
  for (int i = 0; i < ARF_NUM; i++) {
    cout << reg_names[i] << ":\t";
    printf("%08x\t%08x", ref.gpr[i], dut.gpr[i]);
    if (ref.gpr[i] != dut.gpr[i])
      printf("\t Error");
    putchar('\n');
  }
  printf("PC:\t%08x\t%08x\n", ref.pc, dut.pc);
  dlclose(handle[0]);
  dlclose(handle[1]);
  exit(1);
}

void difftest_step() {
  if (difftest_skip) {
    // to skip the checking of an instruction, just copy the reg state to
    // reference design
    for (int i = 0; i < ARF_NUM; i++) {
      ref.gpr[i] = dut.gpr[i];
    }
    ref.pc = dut.pc;
    ref_difftest_regcpy[DIFFTEST](&ref, DIFFTEST_TO_REF);
    difftest_skip = false;
  } else {
    ref_difftest_exec[DIFFTEST](1);
    ref_difftest_regcpy[DIFFTEST](&ref, DIFFTEST_TO_DUT);
    checkregs();
  }
}

void branch_check() {
  for (int i = 0; i < FETCH_WIDTH; i++) {
    ref_difftest_exec[BRANCHCHECK](1);
    ref_difftest_regcpy[BRANCHCHECK](&ref, DIFFTEST_TO_DUT);
    next_PC[i] = ref.pc;
  }
}
