#include <cstdlib>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

CPU_state dut, ref;
bool difftest_skip = false;
extern uint32_t *p_memory;

void (*ref_difftest_memcpy)(uint32_t addr, void *buf, size_t n,
                            bool direction) = NULL;

void (*ref_difftest_regcpy)(void *dut, bool direction) = NULL;
void (*ref_difftest_exec)(uint64_t n) = NULL;
void isa_reg_display();
void *handle;

void init_difftest(const char *ref_so_file, long img_size) {

  assert(ref_so_file != NULL);

  handle = dlopen(ref_so_file, RTLD_LAZY);
  assert(handle);

  ref_difftest_memcpy = (void (*)(uint32_t, void *, size_t, bool))dlsym(
      handle, "difftest_memcpy");
  assert(ref_difftest_memcpy);

  ref_difftest_regcpy =
      (void (*)(void *, bool))dlsym(handle, "difftest_regcpy");
  assert(ref_difftest_regcpy);

  ref_difftest_exec = (void (*)(uint64_t))dlsym(handle, "difftest_exec");
  assert(ref_difftest_exec);

  void (*ref_difftest_init)() = (void (*)())dlsym(handle, "difftest_init");
  assert(ref_difftest_init);

  ref_difftest_init();
  ref_difftest_memcpy(0x80000000, p_memory + 0x80000000 / 4, img_size,
                      DIFFTEST_TO_REF);

  for (int i = 0; i < ARF_NUM; i++) {
    ref.gpr[i] = 0;
    dut.gpr[i] = 0;
  }
  ref.pc = 0x80000000;
  dut.pc = 0x80000000;

  ref_difftest_regcpy(&ref, DIFFTEST_TO_REF);
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
  dlclose(handle);
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
    ref_difftest_regcpy(&ref, DIFFTEST_TO_REF);
    difftest_skip = false;
  } else {
    ref_difftest_exec(1);
    ref_difftest_regcpy(&ref, DIFFTEST_TO_DUT);
    checkregs();
  }
}
