#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <fstream>

int time_i = 0;

using namespace std;

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);
uint32_t next_PC[2];

// 后端执行
Back_Top back = Back_Top();

void branch_check();

int commit_num;
int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  ifstream inst_data(argv[argc - 1], ios::in);

  char **ptr = NULL;
  long i = 0;
  bool ret;

  // init physical memory
  for (i = 0; i < PHYSICAL_MEMORY_LENGTH; i++) {
    if (inst_data.eof())
      break;
    char inst_data_line[20];
    inst_data.getline(inst_data_line, 100);
    uint32_t inst_32b = strtol(inst_data_line, ptr, 16);
    p_memory[i + POS_MEMORY_SHIFT] = inst_32b;
  }
  const char *diff_so = "./nemu/build/riscv32-nemu-interpreter-so";

  // init difftest and back-end
  init_difftest(diff_so, i * 4);
  back.init();

  uint32_t number_PC;
  ofstream outfile;
  bool stall, misprediction, exception;
  number_PC = 0x80000000;
  stall = misprediction = exception = false;

  // main loop
  for (i = 0; i < MAX_SIM_TIME; i++) {

    if (LOG)
      cout << "****************************************************************"
           << endl;

    /*if (!stall || misprediction || exception) {*/

    if (!stall) {
      if (i != 0)
        branch_check();

      /*for (int j = 0; j < INST_WAY; j++) {*/
      /*  if (next_PC[j] != number_PC + 4 * j) {*/
      /*    back.ptab.in.valid[j] = true;*/
      /*    back.ptab.in.ptab_wdata[j] = next_PC[j];*/
      /*  } else {*/
      /*    back.ptab.in.valid[j] = false;*/
      /*  }*/
      /*  back.ptab.comb_alloc();*/
      /*}*/

      for (int j = 0; j < INST_WAY; j++) {
        back.in.pc[j] = next_PC[j];
        if (LOG)
          cout << "指令index:" << dec << i << " 当前PC的取值为:" << hex
               << next_PC[j] << endl;

        back.in.inst[j] = p_memory[next_PC[j] / 4];
        /*back.in.valid[j] = back.ptab.out.ready[j];*/
        back.in.valid[j] = true;
      }
    }

    /*}*/

    // TODO
    // asy and page fault
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    back.Back_comb();
    back.Back_seq();

    bool wen = back.out.store;
    if (wen) {
      uint32_t wdata = back.out.store_data;
      uint32_t waddr = back.out.store_addr;
      uint32_t wstrb = back.out.store_strb;

      if (waddr == 0x1c) {
        ret = wdata;
        break;
      }

      uint32_t old_data = p_memory[waddr / 4];
      uint32_t mask = 0;
      if (wstrb & 0b1)
        mask |= 0xFF;
      if (wstrb & 0b10)
        mask |= 0xFF00;
      if (wstrb & 0b100)
        mask |= 0xFF0000;
      if (wstrb & 0b1000)
        mask |= 0xFF000000;

      p_memory[waddr / 4] = (mask & wdata) | (~mask & old_data);

      if (waddr == UART_BASE) {
        char temp = wdata & 0xFF;
        cout << temp;
      }

      if (LOG) {
        cout << "store data " << hex << ((mask & wdata) | (~mask & old_data))
             << " in " << (waddr & 0xFFFFFFFC) << endl;
      }
    }

    stall = back.out.stall;
    misprediction = back.out.mispred;
    exception = back.out.exception;
    number_PC = next_PC[1];

    /*if (misprediction || exception) {*/
    /*  number_PC = back.out.pc;*/
    /*} else if (!stall) {*/
    /*  number_PC = next_PC[1];*/
    /*} else {*/
    /*  for (int j = 0; j < INST_WAY; j++) {*/
    /*    if (back.out.fire[j])*/
    /*      back.in.valid[j] = false;*/
    /*  }*/
    /*}*/

    if (stall) {
      for (int j = 0; j < INST_WAY; j++) {
        if (back.out.fire[j])
          back.in.valid[j] = false;
      }
    }
  }

  delete[] p_memory;

  if (i != MAX_SIM_TIME) {
    if (ret == 0) {
      cout << "\033[1;32m-----------------------------\033[0m" << endl;
      cout << "\033[1;32mSuccess!!!!\033[0m" << endl;
      printf("\033[1;32minstruction num: %d\033[0m\n", commit_num);
      printf("\033[1;32mcycle num      : %ld\033[0m\n", i);
      printf("\033[1;32mipc            : %f\033[0m\n", (double)commit_num / i);
      cout << "\033[1;32m-----------------------------\033[0m" << endl;
    } else {
      cout << "\033[1;31m------------------------------\033[0m" << endl;
      cout << "\033[1;31mFail!!!!QAQ\033[0m" << endl;
      cout << "\033[1;31m------------------------------\033[0m" << endl;
      exit(1);
    }
  } else {
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    cout << "\033[1;31mTIME OUT!!!!QAQ\033[0m" << endl;
    cout << "\033[1;31m------------------------------\033[0m" << endl;
    exit(1);
  }

#ifdef CONFIG_DIFFTEST
  extern void *handle;
  dlclose(handle);
#endif

  return 0;
}

void load_data() {
  uint32_t address = back.out.load_addr;
  back.in.load_data = p_memory[address / 4];
}
