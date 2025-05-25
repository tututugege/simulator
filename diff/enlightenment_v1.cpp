const int bit_width = 1798 + 64;
bool log = false;
int time_i = 0;
#include "../include/RISCV.h"
#include "include/RISCV.h"
#include <config.h>
#include <diff.h>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
// #include"include/cvt.h"
#include "include/RISCV_MMU.h"
#define USE_MMU_PHYSICAL_MEMORY 1
extern bool USE_LINUX_SIMU; // 是否为启动 Linux 模式

enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

// 控制是否使用 v1 在 uart 输出
bool v1_uart_enable = true;

const int BIT_WIDTH_INPUT = bit_width;
const int BIT_WIDTH_OUTPUT = 1826;
const int BIT_WIDTH_PC = 32;
const int BIT_WIDHT_OP_CODE = 7;

const int BIT_WIDTH_REG_STATES = 1696; // 32+21

const int POS_IN_INST = 1696;
const int POS_IN_PC = 1728;
const int POS_IN_LOAD_DATA = 1760;
const int POS_IN_REG_A = 1798;
const int POS_IN_REG_B = 1830;

const int POS_OUT_PC = 1696;
const int POS_OUT_LOAD_ADDR = 1728;
const int POS_OUT_STORE_DATA = 1760;
const int POS_OUT_STORE_ADDR = 1792;
const int POS_PAGE_FAULT_INST = 1793;
const int POS_PAGE_FAULT_LOAD = 1794;
const int POS_PAGE_FAULT_STORE = 1795;

// const long VIRTUAL_MEMORY_LENGTH = 1024 * 1024 * 1024; //4B

static bool input_data_to_RISCV[BIT_WIDTH_INPUT] = {0};
static bool output_data_from_RISCV[BIT_WIDTH_OUTPUT] = {0};

static bool number_PC_bit[BIT_WIDTH_PC] = {0};
static bool number_op_code_bit[BIT_WIDHT_OP_CODE] = {0};
static bool p_addr[32] = {0};
static bool MMU_ret_state = true;

static bool filelog = true;
static uint32_t number_PC = 0;
static ofstream outfile;
// extern bool log = true;
// extern int time_i = 0;
static long i = 0;
// static bool USE_MMU_PHYSICAL_MEMORY = true;

uint32_t *ref_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
extern CPU_state dut, ref;

void v1_difftest_init(uint32_t pc_start) {
  // copy 0x80000000 to output_data_from_RISCV+POS_OUT_PC
  number_PC = pc_start;
  v1_uart_enable = USE_LINUX_SIMU;
  cvt_number_to_bit_unsigned(number_PC_bit, number_PC, 32);
  cvt_number_to_bit_unsigned(output_data_from_RISCV + POS_OUT_PC, number_PC,
                             32);
}

void v1_difftest_exec() {
  if (i % 1000000 == 0) {
    cout << hex << i << ' ' << number_PC << endl;
  }
  if (log)
    cout << "******************************************************************"
            "************************"
         << endl;

  time_i = i++;
  // copy registers states, include: 32+21 (include satp)
  copy_indice(input_data_to_RISCV, 0, output_data_from_RISCV, 0,
              BIT_WIDTH_REG_STATES);
  copy_indice(input_data_to_RISCV, 1796, output_data_from_RISCV, 1824, 2);
  init_indice(input_data_to_RISCV, 1696, 32 * 3 + 4);
  copy_indice(number_PC_bit, 0, output_data_from_RISCV, BIT_WIDTH_REG_STATES,
              32);

  if (i == 1) {
    // cvt_number_to_bit_unsigned(number_PC_bit, 0x00001000, 32);

    // - dhrystone/coremark 时，为 0x00000000
    // - linux_simu 时，为 0x80000000
    // - 这一步已经在 v1_difftest_init 中做了
    // cvt_number_to_bit_unsigned(number_PC_bit, 0x00000000, 32);

    //
    // MISA: U/S/I/A
    //
    cvt_number_to_bit_unsigned(input_data_to_RISCV + 1664 * sizeof(bool),
                               0x40140101, 32); // 0x4014112d //0x40140101
    cvt_number_to_bit_unsigned(output_data_from_RISCV + 1664 * sizeof(bool),
                               0x40140101, 32);
    if (USE_LINUX_SIMU)
      ref_memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
    // machine state
    // 11 -> M, 3
    // 01 -> S, 1
    input_data_to_RISCV[1796] = true;
    input_data_to_RISCV[1797] = true;
    output_data_from_RISCV[1824] = true;
    output_data_from_RISCV[1825] = true;
  }

  number_PC = cvt_bit_to_number(number_PC_bit, BIT_WIDTH_PC);

  uint32_t privilege =
      cvt_bit_to_number_unsigned(input_data_to_RISCV + 1796 * sizeof(bool), 2);

  if (number_PC == 0x80000000) {
    privilege = 1;
    input_data_to_RISCV[1796] = false;
    input_data_to_RISCV[1797] = true;
    output_data_from_RISCV[1824] = false;
    output_data_from_RISCV[1825] = true;
  }

  bool bit_inst[32] = {false};
  bool *satp = &input_data_to_RISCV[1600];
  bool *mstatus = &input_data_to_RISCV[1248];
  bool *sstatus = &input_data_to_RISCV[1504];
  if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&
      privilege != 3) { // mmu physical
    MMU_ret_state = va2pa(p_addr, satp, number_PC_bit, ref_memory, 0, mstatus,
                          privilege, sstatus);
    input_data_to_RISCV[POS_PAGE_FAULT_INST] = !MMU_ret_state;
    if (MMU_ret_state) {
      uint32_t number_PC_p = cvt_bit_to_number_unsigned(p_addr, 32);
      if (log)
        cout << "当前物理地址为: " << hex << number_PC_p << endl;
      cvt_number_to_bit_unsigned(
          bit_inst, ref_memory[uint32_t(number_PC_p / 4)],
          32); // TODO。不太对, 没有经过初始化，哪里有指令？
      if (log)
        cout << "当前指令为:" << hex << ref_memory[uint32_t(number_PC_p / 4)]
             << endl;
    } // page fault的话，到下面RISCV会处理
    else {
      if (log)
        cout << "PAGE FAULT INST" << endl;
      copy_indice(input_data_to_RISCV, POS_IN_PC, number_PC_bit, 0, 32);
      RISCV(input_data_to_RISCV, output_data_from_RISCV);
      // continue;
      return;
    }
  } else if (USE_MMU_PHYSICAL_MEMORY) {
    cvt_number_to_bit_unsigned(bit_inst, ref_memory[number_PC / 4],
                               32); // 取指令
    if (log)
      cout << "当前指令为:" << hex << ref_memory[number_PC / 4] << endl;
  }
  copy_indice(input_data_to_RISCV, POS_IN_INST, bit_inst, 0, 32);
  copy_indice(input_data_to_RISCV, POS_IN_PC, number_PC_bit, 0, 32); // 取PC
  init_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, 32); // load data init

  // TODO
  // asy and page fault
  bool rs_a_code[5]; // 12-16
  bool rs_b_code[5]; // 7-11
  copy_indice(rs_a_code, 0, bit_inst, 12, 5);
  copy_indice(rs_b_code, 0, bit_inst, 7, 5);
  uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);
  copy_indice(input_data_to_RISCV, POS_IN_REG_A, output_data_from_RISCV,
              32 * reg_a_index, 32);
  copy_indice(input_data_to_RISCV, POS_IN_REG_B, output_data_from_RISCV,
              32 * reg_b_index, 32);
  copy_indice(number_op_code_bit, 0, bit_inst, 25, 7); // 取操作码
  uint32_t number_op_code = cvt_bit_to_number_unsigned(number_op_code_bit, 7);
  // for (int i = 0;i < 32;i++) cout << bit_inst[i];

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  RISCV(input_data_to_RISCV, output_data_from_RISCV);
  if (log) {
    // print_regs(output_data_from_RISCV);
    // print_csr_regs(output_data_from_RISCV);
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (number_op_code == 3) { // load
    bool bit_load_address[32];
    bool bit_load_data[32];
    uint32_t number_load_data = 0, number_load_address = 0;

    copy_indice(bit_load_address, 0, output_data_from_RISCV, POS_OUT_LOAD_ADDR,
                32); // 从output中读取address

    bool *satp = &output_data_from_RISCV[1600];
    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) {
      // mmu
      MMU_ret_state = va2pa(p_addr, satp, bit_load_address, ref_memory, 1,
                            mstatus, privilege, sstatus);
      input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;

      if (MMU_ret_state) { // no page fault
        // for physical memory
        number_load_address = cvt_bit_to_number_unsigned(p_addr, 32);
        number_load_data = ref_memory[uint32_t(number_load_address / 4)];
        cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
      } else {
        if (log)
          cout << "PAGE FAULT LOAD" << endl;
        RISCV(input_data_to_RISCV, output_data_from_RISCV);
        // continue;
        return;
      }
    } else if (USE_MMU_PHYSICAL_MEMORY) {
      number_load_address = cvt_bit_to_number_unsigned(bit_load_address, 32);
      number_load_data = ref_memory[uint32_t(number_load_address / 4)];
      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
    }
    if (number_load_address == 0x1fd0e000) {
      number_load_data = time_i;
      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
    }
    if (number_load_address == 0x1fd0e004) {
      number_load_data = 0;
      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
    }
    bool bit_temp[32] = {0};
    bool bit_funct3[3];
    copy_indice(bit_funct3, 0, bit_inst, 17, 3); // get data
    uint32_t number_funct3 = cvt_bit_to_number_unsigned(bit_funct3, 3);
    switch (number_funct3) {
    case 0: { // lb
      uint32_t pos = uint32_t(1 ^ (*(bit_load_address + 31))) +
                     2 * uint32_t(1 ^ (*(bit_load_address + 30)));
      pos *= 8;
      copy_indice(bit_temp, 24, bit_load_data, pos * sizeof(bool), 8);
      break;
    }
    case 1: { // lh
      uint32_t pos = (*(bit_load_address + 30)) ? 0 : 16;
      copy_indice(bit_temp, 16, bit_load_data, pos * sizeof(bool), 16);
      break;
    }
    case 2: { // lw
      copy_indice(bit_temp, 0, bit_load_data, 0, 32);
      break;
    }
    case 4: { // lbu
      uint32_t pos = uint32_t(1 ^ (*(bit_load_address + 31))) +
                     2 * uint32_t(1 ^ (*(bit_load_address + 30)));
      pos *= 8;
      copy_indice(bit_temp, 24, bit_load_data, pos * sizeof(bool), 8);
      break;
    }
    case 5: { // lhu
      uint32_t pos = (*(bit_load_address + 30)) ? 0 : 16;
      copy_indice(bit_temp, 16, bit_load_data, pos * sizeof(bool), 16);
      break;
    }
    }
    copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, bit_temp, 0,
                32); // 将load的data存入input data， 之后放到寄存器里面
    // load 之后再次执行指令，放到寄存器中
    RISCV(input_data_to_RISCV, output_data_from_RISCV);
    if (log)
      cout << " ======= LOAD ===== " << "addr 0x" << hex << number_load_address
           << " data: " << hex << number_load_data << endl;
  }

  if (number_op_code == 35) { // store
    bool bit_store_address[32];
    bool bit_store_data[32];
    uint32_t number_store_address = 0, number_memory_temp = 0;

    copy_indice(bit_store_address, 0, output_data_from_RISCV,
                POS_OUT_STORE_ADDR, 32); // get addr

    // store data ( physical or virtual)
    bool *satp = &output_data_from_RISCV[1600];
    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) { // physical
      MMU_ret_state = va2pa(p_addr, satp, bit_store_address, ref_memory, 2,
                            mstatus, privilege, sstatus);
      input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;
      if (!MMU_ret_state) { // can't load inst, give flag and let core process
        if (log)
          cout << "PAGE FAULT STORE" << endl;
        RISCV(input_data_to_RISCV, output_data_from_RISCV);
        // continue;
        return;
      }
      number_store_address = cvt_bit_to_number_unsigned(p_addr, 32);
      number_memory_temp = ref_memory[uint32_t(number_store_address / 4)];
    } else if (USE_MMU_PHYSICAL_MEMORY) {
      number_store_address = cvt_bit_to_number_unsigned(bit_store_address, 32);
      number_memory_temp = ref_memory[uint32_t(number_store_address / 4)];
    }

    bool bit_memory_temp[32];
    cvt_number_to_bit_unsigned(bit_memory_temp, number_memory_temp, 32);
    copy_indice(bit_store_data, 0, output_data_from_RISCV, POS_OUT_STORE_DATA,
                32); // get data
    uint32_t number_store_data = cvt_bit_to_number_unsigned(bit_store_data, 32);

    bool bit_funct3[3];
    copy_indice(bit_funct3, 0, bit_inst, 17, 3); // get data
    uint32_t number_funct3 = cvt_bit_to_number_unsigned(bit_funct3, 3);
    if ((number_funct3 == 1 && number_store_address % 2 == 1) ||
        (number_funct3 == 2 && number_store_address % 4 != 0)) {
      cout << "Store Memory Address Align Error!!!" << endl;
      cout << "funct3 code: " << dec << number_funct3 << endl;
      cout << "addr: " << hex << number_store_address << endl;
      exit(-1);
    }
    switch (number_funct3) {
    case 0: { // sb
      uint32_t pos = uint32_t(1 ^ bit_store_address[31]) +
                     2 * uint32_t(1 ^ bit_store_address[30]);
      pos *= 8;
      copy_indice(bit_memory_temp, pos, bit_store_data, 24, 8);
      if (log)
        cout << " ======= STOREB ===== " << "addr 0x" << hex
             << number_store_address << " data: " << hex
             << (number_store_data & 0x000000ff) << endl;
      break;
    }
    case 1: { // sh
      uint32_t pos = bit_store_address[30] ? 0 : 16;
      copy_indice(bit_memory_temp, pos, bit_store_data, 16, 16);
      if (log)
        cout << " ======= STOREH ===== " << "addr 0x" << hex
             << number_store_address << " data: " << hex
             << (number_store_data & 0x0000ffff) << endl;
      break;
    }
    case 2: { // sw
      copy_indice(bit_memory_temp, 0, bit_store_data, 0, 32);
      if (log)
        cout << " ======= STOREW ===== " << "addr 0x" << hex
             << number_store_address << " data: " << hex << number_store_data
             << endl;
      break;
    }
    default:
      cout << "Error!!!" << endl;
    }

    // save data
    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) // physical
      ref_memory[uint32_t(number_store_address / 4)] =
          cvt_bit_to_number_unsigned(bit_memory_temp, 32);
    else if (USE_MMU_PHYSICAL_MEMORY)
      ref_memory[uint32_t(number_store_address / 4)] =
          cvt_bit_to_number_unsigned(bit_memory_temp, 32);

    // uart显示   physical TODO
    if (v1_uart_enable)
      if (number_store_address == 0x10000000) {
        char temp;
        temp = number_store_data & 0x000000ff;
        ref_memory[0x10000000 / 4] = ref_memory[0x10000000 / 4] & 0xffffff00;
        cerr << temp;

        if (temp == 0x3F) {
          cerr << hex << time_i << endl;
          // log = true;
        }
      }
    if (number_store_address == 0x10000001 &&
        (number_store_data & 0x000000ff) == 7) {
      // cerr << "UART enabled!" << endl;
      output_data_from_RISCV[1152 + 31 - 9] = 1; // mip
      output_data_from_RISCV[1568 + 31 - 9] = 1; // sip
      ref_memory[0xc201004 / 4] = 0xa;
      // log = true;
      ref_memory[0x10000000 / 4] = ref_memory[0x10000000 / 4] & 0xfff0ffff;
    }
    if (number_store_address == 0x10000001 &&
        (number_store_data & 0x000000ff) == 5) {
      // cerr << "UART disabled2!" << endl;
      //  ref_memory[0xc201004/4] = 0x0;
      ref_memory[0x10000000 / 4] =
          ref_memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
    }
    if (number_store_address == 0xc201004 &&
        (number_store_data & 0x000000ff) == 0xa) {
      // cerr << "UART disabled1!" << endl;
      ref_memory[0xc201004 / 4] = 0x0;
      output_data_from_RISCV[1152 + 31 - 9] = 0; // mip
      output_data_from_RISCV[1568 + 31 - 9] = 0; // sip
    }
  }

  if (number_op_code == 47) {
    bool bit_funct5[5];
    copy_indice(bit_funct5, 0, bit_inst, 0, 5);
    uint32_t number_funct5_unsigned = cvt_bit_to_number_unsigned(bit_funct5, 5);
    switch (number_funct5_unsigned) {
    case 2: { // lr.w
      bool bit_load_address[32];
      bool bit_load_data[32];
      uint32_t number_load_address = 0, number_load_data = 0;

      copy_indice(bit_load_address, 0, output_data_from_RISCV,
                  POS_OUT_LOAD_ADDR, 32); // 从output中读取address

      bool *satp = &output_data_from_RISCV[1600];
      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) {
        // mmu
        MMU_ret_state = va2pa(p_addr, satp, bit_load_address, ref_memory, 1,
                              mstatus, privilege, sstatus);
        input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;

        if (MMU_ret_state) { // no page fault
          // for physical memory
          number_load_address = cvt_bit_to_number_unsigned(p_addr, 32);
          number_load_data = ref_memory[uint32_t(number_load_address / 4)];
          cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
        } else {
          if (log)
            cout << "PAGE FAULT LOAD" << endl;
          RISCV(input_data_to_RISCV, output_data_from_RISCV);
          // continue;
          return;
        }
      } else if (USE_MMU_PHYSICAL_MEMORY) {
        number_load_address = cvt_bit_to_number_unsigned(bit_load_address, 32);
        number_load_data = ref_memory[uint32_t(number_load_address / 4)];
        cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
      }
      // load 之后再次执行指令，放到寄存器中
      copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, bit_load_data, 0,
                  32); // 将load的data存入input data， 之后放到寄存器里面
      RISCV(input_data_to_RISCV, output_data_from_RISCV);
      if (log)
        cout << " ======= LR ===== " << "addr 0x" << hex << number_load_address
             << " data: " << hex << number_load_data << endl;
      break;
    }
    case 3: { // sc.w
      bool bit_store_address[32];
      bool bit_store_data[32];
      uint32_t number_store_address = 0, number_store_data = 0;

      copy_indice(bit_store_address, 0, output_data_from_RISCV,
                  POS_OUT_STORE_ADDR, 32); // get addr

      // store data ( physical or virtual)
      bool *satp = &output_data_from_RISCV[1600];
      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&
          privilege != 3) { // physical
        MMU_ret_state = va2pa(p_addr, satp, bit_store_address, ref_memory, 2,
                              mstatus, privilege, sstatus);
        input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;
        if (!MMU_ret_state) { // can't load inst, give flag and let core process
          if (log)
            cout << "PAGE FAULT STORE" << endl;
          RISCV(input_data_to_RISCV, output_data_from_RISCV);
          // continue;
          return;
        }
        number_store_address = cvt_bit_to_number_unsigned(p_addr, 32);
        copy_indice(bit_store_data, 0, output_data_from_RISCV,
                    POS_OUT_STORE_DATA, 32); // get data
        uint32_t number_store_data =
            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to memory
        ref_memory[uint32_t(number_store_address / 4)] = number_store_data;
      } else if (USE_MMU_PHYSICAL_MEMORY) {
        number_store_address =
            cvt_bit_to_number_unsigned(bit_store_address, 32);
        copy_indice(bit_store_data, 0, output_data_from_RISCV,
                    POS_OUT_STORE_DATA, 32); // get data
        uint32_t number_store_data =
            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to memory
        ref_memory[uint32_t(number_store_address / 4)] = number_store_data;
      }

      if (number_store_address % 4 != 0) {
        cout << "Store Memory Address Align Error!!!" << endl;
        cout << "sc.w" << endl;
        cout << "addr: " << hex << number_store_address << endl;
        exit(-1);
      }
      if (log)
        cout << " ======= SC ===== " << "addr 0x" << hex << number_store_address
             << " data: " << hex << number_store_data << endl;
      break;
    }
    default: { // amoswap.w, amoadd.w, amoxor.w, amoand.w, amoor.w, amomin.w,
               // amomax.w, amominu.w, amomaxu.w
      // first load data
      bool bit_load_address[32];
      bool bit_load_data[32];
      uint32_t number_load_address = 0, number_load_data = 0;

      copy_indice(bit_load_address, 0, output_data_from_RISCV,
                  POS_OUT_LOAD_ADDR, 32); // 从output中读取address

      bool *satp = &output_data_from_RISCV[1600];
      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) {
        // mmu
        MMU_ret_state = va2pa(p_addr, satp, bit_load_address, ref_memory, 1,
                              mstatus, privilege, sstatus);
        input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;

        if (MMU_ret_state) { // page fault
          // for physical memory
          number_load_address = cvt_bit_to_number_unsigned(p_addr, 32);
          number_load_data = ref_memory[uint32_t(number_load_address / 4)];
          cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
          copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, bit_load_data, 0,
                      32); // 将load的data存入input data， 之后放到寄存器里面
        } else {
          if (log)
            cout << "PAGE FAULT LOAD" << endl;
          RISCV(input_data_to_RISCV, output_data_from_RISCV);
          // continue;
          return;
        }
      } else if (USE_MMU_PHYSICAL_MEMORY) {
        number_load_address = cvt_bit_to_number_unsigned(bit_load_address, 32);
        number_load_data = ref_memory[uint32_t(number_load_address / 4)];
        cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);
        copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, bit_load_data, 0,
                    32); // 将load的data存入input data， 之后放到寄存器里面
      }
      // load 之后再次执行指令，放到寄存器中
      RISCV(input_data_to_RISCV, output_data_from_RISCV);
      if (log)
        cout << " ======= LOAD ===== " << "addr 0x" << hex
             << number_load_address << " data: " << hex << number_load_data
             << endl;
      // then store to the other memory
      bool bit_store_address[32];
      bool bit_store_data[32];
      uint32_t number_store_address = 0, number_store_data = 0;
      copy_indice(bit_store_address, 0, output_data_from_RISCV,
                  POS_OUT_STORE_ADDR, 32); // get addr

      // store data ( physical or virtual)
      // bool *satp =  &output_data_from_RISCV[1600];
      satp = &output_data_from_RISCV[1600];
      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&
          privilege != 3) { // physical
        MMU_ret_state = va2pa(p_addr, satp, bit_store_address, ref_memory, 2,
                              mstatus, privilege, sstatus);
        input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;
        if (!MMU_ret_state) { // can't load inst, give flag and let core process
          if (log)
            cout << "PAGE FAULT STORE" << endl;
          RISCV(input_data_to_RISCV, output_data_from_RISCV);
          // continue;
          return;
        }
        number_store_address = cvt_bit_to_number_unsigned(p_addr, 32);
        copy_indice(bit_store_data, 0, output_data_from_RISCV,
                    POS_OUT_STORE_DATA, 32); // get data
        uint32_t number_store_data =
            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to memory
        ref_memory[uint32_t(number_store_address / 4)] = number_store_data;
      } else if (USE_MMU_PHYSICAL_MEMORY) {
        number_store_address =
            cvt_bit_to_number_unsigned(bit_store_address, 32);
        copy_indice(bit_store_data, 0, output_data_from_RISCV,
                    POS_OUT_STORE_DATA, 32); // get data
        uint32_t number_store_data =
            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to memory
        ref_memory[uint32_t(number_store_address / 4)] = number_store_data;
      }

      if (number_store_address % 4 != 0) {
        cout << "Store Memory Address Align Error!!!" << endl;
        cout << "amo" << endl;
        cout << "addr: " << hex << number_store_address << endl;
        exit(-1);
      }
      if (log)
        cout << " ======= SC ===== " << "addr 0x" << hex << number_store_address
             << " data: " << hex << number_store_data << endl;
      break;
    }
    }
  }
  // log
  if (filelog) {
    outfile.write((char *)(input_data_to_RISCV + 1760), 4);    // load_data
    outfile.write((char *)(output_data_from_RISCV + 1728), 4); // load_address
    outfile.write((char *)(output_data_from_RISCV + 1760), 4); // store_data
    outfile.write((char *)(output_data_from_RISCV + 1792), 4); // store_address
    // for (int q = 1760; q < 1760+32; q++) outfile << input_data_to_RISCV[q];
    // // load_data for (int q = 1728; q < 1728+32; q++) outfile <<
    // output_data_from_RISCV[q]; // load_address for (int q = 1760; q <
    // 1760+32; q++) outfile << output_data_from_RISCV[q]; // store_data for
    // (int q = 1792; q < 1792+2; q++) outfile << output_data_from_RISCV[q]; //
    // store_address outfile << endl;
  }
}

void v1_difftest_regcpy(CPU_state *ref, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    // copy gpr state from output of v1
    for (int i = 0; i < 32; i++) {
      uint32_t number_temp =
          cvt_bit_to_number_unsigned(output_data_from_RISCV + 32 * i, 32);
      ref->gpr[i] = number_temp;
    }
    ref->pc =
        cvt_bit_to_number_unsigned(output_data_from_RISCV + POS_OUT_PC, 32);
    // TODO: copy csr state from output of v1
  } else {
    // this should not happen in current design
    std::cerr << "v1_difftest_regcpy: direction is not DIFFTEST_TO_REF"
              << std::endl;
    exit(1);
  }
}
