#include "RISCV.h"
#include "cvt.h"

void RISCV(bool input_data[BIT_WIDTH], bool *output_data) {

  bool instruction[32];
  bool this_priviledge[2];
  bool csr_mie[32];
  bool csr_mip[32];
  bool csr_mstatus[32];
  bool csr_mideleg[32];
  bool csr_medeleg[32];
  copy_indice(csr_mie, 0, input_data, 1120, 32);
  copy_indice(csr_mip, 0, input_data, 1152, 32);
  copy_indice(csr_mstatus, 0, input_data, 1248, 32);
  copy_indice(csr_mideleg, 0, input_data, 1280, 32);
  copy_indice(csr_medeleg, 0, input_data, 1312, 32);
  // copy_indice(csr_sepc, 0, input_data, 1344, 32);
  // copy_indice(csr_stvec, 0, input_data, 1376, 32);
  // copy_indice(csr_scause, 0, input_data, 1408, 32);
  // copy_indice(csr_sscratch, 0, input_data, 1440, 32);
  // copy_indice(csr_stval, 0, input_data, 1472, 32);
  copy_indice(instruction, 0, input_data, 1696, 32);
  bool asy = input_data[1792];
  bool page_fault_inst = input_data[1793];
  bool page_fault_load = input_data[1794];
  bool page_fault_store = input_data[1795];
  copy_indice(this_priviledge, 0, input_data, 1796, 2);

  // split instruction
  bool bit_op_code[7];   // 25-31
  bool bit_csr_code[12]; // 0-11
  copy_indice(bit_op_code, 0, instruction, 25, 7);
  uint32_t number_op_code_unsigned = cvt_bit_to_number_unsigned(bit_op_code, 7);
  copy_indice(bit_csr_code, 0, instruction, 0, 12);
  uint32_t number_csr_code_unsigned =
      cvt_bit_to_number_unsigned(bit_csr_code, 12);
  bool bit_funct3[3];
  copy_indice(bit_funct3, 0, instruction, 17, 3);
  uint32_t number_funct3_unsigned = cvt_bit_to_number_unsigned(bit_funct3, 3);

  uint32_t number_this_priviledge =
      cvt_bit_to_number_unsigned(this_priviledge, 2);
  uint32_t number_op_code = cvt_bit_to_number_unsigned(bit_op_code, 7);
  bool M_software_interrupt =
      csr_mip[31 - 3] && csr_mie[31 - 3] && (csr_mideleg[31 - 3] == 0) &&
      (number_this_priviledge < 3 ||
       csr_mstatus[31 - 3] == 1); // M_software_interrupt
  bool M_timer_interrupt = csr_mip[31 - 7] && csr_mie[31 - 7] &&
                           (csr_mideleg[31 - 7] == 0) &&
                           (number_this_priviledge < 3 ||
                            csr_mstatus[31 - 3] == 1); // M_timer_interrupt
  bool M_external_interrupt =
      csr_mip[31 - 11] && csr_mie[31 - 11] && (csr_mideleg[31 - 11] == 0) &&
      (number_this_priviledge < 3 || csr_mstatus[31 - 3] == 1);
  bool S_software_interrupt =
      (csr_mip[31 - 3] && csr_mie[31 - 3] && csr_mideleg[31 - 3] == 1 &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1)) ||
      (csr_mip[31 - 1] && csr_mie[31 - 1] && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1));
  bool S_timer_interrupt =
      (csr_mip[31 - 7] && csr_mie[31 - 7] && csr_mideleg[31 - 7] == 1 &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1)) ||
      (csr_mip[31 - 5] && csr_mie[31 - 5] && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1));
  bool S_external_interrupt =
      (csr_mip[31 - 11] && csr_mie[31 - 11] && csr_mideleg[31 - 11] == 1 &&
       number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1)) ||
      (csr_mip[31 - 9] && csr_mie[31 - 9] && number_this_priviledge < 2 &&
       (number_this_priviledge < 1 || csr_mstatus[31 - 1] == 1));
  bool ecall = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
                cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0);

  bool MRET = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
               (cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0x302));
  bool SRET = (number_op_code == 0x73 && number_funct3_unsigned == 0 &&
               (cvt_bit_to_number_unsigned(bit_csr_code, 12) == 0x102));
  bool MTrap =
      (M_software_interrupt) || (M_timer_interrupt) || (M_external_interrupt) ||
      (ecall && (number_this_priviledge == 0) && !csr_medeleg[31 - 8]) ||
      (ecall && (number_this_priviledge == 1) && !csr_medeleg[31 - 9]) ||
      (ecall && (number_this_priviledge == 3)) // MTrap下的ecall一定在MTrap处理
      || (page_fault_inst && !csr_medeleg[31 - 12]) ||
      (page_fault_load && !csr_medeleg[31 - 13]) ||
      (page_fault_store && !csr_medeleg[31 - 15]);
  bool STrap =
      S_software_interrupt || S_timer_interrupt || S_external_interrupt ||
      (ecall && (number_this_priviledge == 0) && csr_medeleg[31 - 8]) ||
      (ecall && (number_this_priviledge == 1) && csr_medeleg[31 - 9])
      //||	(ecall 	&& (number_this_priviledge==3) && csr_medeleg[31-11])
      ////M态ECALL无论如何不会进入STrap
      || (page_fault_inst && csr_medeleg[31 - 12]) ||
      (page_fault_load && csr_medeleg[31 - 13]) ||
      (page_fault_store && csr_medeleg[31 - 15]);

  asy = MTrap || STrap || MRET || SRET;
  uint32_t number_instruction = cvt_bit_to_number_unsigned(instruction, 32);
  if (number_instruction == 0x10500073 && !asy && !page_fault_inst &&
      !page_fault_load && !page_fault_store) {
    cerr << "wfi" << endl;
    exit(-1);
  }

  if (asy || page_fault_inst || page_fault_load || page_fault_store ||
      number_op_code_unsigned ==
          number_10_opcode_ecall) { // 进入中断控制程序，这里只完成了对1个中断的处理（只设置了1个mcause值）
    RISCV_CSR(input_data, output_data);
  } else if (number_op_code_unsigned == number_11_opcode_lrw) {
    RISCV_32A(input_data, output_data);
  } else {
    RISCV_32I(input_data, output_data);
  }
}
