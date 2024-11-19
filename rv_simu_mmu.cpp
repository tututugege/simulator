#include <RISCV.h>
#include <TOP.h>
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <fstream>

const int bit_width = POS_IN_REG_B + 32;
int time_i = 0;

using namespace std;

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);

// 后端执行
Back_Top back = Back_Top();

static bool input_data_to_RISCV[BIT_WIDTH_INPUT] = {0};
static bool output_data_from_RISCV[BIT_WIDTH_OUTPUT] = {0};

int commit_num;

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  ifstream inst_data(argv[argc - 1], ios::in);

  char **ptr = NULL;
  long i = 0;
  bool ret;

  bool USE_MMU_PHYSICAL_MEMORY = true;
  /*init_indice(p_memory, 0, PHYSICAL_MEMORY_LENGTH);*/

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
  init_difftest(diff_so, i);
  back.init();

  bool number_PC_bit[INST_WAY][BIT_WIDTH_PC] = {0};
  bool p_addr[INST_WAY][32] = {0};
  bool MMU_ret_state = true;
  bool filelog = true;
  uint32_t number_PC = 0;
  ofstream outfile;
  bool stall, misprediction;

  // main loop
  for (i = 0; i < MAX_SIM_TIME; i++) {

    if (LOG)
      cout << "****************************************************************"
           << endl;

    // copy registers states, include: 32+21 (include satp)
    copy_indice(input_data_to_RISCV, 0, output_data_from_RISCV, 0,
                BIT_WIDTH_REG_STATES);
    copy_indice(input_data_to_RISCV, POS_IN_PRIVILEGE, output_data_from_RISCV,
                POS_OUT_PRIVILEGE, 2);
    /*init_indice(input_data_to_RISCV, POS_IN_INST,*/
    /*            32 * 3 + 4); // inst, pc, load data, asy, page fault*/

    if (i == 0) {

      // 复位pc 0x80000000
      for (int j = 0; j < INST_WAY; j++) {
        cvt_number_to_bit_unsigned(number_PC_bit[j], 0x80000000 + j * 4, 32);
      }
      /*// 写misa 寄存器  32-IA 支持User和Supervisor*/
      /*cvt_number_to_bit_unsigned(input_data_to_RISCV +*/
      /*                               POS_CSR_MISA * sizeof(bool),*/
      /*                           0x40140101, 32); // 0x4014112d //0x40140101*/
      /*                                            //*/
      /*cvt_number_to_bit_unsigned(*/
      /*    output_data_from_RISCV + POS_CSR_MISA * sizeof(bool), 0x40140101,
       * 32);*/
      /*p_memory[0x10000004 / 4] = 0x00006000;*/

      // M-mode
      input_data_to_RISCV[POS_IN_PRIVILEGE] = true;
      input_data_to_RISCV[POS_IN_PRIVILEGE + 1] = true;
      output_data_from_RISCV[POS_OUT_PRIVILEGE] = true;
      output_data_from_RISCV[POS_OUT_PRIVILEGE + 1] = true;
    }

    if (!stall || misprediction) {
      for (int j = 0; j < INST_WAY; j++) {
        number_PC = cvt_bit_to_number(number_PC_bit[j], BIT_WIDTH_PC);

        if (LOG)
          cout
              << "指令index:" << dec << i + 1 << " 当前PC的取值为:" << hex
              << number_PC
              << endl; // << "SIE"<<
                       // cvt_bit_to_number_unsigned(&input_data_to_RISCV[1536],
                       // 32) << endl;
        // cout << hex<< number_PC<<endl;

        uint32_t privilege = cvt_bit_to_number_unsigned(
            input_data_to_RISCV + POS_IN_PRIVILEGE * sizeof(bool), 2);

        if (number_PC == 0x80000000) {
          privilege = 1;
          // Supervisor
          input_data_to_RISCV[POS_IN_PRIVILEGE] = false;
          input_data_to_RISCV[POS_IN_PRIVILEGE + 1] = true;
          output_data_from_RISCV[POS_OUT_PRIVILEGE] = false;
          output_data_from_RISCV[POS_OUT_PRIVILEGE + 1] = true;
        }

        bool bit_inst[INST_WAY][32] = {false};
        /*bool *satp = &input_data_to_RISCV[POS_CSR_SATP];*/
        /*bool *mstatus = &input_data_to_RISCV[POS_CSR_MSTATUS];*/
        /*bool *sstatus = &input_data_to_RISCV[POS_CSR_SSTATUS];*/

        uint32_t inst;
        inst = p_memory[number_PC / 4];

        cvt_number_to_bit_unsigned(bit_inst[j], inst,
                                   32); // 取指令

        *(input_data_to_RISCV + POS_IN_INST_VALID + j) = true;
        copy_indice(input_data_to_RISCV, POS_IN_INST + 32 * j, bit_inst[j], 0,
                    32);
        copy_indice(input_data_to_RISCV, POS_IN_PC + 32 * j, number_PC_bit[j],
                    0,
                    32); // 取PC
        init_indice(input_data_to_RISCV, POS_IN_LOAD_DATA,
                    32); // load data init
      }
    }

    // TODO
    // asy and page fault
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    RISCV_32I(input_data_to_RISCV, output_data_from_RISCV);

    bool wen = *(output_data_from_RISCV + POS_OUT_STORE);
    if (wen) {
      uint32_t wdata = cvt_bit_to_number_unsigned(
          output_data_from_RISCV + POS_OUT_STORE_DATA, 32);

      uint32_t waddr = cvt_bit_to_number_unsigned(
          output_data_from_RISCV + POS_OUT_STORE_ADDR, 32);

      uint32_t wstrb = cvt_bit_to_number_unsigned(
          output_data_from_RISCV + POS_OUT_STORE_STRB, 4);

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

    misprediction = *(output_data_from_RISCV + POS_OUT_BRANCH);
    if (misprediction) {
      number_PC =
          cvt_bit_to_number_unsigned(output_data_from_RISCV + POS_OUT_PC, 32);
      number_PC -= 4;
    }

    stall = *(output_data_from_RISCV + POS_OUT_STALL);
    if (!stall || misprediction) {
      for (int j = 0; j < INST_WAY; j++) {
        number_PC += 4;
        cvt_number_to_bit_unsigned(number_PC_bit[j], number_PC, 32);
      }
    } else {
      bool *fire = output_data_from_RISCV + POS_OUT_FIRE;
      for (int j = 0; j < INST_WAY; j++) {
        if (fire[j])
          *(input_data_to_RISCV + POS_IN_INST_VALID + j) = false;
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
  uint32_t address = cvt_bit_to_number_unsigned(
      output_data_from_RISCV + POS_OUT_LOAD_ADDR, 32);

  uint32_t data = p_memory[address / 4];
  cvt_number_to_bit_unsigned(input_data_to_RISCV + POS_IN_LOAD_DATA, data, 32);
}
