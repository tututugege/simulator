#include "BPU/target_predictor/btb.h"
#include "frontend.h"
#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <front_IO.h>
#include <front_module.h>
#include <fstream>

int inst_idx;
int mispred_num = 0;
int branch_num = 0;
int back2front_num = 0;

using namespace std;

void load_slave_comb();
void load_slave_seq();
void store_slave_seq();
void store_slave_comb();

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);
uint32_t next_PC[FETCH_WIDTH];
uint32_t fetch_PC[FETCH_WIDTH];

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

  // init physical memory
  long idx = 0;
  for (idx = 0; idx < PHYSICAL_MEMORY_LENGTH; idx++) {
    if (inst_data.eof())
      break;
    char inst_data_line[20];
    inst_data.getline(inst_data_line, 100);
    uint32_t inst_32b = strtol(inst_data_line, ptr, 16);
    p_memory[idx + POS_MEMORY_SHIFT] = inst_32b;
  }
  const char *diff_so = "./nemu/build/riscv32-nemu-interpreter-so";

#ifdef CONFIG_DIFFTEST
  init_difftest(diff_so, idx * 4);
#endif

  back.init();

  uint32_t number_PC;
  ofstream outfile;
  bool stall, misprediction, exception;
  number_PC = 0x80000000;
  stall = misprediction = exception = false;

  front_top_out front_out;
  front_top_in front_in;

  int sim_time;
  // main loop
  for (sim_time = 0; sim_time < MAX_SIM_TIME; sim_time++) {
    inst_idx = sim_time;

    if (LOG)
      cout << "****************************************************************"
           << endl;

    if (!stall || misprediction || exception) {

#if defined(CONFIG_BRANCHCHECK)

#elif defined(CONFIG_BPU)

      // reset
      if (sim_time == 0) {
        front_in.reset = true;
        front_in.FIFO_read_enable = true;
        front_top(&front_in, &front_out);
        cout << hex << front_out.pc[0] << endl;
        front_in.reset = false;

        for (int j = 0; j < FETCH_WIDTH; j++) {
          for (int i = 0; i < COMMIT_WIDTH; i++) {
            front_in.back2front_valid[i] = false;
          }
        }
      }

      // 取指令
      front_in.FIFO_read_enable = true;
      front_in.refetch = false;

      for (int i = 0; i < COMMIT_WIDTH; i++) {
        if (front_in.back2front_valid[i]) {
          back2front_num++;
        }
      }

      front_top(&front_in, &front_out);

      if (sim_time == 0) {
        cout << front_out.pc[0] << endl;
      }

#else
      for (int j = 0; j < FETCH_WIDTH; j++) {
        front_out.pc[j] = number_PC;
        front_out.FIFO_valid = true;
        front_out.instructions[j] = p_memory[front_out.pc[j] / 4];
        /*if (LOG)*/
        /*  cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" <<
         * hex*/
        /*       << number_PC << endl;*/
        /**/
        front_out.predict_dir[j] = false;
        number_PC += 4;
      }

      front_out.predict_next_fetch_address = number_PC;

#endif

#ifdef CONFIG_BRANCHCHECK

      branch_check();
      for (int j = 0; j < FETCH_WIDTH; j++) {
        back.in.valid[j] = true;
        back.in.pc[j] = fetch_PC[j];
        back.in.inst[j] = p_memory[fetch_PC[j] >> 2];
        if (LOG)
          cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" << hex
               << fetch_PC[j] << endl;

        if (j != FETCH_WIDTH - 1)
          back.in.predict_next_fetch_address[j] = fetch_PC[j + 1];
        else
          back.in.predict_next_fetch_address[j] = next_PC[0];

        back.in.predict_dir[j] =
            (back.in.predict_next_fetch_address[j] != fetch_PC[j] + 4);
      }

      for (int j = 0; j < FETCH_WIDTH; j++) {
        fetch_PC[j] = next_PC[j];
      }

#else
      bool no_taken = true;
      for (int j = 0; j < FETCH_WIDTH; j++) {
        back.in.valid[j] = front_out.FIFO_valid && no_taken;
        back.in.pc[j] = front_out.pc[j];
        back.in.predict_next_fetch_address[j] =
            front_out.predict_next_fetch_address;
        back.in.inst[j] = front_out.instructions[j];
        if (LOG && back.in.valid[j]) {
          cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" << hex
               << front_out.pc[j] << " Inst: " << back.in.inst[j] << endl;
        }

        back.in.predict_dir[j] = front_out.predict_dir[j];
        back.in.alt_pred[j] = front_out.alt_pred[j];
        back.in.altpcpn[j] = front_out.altpcpn[j];
        back.in.pcpn[j] = front_out.pcpn[j];

        if (front_out.predict_dir[j])
          no_taken = false;
      }

#endif
    }

    load_slave_comb();
    store_slave_comb();
    back.Back_comb();

#ifdef CONFIG_BPU

    front_in.FIFO_read_enable = false;
    for (int i = 0; i < COMMIT_WIDTH; i++) {
      Inst_info *inst = &back.out.commit_entry[i].inst;
      front_in.back2front_valid[i] = back.out.commit_entry[i].valid;
      if (front_in.back2front_valid[i]) {

        front_in.predict_dir[i] = inst->pred_br_taken;
        front_in.predict_base_pc[i] = inst->pc;
        /*cout << hex << "commit pc " << inst->pc << endl;*/
        front_in.actual_dir[i] = inst->br_taken;
        front_in.actual_target[i] = inst->pc_next;
        int br_type = BR_DIRECT;

        if (inst->op == JALR) {
          if (inst->src1_areg == 1)
            br_type = BR_RET;
          else
            br_type = BR_IDIRECT;
        } else if (inst->op == JAL) {
          if (inst->dest_areg == 1)
            br_type = BR_CALL;
        }

        front_in.actual_br_type[i] = br_type;
        front_in.alt_pred[i] = inst->alt_pred;
        front_in.altpcpn[i] = inst->altpcpn;
        front_in.pcpn[i] = inst->pcpn;
      }
    }

    if (back.out.mispred) {
      front_in.refetch = true;
      front_in.refetch_address = back.out.redirect_pc;
      /*cout << hex << "re pc " << front_in.refetch_address << endl;*/
    } else {
      front_in.refetch = false;
    }

    front_top(&front_in, &front_out);

    for (int i = 0; i < COMMIT_WIDTH; i++) {
      front_in.back2front_valid[i] = false;
    }

    front_in.refetch = false;
#endif

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
      for (int j = 0; j < FETCH_WIDTH; j++) {
        if (back.out.fire[j])
          back.in.valid[j] = false;
      }
    }
  }

SIM_END:

  delete[] p_memory;
  extern int tage_cnt;
  extern int tage_miss;
  extern int dir_ok_addr_error;
  extern int pred_ok;
  extern int taken_num;

  if (sim_time != MAX_SIM_TIME) {
    if (ret == 0) {
      cout << "\033[1;32m-----------------------------\033[0m" << endl;
      cout << "\033[1;32mSuccess!!!!\033[0m" << endl;
      printf("\033[1;32minstruction num: %d\033[0m\n", commit_num);
      printf("\033[1;32mcycle num      : %d\033[0m\n", sim_time);
      printf("\033[1;32mipc            : %f\033[0m\n",
             (double)commit_num / sim_time);
      printf("\033[1;32mbranch num     : %d\033[0m\n", branch_num);
      printf("\033[1;32mmispred num    : %d\033[0m\n", mispred_num);
      cout << "\033[1;32m-----------------------------\033[0m" << endl;

      cout << "addr error :" << dec << dir_ok_addr_error << endl;
      cout << "tage cnt :" << dec << tage_cnt << endl;
      cout << "tage miss :" << dec << tage_miss << endl;
      cout << "b2f miss :" << dec << back2front_num << endl;
      /*cout << "pred ok :" << dec << pred_ok << endl;*/
      /*cout << "taken_num:" << dec << taken_num << endl;*/
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

      /*if (waddr == 0x1c) {*/
      /*  ret = wdata;*/
      /*  sim_end = true;*/
      /*  return;*/
      /*}*/

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
    if (back.out.bready && back.in.bvalid) {
      store_slave_state = IDLE;
    }
  }
}
