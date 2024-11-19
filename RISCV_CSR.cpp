#include "RISCV.h"
#include "cvt.h"
#include <ctime>
// #include"CheckInterrupt.h"

void RISCV_CSR(bool input_data[BIT_WIDTH], bool *output_data) {
  // get input data
  bool general_regs[1024];
  bool csr[1 + 0xfff][32] = {0};
  bool instruction[32];

  bool bit_this_pc[32];
  bool bit_load_data[32];
  bool this_priviledge[2];
  copy_indice(general_regs, 0, input_data, 0, 1024);
  copy_indice(csr[number_mtvec], 0, input_data, 1024, 32);
  copy_indice(csr[number_mepc], 0, input_data, 1056, 32);
  copy_indice(csr[number_mcause], 0, input_data, 1088, 32);
  copy_indice(csr[number_mie], 0, input_data, 1120, 32);
  copy_indice(csr[number_mip], 0, input_data, 1152, 32);
  copy_indice(csr[number_mtval], 0, input_data, 1184, 32);
  copy_indice(csr[number_mscratch], 0, input_data, 1216, 32);
  copy_indice(csr[number_mstatus], 0, input_data, 1248, 32);
  copy_indice(csr[number_mideleg], 0, input_data, 1280, 32);
  copy_indice(csr[number_medeleg], 0, input_data, 1312, 32);
  copy_indice(csr[number_sepc], 0, input_data, 1344, 32);
  copy_indice(csr[number_stvec], 0, input_data, 1376, 32);
  copy_indice(csr[number_scause], 0, input_data, 1408, 32);
  copy_indice(csr[number_sscratch], 0, input_data, 1440, 32);
  copy_indice(csr[number_stval], 0, input_data, 1472, 32);
  copy_indice(csr[number_sstatus], 0, input_data, 1504, 32);
  copy_indice(csr[number_sie], 0, input_data, 1536, 32);
  copy_indice(csr[number_sip], 0, input_data, 1568, 32);
  copy_indice(csr[number_satp], 0, input_data, 1600, 32);
  copy_indice(csr[number_mhartid], 0, input_data, 1632, 32);
  copy_indice(csr[number_misa], 0, input_data, 1664, 32);
  copy_indice(instruction, 0, input_data, 1696, 32);
  copy_indice(bit_this_pc, 0, input_data, 1728, 32);
  copy_indice(bit_load_data, 0, input_data, 1760, 32);
  bool asy = input_data[1792];
  bool page_fault_inst = input_data[1793];
  bool page_fault_load = input_data[1794];
  bool page_fault_store = input_data[1795];
  copy_indice(this_priviledge, 0, input_data, 1796, 2);

  // initialize output data
  bool next_general_regs[1024];
  bool next_csr[1 + 0xfff][32] = {0};
  bool bit_next_pc[32];
  bool bit_load_address[32];
  bool bit_store_data[32];
  bool bit_store_address[32];
  bool bit_result_tensor[32];
  bool bit_pc_4[32];
  bool next_priviledge[2];
  copy_indice(next_general_regs, 0, general_regs, 0, 1024);
  copy_indice(next_csr[number_mtvec], 0, csr[number_mtvec], 0, 32);
  copy_indice(next_csr[number_mepc], 0, csr[number_mepc], 0, 32);
  copy_indice(next_csr[number_mcause], 0, csr[number_mcause], 0, 32);
  copy_indice(next_csr[number_mie], 0, csr[number_mie], 0, 32);
  copy_indice(next_csr[number_mip], 0, csr[number_mip], 0, 32);
  copy_indice(next_csr[number_mtval], 0, csr[number_mtval], 0, 32);
  copy_indice(next_csr[number_mscratch], 0, csr[number_mscratch], 0, 32);
  copy_indice(next_csr[number_mstatus], 0, csr[number_mstatus], 0, 32);
  copy_indice(next_csr[number_mideleg], 0, csr[number_mideleg], 0, 32);
  copy_indice(next_csr[number_medeleg], 0, csr[number_medeleg], 0, 32);
  copy_indice(next_csr[number_sepc], 0, csr[number_sepc], 0, 32);
  copy_indice(next_csr[number_stvec], 0, csr[number_stvec], 0, 32);
  copy_indice(next_csr[number_scause], 0, csr[number_scause], 0, 32);
  copy_indice(next_csr[number_sscratch], 0, csr[number_sscratch], 0, 32);
  copy_indice(next_csr[number_stval], 0, csr[number_stval], 0, 32);
  copy_indice(next_csr[number_sstatus], 0, csr[number_sstatus], 0, 32);
  copy_indice(next_csr[number_sie], 0, csr[number_sie], 0, 32);
  copy_indice(next_csr[number_sip], 0, csr[number_sip], 0, 32);
  copy_indice(next_csr[number_satp], 0, csr[number_satp], 0, 32);
  copy_indice(next_csr[number_mhartid], 0, csr[number_mhartid], 0, 32);
  copy_indice(next_csr[number_misa], 0, csr[number_misa], 0, 32);
  init_indice(bit_next_pc, 0, 32);
  init_indice(bit_load_address, 0, 32);
  init_indice(bit_store_data, 0, 32);
  init_indice(bit_store_address, 0, 32);
  init_indice(bit_result_tensor, 0, 32);
  init_indice(bit_pc_4, 0, 32);
  copy_indice(next_priviledge, 0, this_priviledge, 0, 2);

  // pc + 4
  uint32_t number_pc_unsigned = cvt_bit_to_number_unsigned(bit_this_pc, 32);
  uint32_t number_pc_4 = number_pc_unsigned + 4;
  cvt_number_to_bit_unsigned(bit_pc_4, number_pc_4, 32);
  copy_indice(bit_next_pc, 0, bit_pc_4, 0, 32);

  // split instruction
  bool bit_op_code[7];   // 25-31
  bool rd_code[5];       // 20-24
  bool rs_a_code[5];     // 12-16
  bool rs_b_code[5];     // 7-11
  bool bit_csr_code[12]; // 0-11
  copy_indice(bit_op_code, 0, instruction, 25, 7);
  copy_indice(rd_code, 0, instruction, 20, 5);
  copy_indice(rs_a_code, 0, instruction, 12, 5);
  copy_indice(rs_b_code, 0, instruction, 7, 5);
  copy_indice(bit_csr_code, 0, instruction, 0, 12);
  uint32_t number_csr_code_unsigned =
      cvt_bit_to_number_unsigned(bit_csr_code, 12);
  bool bit_funct3[3];
  copy_indice(bit_funct3, 0, instruction, 17, 3);
  uint32_t number_funct3_unsigned = cvt_bit_to_number_unsigned(bit_funct3, 3);

  // 准备寄存器
  uint32_t reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
  uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);
  bool bit_reg_data_a[32];
  bool bit_reg_data_b[32];
  copy_indice(bit_reg_data_a, 0, general_regs, 32 * reg_a_index, 32);
  copy_indice(bit_reg_data_b, 0, general_regs, 32 * reg_b_index, 32);

  bool bit_funct5[5];
  copy_indice(bit_funct5, 0, instruction, 0, 5);
  uint32_t number_funct5_unsigned = cvt_bit_to_number_unsigned(bit_funct5, 5);

  uint32_t number_this_priviledge =
      cvt_bit_to_number_unsigned(this_priviledge, 2);
  uint32_t number_op_code = cvt_bit_to_number_unsigned(bit_op_code, 7);
  bool M_software_interrupt =
      csr[number_mip][31 - 3] && csr[number_mie][31 - 3] &&
      (csr[number_mideleg][31 - 3] == 0) &&
      (number_this_priviledge < 3 ||
       csr[number_mstatus][31 - 3] == 1); // M_software_interrupt
  bool M_timer_interrupt =
      csr[number_mip][31 - 7] && csr[number_mie][31 - 7] &&
      (csr[number_mideleg][31 - 7] == 0) &&
      (number_this_priviledge < 3 ||
       csr[number_mstatus][31 - 3] == 1); // M_timer_interrupt
  bool M_external_interrupt =
      csr[number_mip][31 - 11] && csr[number_mie][31 - 11] &&
      (csr[number_mideleg][31 - 11] == 0) &&
      (number_this_priviledge < 3 || csr[number_mstatus][31 - 3] == 1);
  // S_interrupt条件：1、原本到M态的中断被给到了S态处理（此时global
  // interrupt被视为S态的，即特权级小于S或等于S且SIE为1）。2、本身是到S态的中断，需要privilege小于S或等于S且SIE为1。
  bool S_software_interrupt =
      (csr[number_mip][31 - 3] && csr[number_mie][31 - 3] &&
       csr[number_mideleg][31 - 3] == 1 && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr[number_mstatus][31 - 1] == 1)) ||
      (csr[number_mip][31 - 1] && csr[number_mie][31 - 1] &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr[number_mstatus][31 - 1] == 1));
  bool S_timer_interrupt =
      (csr[number_mip][31 - 7] && csr[number_mie][31 - 7] &&
       csr[number_mideleg][31 - 7] == 1 && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr[number_mstatus][31 - 1] == 1)) ||
      (csr[number_mip][31 - 5] && csr[number_mie][31 - 5] &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr[number_mstatus][31 - 1] == 1));
  // bool S_external_interrupt = (csr[number_mip][31-11] &&
  // csr[number_mie][31-11] && csr[number_mideleg][31-11]==1 &&
  // number_this_priviledge!=3 && (number_this_priviledge<1 ||
  // csr[number_mstatus][31-1]==1)) || (csr[number_mip][31-9] &&
  // csr[number_mie][31-9] && number_this_priviledge!=3 &&
  // (number_this_priviledge<1 || csr[number_mstatus][31-1]==1));
  bool S_external_interrupt =
      (csr[number_mip][31 - 11] && csr[number_mie][31 - 11] &&
       csr[number_mideleg][31 - 11] == 1 && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr[number_mstatus][31 - 1] == 1)) ||
      (csr[number_mip][31 - 9] && csr[number_mie][31 - 9] &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 ||
        csr[number_mstatus][31 - 1] ==
            1)); // || (csr[number_sstatus][31-1]==true && csr[number_sie][31-9]
                 // == true && csr[number_sip][31-9] == true);
  bool ecall = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
                cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0);
  bool illegal_exception = (number_csr_code_unsigned == number_timeh ||
                            number_csr_code_unsigned == number_time);
  bool MRET = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
               (cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0x302));
  bool SRET = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
               (cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0x102));
  bool MTrap =
      (M_software_interrupt) || (M_timer_interrupt) || (M_external_interrupt) ||
      (ecall && (number_this_priviledge == 0) &&
       !csr[number_medeleg][31 - 8]) ||
      (ecall && (number_this_priviledge == 1) &&
       !csr[number_medeleg][31 - 9]) ||
      (ecall && (number_this_priviledge == 3)) // MTrap下的ecall一定在MTrap处理
      || (page_fault_inst && !csr[number_medeleg][31 - 12]) ||
      (page_fault_load && !csr[number_medeleg][31 - 13]) ||
      (page_fault_store && !csr[number_medeleg][31 - 15]) || illegal_exception;
  bool STrap =
      S_software_interrupt || S_timer_interrupt || S_external_interrupt ||
      (ecall && (number_this_priviledge == 0) && csr[number_medeleg][31 - 8]) ||
      (ecall && (number_this_priviledge == 1) && csr[number_medeleg][31 - 9])
      //||	(ecall 	&& (number_this_priviledge==3) &&
      // csr[number_medeleg][31-11]) //M态ECALL无论如何不会进入STrap
      || (page_fault_inst && csr[number_medeleg][31 - 12]) ||
      (page_fault_load && csr[number_medeleg][31 - 13]) ||
      (page_fault_store && csr[number_medeleg][31 - 15]);
  bool info[32] = {0};
  if (page_fault_inst || page_fault_load || page_fault_store) {
    if (page_fault_inst)
      copy_indice(info, 0, bit_this_pc, 0, 32);
    else if (number_op_code == number_11_opcode_lrw)
      copy_indice(info, 0, bit_reg_data_a, 0, 32);
    else if (page_fault_load) {
      bool bit_temp[32];
      bool bit_immi_i_type[12]; // I-type
      copy_indice(bit_immi_i_type, 0, instruction, 0, 12);
      sign_extend(bit_temp, 32, bit_immi_i_type, 12);
      add_bit_list(info, bit_reg_data_a, bit_temp, 32);
    } else if (page_fault_store) {
      bool bit_temp[32];
      bool bit_immi_s_type[12]; // S-type
      copy_indice(bit_immi_s_type, 0, instruction, 0, 7);
      copy_indice(bit_immi_s_type, 7, instruction, 20, 5);
      sign_extend(bit_temp, 32, bit_immi_s_type, 12);
      add_bit_list(info, bit_reg_data_a, bit_temp, 32);
    }
  }

  if (MTrap) {
    // cout << "MTrap begin" << endl;
    // cout << M_software_interrupt << endl;
    // cout << M_timer_interrupt << endl;
    // cout << M_external_interrupt << endl;
    // cout << (ecall 	&& (number_this_priviledge==0) &&
    // !csr[number_medeleg][31- 8]) << endl; cout << (ecall 	&&
    // (number_this_priviledge==1) && !csr[number_medeleg][31- 9]) << endl; cout
    // << (ecall 	&& (number_this_priviledge==3)) << endl; cout <<
    // (page_fault_inst && !csr[number_medeleg][31-12]) << endl; cout <<
    // (page_fault_load && !csr[number_medeleg][31-13]) << endl; cout <<
    // (page_fault_store && !csr[number_medeleg][31-15]) << endl;
    // print_csr_regs(input_data);
    // cout << "end" << endl;
    copy_indice(next_csr[number_mepc], 0, bit_this_pc, 0, 32);
    // next_mcause = interruptType;
    bool bit_interruptType[32] = {0};
    bit_interruptType[31 - 31] =
        M_software_interrupt || M_timer_interrupt || M_external_interrupt;
    uint32_t number_interruptType =
        cvt_bit_to_number_unsigned(bit_interruptType, 32);
    number_interruptType +=
        M_software_interrupt ? 3
        : M_timer_interrupt  ? 7
        : (M_external_interrupt || (ecall && (number_this_priviledge == 3) &&
                                    !csr[number_medeleg][31 - 11]))
            ? 11
        : (page_fault_inst && !csr[number_medeleg][31 - 12]) ? 12
        : illegal_exception                                  ? 2
        : (ecall && (number_this_priviledge == 0) &&
           !csr[number_medeleg][31 - 8])
            ? 8
        : (ecall && (number_this_priviledge == 1) &&
           !csr[number_medeleg][31 - 9])
            ? 9
        : (page_fault_load && !csr[number_medeleg][31 - 13]) ? 13
        : (page_fault_store && !csr[number_medeleg][31 - 15])
            ? 15
            : 0; // 给后31位赋值
    cvt_number_to_bit_unsigned(bit_interruptType, number_interruptType, 32);
    copy_indice(next_csr[number_mcause], 0, bit_interruptType, 0, 32);
    if (csr[number_mtvec][31] == 1 && csr[number_mtvec][30] == 0 &&
        bit_interruptType[0] == 1) {
      init_indice(bit_next_pc, 0, 32);
      uint32_t temp = cvt_bit_to_number_unsigned(csr[number_mtvec], 32);
      temp &= 0xfffffffc;
      temp += 4 * (number_interruptType & 0x7fffffff);
      cvt_number_to_bit_unsigned(bit_next_pc, temp, 32);
    } else {
      init_indice(bit_next_pc, 0, 32);
      copy_indice(bit_next_pc, 0, csr[number_mtvec], 0, 30);
    }
    if (page_fault_inst || page_fault_load ||
        page_fault_store) { // next_mtval = ;//要看具体实现
      copy_indice(next_csr[number_mtval], 0, info, 0, 32);
    } else if (illegal_exception) {
      copy_indice(next_csr[number_mtval], 0, instruction, 0, 32);
    } else {
      init_indice(next_csr[number_mtval], 0, 32);
    }

    copy_indice(next_csr[number_mstatus], 31 - 12, this_priviledge, 0,
                2); // next_mstatus.MPP = this_priviledge
    next_csr[number_mstatus][31 - 7] =
        csr[number_mstatus][31 - 3];      // next_mstatus.MPIE = mstatus.MIE;
    next_csr[number_mstatus][31 - 3] = 0; // next_mstatus.MIE = 0;
    copy_indice(next_csr[number_sstatus], 31 - 12, this_priviledge, 0,
                2); // next_mstatus.MPP = this_priviledge
    next_csr[number_sstatus][31 - 7] =
        csr[number_sstatus][31 - 3];      // next_mstatus.MPIE = mstatus.MIE;
    next_csr[number_sstatus][31 - 3] = 0; // next_mstatus.MIE = 0;
    cvt_number_to_bit_unsigned(next_priviledge, 3, 2);
  } else if (STrap) {
    // cout << "STrap begin" << endl;
    // cout << S_software_interrupt << endl;
    // cout << S_timer_interrupt << endl;
    // cout << S_external_interrupt << endl;
    // cout << (ecall 	&& (number_this_priviledge==0) &&
    // csr[number_medeleg][31- 8]) << endl; cout << (ecall 	&&
    // (number_this_priviledge==1) && csr[number_medeleg][31- 9]) << endl; cout
    // << (page_fault_inst && csr[number_medeleg][31-12])	 << endl; cout
    // << (page_fault_load && csr[number_medeleg][31-13])	 << endl; cout
    // << (page_fault_store && csr[number_medeleg][31-15]) << endl;
    // print_csr_regs(input_data);
    // cout << "end" << endl;
    copy_indice(next_csr[number_sepc], 0, bit_this_pc, 0,
                32); // next_sepc = this_pc;
    // next_scause = interruptType;
    bool bit_interruptType[32] = {0};
    bit_interruptType[31 - 31] =
        S_software_interrupt || S_timer_interrupt || S_external_interrupt;
    uint32_t number_interruptType =
        cvt_bit_to_number_unsigned(bit_interruptType, 32);
    number_interruptType +=
        (S_external_interrupt || (ecall && (number_this_priviledge == 1) &&
                                  csr[number_medeleg][31 - 9]))
            ? 9
        : S_timer_interrupt ? 5
        : (ecall && (number_this_priviledge == 0) &&
           csr[number_medeleg][31 - 8])
            ? 8
        : S_software_interrupt                              ? 1
        : (page_fault_inst && csr[number_medeleg][31 - 12]) ? 12
        : (page_fault_load && csr[number_medeleg][31 - 13]) ? 13
        : (page_fault_store && csr[number_medeleg][31 - 15])
            ? 15
            : 0; // 给后31位赋值
    // number_interruptType += S_software_interrupt ? 3
    // 						: S_timer_interrupt ? 7
    // 						: (ecall 	&&
    // (number_this_priviledge==0) && csr[number_medeleg][31- 8]) ? 8
    // : (ecall
    // && (number_this_priviledge==1) && csr[number_medeleg][31- 9]) ? 9
    // : (S_external_interrupt ||(ecall 	&& (number_this_priviledge==3)
    // && csr[number_medeleg][31-11]))  ? 11
    // : (page_fault_inst && csr[number_medeleg][31-12]) ? 12
    // : (page_fault_load && csr[number_medeleg][31-13]) ? 13
    // : (page_fault_store && csr[number_medeleg][31-15]) ? 15
    // : 0 ;//给后31位赋值
    cvt_number_to_bit_unsigned(bit_interruptType, number_interruptType, 32);
    copy_indice(next_csr[number_scause], 0, bit_interruptType, 0, 32);
    if (csr[number_stvec][31] == 1 && csr[number_stvec][30] == 0 &&
        bit_interruptType[0] == 1) {
      init_indice(bit_next_pc, 0, 32);
      uint32_t temp = cvt_bit_to_number_unsigned(csr[number_stvec], 32);
      temp &= 0xfffffffc;
      temp += 4 * (number_interruptType & 0x7fffffff);
      cvt_number_to_bit_unsigned(bit_next_pc, temp, 32);
    } else {
      init_indice(bit_next_pc, 0, 32);
      copy_indice(bit_next_pc, 0, csr[number_stvec], 0, 30);
    }
    if (page_fault_inst || page_fault_load ||
        page_fault_store) { /// next_stval = 0;//要看具体实现
      copy_indice(next_csr[number_stval], 0, info, 0, 32);
    } else {
      init_indice(next_csr[number_stval], 0, 32);
    }
    // sstatus是mstatus的子集，sstatus改变时mstatus也要变
    next_csr[number_mstatus][31 - 8] =
        (number_this_priviledge == 0)
            ? 0
            : 1; // next_mstatus.SPP = this_priviledge;
    next_csr[number_sstatus][31 - 8] =
        (number_this_priviledge == 0)
            ? 0
            : 1; // next_sstatus.SPP = this_priviledge;
    next_csr[number_mstatus][31 - 5] =
        csr[number_mstatus][31 - 1]; // next_mstatus.SPIE = mstatus.SIE;
    next_csr[number_sstatus][31 - 5] =
        csr[number_sstatus][31 - 1];      // next_sstatus.SPIE = sstatus.SIE;
    next_csr[number_mstatus][31 - 1] = 0; // next_mstatus.SIE = 0;
    next_csr[number_sstatus][31 - 1] = 0; // next_sstatus.SIE = 0;
    cvt_number_to_bit_unsigned(next_priviledge, 1, 2);
    // if (S_external_interrupt) { //PLIC的行为
    // 	init_indice(next_csr[number_sip], 0, 32);
    // 	init_indice(next_csr[number_mip], 0, 32);
    // }
  } else if (MRET) {
    next_csr[number_mstatus][31 - 3] =
        csr[number_mstatus][31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
    next_csr[number_sstatus][31 - 3] =
        csr[number_sstatus][31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
    copy_indice(next_priviledge, 0, csr[number_mstatus], 31 - 12,
                2);                       // next_priviledge = mstatus.MPP;
    next_csr[number_mstatus][31 - 7] = 1; // next_mstatus.MPIE = 1;
    next_csr[number_mstatus][31 - 12] = 0;
    next_csr[number_mstatus][31 - 11] = 0; // next_mstatus.MPP = U;
    next_csr[number_sstatus][31 - 7] = 1;  // next_mstatus.MPIE = 1;
    next_csr[number_sstatus][31 - 12] = 0;
    next_csr[number_sstatus][31 - 11] = 0; // next_mstatus.MPP = U;
    copy_indice(bit_next_pc, 0, csr[number_mepc], 0, 32); // next_pc = mepc;
  } else if (SRET) {
    next_csr[number_mstatus][31 - 1] =
        csr[number_mstatus][31 - 5]; // next_mstatus.SIE = mstatus.SPIE;
    next_csr[number_sstatus][31 - 1] =
        csr[number_sstatus][31 - 5]; // next_sstatus.SIE = sstatus.SPIE;
    next_priviledge[0] = 0;
    next_priviledge[1] =
        csr[number_sstatus][31 - 8];      // next_priviledge = sstatus.SPP;
    next_csr[number_mstatus][31 - 5] = 1; // next_mstatus.SPIE = 1;
    next_csr[number_sstatus][31 - 5] = 1; // next_sstatus.SPIE = 1;
    next_csr[number_mstatus][31 - 8] = 0; // next_mstatus.SPP = U;
    next_csr[number_sstatus][31 - 8] = 0; // next_sstatus.SPP = U;
    copy_indice(bit_next_pc, 0, csr[number_sepc], 0, 32); // next_pc = sepc;
  } else if (number_csr_code_unsigned != number_mtvec &&
             number_csr_code_unsigned != number_mepc &&
             number_csr_code_unsigned != number_mcause &&
             number_csr_code_unsigned != number_mie &&
             number_csr_code_unsigned != number_mip &&
             number_csr_code_unsigned != number_mtval &&
             number_csr_code_unsigned != number_mscratch &&
             number_csr_code_unsigned != number_mstatus &&
             number_csr_code_unsigned != number_mideleg &&
             number_csr_code_unsigned != number_medeleg &&
             number_csr_code_unsigned != number_sepc &&
             number_csr_code_unsigned != number_stvec &&
             number_csr_code_unsigned != number_scause &&
             number_csr_code_unsigned != number_sscratch &&
             number_csr_code_unsigned != number_stval &&
             number_csr_code_unsigned != number_sstatus &&
             number_csr_code_unsigned != number_sie &&
             number_csr_code_unsigned != number_sip &&
             number_csr_code_unsigned != number_satp &&
             number_csr_code_unsigned != number_mhartid &&
             number_csr_code_unsigned != number_misa &&
             number_csr_code_unsigned != number_time &&
             number_csr_code_unsigned != number_timeh) {
    ;
  } else if (number_op_code == number_10_opcode_ecall) {
    bool bit_csr_data[32];
    copy_indice(bit_csr_data, 0, csr[number_csr_code_unsigned], 0, 32);
    if (number_csr_code_unsigned == number_time) {
      // uint32_t now = time(0);
      uint32_t now = time_i;
      // cerr << hex << time_i << endl;
      cvt_number_to_bit_unsigned(bit_csr_data, now, 32);
      // init_indice(bit_csr_data, 0, 32);
    }
    if (number_csr_code_unsigned == number_timeh) {
      uint32_t now = time(0);
      init_indice(bit_csr_data, 0, 32);
    }

    bool bit_csr_data_result[32];
    init_indice(bit_csr_data_result, 0, 32);
    switch (number_funct3_unsigned) {
    case 0: {                   // sfence.vma
      bool bit_immi_s_type[12]; // S-type
      copy_indice(bit_immi_s_type, 0, instruction, 0, 7);
      copy_indice(bit_immi_s_type, 7, instruction, 20, 5);
      uint32_t number_immi_s_type_unsigned =
          cvt_bit_to_number_unsigned(bit_immi_s_type, 12);
      copy_indice(bit_csr_data_result, 0, bit_csr_data, 0, 32);
      if (number_immi_s_type_unsigned == 0b100100000) {
        if (LOG)
          cout << "sfence.vma" << endl;
        break;
      }
      // if((!instruction[2]) && (!instruction[3])){
      // 	int	next_reg_mcause_number = 11;
      // 	cvt_number_to_bit(next_csr[number_mcause],
      // next_reg_mcause_number,32); 	copy_indice(bit_next_pc, 0,
      // csr[number_mtvec], 0, 32); 	copy_indice(next_csr[number_mepc], 0,
      // bit_this_pc, 0, 32); }else{ 	copy_indice(bit_next_pc, 0,
      // csr[number_mepc], 0, 32); 	init_indice(next_csr[number_mcause], 0,
      // 32);
      // }
      break;
    }
    case 1: { // csrrw
      copy_indice(bit_csr_data_result, 0, bit_reg_data_a, 0, 32);
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    case 2: { // csrrs
      for (int i = 0; i < 32; i++)
        bit_csr_data_result[i] = bit_csr_data[i] | bit_reg_data_a[i];
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    case 3: { // csrrc
      for (int i = 0; i < 32; i++)
        bit_csr_data_result[i] = bit_csr_data[i] & ~bit_reg_data_a[i];
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    case 5: { // csrrwi
      copy_indice(bit_csr_data_result, 27, rs_a_code, 0, 5);
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    case 6: { // csrrsi
      copy_indice(bit_csr_data_result, 0, bit_csr_data, 0, 27);
      for (int i = 0; i < 5; i++)
        bit_csr_data_result[i + 27] = bit_csr_data[i + 27] | rs_a_code[i];
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    case 7: { // csrrci
      copy_indice(bit_csr_data_result, 0, bit_csr_data, 0, 27);
      for (int i = 0; i < 5; i++)
        bit_csr_data_result[i + 27] = bit_csr_data[i + 27] & ~rs_a_code[i];
      copy_indice(next_general_regs, reg_d_index * 32, bit_csr_data, 0, 32);
      break;
    }
    default: {
      copy_indice(bit_csr_data_result, 0, bit_csr_data, 0, 32);
    }
    }
    // bit_csr_data, bit_csr_data_result
    switch (number_csr_code_unsigned) {
    case number_sie: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0xfffffccc) |
                               (number_csr_data_result & 0x00000333);
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sie], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mie], 0, bit_csr_data_result, 0, 32);
      break;
    }
    case number_sip: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0xfffffccc) |
                               (number_csr_data_result & 0x00000333);
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sip], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mip], 0, bit_csr_data_result, 0, 32);
      break;
    }
    case number_sstatus: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0x7ff21ecc) |
                               (number_csr_data_result & (~0x7ff21ecc));
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sstatus], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mstatus], 0, bit_csr_data_result, 0, 32);
      break;
    }
    case number_mie: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0xfffff444) |
                               (number_csr_data_result & 0x00000bbb);
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sie], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mie], 0, bit_csr_data_result, 0, 32);
      break;
    }
    case number_mip: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0xfffff444) |
                               (number_csr_data_result & 0x00000bbb);
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sip], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mip], 0, bit_csr_data_result, 0, 32);
      break;
    }
    case number_mstatus: {
      uint32_t number_csr_data = cvt_bit_to_number_unsigned(bit_csr_data, 32);
      uint32_t number_csr_data_result =
          cvt_bit_to_number_unsigned(bit_csr_data_result, 32);
      number_csr_data_result = (number_csr_data & 0x7f800644) |
                               (number_csr_data_result & (~0x7f800644));
      cvt_number_to_bit_unsigned(bit_csr_data_result, number_csr_data_result,
                                 32);
      copy_indice(next_csr[number_sstatus], 0, bit_csr_data_result, 0, 32);
      copy_indice(next_csr[number_mstatus], 0, bit_csr_data_result, 0, 32);
      break;
    }
    default: {
      copy_indice(next_csr[number_csr_code_unsigned], 0, bit_csr_data_result, 0,
                  32);
      break;
    }
    }
  }

  init_indice(next_general_regs, 0, 32);
  copy_indice(output_data, 0, next_general_regs, 0, 1024);
  copy_indice(output_data, 1024, next_csr[number_mtvec], 0, 32);
  copy_indice(output_data, 1056, next_csr[number_mepc], 0, 32);
  copy_indice(output_data, 1088, next_csr[number_mcause], 0, 32);
  copy_indice(output_data, 1120, next_csr[number_mie], 0, 32);
  copy_indice(output_data, 1152, next_csr[number_mip], 0, 32);
  copy_indice(output_data, 1184, next_csr[number_mtval], 0, 32);
  copy_indice(output_data, 1216, next_csr[number_mscratch], 0, 32);
  copy_indice(output_data, 1248, next_csr[number_mstatus], 0, 32);
  copy_indice(output_data, 1280, next_csr[number_mideleg], 0, 32);
  copy_indice(output_data, 1312, next_csr[number_medeleg], 0, 32);
  copy_indice(output_data, 1344, next_csr[number_sepc], 0, 32);
  copy_indice(output_data, 1376, next_csr[number_stvec], 0, 32);
  copy_indice(output_data, 1408, next_csr[number_scause], 0, 32);
  copy_indice(output_data, 1440, next_csr[number_sscratch], 0, 32);
  copy_indice(output_data, 1472, next_csr[number_stval], 0, 32);
  copy_indice(output_data, 1504, next_csr[number_sstatus], 0, 32);
  copy_indice(output_data, 1536, next_csr[number_sie], 0, 32);
  copy_indice(output_data, 1568, next_csr[number_sip], 0, 32);
  copy_indice(output_data, 1600, next_csr[number_satp], 0, 32);
  copy_indice(output_data, 1632, next_csr[number_mhartid], 0, 32);
  copy_indice(output_data, 1664, next_csr[number_misa], 0, 32);
  copy_indice(output_data, 1696, bit_next_pc, 0, 32);
  copy_indice(output_data, 1728, bit_load_address, 0, 32);
  copy_indice(output_data, 1760, bit_store_data, 0, 32);
  copy_indice(output_data, 1792, bit_store_address, 0, 32);
  copy_indice(output_data, 1824, next_priviledge, 0, 2);
}
