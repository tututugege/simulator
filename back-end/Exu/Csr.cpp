#include "Csr.h"
#include "DebugPtwTrace.h"
#include "config.h"
#include "ref.h"

namespace {
struct IrqState {
  bool m_software_interrupt = false;
  bool m_timer_interrupt = false;
  bool m_external_interrupt = false;
  bool s_software_interrupt = false;
  bool s_timer_interrupt = false;
  bool s_external_interrupt = false;
};

inline IrqState eval_interrupts(uint32_t mstatus, uint32_t mie_reg,
                                uint32_t mip_reg, uint32_t mideleg,
                                uint32_t privilege) {
  IrqState irq;
  bool mstatus_mie = (mstatus & MSTATUS_MIE) != 0;
  bool mstatus_sie = (mstatus & MSTATUS_SIE) != 0;

  irq.m_software_interrupt = (mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) &&
                             !(mideleg & MIP_MSIP) &&
                             (privilege < 3 || mstatus_mie);
  irq.m_timer_interrupt = (mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) &&
                          !(mideleg & MIP_MTIP) &&
                          (privilege < 3 || mstatus_mie);
  irq.m_external_interrupt = (mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) &&
                             !(mideleg & MIP_MEIP) &&
                             (privilege < 3 || mstatus_mie);

  bool s_irq_enable = (privilege < 1 || (privilege == 1 && mstatus_sie));
  irq.s_software_interrupt =
      (((mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) && (mideleg & MIP_MSIP)) ||
       ((mip_reg & MIP_SSIP) && (mie_reg & MIP_SSIP))) &&
      (privilege < 2 && s_irq_enable);
  irq.s_timer_interrupt =
      (((mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) && (mideleg & MIP_MTIP)) ||
       ((mip_reg & MIP_STIP) && (mie_reg & MIP_STIP))) &&
      (privilege < 2 && s_irq_enable);
  irq.s_external_interrupt =
      (((mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) && (mideleg & MIP_MEIP)) ||
       ((mip_reg & MIP_SEIP) && (mie_reg & MIP_SEIP))) &&
      (privilege < 2 && s_irq_enable);

  return irq;
}
} // namespace

void Csr::init() {
  CSR_RegFile[csr_misa] = 0x40141103;   // U/S/M  RV32I/A/M/B
  CSR_RegFile_1[csr_misa] = 0x40141103; // U/S/M  RV32I/A/M/B
}

void Csr::comb_begin() {
  for (int i = 0; i < CSR_NUM; i++) {
    CSR_RegFile_1[i] = CSR_RegFile[i];
  }
  privilege_1 = privilege;
  csr_idx_1 = csr_idx;
  csr_wdata_1 = csr_wdata;
  csr_wcmd_1 = csr_wcmd;
  csr_we_1 = csr_we;
}

void Csr::comb_csr_status() {
  out.csr_status->mstatus = CSR_RegFile[csr_mstatus];
  out.csr_status->sstatus = CSR_RegFile[csr_sstatus];
  out.csr_status->satp = CSR_RegFile[csr_satp];
  out.csr_status->privilege = privilege;
}

void Csr::comb_csr_read() {
  if (in.exe2csr->re) {
    out.csr2exe->rdata = CSR_RegFile[cvt_number_to_csr(in.exe2csr->idx)];
  }
}

void Csr::comb_interrupt() {
  uint32_t mstatus = CSR_RegFile[csr_mstatus];
  uint32_t mie_reg = CSR_RegFile[csr_mie];
  uint32_t mip_reg = CSR_RegFile[csr_mip];
  uint32_t mideleg = CSR_RegFile[csr_mideleg];
  IrqState irq = eval_interrupts(mstatus, mie_reg, mip_reg, mideleg, privilege);
  out.csr2rob->interrupt_req =
      irq.m_software_interrupt || irq.m_timer_interrupt ||
      irq.m_external_interrupt || irq.s_software_interrupt ||
      irq.s_timer_interrupt || irq.s_external_interrupt;
}

void Csr::comb_exception() {
  uint32_t mstatus = CSR_RegFile[csr_mstatus];
  uint32_t sstatus = CSR_RegFile[csr_sstatus];
  uint32_t mie_reg = CSR_RegFile[csr_mie];
  uint32_t mip_reg = CSR_RegFile[csr_mip];
  uint32_t mideleg = CSR_RegFile[csr_mideleg];
  uint32_t medeleg = CSR_RegFile[csr_medeleg];
  uint32_t mtvec = CSR_RegFile[csr_mtvec];
  uint32_t stvec = CSR_RegFile[csr_stvec];

  bool ecall = in.rob_bcast->ecall;
  bool page_fault_inst = in.rob_bcast->page_fault_inst;
  bool page_fault_load = in.rob_bcast->page_fault_load;
  bool page_fault_store = in.rob_bcast->page_fault_store;
  bool illegal_exceptinn = in.rob_bcast->illegal_inst;

  // 异常委托位 (Exceptions)
  bool medeleg_U_ecall = (medeleg >> 8) & 1;
  bool medeleg_S_ecall = (medeleg >> 9) & 1;
  // bool medeleg_M_ecall = (medeleg >> 11) & 1; // 通常机器模式 ECALL 不会委托

  bool medeleg_page_fault_inst = (medeleg >> 12) & 1;
  bool medeleg_page_fault_load = (medeleg >> 13) & 1;
  bool medeleg_page_fault_store = (medeleg >> 15) & 1;
  bool medeleg_illegal_inst = (medeleg >> 2) & 1;

  IrqState irq = eval_interrupts(mstatus, mie_reg, mip_reg, mideleg, privilege);
  bool S_interrupt_resp = (irq.s_software_interrupt || irq.s_timer_interrupt ||
                           irq.s_external_interrupt) &&
                          in.rob2csr->interrupt_resp;
  bool M_interrupt_resp = (irq.m_software_interrupt || irq.m_timer_interrupt ||
                           irq.m_external_interrupt) &&
                          in.rob2csr->interrupt_resp;

  bool MTrap =
      M_interrupt_resp || (ecall && (privilege == 0) && !medeleg_U_ecall) ||
      (ecall && (privilege == 1) && !medeleg_S_ecall) ||
      (ecall && (privilege == 3)) ||
      (page_fault_inst && !medeleg_page_fault_inst) ||
      (page_fault_load && !medeleg_page_fault_load) ||
      (page_fault_store && !medeleg_page_fault_store) ||
      (illegal_exceptinn && !medeleg_illegal_inst);

  bool STrap = S_interrupt_resp ||
               (ecall && (privilege == 0) && medeleg_U_ecall) ||
               (ecall && (privilege == 1) && medeleg_S_ecall) ||
               (page_fault_inst && medeleg_page_fault_inst) ||
               (page_fault_load && medeleg_page_fault_load) ||
               (page_fault_store && medeleg_page_fault_store) ||
               (illegal_exceptinn && medeleg_illegal_inst);

  if (MTrap) {
    CSR_RegFile_1[csr_mepc] = in.rob_bcast->pc;

    // next_mcause = interruptType;
    uint32_t cause =
        (irq.m_software_interrupt || irq.m_timer_interrupt ||
         irq.m_external_interrupt)
            ? 1U << 31
            : 0;

    cause += irq.m_software_interrupt ? 3
             : irq.m_timer_interrupt  ? 7
             : (irq.m_external_interrupt ||
                (ecall && (privilege == 3) && !medeleg_U_ecall))
                 ? 11
             /*: illegal_exceptinn                                  ? 2*/
             : (ecall && (privilege == 0) && !medeleg_U_ecall) ? 8
             : (ecall && (privilege == 1) && !medeleg_S_ecall) ? 9
             : (page_fault_inst && !medeleg_page_fault_inst)   ? 12
             : (page_fault_load && !medeleg_page_fault_load)   ? 13
             : (page_fault_store && !medeleg_page_fault_store) ? 15
             : (illegal_exceptinn)                             ? 2
                                   : 0; // 给后 31 位赋值

    CSR_RegFile_1[csr_mcause] = cause;

    if ((mtvec & 1) && (cause & (1u << 31))) {
      out.csr2front->trap_pc = CSR_RegFile[csr_mtvec] & 0xfffffffc;
      out.csr2front->trap_pc += 4 * (cause & 0x7fffffff);
    } else {
      out.csr2front->trap_pc = CSR_RegFile[csr_mtvec];
    }

    mstatus = (mstatus & ~MSTATUS_MPP) | ((privilege & 0x3) << 11);
    // MPIE = MIE
    if (mstatus & MSTATUS_MIE)
      mstatus |= MSTATUS_MPIE;
    else
      mstatus &= ~MSTATUS_MPIE;
    // MIE = 0
    mstatus &= ~MSTATUS_MIE;

    // 同步 sstatus (sstatus 是 mstatus 的影子)
    uint32_t sstatus_mask = 0x800DE133;
    CSR_RegFile_1[csr_mstatus] = mstatus;
    CSR_RegFile_1[csr_sstatus] = mstatus & sstatus_mask;

    privilege_1 = RISCV_MODE_M; // 机器模式 (Machine Mode)

    if (page_fault_store || page_fault_load || page_fault_inst) {
      CSR_RegFile_1[csr_mtval] = in.rob_bcast->trap_val;
    } else if (illegal_exceptinn) {
      CSR_RegFile_1[csr_mtval] = in.rob_bcast->trap_val;
    } else {
      CSR_RegFile_1[csr_mtval] = 0;
    }

  } else if (STrap) {
    CSR_RegFile_1[csr_sepc] = in.rob_bcast->pc;
    uint32_t cause =
        (irq.s_software_interrupt || irq.s_timer_interrupt ||
         irq.s_external_interrupt)
            ? 1U << 31
            : 0;

    cause +=
        (irq.s_external_interrupt || (ecall && (privilege == 1) && medeleg_S_ecall))
            ? 9
        : irq.s_timer_interrupt                          ? 5
        : (ecall && (privilege == 0) && medeleg_U_ecall) ? 8
        : irq.s_software_interrupt                       ? 1
        : (page_fault_inst && medeleg_page_fault_inst)   ? 12
        : (page_fault_load && medeleg_page_fault_load)   ? 13
        : (page_fault_store && medeleg_page_fault_store) ? 15
        : (illegal_exceptinn && medeleg_illegal_inst)    ? 2
                                                         : 0; // 给后 31 位赋值

    CSR_RegFile_1[csr_scause] = cause;

    if ((stvec & 1) && (cause & (1u << 31))) {
      out.csr2front->trap_pc = CSR_RegFile[csr_stvec] & 0xfffffffc;
      out.csr2front->trap_pc += 4 * (cause & 0x7fffffff);
    } else {
      out.csr2front->trap_pc = CSR_RegFile[csr_stvec];
    }

    // 更新 sstatus
    // SPP = privilege
    if (privilege == 1)
      sstatus |= MSTATUS_SPP;
    else
      sstatus &= ~MSTATUS_SPP;

    // SPIE = SIE
    if (sstatus & MSTATUS_SIE)
      sstatus |= MSTATUS_SPIE;
    else
      sstatus &= ~MSTATUS_SPIE;

    // SIE = 0
    sstatus &= ~MSTATUS_SIE;

    // 写回
    uint32_t sstatus_mask = 0x800DE133;
    CSR_RegFile_1[csr_sstatus] = sstatus;
    CSR_RegFile_1[csr_mstatus] =
        (CSR_RegFile[csr_mstatus] & ~sstatus_mask) | (sstatus & sstatus_mask);

    privilege_1 = 1; // 监管者模式 (Supervisor Mode)

    if (page_fault_store || page_fault_load || page_fault_inst ||
        illegal_exceptinn) {
      CSR_RegFile_1[csr_stval] = in.rob_bcast->trap_val;
    } else {
      CSR_RegFile_1[csr_stval] = 0;
    }

  } else if (in.rob_bcast->mret) {
    // MIE = MPIE
    if (mstatus & MSTATUS_MPIE)
      mstatus |= MSTATUS_MIE;
    else
      mstatus &= ~MSTATUS_MIE;

    // Privilege = MPP
    privilege_1 = GET_MPP(mstatus);

    // MPIE = 1
    mstatus |= MSTATUS_MPIE;
    // MPP = U (0)
    mstatus &= ~MSTATUS_MPP;

    // 同步 sstatus
    uint32_t sstatus_mask = 0x800DE133;
    CSR_RegFile_1[csr_mstatus] = mstatus;
    CSR_RegFile_1[csr_sstatus] = mstatus & sstatus_mask;
    out.csr2front->epc = CSR_RegFile[csr_mepc];
  } else if (in.rob_bcast->sret) {
    // SIE = SPIE
    if (sstatus & MSTATUS_SPIE)
      sstatus |= MSTATUS_SIE;
    else
      sstatus &= ~MSTATUS_SIE;

    // Privilege = SPP
    privilege_1 = GET_SPP(sstatus);

    // SPIE = 1
    sstatus |= MSTATUS_SPIE;
    // SPP = U (0)
    sstatus &= ~MSTATUS_SPP;

    uint32_t sstatus_mask = 0x800DE133;
    CSR_RegFile_1[csr_sstatus] = sstatus;
    CSR_RegFile_1[csr_mstatus] =
        (CSR_RegFile[csr_mstatus] & ~sstatus_mask) | (sstatus & sstatus_mask);

    out.csr2front->epc = CSR_RegFile[csr_sepc];
  }
}

void Csr::comb_csr_write() {
  if (in.exe2csr->we) {
    csr_we_1 = in.exe2csr->we;
    csr_idx_1 = in.exe2csr->idx;
    csr_wcmd_1 = in.exe2csr->wcmd;
    csr_wdata_1 = in.exe2csr->wdata;
  }

  if (in.rob2csr->commit && csr_we) {
    if (csr_wcmd == CSR_S) {
      csr_wdata = (csr_wdata | CSR_RegFile[cvt_number_to_csr(csr_idx)]);
    } else if (csr_wcmd == CSR_C) {
      csr_wdata = (~csr_wdata & CSR_RegFile[cvt_number_to_csr(csr_idx)]);
    }

    if (csr_idx == number_mie || csr_idx == number_sie) {
      uint32_t mie_mask = 0x00000bbb;
      uint32_t sie_mask = 0x00000333;

      if (csr_idx == number_sie) {
        CSR_RegFile_1[csr_mie] =
            (CSR_RegFile[csr_mie] & ~sie_mask) | (csr_wdata & sie_mask);
      } else {
        CSR_RegFile_1[csr_mie] = csr_wdata & mie_mask;
      }
      CSR_RegFile_1[csr_sie] = CSR_RegFile_1[csr_mie] & sie_mask;
    } else if (csr_idx == number_mip || csr_idx == number_sip) {
      uint32_t mip_mask = 0x00000bbb;
      uint32_t sip_mask = 0x00000333;

      if (csr_idx == number_sip) {
        CSR_RegFile_1[csr_mip] =
            (CSR_RegFile[csr_mip] & ~sip_mask) | (csr_wdata & sip_mask);
      } else {
        CSR_RegFile_1[csr_mip] = csr_wdata & mip_mask;
      }

      CSR_RegFile_1[csr_sip] = CSR_RegFile_1[csr_mip] & sip_mask;
    } else if (csr_idx == number_mstatus || csr_idx == number_sstatus) {

      uint32_t mstatus_mask = 0x807FF9BB;
      uint32_t sstatus_mask = 0x800DE133;

      if (csr_idx == number_sstatus) {
        CSR_RegFile_1[csr_mstatus] = (CSR_RegFile[csr_mstatus] & ~sstatus_mask) |
                                     (csr_wdata & sstatus_mask);
      } else {
        CSR_RegFile_1[csr_mstatus] = (CSR_RegFile[csr_mstatus] & ~mstatus_mask) |
                                     (csr_wdata & mstatus_mask);
      }

      CSR_RegFile_1[csr_sstatus] = CSR_RegFile_1[csr_mstatus] & sstatus_mask;

    } else if (csr_idx == number_satp) {
      const uint32_t old_satp = CSR_RegFile[csr_satp];
      CSR_RegFile_1[csr_satp] = csr_wdata;
      if (old_satp != csr_wdata) {
        debug_ptw_trace::record_satp_write(
            old_satp, static_cast<uint32_t>(csr_wdata),
            static_cast<uint8_t>(privilege));
      }
    } else {
      CSR_RegFile_1[cvt_number_to_csr(csr_idx)] = csr_wdata;
    }

    csr_we_1 = false;
  }
}

void Csr::seq() {
  for (int i = 0; i < CSR_NUM; i++) {
    CSR_RegFile[i] = CSR_RegFile_1[i];
  }

  csr_idx = csr_idx_1;
  csr_wdata = csr_wdata_1;
  csr_wcmd = csr_wcmd_1;
  csr_we = csr_we_1;
  privilege = privilege_1;
}
