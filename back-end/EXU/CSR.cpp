#include "config.h"
#include <CSR.h>
#include <cstdint>
#include <cvt.h>

void CSRU::init() {
  CSR_RegFile[number_misa] = 0x40141101; // U/S/M  RV32I/A/M
}

void CSRU::comb() {
  io.csr2exe->rdata = CSR_RegFile[io.exe2csr->idx];
  if (io.rob_bcast->exception) {

    bool mstatus[32];
    bool sstatus[32];
    bool mie[32];
    bool mip[32];
    bool mideleg[32];
    bool medeleg[32];
    bool mtvec[32];
    bool stvec[32];

    cvt_number_to_bit_unsigned(mstatus, CSR_RegFile[number_mstatus], 32);
    cvt_number_to_bit_unsigned(sstatus, CSR_RegFile[number_sstatus], 32);
    cvt_number_to_bit_unsigned(mie, CSR_RegFile[number_mie], 32);
    cvt_number_to_bit_unsigned(mip, CSR_RegFile[number_mip], 32);
    cvt_number_to_bit_unsigned(mideleg, CSR_RegFile[number_mideleg], 32);
    cvt_number_to_bit_unsigned(medeleg, CSR_RegFile[number_medeleg], 32);
    cvt_number_to_bit_unsigned(mtvec, CSR_RegFile[number_mtvec], 32);
    cvt_number_to_bit_unsigned(stvec, CSR_RegFile[number_stvec], 32);

    bool ecall = io.rob_bcast->ecall;
    bool page_fault_inst = io.rob_bcast->page_fault_inst;
    bool page_fault_load = io.rob_bcast->page_fault_load;
    bool page_fault_store = io.rob_bcast->page_fault_store;
    bool illegal_exception = io.rob_bcast->illegal_inst;

    bool mstatus_mie = mstatus[31 - 3];
    bool medeleg_U_ecall = medeleg[31 - 8];
    bool medeleg_S_ecall = medeleg[31 - 9];
    bool medeleg_M_ecall = medeleg[31 - 11];

    bool medeleg_page_fault_inst = medeleg[31 - 12];
    bool medeleg_page_fault_store = medeleg[31 - 15];
    bool medeleg_page_fault_load = medeleg[31 - 13];

    bool mip_ssip = mip[31 - 1];
    bool mie_ssie = mie[31 - 1];

    bool mip_stip = mip[31 - 5];
    bool mie_stie = mie[31 - 5];

    bool mip_seip = mip[31 - 9];
    bool mie_seie = mie[31 - 9];

    bool mip_msip = mip[31 - 3];
    bool mie_msie = mie[31 - 3];
    bool mideleg_msip = mideleg[31 - 3];

    bool mip_mtip = mip[31 - 7];
    bool mie_mtie = mie[31 - 7];
    bool mideleg_mtip = mideleg[31 - 7];

    bool mip_meip = mip[31 - 11];
    bool mie_meie = mie[31 - 11];
    bool mideleg_meip = mideleg[31 - 11];

    bool M_software_interrupt =
        mip_msip && mie_msie && !mideleg_msip &&
        (privilege < 3 || mstatus_mie); // M_software_interrupt

    bool M_timer_interrupt =
        mip_mtip && mie_mtie && !mideleg_mtip &&
        (privilege < 3 || mstatus_mie); // M_timer_interrupt

    bool M_external_interrupt =
        mip_meip && mie_meie && !mideleg_meip && (privilege < 3 || mstatus_mie);

    bool S_software_interrupt =
        (mip_msip && mie_msie && mideleg_msip && privilege < 2 &&
         (privilege < 1 || mstatus_mie)) ||
        (mip_ssip && mie_ssie && privilege < 2 &&
         (privilege < 1 || mstatus_mie));

    bool S_timer_interrupt =
        (mip_mtip && mie_mtie && mideleg_mtip && privilege < 2 &&
         (privilege < 1 || mstatus_mie)) ||
        (mip_stip && mie_stie && privilege < 2 &&
         (privilege < 1 || mstatus_mie == 1));

    bool S_external_interrupt =
        (mip_meip && mie_mtie && mideleg_meip && privilege < 2 &&
         (privilege < 1 || mstatus_mie)) ||
        (mip_seip && mie_seie && privilege < 2 &&
         (privilege < 1 || mstatus_mie));

    bool MTrap =
        (M_software_interrupt) || (M_timer_interrupt) ||
        (M_external_interrupt) ||
        (io.rob_bcast->exception && (privilege == 0) && !medeleg_U_ecall) ||
        (ecall && (privilege == 1) && !medeleg_S_ecall) ||
        (ecall && (privilege == 3)) // MTrap下的ecall一定在MTrap处理
        || (page_fault_inst && !medeleg_page_fault_inst) ||
        (page_fault_load && !medeleg_page_fault_load) ||
        (page_fault_store && !medeleg_page_fault_store) || illegal_exception;

    bool STrap = S_software_interrupt || S_timer_interrupt ||
                 S_external_interrupt ||
                 (ecall && (privilege == 0) && medeleg_U_ecall) ||
                 (ecall && (privilege == 1) && medeleg_S_ecall)
                 //||	(ecall 	&& (privilege==3) && csr_medeleg[31-11])
                 ////M态ECALL无论如何不会进入STrap
                 || (page_fault_inst && medeleg_page_fault_inst) ||
                 (page_fault_load && medeleg_page_fault_load) ||
                 (page_fault_store && medeleg_page_fault_store);

    if (MTrap) {
      CSR_RegFile[number_mepc] = io.rob_bcast->pc;

      // next_mcause = interruptType;
      uint32_t cause =
          (M_software_interrupt || M_timer_interrupt || M_external_interrupt)
          << 31;

      cause += M_software_interrupt ? 3
               : M_timer_interrupt  ? 7
               : (M_external_interrupt ||
                  (ecall && (privilege == 3) && !medeleg_U_ecall))
                   ? 11
               /*: illegal_exception                                  ? 2*/
               : (ecall && (privilege == 0) && !medeleg_U_ecall) ? 8
               : (ecall && (privilege == 1) && !medeleg_S_ecall) ? 9
               : (page_fault_inst && !medeleg_page_fault_inst)   ? 12
               : (page_fault_load && !medeleg_page_fault_load)   ? 13
               : (page_fault_store && !medeleg_page_fault_store) ? 15
               : (illegal_exception)                             ? 2
                                     : 0; // 给后31位赋值

      CSR_RegFile[number_mcause] = cause;

      if (mtvec[31 - 0] && !mtvec[31 - 1] && cause & (1 << 31)) {
        io.csr2exe->trap_pc = CSR_RegFile[number_mtvec] & 0xfffffffc;
        io.csr2exe->trap_pc += 4 * (cause & 0x7fffffff);
      } else {
        io.csr2exe->trap_pc = CSR_RegFile[number_mtvec];
      }

      mstatus[31 - 11] = privilege & 0b1;
      mstatus[31 - 12] =
          (privilege >> 1) & 0b1;        // next_mstatus.MPP = this_priviledge*/
      mstatus[31 - 7] = mstatus[31 - 3]; // next_mstatus.MPIE = mstatus.MIE;
      mstatus[31 - 3] = 0;               // next_mstatus.MIE = 0;

      sstatus[31 - 11] = privilege & 0b1;
      sstatus[31 - 12] =
          (privilege >> 1) & 0b1;        // next_sstatus.MPP = this_priviledge*/
      sstatus[31 - 7] = sstatus[31 - 3]; // next_sstatus.MPIE = sstatus.MIE;
      sstatus[31 - 3] = 0;               // next_sstatus.MIE = 0;
      privilege = 0b11;

      if (page_fault_store || page_fault_load || page_fault_inst) {
        CSR_RegFile[number_mtval] = io.rob_bcast->trap_val;
      } else if (illegal_exception) {
        CSR_RegFile[number_mtval] = io.rob_bcast->trap_val;
      } else {
        CSR_RegFile[number_mtval] = 0;
      }

    } else if (STrap) {
      CSR_RegFile[number_sepc] = io.rob_bcast->pc;
      uint32_t cause =
          (M_software_interrupt || M_timer_interrupt || M_external_interrupt)
          << 31;

      cause += (S_external_interrupt ||
                (ecall && (privilege == 1) && medeleg_S_ecall))
                   ? 9
               : S_timer_interrupt                              ? 5
               : (ecall && (privilege == 0) && medeleg_U_ecall) ? 8
               : S_software_interrupt                           ? 1
               : (page_fault_inst && medeleg_page_fault_inst)   ? 12
               : (page_fault_load && medeleg_page_fault_load)   ? 13
               : (page_fault_store && medeleg_page_fault_store)
                   ? 15
                   : 0; // 给后31位赋值

      CSR_RegFile[number_scause] = cause;

      if (stvec[31 - 0] && !stvec[31 - 1] && cause & (1 << 31)) {
        io.csr2exe->trap_pc = CSR_RegFile[number_stvec] & 0xfffffffc;
        io.csr2exe->trap_pc += 4 * (cause & 0x7fffffff);
      } else {
        io.csr2exe->trap_pc = CSR_RegFile[number_stvec];
      }

      // sstatus是mstatus的子集，sstatus改变时mstatus也要变
      sstatus[31 - 8] =
          (privilege == 0) ? 0 : 1; // next_sstatus.SPP = this_priviledge;
      mstatus[31 - 8] =
          (privilege == 0) ? 0 : 1; // next_mstatus.SPP = this_priviledge;

      mstatus[31 - 5] = mstatus[31 - 1]; // next_mstatus.SPIE = mstatus.SIE;
      sstatus[31 - 5] = sstatus[31 - 1]; // next_sstatus.SPIE = sstatus.SIE;
      mstatus[31 - 1] = 0;               // next_mstatus.SIE = 0;
      sstatus[31 - 1] = 0;               // next_sstatus.SIE = 0;
      privilege = 1;
      if (page_fault_store || page_fault_load || page_fault_inst) {
        CSR_RegFile[number_stval] = io.rob_bcast->trap_val;
      } else {
        CSR_RegFile[number_stval] = 0;
      }

    } else if (io.rob_bcast->mret) {
      mstatus[31 - 3] = mstatus[31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
      sstatus[31 - 3] = sstatus[31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
      privilege = mstatus[31 - 11] + 2 * mstatus[31 - 12];
      mstatus[31 - 7] = 1; // next_mstatus.MPIE = 1;
      mstatus[31 - 12] = 0;
      mstatus[31 - 11] = 0; // next_mstatus.MPP = U;

      sstatus[31 - 7] = 1; // next_mstatus.MPIE = 1;
      sstatus[31 - 12] = 0;
      sstatus[31 - 11] = 0; // next_mstatus.MPP = U;
      io.csr2exe->epc = CSR_RegFile[number_mepc];
    } else if (io.rob_bcast->sret) {
      mstatus[31 - 1] = mstatus[31 - 5]; // next_mstatus.SIE = mstatus.SPIE;
      sstatus[31 - 1] = sstatus[31 - 5]; // next_sstatus.SIE = sstatus.SPIE;
      privilege = sstatus[31 - 8];       // next_priviledge = sstatus.SPP;
      mstatus[31 - 5] = 1;               // next_mstatus.SPIE = 1;
      sstatus[31 - 5] = 1;               // next_sstatus.SPIE = 1;
      mstatus[31 - 8] = 0;               // next_mstatus.SPP = U;
      sstatus[31 - 8] = 0;               // next_sstatus.SPP = U;
      io.csr2exe->epc = CSR_RegFile[number_sepc];
    }
    CSR_RegFile[number_mstatus] = cvt_bit_to_number_unsigned(mstatus, 32);
    CSR_RegFile[number_sstatus] = cvt_bit_to_number_unsigned(sstatus, 32);
  }
}

void CSRU::seq() {
  int csr_idx = io.exe2csr->idx;
  if (io.exe2csr->we) {

    uint32_t csr_wdata;
    if (io.exe2csr->wcmd == CSR_W) {
      csr_wdata = io.exe2csr->wdata;
    } else if (io.exe2csr->wcmd == CSR_S) {
      csr_wdata =
          io.exe2csr->wdata | (~io.exe2csr->wdata & CSR_RegFile[csr_idx]);
    } else if (io.exe2csr->wcmd == CSR_C) {
      csr_wdata = (~io.exe2csr->wdata & CSR_RegFile[csr_idx]);
    }

    if (csr_idx == number_mie || csr_idx == number_sie) {

      if (csr_idx == number_sie)
        csr_wdata =
            (CSR_RegFile[number_mie] & 0xfffffccc) | (csr_wdata & 0x00000333);
      else
        csr_wdata =
            (CSR_RegFile[number_mie] & 0xfffff444) | (csr_wdata & 0x00000bbb);

      CSR_RegFile[number_mie] = csr_wdata;
      CSR_RegFile[number_sie] = csr_wdata;
    } else if (csr_idx == number_mip || csr_idx == number_sip) {

      if (csr_idx == number_mip)
        csr_wdata =
            (CSR_RegFile[number_mip] & 0xfffffccc) | (csr_wdata & 0x00000333);
      else
        csr_wdata =
            (CSR_RegFile[number_mip] & 0xfffff444) | (csr_wdata & 0x00000bbb);

      CSR_RegFile[number_mip] = csr_wdata;
      CSR_RegFile[number_sip] = csr_wdata;
    } else if (csr_idx == number_mstatus || csr_idx == number_sstatus) {

      if (csr_idx == number_sstatus) {
        csr_wdata = (CSR_RegFile[number_sstatus] & 0x7ff21ecc) |
                    (csr_wdata & (~0x7ff21ecc));
      } else {
        csr_wdata = (CSR_RegFile[number_mstatus] & 0x7f800644) |
                    (csr_wdata & (~0x7f800644));
      }

      CSR_RegFile[number_mstatus] = csr_wdata;
      CSR_RegFile[number_sstatus] = csr_wdata;

    } else {
      CSR_RegFile[csr_idx] = csr_wdata;
    }
  }
}
