#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <fstream>

struct front_top_out {
  // to back-end
  bool FIFO_valid;
  uint32_t pc[INST_WAY];
  uint32_t instructions[INST_WAY];
  bool predict_dir[INST_WAY];
  uint32_t predict_next_fetch_address;
  bool alt_pred[INST_WAY];
  uint8_t altpcpn[INST_WAY];
  uint8_t pcpn[INST_WAY];
};

int inst_idx;

using namespace std;

void load_slave_comb();
void load_slave_seq();
void store_slave_seq();
void store_slave_comb();

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);
uint32_t next_PC[2];

// 后端执行
Back_Top back;
bool ret;
bool sim_end = false;

void branch_check();

int commit_num;

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  ifstream inst_data(argv[argc - 1], ios::in);

  char **ptr = NULL;
  long i = 0;

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

  front_top_out front_out;

  // main loop
  for (i = 0; i < MAX_SIM_TIME; i++) {
    inst_idx = i;

    if (LOG)
      cout << "****************************************************************"
           << endl;

    if (!stall || misprediction || exception) {

#if defined(CONFIG_BRANCHCHECK)
      if (i != 0) {
        branch_check();
      }
#elif defined(CONFIG_BPU)

#else
      for (int j = 0; j < INST_WAY; j++) {
        front_out.pc[j] = number_PC;
        front_out.FIFO_valid = true;
        front_out.instructions[j] = p_memory[front_out.pc[j] / 4];
        if (LOG)
          cout << "指令index:" << dec << i << " 当前PC的取值为:" << hex
               << number_PC << endl;

        front_out.predict_dir[j] = false;
        number_PC += 4;
      }

      front_out.predict_next_fetch_address = number_PC;

#endif

      for (int j = 0; j < INST_WAY; j++) {
        back.in.valid[j] =
            front_out.FIFO_valid && front_out.instructions[j] != 0;
        back.in.pc[j] = front_out.pc[j];
        back.in.inst[j] = front_out.instructions[j];
        back.in.predict_dir[j] = front_out.predict_dir[j];
        back.in.alt_pred[j] = front_out.alt_pred[j];
        back.in.altpcpn[j] = front_out.altpcpn[j];
      }
    }

    load_slave_comb();
    store_slave_comb();
    back.Back_comb();

    load_slave_seq();
    store_slave_seq();
    back.Back_seq();

    if (sim_end)
      break;

    stall = back.out.stall;
    misprediction = back.out.mispred;
    exception = back.out.exception;

    if (misprediction || exception) {
      number_PC = back.out.redirect_pc;
    } else if (stall) {
      for (int j = 0; j < INST_WAY; j++) {
        if (back.out.fire[j])
          back.in.valid[j] = false;
      }
    }
  }

  delete[] p_memory;

  extern int stall_num[3];
  if (i != MAX_SIM_TIME) {
    if (ret == 0) {
      cout << "\033[1;32m-----------------------------\033[0m" << endl;
      cout << "\033[1;32mSuccess!!!!\033[0m" << endl;
      printf("\033[1;32minstruction num: %d\033[0m\n", commit_num);
      printf("\033[1;32mcycle num      : %ld\033[0m\n", i);
      printf("\033[1;32mipc            : %f\033[0m\n", (double)commit_num / i);
      /*printf("\033[1;32mIQ stall num   : %d\033[0m\n", stall_num);*/
      /*printf("\033[1;32mint stall num  : %d\033[0m\n", stall_num[0]);*/
      /*printf("\033[1;32mld  stall num  : %d\033[0m\n", stall_num[1]);*/
      /*printf("\033[1;32mst  stall num  : %d\033[0m\n", stall_num[2]);*/

      /*printf("\033[1;32mint inst num   : %f\033[0m\n",*/
      /*       (double)back.int_iq.num / commit_num);*/
      /*printf("\033[1;32mld  inst num   : %f\033[0m\n",*/
      /*       (double)back.ld_iq.num / commit_num);*/
      /*printf("\033[1;32mst  inst num   : %f\033[0m\n",*/
      /*       (double)back.st_iq.num / commit_num);*/

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

enum STATE { IDLE, RET };
int load_slave_state = IDLE;
uint32_t load_slave_addr = 0;

void load_slave_comb() {

  if (load_slave_state == IDLE) {
    back.in.arready = true;
  } else if (load_slave_state == RET) {
    back.in.arready = false;
    back.in.rdata = p_memory[load_slave_addr >> 2];
    back.in.rvalid = true;
  }
}

void load_slave_seq() {
  if (load_slave_state == IDLE) {
    if (back.out.arvalid && back.in.arready) {
      load_slave_state = RET;
      load_slave_addr = back.out.araddr;
    }
  } else if (load_slave_state == RET) {
    if (back.out.rready && back.in.rvalid) {
      load_slave_state = IDLE;
    }
  }
}

int store_slave_state = IDLE;
void store_slave_comb() {
  if (store_slave_state == IDLE) {
    back.in.wready = true;
  } else if (store_slave_state == RET) {
    back.in.wready = false;
    back.in.bvalid = true;
  }
}

void store_slave_seq() {
  if (store_slave_state == IDLE) {
    if (back.out.wvalid && back.in.wready) {
      store_slave_state = RET;
      uint32_t wdata = back.out.wdata;
      uint32_t waddr = back.out.waddr;
      uint32_t wstrb = back.out.wstrb;

      if (waddr == 0x1c) {
        ret = wdata;
        sim_end = true;
        return;
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
  } else if (store_slave_state == RET) {
    if (back.out.rready && back.in.rvalid) {
      store_slave_state = IDLE;
    }
  }
}
