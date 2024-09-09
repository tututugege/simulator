#include <RISCV.h>
#include <TOP.h>
#include <cstdint>
#include <cstdio>
#include <cvt.h>
#include <diff.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const int bit_width = POS_IN_REG_B + 32;
bool log = true;
int time_i = 0;

using namespace std;

//
enum csr_reg {
  CSR_MTVEC = 64,
  CSR_MEPC,
  CSR_MCAUSE,
  CSR_MIE,
  CSR_MIP,
  CSR_MTVAL,
  CSR_MSCRATCH,
  CSR_MSTATUS,
  CSR_MIDELEG,
  CSR_MEDELEG,
  CSR_SEPC,
  CSR_STVEC,
  CSR_SCAUSE,
  CSR_SSCATCH,
  CSR_STVAL,
  CSR_SSTATUS,
  CSR_SIE,
  CSR_SIP,
  CSR_SATP,
  CSR_MHARTID,
  CSR_MISA
};

const int POS_CSR_MTVEC = CSR_MTVEC * 32;
const int POS_CSR_MEPC = CSR_MEPC * 32;
const int POS_CSR_MCAUSE = CSR_MCAUSE * 32;
const int POS_CSR_MIE = CSR_MIE * 32;
const int POS_CSR_MIP = CSR_MIP * 32;
const int POS_CSR_MTVAL = CSR_MTVAL * 32;
const int POS_CSR_MSCRATCH = CSR_MSCRATCH * 32;
const int POS_CSR_MSTATUS = CSR_MSTATUS * 32;
const int POS_CSR_MIDELEG = CSR_MIDELEG * 32;
const int POS_CSR_MEDELEG = CSR_MEDELEG * 32;
const int POS_CSR_SEPC = CSR_SEPC * 32;
const int POS_CSR_STVEC = CSR_STVEC * 32;
const int POS_CSR_SCAUSE = CSR_SCAUSE * 32;
const int POS_CSR_SSCRATCH = CSR_SSCATCH * 32;
const int POS_CSR_STVAL = CSR_STVAL * 32;
const int POS_CSR_SSTATUS = CSR_SSTATUS * 32;
const int POS_CSR_SIE = CSR_SIE * 32;
const int POS_CSR_SIP = CSR_SIP * 32;
const int POS_CSR_SATP = CSR_SATP * 32;
const int POS_CSR_MHARTID = CSR_MHARTID * 32;
const int POS_CSR_MISA = CSR_MISA * 32;

uint32_t *p_memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
uint32_t POS_MEMORY_SHIFT = uint32_t(0x80000000 / 4);

// 后端执行
Back_Top back;

/* =====================================

*/
// ======================================

static bool input_data_to_RISCV[BIT_WIDTH_INPUT] = {0};
static bool output_data_from_RISCV[BIT_WIDTH_OUTPUT] = {0};

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  ifstream inst_data(argv[argc - 1], ios::in);

  char **ptr = NULL;
  long i = 0;

  bool USE_MMU_PHYSICAL_MEMORY = true;
  init_indice(p_memory, 0, PHYSICAL_MEMORY_LENGTH);

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
  back.init(output_data_from_RISCV);

  cout << hex << p_memory[0x80400000 / 4] << endl;
  cout << hex << p_memory[0x80400004 / 4] << endl;
  cout << hex << p_memory[0x80400008 / 4] << endl;
  cout << hex << p_memory[0x8040000c / 4] << endl;
  // cout << "all lines in program = " << i << endl;

  bool number_PC_bit[WAY][BIT_WIDTH_PC] = {0};
  bool p_addr[WAY][32] = {0};
  bool MMU_ret_state = true;
  bool filelog = true;
  uint32_t number_PC = 0;
  ofstream outfile;
  bool stall;

  // main loop
  for (i = 0; i < 20; i++) { // 10398623
    if (i % 100 == 0) {
      cout << hex << i << ' ' << number_PC << endl;
    }
    time_i = i;

    if (log)
      cout << "****************************************************************"
              "**************************"
           << endl;

    // copy registers states, include: 32+21 (include satp)
    copy_indice(input_data_to_RISCV, 0, output_data_from_RISCV, 0,
                BIT_WIDTH_REG_STATES);
    copy_indice(input_data_to_RISCV, POS_IN_PRIVILEGE, output_data_from_RISCV,
                POS_OUT_PRIVILEGE, 2);
    init_indice(input_data_to_RISCV, POS_IN_INST,
                32 * 3 + 4); // inst, pc, load data, asy, page fault
    stall = *(output_data_from_RISCV + POS_OUT_STALL);

    if (i == 0) {

      // 复位pc 0x80000000
      for (int j = 0; j < WAY; j++) {
        cvt_number_to_bit_unsigned(number_PC_bit[j], 0x80000000 + j * 4, 32);
      }
      // 写misa 寄存器  32-IA 支持User和Supervisor
      cvt_number_to_bit_unsigned(input_data_to_RISCV +
                                     POS_CSR_MISA * sizeof(bool),
                                 0x40140101, 32); // 0x4014112d //0x40140101
                                                  //
      cvt_number_to_bit_unsigned(
          output_data_from_RISCV + POS_CSR_MISA * sizeof(bool), 0x40140101, 32);
      p_memory[0x10000004 / 4] = 0x00006000;

      // M-mode
      input_data_to_RISCV[POS_IN_PRIVILEGE] = true;
      input_data_to_RISCV[POS_IN_PRIVILEGE + 1] = true;
      output_data_from_RISCV[POS_OUT_PRIVILEGE] = true;
      output_data_from_RISCV[POS_OUT_PRIVILEGE + 1] = true;
    }

    // log
    /////if (filelog) {
    /////	if (i % 100000000 == 0) {
    /////		outfile.close();
    /////		outfile.open("/mnt/tracelog/jinpengwei/riscv_log/403/"+
    /// to_string(i/10000000) + ".log", ios::binary);
    /////	}
    /////	outfile.write((char*)(input_data_to_RISCV), 32*4); // general
    /// regs
    /////	outfile.write((char*)(input_data_to_RISCV+1696), 4); //
    /// instruction
    /////	outfile.write((char*)(input_data_to_RISCV+1728), 4); //
    /// bit_this_pc
    /////	outfile.write((char*)(input_data_to_RISCV+1796), 1); //
    /// priviledge
    /////	// for (int q = 0; q < 1024; q++) outfile <<
    /// input_data_to_RISCV[q]; // general regs
    /////	// for (int q = 1696; q < 1696+32; q++) outfile <<
    /// input_data_to_RISCV[q]; // instruction
    /////	// for (int q = 1728; q < 1728+32; q++) outfile <<
    /// input_data_to_RISCV[q]; // bit_this_pc
    /////	// for (int q = 1796; q < 1796+2; q++) outfile <<
    /// input_data_to_RISCV[q]; // priviledge
    /////}
    // log

    if (!stall) {
      for (int j = 0; j < WAY; j++) {
        number_PC = cvt_bit_to_number(number_PC_bit[j], BIT_WIDTH_PC);

        if (log)
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

        /*if (log)*/
        /*  cout << "Privilege:" << dec << privilege << endl;*/

        bool bit_inst[WAY][32] = {false};
        bool *satp = &input_data_to_RISCV[POS_CSR_SATP];
        bool *mstatus = &input_data_to_RISCV[POS_CSR_MSTATUS];
        bool *sstatus = &input_data_to_RISCV[POS_CSR_SSTATUS];

        if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&
            privilege != 3) { // mmu physical
          MMU_ret_state = va2pa(p_addr[j], satp, number_PC_bit[j], p_memory, 0,
                                mstatus, privilege, sstatus);
          input_data_to_RISCV[POS_PAGE_FAULT_INST] = !MMU_ret_state;
          if (MMU_ret_state) {
            uint32_t number_PC_p = cvt_bit_to_number_unsigned(p_addr[j], 32);
            if (log)
              cout << "当前物理地址为: " << hex << number_PC_p << endl;
            cvt_number_to_bit_unsigned(
                bit_inst[j], p_memory[uint32_t(number_PC_p / 4)],
                32); // TODO。不太对, 没有经过初始化，哪里有指令？
            if (log)
              cout << "当前指令为:" << hex
                   << p_memory[uint32_t(number_PC_p / 4)] << endl;
          } // page fault的话，到下面RISCV会处理
          else {
            if (log)
              cout << "PAGE FAULT INST" << endl;
            copy_indice(input_data_to_RISCV, POS_IN_PC + 32 * j,
                        number_PC_bit[j], 0, 32);
            RISCV(input_data_to_RISCV, output_data_from_RISCV);
            continue;
          }
        } else if (USE_MMU_PHYSICAL_MEMORY) {
          cvt_number_to_bit_unsigned(bit_inst[j], p_memory[number_PC / 4],
                                     32); // 取指令
          if (log)
            cout << "当前指令为:" << hex << p_memory[number_PC / 4] << endl;
        }

        *(input_data_to_RISCV + POS_IN_INST_VALID + j) = true;
        copy_indice(input_data_to_RISCV, POS_IN_INST + 32 * j, bit_inst[j], 0,
                    32);
        copy_indice(input_data_to_RISCV, POS_IN_PC + 32 * j, number_PC_bit[j],
                    0,
                    32); // 取PC
        init_indice(input_data_to_RISCV, POS_IN_LOAD_DATA,
                    32); // load data init
      }
    } else {
      for (int j = 0; j < WAY; j++)
        *(input_data_to_RISCV + POS_IN_INST_VALID + j) = false;
    }

    // TODO
    // asy and page fault
    /*bool rs_a_code[5]; // 12-16*/
    /*bool rs_b_code[5]; // 7-11*/
    /*copy_indice(rs_a_code, 0, bit_inst, 12, 5);*/
    /*copy_indice(rs_b_code, 0, bit_inst, 7, 5);*/
    /*uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);*/
    /*uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);*/
    /*copy_indice(input_data_to_RISCV, POS_IN_REG_A, output_data_from_RISCV,*/
    /*            32 * reg_a_index, 32);*/
    /*copy_indice(input_data_to_RISCV, POS_IN_REG_B, output_data_from_RISCV,*/
    /*            32 * reg_b_index, 32);*/
    /*copy_indice(number_op_code_bit, 0, bit_inst, 25, 7); // 取操作码*/
    /*uint32_t number_op_code = cvt_bit_to_number_unsigned(number_op_code_bit,
     * 7);*/
    // for (int i = 0;i < 32;i++) cout << bit_inst[i];

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    RISCV_32I(input_data_to_RISCV, output_data_from_RISCV);

    if (!stall) {
      for (int j = 0; j < WAY; j++) {
        number_PC += 4;
        cvt_number_to_bit_unsigned(number_PC_bit[j], number_PC, 32);
        /*copy_indice(input_data_to_RISCV, POS_IN_PC + 32 * j,
         * number_PC_bit[j],*/
        /*            0, 32);*/
      }
    } else if (*(output_data_from_RISCV + POS_OUT_STALL) ==
               false) { // 分支处理完成
                        //
      number_PC =
          cvt_bit_to_number_unsigned(output_data_from_RISCV + POS_OUT_PC, 32);
      for (int j = 0; j < WAY; j++) {
        cvt_number_to_bit_unsigned(number_PC_bit[j], number_PC, 32);
        copy_indice(input_data_to_RISCV, POS_IN_PC + 32 * j, number_PC_bit[j],
                    0, 32);
        number_PC += 4;
      }
      number_PC -= 4;
    }

    /*if (log) {*/
    /*  // print_regs(output_data_from_RISCV);*/
    /*  // print_csr_regs(output_data_from_RISCV);*/
    /*}*/
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /*  if (number_op_code == 3) { // load*/
    /*    bool bit_load_address[32];*/
    /*    bool bit_load_data[32];*/
    /*    uint32_t number_load_data = 0, number_load_address = 0;*/
    /**/
    /*    copy_indice(bit_load_address, 0, output_data_from_RISCV,*/
    /*                POS_OUT_LOAD_ADDR,*/
    /*                32); // 从output中读取address*/
    /**/
    /*    bool *satp = &output_data_from_RISCV[1600];*/
    /*    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) {*/
    /*      // mmu*/
    /*      MMU_ret_state = va2pa(p_addr, satp, bit_load_address, p_memory, 1,*/
    /*                            mstatus, privilege, sstatus);*/
    /*      input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;*/
    /**/
    /*      if (MMU_ret_state) { // no page fault*/
    /*        // for physical memory*/
    /*        number_load_address = cvt_bit_to_number_unsigned(p_addr, 32);*/
    /*        number_load_data = p_memory[uint32_t(number_load_address / 4)];*/
    /*        cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);*/
    /*      } else {*/
    /*        if (log)*/
    /*          cout << "PAGE FAULT LOAD" << endl;*/
    /*        RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*        continue;*/
    /*      }*/
    /*    } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*      number_load_address = cvt_bit_to_number_unsigned(bit_load_address,
     * 32);*/
    /*      number_load_data = p_memory[uint32_t(number_load_address / 4)];*/
    /*      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);*/
    /*    }*/
    /*    if (number_load_address == 0x1fd0e000) {*/
    /*      number_load_data = time_i;*/
    /*      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);*/
    /*    }*/
    /*    if (number_load_address == 0x1fd0e004) {*/
    /*      number_load_data = 0;*/
    /*      cvt_number_to_bit_unsigned(bit_load_data, number_load_data, 32);*/
    /*    }*/
    /*    bool bit_temp[32] = {0};*/
    /*    bool bit_funct3[3];*/
    /*    copy_indice(bit_funct3, 0, bit_inst, 17, 3); // get data*/
    /*    uint32_t number_funct3 = cvt_bit_to_number_unsigned(bit_funct3, 3);*/
    /*    switch (number_funct3) {*/
    /*    case 0: { // lb*/
    /*      uint32_t pos = uint32_t(1 ^ (*(bit_load_address + 31))) +*/
    /*                     2 * uint32_t(1 ^ (*(bit_load_address + 30)));*/
    /*      pos *= 8;*/
    /*      copy_indice(bit_temp, 24, bit_load_data, pos * sizeof(bool), 8);*/
    /*      break;*/
    /*    }*/
    /*    case 1: { // lh*/
    /*      uint32_t pos = (*(bit_load_address + 30)) ? 0 : 16;*/
    /*      copy_indice(bit_temp, 16, bit_load_data, pos * sizeof(bool), 16);*/
    /*      break;*/
    /*    }*/
    /*    case 2: { // lw*/
    /*      copy_indice(bit_temp, 0, bit_load_data, 0, 32);*/
    /*      break;*/
    /*    }*/
    /*    case 4: { // lbu*/
    /*      uint32_t pos = uint32_t(1 ^ (*(bit_load_address + 31))) +*/
    /*                     2 * uint32_t(1 ^ (*(bit_load_address + 30)));*/
    /*      pos *= 8;*/
    /*      copy_indice(bit_temp, 24, bit_load_data, pos * sizeof(bool), 8);*/
    /*      break;*/
    /*    }*/
    /*    case 5: { // lhu*/
    /*      uint32_t pos = (*(bit_load_address + 30)) ? 0 : 16;*/
    /*      copy_indice(bit_temp, 16, bit_load_data, pos * sizeof(bool), 16);*/
    /*      break;*/
    /*    }*/
    /*    }*/
    /*    copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA, bit_temp, 0,*/
    /*                32); // 将load的data存入input data， 之后放到寄存器里面*/
    /*    // load 之后再次执行指令，放到寄存器中*/
    /*    RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*    if (log)*/
    /*      cout << " ======= LOAD ===== " << "addr 0x" << hex*/
    /*           << number_load_address << " data: " << hex <<
     * number_load_data*/
    /*           << endl;*/
    /*  }*/
    /**/
    /*  if (number_op_code == 35) { // store*/
    /*    bool bit_store_address[32];*/
    /*    bool bit_store_data[32];*/
    /*    uint32_t number_store_address = 0, number_memory_temp = 0;*/
    /**/
    /*    copy_indice(bit_store_address, 0, output_data_from_RISCV,*/
    /*                POS_OUT_STORE_ADDR, 32); // get addr*/
    /**/
    /*    // store data ( physical or virtual)*/
    /*    bool *satp = &output_data_from_RISCV[POS_CSR_SATP];*/
    /*    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&*/
    /*        privilege != 3) { // physical*/
    /*      MMU_ret_state = va2pa(p_addr, satp, bit_store_address, p_memory,
     * 2,*/
    /*                            mstatus, privilege, sstatus);*/
    /*      input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;*/
    /*      if (!MMU_ret_state) { // can't load inst, give flag and let core*/
    /*                            // process*/
    /*        if (log)*/
    /*          cout << "PAGE FAULT STORE" << endl;*/
    /*        RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*        continue;*/
    /*      }*/
    /*      number_store_address = cvt_bit_to_number_unsigned(p_addr, 32);*/
    /*      number_memory_temp = p_memory[uint32_t(number_store_address / 4)];*/
    /*    } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*      number_store_address =*/
    /*          cvt_bit_to_number_unsigned(bit_store_address, 32);*/
    /*      number_memory_temp = p_memory[uint32_t(number_store_address / 4)];*/
    /*    }*/
    /**/
    /*    bool bit_memory_temp[32];*/
    /*    cvt_number_to_bit_unsigned(bit_memory_temp, number_memory_temp, 32);*/
    /*    copy_indice(bit_store_data, 0, output_data_from_RISCV,
     * POS_OUT_STORE_DATA,*/
    /*                32); // get data*/
    /*    uint32_t number_store_data =*/
    /*        cvt_bit_to_number_unsigned(bit_store_data, 32);*/
    /**/
    /*    bool bit_funct3[3];*/
    /*    copy_indice(bit_funct3, 0, bit_inst, 17, 3); // get data*/
    /*    uint32_t number_funct3 = cvt_bit_to_number_unsigned(bit_funct3, 3);*/
    /*    if ((number_funct3 == 1 && number_store_address % 2 == 1) ||*/
    /*        (number_funct3 == 2 && number_store_address % 4 != 0)) {*/
    /*      cout << "Store Memory Address Align Error!!!" << endl;*/
    /*      cout << "funct3 code: " << dec << number_funct3 << endl;*/
    /*      cout << "addr: " << hex << number_store_address << endl;*/
    /*      exit(-1);*/
    /*    }*/
    /*    switch (number_funct3) {*/
    /*    case 0: { // sb*/
    /*      uint32_t pos = uint32_t(1 ^ bit_store_address[31]) +*/
    /*                     2 * uint32_t(1 ^ bit_store_address[30]);*/
    /*      pos *= 8;*/
    /*      copy_indice(bit_memory_temp, pos, bit_store_data, 24, 8);*/
    /*      if (log)*/
    /*        cout << " ======= STOREB ===== " << "addr 0x" << hex*/
    /*             << number_store_address << " data: " << hex*/
    /*             << (number_store_data & 0x000000ff) << endl;*/
    /*      break;*/
    /*    }*/
    /*    case 1: { // sh*/
    /*      uint32_t pos = bit_store_address[30] ? 0 : 16;*/
    /*      copy_indice(bit_memory_temp, pos, bit_store_data, 16, 16);*/
    /*      if (log)*/
    /*        cout << " ======= STOREH ===== " << "addr 0x" << hex*/
    /*             << number_store_address << " data: " << hex*/
    /*             << (number_store_data & 0x0000ffff) << endl;*/
    /*      break;*/
    /*    }*/
    /*    case 2: { // sw*/
    /*      copy_indice(bit_memory_temp, 0, bit_store_data, 0, 32);*/
    /*      if (log)*/
    /*        cout << " ======= STOREW ===== " << "addr 0x" << hex*/
    /*             << number_store_address << " data: " << hex <<
     * number_store_data*/
    /*             << endl;*/
    /*      break;*/
    /*    }*/
    /*    default:*/
    /*      cout << "Error!!!" << endl;*/
    /*    }*/
    /**/
    /*    // save data*/
    /*    if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3) //
     * physical*/
    /*      p_memory[uint32_t(number_store_address / 4)] =*/
    /*          cvt_bit_to_number_unsigned(bit_memory_temp, 32);*/
    /*    else if (USE_MMU_PHYSICAL_MEMORY)*/
    /*      p_memory[uint32_t(number_store_address / 4)] =*/
    /*          cvt_bit_to_number_unsigned(bit_memory_temp, 32);*/
    /**/
    /*    // uart显示   physical TODO*/
    /*    if (number_store_address == 0x10000000) {*/
    /*      char temp;*/
    /*      temp = number_store_data & 0x000000ff;*/
    /*      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] & 0xffffff00;*/
    /*      cout << temp;*/
    /*         printf("%d ", temp);*/
    /**/
    /*      if (temp == 0x3F) {*/
    /*        cerr << hex << time_i << endl;*/
    /*        // log = true;*/
    /*      }*/
    /*    }*/
    /*    if (number_store_address == 0x10000001 &&*/
    /*        (number_store_data & 0x000000ff) == 7) {*/
    /*      // cerr << "UART enabled!" << endl;*/
    /*      output_data_from_RISCV[POS_CSR_MIP + 31 - 9] = 1; // mip*/
    /*      output_data_from_RISCV[POS_CSR_SIP + 31 - 9] = 1; // sip*/
    /*      p_memory[0xc201004 / 4] = 0xa;*/
    /*      // log = true;*/
    /*      p_memory[0x10000000 / 4] = p_memory[0x10000000 / 4] &
       0xfff0ffff;*/
    /*    }*/
    /*    if (number_store_address == 0x10000001 &&*/
    /*        (number_store_data & 0x000000ff) == 5) {*/
    /*      // cerr << "UART disabled2!" << endl;*/
    /*      //  p_memory[0xc201004/4] = 0x0;*/
    /*      p_memory[0x10000000 / 4] =*/
    /*          p_memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;*/
    /*    }*/
    /*    if (number_store_address == 0xc201004 &&*/
    /*        (number_store_data & 0x000000ff) == 0xa) {*/
    /*      // cerr << "UART disabled1!" << endl;*/
    /*      p_memory[0xc201004 / 4] = 0x0;*/
    /*      output_data_from_RISCV[POS_CSR_MIP + 31 - 9] = 0; // mip*/
    /*      output_data_from_RISCV[POS_CSR_SIP + 31 - 9] = 0; // sip*/
    /*    }*/
    /*  }*/
    /**/
    /*  if (number_op_code == 47) {*/
    /*    bool bit_funct5[5];*/
    /*    copy_indice(bit_funct5, 0, bit_inst, 0, 5);*/
    /*    uint32_t number_funct5_unsigned =*/
    /*        cvt_bit_to_number_unsigned(bit_funct5, 5);*/
    /*    switch (number_funct5_unsigned) {*/
    /*    case 2: { // lr.w*/
    /*      bool bit_load_address[32];*/
    /*      bool bit_load_data[32];*/
    /*      uint32_t number_load_address = 0, number_load_data = 0;*/
    /**/
    /*      copy_indice(bit_load_address, 0, output_data_from_RISCV,*/
    /*                  POS_OUT_LOAD_ADDR, 32); // 从output中读取address*/
    /**/
    /*      bool *satp = &output_data_from_RISCV[POS_CSR_SATP];*/
    /*      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3)
       {*/
    /*        // mmu*/
    /*        MMU_ret_state = va2pa(p_addr, satp, bit_load_address,
       p_memory, 1,*/
    /*                              mstatus, privilege, sstatus);*/
    /*        input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;*/
    /**/
    /*        if (MMU_ret_state) { // no page fault*/
    /*          // for physical memory*/
    /*          number_load_address = cvt_bit_to_number_unsigned(p_addr,
       32);*/
    /*          number_load_data = p_memory[uint32_t(number_load_address /
       4)];*/
    /*          cvt_number_to_bit_unsigned(bit_load_data, number_load_data,
       32);*/
    /*        } else {*/
    /*          if (log)*/
    /*            cout << "PAGE FAULT LOAD" << endl;*/
    /*          RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*          continue;*/
    /*        }*/
    /*      } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*        number_load_address =*/
    /*            cvt_bit_to_number_unsigned(bit_load_address, 32);*/
    /*        number_load_data = p_memory[uint32_t(number_load_address /
       4)];*/
    /*        cvt_number_to_bit_unsigned(bit_load_data, number_load_data,
       32);*/
    /*      }*/
    /*      // load 之后再次执行指令，放到寄存器中*/
    /*      copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA,
       bit_load_data, 0,*/
    /*                  32); // 将load的data存入input data，
       之后放到寄存器里面*/
    /*      RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*      if (log)*/
    /*        cout << " ======= LR ===== " << "addr 0x" << hex*/
    /*             << number_load_address << " data: " << hex <<
       number_load_data*/
    /*             << endl;*/
    /*      break;*/
    /*    }*/
    /*    case 3: { // sc.w*/
    /*      bool bit_store_address[32];*/
    /*      bool bit_store_data[32];*/
    /*      uint32_t number_store_address = 0, number_store_data = 0;*/
    /**/
    /*      copy_indice(bit_store_address, 0, output_data_from_RISCV,*/
    /*                  POS_OUT_STORE_ADDR, 32); // get addr*/
    /**/
    /*      // store data ( physical or virtual)*/
    /*      bool *satp = &output_data_from_RISCV[POS_CSR_SATP];*/
    /*      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&*/
    /*          privilege != 3) { // physical*/
    /*        MMU_ret_state = va2pa(p_addr, satp, bit_store_address,
       p_memory, 2,*/
    /*                              mstatus, privilege, sstatus);*/
    /*        input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;*/
    /*        if (!MMU_ret_state) { // can't load inst, give flag and let
       core*/
    /*                              // process*/
    /*          if (log)*/
    /*            cout << "PAGE FAULT STORE" << endl;*/
    /*          RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*          continue;*/
    /*        }*/
    /*        number_store_address = cvt_bit_to_number_unsigned(p_addr,
       32);*/
    /*        copy_indice(bit_store_data, 0, output_data_from_RISCV,*/
    /*                    POS_OUT_STORE_DATA, 32); // get data*/
    /*        uint32_t number_store_data =*/
    /*            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to
       memory*/
    /*        p_memory[uint32_t(number_store_address / 4)] =
       number_store_data;*/
    /*      } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*        number_store_address =*/
    /*            cvt_bit_to_number_unsigned(bit_store_address, 32);*/
    /*        copy_indice(bit_store_data, 0, output_data_from_RISCV,*/
    /*                    POS_OUT_STORE_DATA, 32); // get data*/
    /*        uint32_t number_store_data =*/
    /*            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to
       memory*/
    /*        p_memory[uint32_t(number_store_address / 4)] =
       number_store_data;*/
    /*      }*/
    /**/
    /*      if (number_store_address % 4 != 0) {*/
    /*        cout << "Store Memory Address Align Error!!!" << endl;*/
    /*        cout << "sc.w" << endl;*/
    /*        cout << "addr: " << hex << number_store_address << endl;*/
    /*        exit(-1);*/
    /*      }*/
    /*      if (log)*/
    /*        cout << " ======= SC ===== " << "addr 0x" << hex*/
    /*             << number_store_address << " data: " << hex <<
       number_store_data*/
    /*             << endl;*/
    /*      break;*/
    /*    }*/
    /*    default: { // amoswap.w, amoadd.w, amoxor.w, amoand.w, amoor.w,*/
    /*               // amomin.w, amomax.w, amominu.w, amomaxu.w*/
    /*      // first load data*/
    /*      bool bit_load_address[32];*/
    /*      bool bit_load_data[32];*/
    /*      uint32_t number_load_address = 0, number_load_data = 0;*/
    /**/
    /*      copy_indice(bit_load_address, 0, output_data_from_RISCV,*/
    /*                  POS_OUT_LOAD_ADDR, 32); // 从output中读取address*/
    /**/
    /*      bool *satp = &output_data_from_RISCV[1600];*/
    /*      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 && privilege != 3)
       {*/
    /*        // mmu*/
    /*        MMU_ret_state = va2pa(p_addr, satp, bit_load_address,
       p_memory, 1,*/
    /*                              mstatus, privilege, sstatus);*/
    /*        input_data_to_RISCV[POS_PAGE_FAULT_LOAD] = !MMU_ret_state;*/
    /**/
    /*        if (MMU_ret_state) { // page fault*/
    /*          // for physical memory*/
    /*          number_load_address = cvt_bit_to_number_unsigned(p_addr,
       32);*/
    /*          number_load_data = p_memory[uint32_t(number_load_address /
       4)];*/
    /*          cvt_number_to_bit_unsigned(bit_load_data, number_load_data,
       32);*/
    /*          copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA,
       bit_load_data, 0,*/
    /*                      32); // 将load的data存入input data，
       之后放到寄存器里面*/
    /*        } else {*/
    /*          if (log)*/
    /*            cout << "PAGE FAULT LOAD" << endl;*/
    /*          RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*          continue;*/
    /*        }*/
    /*      } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*        number_load_address =*/
    /*            cvt_bit_to_number_unsigned(bit_load_address, 32);*/
    /*        number_load_data = p_memory[uint32_t(number_load_address /
       4)];*/
    /*        cvt_number_to_bit_unsigned(bit_load_data, number_load_data,
       32);*/
    /*        copy_indice(input_data_to_RISCV, POS_IN_LOAD_DATA,
       bit_load_data, 0,*/
    /*                    32); // 将load的data存入input data，
       之后放到寄存器里面*/
    /*      }*/
    /*      // load 之后再次执行指令，放到寄存器中*/
    /*      RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*      if (log)*/
    /*        cout << " ======= LOAD ===== " << "addr 0x" << hex*/
    /*             << number_load_address << " data: " << hex <<
       number_load_data*/
    /*             << endl;*/
    /*      // then store to the other memory*/
    /*      bool bit_store_address[32];*/
    /*      bool bit_store_data[32];*/
    /*      uint32_t number_store_address = 0, number_store_data = 0;*/
    /*      copy_indice(bit_store_address, 0, output_data_from_RISCV,*/
    /*                  POS_OUT_STORE_ADDR, 32); // get addr*/
    /**/
    /*      // store data ( physical or virtual)*/
    /*      // bool *satp =  &output_data_from_RISCV[1600];*/
    /*      satp = &output_data_from_RISCV[POS_CSR_SATP];*/
    /*      if (USE_MMU_PHYSICAL_MEMORY && satp[0] != 0 &&*/
    /*          privilege != 3) { // physical*/
    /*        MMU_ret_state = va2pa(p_addr, satp, bit_store_address,
       p_memory, 2,*/
    /*                              mstatus, privilege, sstatus);*/
    /*        input_data_to_RISCV[POS_PAGE_FAULT_STORE] = !MMU_ret_state;*/
    /*        if (!MMU_ret_state) { // can't load inst, give flag and let
       core*/
    /*                              // process*/
    /*          if (log)*/
    /*            cout << "PAGE FAULT STORE" << endl;*/
    /*          RISCV(input_data_to_RISCV, output_data_from_RISCV);*/
    /*          continue;*/
    /*        }*/
    /*        number_store_address = cvt_bit_to_number_unsigned(p_addr,
       32);*/
    /*        copy_indice(bit_store_data, 0, output_data_from_RISCV,*/
    /*                    POS_OUT_STORE_DATA, 32); // get data*/
    /*        uint32_t number_store_data =*/
    /*            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to
       memory*/
    /*        p_memory[uint32_t(number_store_address / 4)] =
       number_store_data;*/
    /*      } else if (USE_MMU_PHYSICAL_MEMORY) {*/
    /*        number_store_address =*/
    /*            cvt_bit_to_number_unsigned(bit_store_address, 32);*/
    /*        copy_indice(bit_store_data, 0, output_data_from_RISCV,*/
    /*                    POS_OUT_STORE_DATA, 32); // get data*/
    /*        uint32_t number_store_data =*/
    /*            cvt_bit_to_number_unsigned(bit_store_data, 32); // save to
       memory*/
    /*        p_memory[uint32_t(number_store_address / 4)] =
       number_store_data;*/
    /*      }*/
    /**/
    /*      if (number_store_address % 4 != 0) {*/
    /*        cout << "Store Memory Address Align Error!!!" << endl;*/
    /*        cout << "amo" << endl;*/
    /*        cout << "addr: " << hex << number_store_address << endl;*/
    /*        exit(-1);*/
    /*      }*/
    /*      if (log)*/
    /*        cout << " ======= SC ===== " << "addr 0x" << hex*/
    /*             << number_store_address << " data: " << hex <<
       number_store_data*/
    /*             << endl;*/
    /*      break;*/
    /*    }*/
    /*    }*/
  }
  /*  // log*/
  /*  if (filelog) {*/
  /*    outfile.write((char *)(input_data_to_RISCV + POS_IN_LOAD_DATA),*/
  /*                  4); // load_data*/
  /*    outfile.write((char *)(output_data_from_RISCV +
     POS_OUT_LOAD_ADDR),*/
  /*                  4); // load_address*/
  /*    outfile.write((char *)(output_data_from_RISCV +
     POS_OUT_STORE_DATA),*/
  /*                  4); // store_data*/
  /*    outfile.write((char *)(output_data_from_RISCV +
     POS_OUT_STORE_ADDR),*/
  /*                  4); // store_address*/
  /*    // for (int q = 1760; q < 1760+32; q++) outfile <<*/
  /*    // input_data_to_RISCV[q];*/
  /*    // // load_data for (int q = 1728; q < 1728+32; q++) outfile <<*/
  /*    // output_data_from_RISCV[q]; // load_address for (int q = 1760; q
     <*/
  /*    // 1760+32; q++) outfile << output_data_from_RISCV[q]; //
     store_data for*/
  /*    // (int q = 1792; q < 1792+2; q++) outfile <<
     output_data_from_RISCV[q];*/
  /*    // // store_address outfile << endl;*/
  /*  }*/
  /*  // log*/
  /*}*/

  delete[] p_memory;

#ifdef CONFIG_DIFFTEST
  extern void *handle;
  dlclose(handle);
#endif

  return 0;
}

uint32_t load_data(Inst_op op, bool *offset) {
  uint32_t address = cvt_bit_to_number_unsigned(
      output_data_from_RISCV + POS_OUT_LOAD_ADDR, 32);
  uint32_t data = p_memory[address / 8];

  if (offset[1])
    data = data >> 8;

  if (offset[0])
    data = data >> 16;

  switch (op) {
  case LH:
    data = data & 0x0000FFFF;
    if (data & 0x00008000)
      data = data | 0xFFFF0000;
    break;
  case LB:
    data = data & 0x0000FFFF;
    if (data & 0x00000080)
      data = data | 0xFFFFFF00;
    break;
  case LHU:
    data = data & 0x0000FFFF;
    break;
  case LBU:
    data = data & 0x0000FFFF;
    break;
  default:
    break;
  }

  return data;
}

void store_data(Inst_op op) {
  uint32_t address = cvt_bit_to_number_unsigned(
      output_data_from_RISCV + POS_OUT_STORE_ADDR, 32);
  uint32_t data = cvt_bit_to_number_unsigned(
      output_data_from_RISCV + POS_OUT_STORE_DATA, 32);

  uint32_t mask;
  if (op == SH)
    mask = 0xFFFF;
  else if (op == SB)
    mask = 0xFF;
  else
    mask = 0xFFFFFFFF;

  if (address & 0x1) {
    data = data << 8;
    mask = mask << 8;
  }

  if (address & 0x2) {
    data = data << 16;
    mask = mask << 16;
  }

  data = (data & mask) | (p_memory[address / 8] & (~mask));

  p_memory[address / 8] = data;
}
