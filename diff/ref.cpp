#include "ref.h"
#include "CSR.h"
#include "RISCV.h"
#include "config.h"
#include "cvt.h"
#include <cstdint>

#define BITMASK(bits) ((1ull << (bits)) - 1)
#define BITS(x, hi, lo)                                                        \
  (((x) >> (lo)) & BITMASK((hi) - (lo) + 1)) // similar to x[hi:lo] in verilog
#define SEXT(x, len)                                                           \
  ({                                                                           \
    struct {                                                                   \
      int64_t n : len;                                                         \
    } __x = {.n = (int64_t)x};                                                 \
    (uint64_t) __x.n;                                                          \
  })

#define immI(i) SEXT(BITS(i, 31, 20), 12)
#define immU(i) (SEXT(BITS(i, 31, 12), 20) << 12)
#define immS(i) ((SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7))
#define immJ(i)                                                                \
  ((SEXT(BITS(i, 31, 31), 1) << 20) | (BITS(i, 19, 12) << 12) |                \
   (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1))
#define immB(i)                                                                \
  ((SEXT(BITS(i, 31, 31), 1) << 12) | (BITS(i, 7, 7) << 11) |                  \
   (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1))

bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

int cvt_number_to_csr(int csr_idx);

void Ref_cpu::init(uint32_t reset_pc) {
  state.pc = reset_pc;
  memory = new uint32_t[PHYSICAL_MEMORY_LENGTH];
  for (int i = 0; i < 32; i++) {
    state.gpr[i] = 0;
  }
  for (int i = 0; i < 21; i++) {
    state.csr[i] = 0;
  }
  state.csr[csr_misa] = 0x40141101;
  privilege = 0b11;

  state.store = false;
  asy = false;
  page_fault_inst = false;
  page_fault_load = false;
  page_fault_store = false;
}

void Ref_cpu::exec() {
  is_br = br_taken = false;
  illegal_exception = page_fault_load = page_fault_inst = page_fault_store =
      asy = false;
  state.store = false;
  if (state.pc == 0x80000000) {
    privilege = 0b01;
  }

  bool mstatus[32], sstatus[32];
  cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
  cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);
  uint32_t p_addr = state.pc;

  if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
    page_fault_inst = !va2pa(p_addr, state.pc, state.csr[csr_satp], 0, mstatus,
                             sstatus, privilege, memory);
    if (page_fault_inst) {
      exception(state.pc);
      return;
    } else {
      Instruction = memory[p_addr >> 2];
    }
  } else {
    Instruction = memory[p_addr >> 2];
  }
  RISCV();
}

void Ref_cpu::exception(uint32_t trap_val) {

  uint32_t next_pc = state.pc + 4;
  bool ecall = (Instruction == INST_ECALL);
  bool MRET = (Instruction == INST_MRET);
  bool SRET = (Instruction == INST_SRET);

  bool mideleg[32];
  bool medeleg[32];
  bool mstatus[32];
  bool sstatus[32];
  bool mtvec[32];
  bool stvec[32];

  cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
  cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);
  cvt_number_to_bit_unsigned(mideleg, state.csr[csr_mideleg], 32);
  cvt_number_to_bit_unsigned(medeleg, state.csr[csr_medeleg], 32);
  cvt_number_to_bit_unsigned(mtvec, state.csr[csr_mtvec], 32);
  cvt_number_to_bit_unsigned(stvec, state.csr[csr_stvec], 32);

  bool medeleg_U_ecall = medeleg[31 - 8];
  bool medeleg_S_ecall = medeleg[31 - 9];
  bool medeleg_M_ecall = medeleg[31 - 11];

  bool medeleg_page_fault_inst = medeleg[31 - 12];
  bool medeleg_page_fault_store = medeleg[31 - 15];
  bool medeleg_page_fault_load = medeleg[31 - 13];

  bool MTrap =
      (M_software_interrupt) || (M_timer_interrupt) || (M_external_interrupt) ||
      ((privilege == 0) && !medeleg_U_ecall) ||
      (ecall && (privilege == 1) && !medeleg_S_ecall) ||
      (ecall && (privilege == 3)) // MTrap下的ecall一定在MTrap处理
      || (page_fault_inst && !medeleg_page_fault_inst) ||
      (page_fault_load && !medeleg_page_fault_load) ||
      (page_fault_store && !medeleg_page_fault_store) || illegal_exception;

  bool STrap = S_software_interrupt || S_timer_interrupt ||
               S_external_interrupt ||
               (ecall && (privilege == 0) && medeleg_U_ecall) ||
               (ecall && (privilege == 1) && medeleg_S_ecall) ||
               (page_fault_inst && medeleg_page_fault_inst) ||
               (page_fault_load && medeleg_page_fault_load) ||
               (page_fault_store && medeleg_page_fault_store);

  if (MTrap) {
    state.csr[csr_mepc] = state.pc;

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

    state.csr[csr_mcause] = cause;

    if (mtvec[31 - 0] && !mtvec[31 - 1] && cause & (1 << 31)) {
      next_pc = state.csr[csr_mtvec] & 0xfffffffc;
      next_pc += 4 * (cause & 0x7fffffff);
    } else {
      next_pc = state.csr[csr_mtvec];
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

    state.csr[csr_mtval] = trap_val;

  } else if (STrap) {
    state.csr[csr_sepc] = state.pc;
    uint32_t cause =
        (M_software_interrupt || M_timer_interrupt || M_external_interrupt)
        << 31;

    cause +=
        (S_external_interrupt || (ecall && (privilege == 1) && medeleg_S_ecall))
            ? 9
        : S_timer_interrupt                              ? 5
        : (ecall && (privilege == 0) && medeleg_U_ecall) ? 8
        : S_software_interrupt                           ? 1
        : (page_fault_inst && medeleg_page_fault_inst)   ? 12
        : (page_fault_load && medeleg_page_fault_load)   ? 13
        : (page_fault_store && medeleg_page_fault_store) ? 15
                                                         : 0; // 给后31位赋值

    state.csr[csr_scause] = cause;

    if (stvec[31 - 0] && !stvec[31 - 1] && cause & (1 << 31)) {
      next_pc = state.csr[csr_stvec] & 0xfffffffc;
      next_pc += 4 * (cause & 0x7fffffff);
    } else {
      next_pc = state.csr[csr_stvec];
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
    state.csr[csr_stval] = trap_val;
  } else if (MRET) {
    mstatus[31 - 3] = mstatus[31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
    sstatus[31 - 3] = sstatus[31 - 7]; // next_mstatus.MIE = mstatus.MPIE;
    privilege = mstatus[31 - 11] + 2 * mstatus[31 - 12];
    mstatus[31 - 7] = 1; // next_mstatus.MPIE = 1;
    mstatus[31 - 12] = 0;
    mstatus[31 - 11] = 0; // next_mstatus.MPP = U;

    sstatus[31 - 7] = 1; // next_mstatus.MPIE = 1;
    sstatus[31 - 12] = 0;
    sstatus[31 - 11] = 0; // next_mstatus.MPP = U;
    next_pc = state.csr[csr_mepc];
  } else if (SRET) {
    mstatus[31 - 1] = mstatus[31 - 5]; // next_mstatus.SIE = mstatus.SPIE;
    sstatus[31 - 1] = sstatus[31 - 5]; // next_sstatus.SIE = sstatus.SPIE;
    privilege = sstatus[31 - 8];       // next_priviledge = sstatus.SPP;
    mstatus[31 - 5] = 1;               // next_mstatus.SPIE = 1;
    sstatus[31 - 5] = 1;               // next_sstatus.SPIE = 1;
    mstatus[31 - 8] = 0;               // next_mstatus.SPP = U;
    sstatus[31 - 8] = 0;               // next_sstatus.SPP = U;
    next_pc = state.csr[csr_sepc];
  }
  state.csr[csr_mstatus] = cvt_bit_to_number_unsigned(mstatus, 32);
  state.csr[csr_sstatus] = cvt_bit_to_number_unsigned(sstatus, 32);
  state.pc = next_pc;
}

void Ref_cpu::RISCV() {

  if (Instruction == INST_EBREAK) {
    state.pc += 4;
    return;
  }

  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, Instruction, 32);
  // split instruction
  bool bit_op_code[7]; // 25-31
  copy_indice(bit_op_code, 0, inst_bit, 25, 7);
  uint32_t number_op_code_unsigned = cvt_bit_to_number_unsigned(bit_op_code, 7);

  bool ecall = (Instruction == INST_ECALL);
  bool MRET = (Instruction == INST_MRET);
  bool SRET = (Instruction == INST_SRET);

  bool mstatus[32];
  bool sstatus[32];
  bool mie[32];
  bool mip[32];
  bool mideleg[32];
  bool medeleg[32];
  bool mtvec[32];
  bool stvec[32];

  cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
  cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);
  cvt_number_to_bit_unsigned(mie, state.csr[csr_mie], 32);
  cvt_number_to_bit_unsigned(mip, state.csr[csr_mip], 32);
  cvt_number_to_bit_unsigned(mideleg, state.csr[csr_mideleg], 32);
  cvt_number_to_bit_unsigned(medeleg, state.csr[csr_medeleg], 32);
  cvt_number_to_bit_unsigned(mtvec, state.csr[csr_mtvec], 32);
  cvt_number_to_bit_unsigned(stvec, state.csr[csr_stvec], 32);

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

  M_software_interrupt = mip_msip && mie_msie && !mideleg_msip &&
                         (privilege < 3 || mstatus_mie); // M_software_interrupt

  M_timer_interrupt = mip_mtip && mie_mtie && !mideleg_mtip &&
                      (privilege < 3 || mstatus_mie); // M_timer_interrupt

  M_external_interrupt =
      mip_meip && mie_meie && !mideleg_meip && (privilege < 3 || mstatus_mie);

  S_software_interrupt =
      (mip_msip && mie_msie && mideleg_msip && privilege < 2 &&
       (privilege < 1 || mstatus_mie)) ||
      (mip_ssip && mie_ssie && privilege < 2 && (privilege < 1 || mstatus_mie));

  S_timer_interrupt = (mip_mtip && mie_mtie && mideleg_mtip && privilege < 2 &&
                       (privilege < 1 || mstatus_mie)) ||
                      (mip_stip && mie_stie && privilege < 2 &&
                       (privilege < 1 || mstatus_mie == 1));

  S_external_interrupt =
      (mip_meip && mie_mtie && mideleg_meip && privilege < 2 &&
       (privilege < 1 || mstatus_mie)) ||
      (mip_seip && mie_seie && privilege < 2 && (privilege < 1 || mstatus_mie));

  bool MTrap = (M_software_interrupt) || (M_timer_interrupt) ||
               (M_external_interrupt) || (privilege == 0 && !medeleg_U_ecall) ||
               (ecall && (privilege == 1) && !medeleg_S_ecall) ||
               (ecall && (privilege == 3)) ||
               (page_fault_inst && !medeleg_page_fault_inst) ||
               illegal_exception;

  bool STrap = S_software_interrupt || S_timer_interrupt ||
               S_external_interrupt ||
               (ecall && (privilege == 0) && medeleg_U_ecall) ||
               (ecall && (privilege == 1) && medeleg_S_ecall) ||
               (page_fault_inst && medeleg_page_fault_inst);

  asy = MTrap || STrap || MRET || SRET;

  if (Instruction == 0x10500073 && !asy && !page_fault_inst &&
      !page_fault_load && !page_fault_store) {
    cerr << "wfi" << endl;
    exit(-1);
  }

  if (page_fault_inst) {
    exception(state.pc);
  } else if (illegal_exception) {
    exception(Instruction);
  } else if (asy || Instruction == INST_ECALL) {
    exception(0);
  } else if (number_op_code_unsigned == number_10_opcode_ecall) {
    RV32CSR();
  } else if (number_op_code_unsigned == number_11_opcode_lrw) {
    RV32A();
  } else {
    RV32IM();
  }
  state.gpr[0] = 0;
}

void Ref_cpu::RV32CSR() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;

  // split instruction
  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, Instruction, 32);

  bool rd_code[5];       // 20-24
  bool rs_a_code[5];     // 12-16
  bool rs_b_code[5];     // 7-11
  bool bit_csr_code[12]; // 0-11
  copy_indice(rd_code, 0, inst_bit, 20, 5);
  copy_indice(rs_a_code, 0, inst_bit, 12, 5);
  copy_indice(rs_b_code, 0, inst_bit, 7, 5);
  copy_indice(bit_csr_code, 0, inst_bit, 0, 12);
  uint32_t number_csr_code_unsigned =
      cvt_bit_to_number_unsigned(bit_csr_code, 12);
  bool bit_funct3[3];
  copy_indice(bit_funct3, 0, inst_bit, 17, 3);
  uint32_t number_funct3_unsigned = cvt_bit_to_number_unsigned(bit_funct3, 3);

  // 准备寄存器
  uint32_t reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
  uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];

  bool we = number_funct3_unsigned == 1 || reg_a_index != 0;
  bool re = number_funct3_unsigned != 1 || reg_d_index != 0;
  uint32_t wcmd = number_funct3_unsigned & 0b11;
  uint32_t csr_wdata, wdata;

  if (bit_funct3[0] && (bit_funct3[2] || bit_funct3[1])) {
    wdata = reg_a_index;
  } else {
    wdata = reg_rdata1;
  }

  if (number_csr_code_unsigned != number_mtvec &&
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
  } else if (number_csr_code_unsigned == number_time ||
             number_csr_code_unsigned == number_timeh) {
    illegal_exception = true;
    exception(Instruction);
    return;
  } else {

    int csr_idx = cvt_number_to_csr(number_csr_code_unsigned);
    if (re) {
      state.gpr[reg_d_index] = state.csr[csr_idx];
    }

    if (we) {
      uint32_t csr_wdata;
      if (wcmd == CSR_W) {
        csr_wdata = wdata;
      } else if (wcmd == CSR_S) {
        csr_wdata = wdata | (~wdata & state.csr[csr_idx]);
      } else if (wcmd == CSR_C) {
        csr_wdata = (~wdata & state.csr[csr_idx]);
      }

      if (csr_idx == csr_mie || csr_idx == csr_sie) {
        if (csr_idx == csr_sie)
          csr_wdata =
              (state.csr[csr_mie] & 0xfffffccc) | (csr_wdata & 0x00000333);
        else
          csr_wdata =
              (state.csr[csr_mie] & 0xfffff444) | (csr_wdata & 0x00000bbb);

        state.csr[csr_mie] = csr_wdata;
        state.csr[csr_sie] = csr_wdata;
      } else if (csr_idx == number_mip || csr_idx == number_sip) {

        if (csr_idx == number_mip)
          csr_wdata =
              (state.csr[csr_mip] & 0xfffffccc) | (csr_wdata & 0x00000333);
        else
          csr_wdata =
              (state.csr[csr_mip] & 0xfffff444) | (csr_wdata & 0x00000bbb);

        state.csr[csr_mip] = csr_wdata;
        state.csr[csr_sip] = csr_wdata;
      } else if (csr_idx == csr_mstatus || csr_idx == csr_sstatus) {

        if (csr_idx == csr_sstatus) {
          csr_wdata = (state.csr[csr_sstatus] & 0x7ff21ecc) |
                      (csr_wdata & (~0x7ff21ecc));
        } else {
          csr_wdata = (state.csr[csr_mstatus] & 0x7f800644) |
                      (csr_wdata & (~0x7f800644));
        }

        state.csr[csr_mstatus] = csr_wdata;
        state.csr[csr_sstatus] = csr_wdata;

      } else {
        state.csr[csr_idx] = csr_wdata;
      }
    }
  }

  state.pc = next_pc;
}

void Ref_cpu::RV32A() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;

  // split instruction
  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, Instruction, 32);
  bool bit_op_code[7]; // 25-31
  bool rd_code[5];     // 20-24
  bool rs_a_code[5];   // 12-16
  bool rs_b_code[5];   // 7-11
  copy_indice(bit_op_code, 0, inst_bit, 25, 7);
  copy_indice(rd_code, 0, inst_bit, 20, 5);
  copy_indice(rs_a_code, 0, inst_bit, 12, 5);
  copy_indice(rs_b_code, 0, inst_bit, 7, 5);

  // 准备寄存器
  uint32_t reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
  uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  bool bit_funct5[5];
  copy_indice(bit_funct5, 0, inst_bit, 0, 5);
  uint32_t number_funct5_unsigned = cvt_bit_to_number_unsigned(bit_funct5, 5);
  uint32_t v_addr = reg_rdata1;
  uint32_t p_addr = v_addr;
  bool mstatus[32], sstatus[32];
  cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
  cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);

  if (state.csr[csr_satp] & 0x80000000 && privilege != 3) {
    bool page_fault_1 = !va2pa(p_addr, v_addr, state.csr[csr_satp], 1, mstatus,
                               sstatus, privilege, memory);
    bool page_fault_2 = !va2pa(p_addr, v_addr, state.csr[csr_satp], 2, mstatus,
                               sstatus, privilege, memory);

    if (page_fault_1 || page_fault_2) {
      if (number_funct5_unsigned == 3 && page_fault_2) {
        page_fault_store = true;
      } else if (page_fault_1) {
        page_fault_load = true;
      } else {
        page_fault_store = true;
      }

      exception(v_addr);
      return;
    }
  }

  if (number_funct5_unsigned != 2) {
    state.store = true;
    state.store_addr = p_addr;
  }

  switch (number_funct5_unsigned) {
  case 0: { // amoadd.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] = memory[p_addr >> 2] + reg_rdata2;
    break;
  }
  case 1: { // amoswap.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] = reg_rdata2;
    break;
  }
  case 2: { // lr.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    break;
  }
  case 3: { // sc.w
    state.store_data = memory[p_addr >> 2] = reg_rdata2;
    state.gpr[reg_d_index] = 0;
    break;
  }
  case 4: { // amoxor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] = memory[p_addr >> 2] ^ reg_rdata2;
    break;
  }
  case 8: { // amoor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] = memory[p_addr >> 2] | reg_rdata2;
    break;
  }
  case 12: { // amoand.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] = memory[p_addr >> 2] & reg_rdata2;
    break;
  }
  case 16: { // amomin.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] =
        ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
            ? reg_rdata2
            : memory[p_addr >> 2];
    break;
  }
  case 20: { // amomax.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] =
        ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
            ? memory[p_addr >> 2]
            : reg_rdata2;
    break;
  }
  case 24: { // amominu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] =
        ((uint32_t)memory[p_addr >> 2] < (uint32_t)reg_rdata2)
            ? memory[p_addr >> 2]
            : reg_rdata2;
    break;
  }
  case 28: { // amomaxu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] =
        ((uint32_t)memory[p_addr >> 2] > (uint32_t)reg_rdata2)
            ? memory[p_addr >> 2]
            : reg_rdata2;
    break;
  }
  default: {
    break;
  }
  }

  state.pc = next_pc;
}

void Ref_cpu::RV32IM() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;

  // split instruction
  bool inst_bit[32];
  cvt_number_to_bit_unsigned(inst_bit, Instruction, 32);

  bool *bit_op_code = inst_bit + 25; // 25-31
  bool *rd_code = inst_bit + 20;     // 20-24
  bool *rs_a_code = inst_bit + 12;   // 12-16
  bool *rs_b_code = inst_bit + 7;    // 7-11
  bool *bit_csr_code = inst_bit + 0; // 0-11

  // 准备opcode、funct3、funct7
  uint32_t number_op_code_unsigned = cvt_bit_to_number_unsigned(bit_op_code, 7);
  bool *bit_funct3 = inst_bit + 17; // 3
  uint32_t number_funct3_unsigned = cvt_bit_to_number_unsigned(bit_funct3, 3);
  bool *bit_funct7 = inst_bit + 0; // 7
  uint32_t number_funct7_unsigned = cvt_bit_to_number_unsigned(bit_funct7, 7);

  // 准备寄存器
  uint32_t reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
  uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
  uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  switch (number_op_code_unsigned) {
  case number_0_opcode_lui: { // lui
    state.gpr[reg_d_index] = immU(Instruction);
    break;
  }
  case number_1_opcode_auipc: { // auipc
    bool bit_temp[32];
    state.gpr[reg_d_index] = immU(Instruction) + state.pc;
    break;
  }
  case number_2_opcode_jal: { // jal
    is_br = true;
    br_taken = true;
    next_pc = state.pc + immJ(Instruction);
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_3_opcode_jalr: { // jalr
    is_br = true;
    br_taken = true;
    bool bit_temp[32];
    next_pc = (reg_rdata1 + immI(Instruction)) & 0xFFFFFFFC;
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    is_br = true;
    switch (number_funct3_unsigned) {
    case 0: { // beq
      if (reg_rdata1 == reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 1: { // bne
      if (reg_rdata1 != reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 4: { // blt
      if ((int32_t)reg_rdata1 < (int32_t)reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 5: { // bge
      if ((int32_t)reg_rdata1 >= (int32_t)reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 6: { // bltu
      if ((uint32_t)reg_rdata1 < (uint32_t)reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 7: { // bgeu
      if ((uint32_t)reg_rdata1 >= (uint32_t)reg_rdata2) {
        br_taken = true;
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    }
    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
    cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);

    uint32_t v_addr = reg_rdata1 + immI(Instruction);
    uint32_t p_addr = v_addr;
    if (state.csr[csr_satp] & 0x80000000 && privilege != 3) {
      bool mstatus[32], sstatus[32];
      cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);

      cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);

      page_fault_load = !va2pa(p_addr, v_addr, state.csr[csr_satp], 1, mstatus,
                               sstatus, privilege, memory);
    }

    if (page_fault_load) {
      exception(v_addr);
      return;

    } else {
      uint32_t data = memory[p_addr >> 2];
      uint32_t offset = p_addr & 0b11;
      uint32_t size = number_funct3_unsigned & 0b11;
      uint32_t sign = 0, mask;
      data = data >> (offset * 8);
      if (size == 0) {
        mask = 0xFF;
        if (data & 0x80)
          sign = 0xFFFFFF00;
      } else if (size == 0b01) {
        mask = 0xFFFF;
        if (data & 0x8000)
          sign = 0xFFFF0000;
      } else {
        mask = 0xFFFFFFFF;
      }

      data = data & mask;

      // 有符号数
      if (!(number_funct3_unsigned & 0b100)) {
        data = data | sign;
      }

      if (p_addr == 0x1fd0e000) {
        data = sim_time;
      }
      if (p_addr == 0x1fd0e004) {
        data = 0;
      }

      state.gpr[reg_d_index] = data;
    }
    break;
  }
  case number_6_opcode_sb: { // sb, sh, sw
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);
    cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);

    uint32_t v_addr = reg_rdata1 + immS(Instruction);
    uint32_t p_addr = v_addr;
    if (state.csr[csr_satp] & 0x80000000 && privilege != 3) {
      bool mstatus[32], sstatus[32];
      cvt_number_to_bit_unsigned(mstatus, state.csr[csr_mstatus], 32);

      cvt_number_to_bit_unsigned(sstatus, state.csr[csr_sstatus], 32);

      page_fault_store = !va2pa(p_addr, v_addr, state.csr[csr_satp], 2, mstatus,
                                sstatus, privilege, memory);
    }

    if (page_fault_store) {
      exception(v_addr);
      return;
    } else {

      uint32_t wstrb;
      uint32_t wdata = reg_rdata2;
      if (number_funct3_unsigned == 0b00) {
        wstrb = 0b1;
        state.store_data = wdata & 0xFF;
      } else if (number_funct3_unsigned == 0b01) {
        wstrb = 0b11;
        state.store_data = wdata & 0xFFFF;
      } else {
        wstrb = 0b1111;
        state.store_data = wdata;
      }

      int offset = p_addr & 0x3;
      wstrb = wstrb << offset;
      wdata = wdata << (offset * 8);

      uint32_t old_data = memory[p_addr / 4];
      uint32_t mask = 0;
      if (wstrb & 0b1)
        mask |= 0xFF;
      if (wstrb & 0b10)
        mask |= 0xFF00;
      if (wstrb & 0b100)
        mask |= 0xFF0000;
      if (wstrb & 0b1000)
        mask |= 0xFF000000;
      if ((number_funct3_unsigned == 1 && p_addr % 2 == 1) ||
          (number_funct3_unsigned == 2 && p_addr % 4 != 0)) {
        cout << "Store Memory Address Align Error!!!" << endl;
        cout << "funct3 code: " << dec << number_funct3_unsigned << endl;
        cout << "addr: " << hex << p_addr << endl;
        exit(-1);
      }

      memory[p_addr / 4] = (mask & wdata) | (~mask & old_data);
      state.store = true;
      state.store_addr = p_addr;

      /*if ((p_addr & 0xFFFFFFFC) == (0x8fe06d44)) {*/
      /*  cout << "store data " << hex << state.store_data << " in " << hex*/
      /*       << state.store_addr << " funct3:" << number_funct3_unsigned*/
      /*       << endl;*/
      /*  cout << Instruction << endl;*/
      /*}*/

      if (p_addr == UART_BASE) {
        char temp;
        temp = wdata & 0x000000ff;
        memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xffffff00;
        /*cout << temp;*/
      }

      if (p_addr == 0x10000001 && (wdata & 0x000000ff) == 7) {
        memory[0xc201004 / 4] = 0xa;
        memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xfff0ffff;
      }

      if (p_addr == 0x10000001 && (wdata & 0x000000ff) == 5) {
        memory[0x10000000 / 4] =
            memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
      }

      if (p_addr == 0xc201004 && (wdata & 0x000000ff) == 0xa) {
        memory[0xc201004 / 4] = 0x0;
      }
    }
    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
                               // srli, srai
    switch (number_funct3_unsigned) {
    case 0: { // addi
      state.gpr[reg_d_index] = reg_rdata1 + immI(Instruction);
      break;
    }
    case 2: { // slti
      state.gpr[reg_d_index] =
          (int32_t)reg_rdata1 < (int32_t)immI(Instruction) ? 1 : 0;
      break;
    }
    case 3: { // sltiu
      state.gpr[reg_d_index] =
          (uint32_t)reg_rdata1 < (uint32_t)immI(Instruction) ? 1 : 0;
      break;
    }
    case 4: { // xori
      state.gpr[reg_d_index] = reg_rdata1 ^ immI(Instruction);
      break;
    }
    case 6: { // ori
      state.gpr[reg_d_index] = reg_rdata1 | immI(Instruction);
      break;
    }
    case 7: { // andi
      state.gpr[reg_d_index] = reg_rdata1 & immI(Instruction);
      break;
    }
    case 1: { // slli
      state.gpr[reg_d_index] = reg_rdata1 << immI(Instruction);
      break;
    }
    case 5: { // srli, srai
      switch (number_funct7_unsigned) {
      case 0: { // srli
        if ((*(inst_bit + 6)) == 0) {
          state.gpr[reg_d_index] = (uint32_t)reg_rdata1 >> immI(Instruction);
        }
        break;
      }
      case 32: { // srai
        if ((*(inst_bit + 6)) == 0) {
          state.gpr[reg_d_index] = (int32_t)reg_rdata1 >> immI(Instruction);
        }
        break;
      }
      }
      break;
    }
    }
    break;
  }
  case number_8_opcode_add: { // add, sub, sll, slt, sltu, xor, srl, sra, or,
                              // and
    if (number_funct7_unsigned == 1) { // mul div
      switch (number_funct3_unsigned) {
      case 0: { // mul
        state.gpr[reg_d_index] = (int32_t)reg_rdata1 * (int32_t)reg_rdata2;
        break;
      }
      case 1: { // mulh
        state.gpr[reg_d_index] =
            ((int64_t)reg_rdata1 * (int64_t)reg_rdata2) >> 32;
        break;
      }
      case 2: { // mulsu
        state.gpr[reg_d_index] = ((int32_t)reg_rdata1 * (uint32_t)reg_rdata2);

        break;
      }
      case 3: { // mulhu
        state.gpr[reg_d_index] =
            ((uint64_t)reg_rdata1 * (uint64_t)reg_rdata2) >> 32;
        break;
      }
      case 4: { // div
        state.gpr[reg_d_index] = ((int64_t)reg_rdata1 / (int64_t)reg_rdata2);
        break;
      }
      case 5: { // divu
        state.gpr[reg_d_index] = ((uint64_t)reg_rdata1 / (uint64_t)reg_rdata2);
        break;
      }
      case 6: { // rem
        state.gpr[reg_d_index] = ((int64_t)reg_rdata1 % (int64_t)reg_rdata2);
        break;
      }
      case 7: { // remu
        state.gpr[reg_d_index] = ((uint64_t)reg_rdata1 % (uint64_t)reg_rdata2);
        break;
      }
      default:
        break;
      }
    } else {
      switch (number_funct3_unsigned) {
      case 0: { // add, sub
        switch (number_funct7_unsigned) {
        case 0: { // add
          state.gpr[reg_d_index] = reg_rdata1 + reg_rdata2;
          break;
        }
        case 32: { // sub
          state.gpr[reg_d_index] = reg_rdata1 - reg_rdata2;
          break;
        }
        }
        break;
      }
      case 1: { // sll
        state.gpr[reg_d_index] = reg_rdata1 << reg_rdata2;
        break;
      }
      case 2: { // slt
        state.gpr[reg_d_index] =
            (int32_t)reg_rdata1 < (int32_t)reg_rdata2 ? 1 : 0;
        break;
      }
      case 3: { // sltu
        state.gpr[reg_d_index] =
            (uint32_t)reg_rdata1 < (uint32_t)reg_rdata2 ? 1 : 0;
        break;
      }
      case 4: { // xor
        state.gpr[reg_d_index] = reg_rdata1 ^ reg_rdata2;
        break;
      }
      case 5: { // srl, sra
        switch (number_funct7_unsigned) {
        case 0: { // srl
          state.gpr[reg_d_index] = (uint32_t)reg_rdata1 >> reg_rdata2;
          break;
        }
        case 32: { // sra
          state.gpr[reg_d_index] = (int32_t)reg_rdata1 >> reg_rdata2;
          break;
        }
        }
        break;
      }
      case 6: { // or
        state.gpr[reg_d_index] = reg_rdata1 | reg_rdata2;
        break;
      }
      case 7: { // and
        state.gpr[reg_d_index] = reg_rdata1 & reg_rdata2;
        break;
      }
      }
    }
    break;
  }
  case number_9_opcode_fence: { // fence, fence.i
    break;
  }
  default: {
    break;
  }
  }

  state.pc = next_pc;
}

int cvt_number_to_csr(int csr_idx) {
  int ret;
  switch (csr_idx) {
  case number_mtvec:
    ret = csr_mtvec;
    break;
  case number_mepc:
    ret = csr_mepc;
    break;
  case number_mcause:
    ret = csr_mcause;
    break;
  case number_mie:
    ret = csr_mie;
    break;
  case number_mip:
    ret = csr_mip;
    break;
  case number_mtval:
    ret = csr_mtval;
    break;
  case number_mscratch:
    ret = csr_mscratch;
    break;
  case number_mstatus:
    ret = csr_mstatus;
    break;
  case number_mideleg:
    ret = csr_mideleg;
    break;
  case number_medeleg:
    ret = csr_medeleg;
    break;
  case number_sepc:
    ret = csr_sepc;
    break;
  case number_stvec:
    ret = csr_stvec;
    break;
  case number_scause:
    ret = csr_scause;
    break;
  case number_sscratch:
    ret = csr_sscratch;
    break;
  case number_stval:
    ret = csr_stval;
    break;
  case number_sstatus:
    ret = csr_sstatus;
    break;
  case number_sie:
    ret = csr_sie;
    break;
  case number_sip:
    ret = csr_sip;
    break;
  case number_satp:
    ret = csr_satp;
    break;
  case number_mhartid:
    ret = csr_mhartid;
    break;
  case number_misa:
    ret = csr_misa;
    break;
  case number_time:
    ret = csr_time;
    break;
  case number_timeh:
    ret = csr_timeh;
    break;
  default:
    assert(0);
  }
  return ret;
}
