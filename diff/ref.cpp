#include "ref.h"
#include "RISCV.h"
#include "SimCpu.h"
#include "config.h"
#include <cstdint>
#include <iostream>
extern "C" {
#include "softfloat.h"
}

// ---------------- 辅助工具 ----------------
static inline float32_t to_f32(uint32_t v) {
  float32_t f;
  f.v = v;
  return f;
}

static inline uint32_t from_f32(float32_t f) { return f.v; }

static inline bool is_nan(uint32_t v) {
  // 指数全1 (0xFF)，尾数不为0
  return ((v & 0x7F800000) == 0x7F800000) && ((v & 0x007FFFFF) != 0);
}

static inline bool is_snan(uint32_t v) {
  // 是NaN，且尾数最高位(bit 22)为0
  return is_nan(v) && !((v >> 22) & 1);
}

// RISC-V 规范的 FMIN 实现
uint32_t f32_min_riscv(uint32_t a, uint32_t b) {
  // 1. 处理 sNaN 异常 (Invalid Operation)
  if (is_snan(a) || is_snan(b)) {
    softfloat_exceptionFlags |= softfloat_flag_invalid;
  }

  bool a_nan = is_nan(a);
  bool b_nan = is_nan(b);

  // 2. NaN 处理规则
  if (a_nan && b_nan)
    return 0x7fc00000; // Canonical NaN
  if (a_nan)
    return b; // 如果 A 是 NaN，返回 B (即使 B 也是 NaN 的情况上面已处理)
  if (b_nan)
    return a; // 如果 B 是 NaN，返回 A

  // 3. 数值比较
  float32_t fa = to_f32(a);
  float32_t fb = to_f32(b);

  // 注意：f32_lt 对 -0.0 和 +0.0 返回 false (视为相等)
  if (f32_lt(fa, fb))
    return a;
  if (f32_lt(fb, fa))
    return b;

  // 4. 处理相等的情况 (主要是 -0.0 和 +0.0)
  // FMIN(-0.0, +0.0) -> -0.0
  // 原理：(10..0) | (00..0) = (10..0)
  return a | b;
}

// RISC-V 规范的 FMAX 实现
uint32_t f32_max_riscv(uint32_t a, uint32_t b) {
  // 1. 处理 sNaN 异常
  if (is_snan(a) || is_snan(b)) {
    softfloat_exceptionFlags |= softfloat_flag_invalid;
  }

  bool a_nan = is_nan(a);
  bool b_nan = is_nan(b);

  // 2. NaN 处理规则
  if (a_nan && b_nan)
    return 0x7fc00000;
  if (a_nan)
    return b;
  if (b_nan)
    return a;

  // 3. 数值比较
  float32_t fa = to_f32(a);
  float32_t fb = to_f32(b);

  if (f32_lt(fa, fb))
    return b; // A < B, 所以 Max 是 B
  if (f32_lt(fb, fa))
    return a;

  // 4. 处理相等的情况
  // FMAX(-0.0, +0.0) -> +0.0
  // 原理：(10..0) & (00..0) = (00..0)
  return a & b;
}

uint32_t f32_classify(float32_t f) {
  uint32_t bits = from_f32(f);
  uint32_t sign = (bits >> 31) & 1;
  uint32_t exp = (bits >> 23) & 0xFF;
  uint32_t mant = bits & 0x7FFFFF;

  bool is_subnormal = (exp == 0) && (mant != 0);
  bool is_zero = (exp == 0) && (mant == 0);
  bool is_inf = (exp == 0xFF) && (mant == 0);
  bool is_nan = (exp == 0xFF) && (mant != 0);
  bool is_snan = is_nan && !((mant >> 22) & 1); // MSB of mantissa is 0
  bool is_qnan = is_nan && ((mant >> 22) & 1);  // MSB of mantissa is 1

  uint32_t res = 0;

  if (is_inf && sign)
    res |= (1 << 0); // -inf
  else if (!is_inf && !is_zero && !is_nan && !is_subnormal && sign)
    res |= (1 << 1); // -normal
  else if (is_subnormal && sign)
    res |= (1 << 2); // -subnormal
  else if (is_zero && sign)
    res |= (1 << 3); // -0
  else if (is_zero && !sign)
    res |= (1 << 4); // +0
  else if (is_subnormal && !sign)
    res |= (1 << 5); // +subnormal
  else if (!is_inf && !is_zero && !is_nan && !is_subnormal && !sign)
    res |= (1 << 6); // +normal
  else if (is_inf && !sign)
    res |= (1 << 7); // +inf

  if (is_snan)
    res |= (1 << 8);
  if (is_qnan)
    res |= (1 << 9);

  return res;
}

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
  illegal_exception = page_fault_load = page_fault_inst = page_fault_store =
      asy = false;
  state.store = false;

  uint32_t p_addr = state.pc;

  if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
    if (fast_run) {
      page_fault_inst = !va2pa(p_addr, state.pc, 0);
    } else {

#ifdef CONFIG_MMU
      page_fault_inst = !va2pa_fixed(p_addr, state.pc, 0);
#else
      page_fault_inst = !va2pa(p_addr, state.pc, 0);
#endif
    }

    if (page_fault_inst) {
      exception(state.pc);
      return;
    } else {
      Instruction = memory[p_addr >> 2];
    }
  } else {
    Instruction = memory[p_addr >> 2];
  }

  if (Instruction == INST_EBREAK) {
    state.pc += 4;
#ifdef CONFIG_RUN_REF
    cout << "sim_time: " << sim_time << endl;
    sim_end = true;
    exit(0);
#else
    return;
#endif
  }
  RISCV();
}

void Ref_cpu::exception(uint32_t trap_val) {
  uint32_t next_pc = state.pc + 4;

  // 重新获取当前状态（因为exec可能没传进来最新的）
  bool ecall = (Instruction == INST_ECALL);
  bool mret = (Instruction == INST_MRET);
  bool sret = (Instruction == INST_SRET);

  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t sstatus = state.csr[csr_sstatus];
  uint32_t medeleg = state.csr[csr_medeleg];
  uint32_t mtvec = state.csr[csr_mtvec];
  uint32_t stvec = state.csr[csr_stvec];

  // 再次计算 Trap 原因 (与 RISCV() 中逻辑一致，但这里是为了确定是用 MTrap 还是
  // STrap 处理)
  // 注意：为了代码复用，这里其实可以简化，但为了保持你原有逻辑结构：

  bool medeleg_U_ecall = (medeleg >> 8) & 1;
  bool medeleg_S_ecall = (medeleg >> 9) & 1;
  bool medeleg_page_fault_inst = (medeleg >> 12) & 1;
  bool medeleg_page_fault_load = (medeleg >> 13) & 1;
  bool medeleg_page_fault_store = (medeleg >> 15) & 1;

  // 这里直接复用成员变量里的中断状态 (假设RISCV函数刚跑完，状态是新的)
  // 如果不是，需要重新计算 M_software_interrupt 等

  bool MTrap =
      (M_software_interrupt) || (M_timer_interrupt) || (M_external_interrupt) ||
      ((privilege == 0) && !medeleg_U_ecall && ecall) ||
      (ecall && (privilege == 1) && !medeleg_S_ecall) ||
      (ecall && (privilege == 3)) ||
      (page_fault_inst && !medeleg_page_fault_inst) ||
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
    uint32_t cause = 0;

    // 计算 MCause
    bool is_interrupt =
        M_software_interrupt || M_timer_interrupt || M_external_interrupt;
    if (is_interrupt)
      cause |= (1u << 31);

    uint32_t exception_code = 0;
    if (M_software_interrupt)
      exception_code = 3;
    else if (M_timer_interrupt)
      exception_code = 7;
    else if (M_external_interrupt ||
             (ecall && privilege == 3 && !medeleg_U_ecall))
      exception_code = 11;
    else if (ecall && privilege == 0 && !medeleg_U_ecall)
      exception_code = 8;
    else if (ecall && privilege == 1 && !medeleg_S_ecall)
      exception_code = 9;
    else if (page_fault_inst && !medeleg_page_fault_inst)
      exception_code = 12;
    else if (page_fault_load && !medeleg_page_fault_load)
      exception_code = 13;
    else if (page_fault_store && !medeleg_page_fault_store)
      exception_code = 15;
    else if (illegal_exception)
      exception_code = 2;

    cause |= exception_code;
    state.csr[csr_mcause] = cause;

    // 向量中断跳转
    if ((mtvec & 1) && (cause & (1u << 31))) {
      next_pc = (mtvec & 0xfffffffc) + 4 * (cause & 0x7fffffff);
    } else {
      next_pc =
          mtvec & 0xfffffffc; // 这里的MASK可能需要根据Spec确认，通常是清除低2位
    }

    // 更新 mstatus
    // MPP = privilege
    mstatus = (mstatus & ~MSTATUS_MPP) | ((privilege & 0x3) << 11);
    // MPIE = MIE
    if (mstatus & MSTATUS_MIE)
      mstatus |= MSTATUS_MPIE;
    else
      mstatus &= ~MSTATUS_MPIE;
    // MIE = 0
    mstatus &= ~MSTATUS_MIE;

    // 同步 sstatus (sstatus 是 mstatus 的影子)
    state.csr[csr_mstatus] = mstatus;
    state.csr[csr_sstatus] = mstatus;

    privilege = 3; // Machine Mode
    state.csr[csr_mtval] = trap_val;

  } else if (STrap) {
    state.csr[csr_sepc] = state.pc;
    uint32_t cause = 0;

    bool is_interrupt =
        S_software_interrupt || S_timer_interrupt || S_external_interrupt;
    if (is_interrupt)
      cause |= (1u << 31);

    uint32_t exception_code = 0;
    if (S_external_interrupt || (ecall && privilege == 1 && medeleg_S_ecall))
      exception_code = 9;
    else if (S_timer_interrupt)
      exception_code = 5;
    else if (ecall && privilege == 0 && medeleg_U_ecall)
      exception_code = 8;
    else if (S_software_interrupt)
      exception_code = 1;
    else if (page_fault_inst && medeleg_page_fault_inst)
      exception_code = 12;
    else if (page_fault_load && medeleg_page_fault_load)
      exception_code = 13;
    else if (page_fault_store && medeleg_page_fault_store)
      exception_code = 15;

    cause |= exception_code;
    state.csr[csr_scause] = cause;

    if ((stvec & 1) && (cause & (1u << 31))) {
      next_pc = (stvec & 0xfffffffc) + 4 * (cause & 0x7fffffff);
    } else {
      next_pc = stvec & 0xfffffffc;
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
    state.csr[csr_sstatus] = sstatus;
    state.csr[csr_mstatus] = sstatus;

    privilege = 1; // Supervisor Mode
    state.csr[csr_stval] = trap_val;

  } else if (mret) {
    // MIE = MPIE
    if (mstatus & MSTATUS_MPIE)
      mstatus |= MSTATUS_MIE;
    else
      mstatus &= ~MSTATUS_MIE;

    // Privilege = MPP
    privilege = GET_MPP(mstatus);

    // MPIE = 1
    mstatus |= MSTATUS_MPIE;
    // MPP = U (0)
    mstatus &= ~MSTATUS_MPP;

    // 同步 sstatus
    state.csr[csr_mstatus] = mstatus;
    state.csr[csr_sstatus] = mstatus;

    next_pc = state.csr[csr_mepc];

  } else if (sret) {
    // SIE = SPIE
    if (sstatus & MSTATUS_SPIE)
      sstatus |= MSTATUS_SIE;
    else
      sstatus &= ~MSTATUS_SIE;

    // Privilege = SPP
    privilege = GET_SPP(sstatus);

    // SPIE = 1
    sstatus |= MSTATUS_SPIE;
    // SPP = U (0)
    sstatus &= ~MSTATUS_SPP;

    state.csr[csr_sstatus] = sstatus;
    state.csr[csr_mstatus] = sstatus;

    next_pc = state.csr[csr_sepc];
  }

  state.pc = next_pc;
}

void Ref_cpu::RISCV() {
  // === 优化 1: 极速解码 ===
  // 使用 BITS 宏直接提取字段，完全替代 bool 数组操作
  uint32_t opcode = BITS(Instruction, 6, 0);

  bool ecall = (Instruction == INST_ECALL);
  bool mret = (Instruction == INST_MRET);
  bool sret = (Instruction == INST_SRET);

  // === 优化 2: 快速读取 CSR 状态 ===
  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t mie_reg = state.csr[csr_mie];
  uint32_t mip_reg = state.csr[csr_mip];
  uint32_t mideleg = state.csr[csr_mideleg];
  uint32_t medeleg = state.csr[csr_medeleg];

  // 提取关键位
  bool mstatus_mie = (mstatus & MSTATUS_MIE) != 0;
  bool mstatus_sie = (mstatus & MSTATUS_SIE) != 0;

  // 异常委托位 (Exceptions)
  bool medeleg_U_ecall = (medeleg >> 8) & 1;
  bool medeleg_S_ecall = (medeleg >> 9) & 1;
  // bool medeleg_M_ecall = (medeleg >> 11) & 1; // 通常M-ecall不委托

  bool medeleg_page_fault_inst = (medeleg >> 12) & 1;
  bool medeleg_page_fault_load = (medeleg >> 13) & 1;
  bool medeleg_page_fault_store = (medeleg >> 15) & 1;

  // === 优化 3: 中断判断逻辑 (位运算) ===
  // M-mode 中断条件:Pending & Enabled & NotDelegated & (CurrentPriv < M ||
  // MIE=1)

  // Software Interrupts
  M_software_interrupt = (mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) &&
                         !(mideleg & MIP_MSIP) &&
                         (privilege < 3 || mstatus_mie);

  // Timer Interrupts
  M_timer_interrupt = (mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) &&
                      !(mideleg & MIP_MTIP) && (privilege < 3 || mstatus_mie);

  // External Interrupts
  M_external_interrupt = (mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) &&
                         !(mideleg & MIP_MEIP) &&
                         (privilege < 3 || mstatus_mie);

  // S-mode 中断条件: Pending & Enabled & Delegated & (CurrentPriv < S || SIE=1)
  // 注意：privilege < 2 (S-mode=1, U-mode=0) 意味着当前是 U 或 S
  bool s_irq_enable = (privilege < 1 || (privilege == 1 && mstatus_sie));

  S_software_interrupt =
      (((mip_reg & MIP_MSIP) && (mie_reg & MIP_MSIP) && (mideleg & MIP_MSIP)) ||
       ((mip_reg & MIP_SSIP) && (mie_reg & MIP_SSIP))) &&
      (privilege < 2 && s_irq_enable);

  S_timer_interrupt =
      (((mip_reg & MIP_MTIP) && (mie_reg & MIP_MTIP) && (mideleg & MIP_MTIP)) ||
       ((mip_reg & MIP_STIP) && (mie_reg & MIP_STIP))) &&
      (privilege < 2 && s_irq_enable);

  S_external_interrupt =
      (((mip_reg & MIP_MEIP) && (mie_reg & MIP_MEIP) && (mideleg & MIP_MEIP)) ||
       ((mip_reg & MIP_SEIP) && (mie_reg & MIP_SEIP))) &&
      (privilege < 2 && s_irq_enable);

  // Trap 判断
  bool MTrap =
      M_software_interrupt || M_timer_interrupt || M_external_interrupt ||
      ((privilege == 0) && !medeleg_U_ecall && ecall) || // ecall from U
      ((privilege == 1) && !medeleg_S_ecall && ecall) || // ecall from S
      ((privilege == 3) && ecall) ||                     // ecall from M
      (page_fault_inst && !medeleg_page_fault_inst) || illegal_exception;

  bool STrap = S_software_interrupt || S_timer_interrupt ||
               S_external_interrupt ||
               ((privilege == 0) && medeleg_U_ecall && ecall) ||
               ((privilege == 1) && medeleg_S_ecall && ecall) ||
               (page_fault_inst && medeleg_page_fault_inst);

  asy = MTrap || STrap || mret || sret;

  // WFI 检查 (简单处理)
  if (Instruction == INST_WFI && !asy && !page_fault_inst && !page_fault_load &&
      !page_fault_store) {
    std::cout << " WFI " << std::endl;
    exit(1);
  }

  if (page_fault_inst) {
    exception(state.pc);
  } else if (illegal_exception) {
    exception(Instruction);
  } else if (asy || Instruction == INST_ECALL) {
    exception(0);
  } else if (opcode == number_10_opcode_ecall) {
    // SYSTEM 指令 (CSR, WFI, MRET等)
    RV32CSR();
  } else if (opcode == number_11_opcode_lrw) {
    RV32A();
  } else if (opcode == number_12_opcode_float ||
             opcode == number_13_opcode_fmadd ||
             opcode == number_15_opcode_fnmsub ||
             opcode == number_16_opcode_fnmadd ||
             opcode == number_14_opcode_fmsub) {
    RV32Zfinx();
  } else {
    RV32IM();
  }
  state.gpr[0] = 0;
}

void Ref_cpu::RV32Zfinx() {

  uint32_t next_pc = state.pc + 4;
  // 1. 解码基础字段
  uint32_t opcode = Instruction & 0x7F;
  uint32_t rd = (Instruction >> 7) & 0x1F;
  uint32_t funct3 = (Instruction >> 12) & 0x07;
  uint32_t rs1 = (Instruction >> 15) & 0x1F;
  uint32_t rs2 = (Instruction >> 20) & 0x1F;
  uint32_t funct7 = (Instruction >> 25) & 0x7F;
  uint32_t rs3 = (Instruction >> 27) & 0x1F; // 用于 Fused Multiply-Add

  uint32_t val_rs1 = state.gpr[rs1];
  uint32_t val_rs2 = state.gpr[rs2];
  uint32_t val_rs3 = state.gpr[rs3]; // 仅用于 FMADD 等指令

  // 2. 处理舍入模式 (RM)
  // RISC-V RM 编码: 0=RNE, 1=RTZ, 2=RDN, 3=RUP, 4=RMM, 7=DYN
  uint8_t rm = funct3;
  if (rm == 7) {
    // 这里暂时硬编码
    rm = 0;
  }

  switch (rm) {
  case 0:
    softfloat_roundingMode = softfloat_round_near_even;
    break;
  case 1:
    softfloat_roundingMode = softfloat_round_minMag;
    break; // RTZ
  case 2:
    softfloat_roundingMode = softfloat_round_min;
    break; // RDN
  case 3:
    softfloat_roundingMode = softfloat_round_max;
    break; // RUP
  case 4:
    softfloat_roundingMode = softfloat_round_near_maxMag;
    break; // RMM
  default:
    illegal_exception = true;
    return;
  }

  // 清除 SoftFloat 异常标志，以便捕获本次指令的异常
  softfloat_exceptionFlags = 0;

  float32_t f_rs1 = to_f32(val_rs1);
  float32_t f_rs2 = to_f32(val_rs2);
  float32_t f_rs3 = to_f32(val_rs3);
  float32_t f_res;
  uint32_t i_res = 0;
  bool write_rd = true; // 大多数指令写回 rd

  // 3. 执行指令
  // Zfinx 复用了浮点操作码，但也包括 Fused-MA 指令
  switch (opcode) {
  case 0x53: // OP-FP (计算类)
    switch (funct7) {
    case 0x00: // FADD.S
      f_res = f32_add(f_rs1, f_rs2);
      i_res = from_f32(f_res);
      break;
    case 0x04: // FSUB.S
      f_res = f32_sub(f_rs1, f_rs2);
      i_res = from_f32(f_res);
      break;
    case 0x08: // FMUL.S
      f_res = f32_mul(f_rs1, f_rs2);
      i_res = from_f32(f_res);
      break;
    case 0x0C: // FDIV.S
      f_res = f32_div(f_rs1, f_rs2);
      i_res = from_f32(f_res);
      break;
    case 0x2C: // FSQRT.S (rs2 必须为 0)
      if (rs2 != 0) {
        illegal_exception = true;
        return;
      }
      f_res = f32_sqrt(f_rs1);
      i_res = from_f32(f_res);
      break;

    // 注入符号 (FSGNJ) - 纯位操作，不触发异常
    case 0x10:
      if (funct3 == 0) // FSGNJ.S
        i_res = (val_rs1 & ~0x80000000) | (val_rs2 & 0x80000000);
      else if (funct3 == 1) // FSGNJN.S
        i_res = (val_rs1 & ~0x80000000) | (~val_rs2 & 0x80000000);
      else if (funct3 == 2) // FSGNJX.S
        i_res = val_rs1 ^ (val_rs2 & 0x80000000);
      else
        illegal_exception = true;
      // FSGNJ 系列不更新 fflags，也不受 rm 影响
      goto skip_flags_update;

    // 比较 (Compare) - 结果写回整数寄存器 (0 或 1)
    case 0x50:
      if (funct3 == 2) // FEQ.S
        i_res = f32_eq(f_rs1, f_rs2);
      else if (funct3 == 1) // FLT.S
        i_res = f32_lt(f_rs1, f_rs2);
      else if (funct3 == 0) // FLE.S
        i_res = f32_le(f_rs1, f_rs2);
      else
        illegal_exception = true;
      break;

    // 最小/最大 (Min/Max)
    case 0x14:
      if (funct3 == 0) { // FMIN.S
        i_res = f32_min_riscv(val_rs1, val_rs2);
      } else if (funct3 == 1) { // FMAX.S
        i_res = f32_max_riscv(val_rs1, val_rs2);
      } else {
        illegal_exception = true;
      }
      break;
    // 转换 (Convert)
    case 0x60: // FCVT.W.S (Float to Int32)
      if (rs2 == 0)
        i_res = (uint32_t)f32_to_i32(f_rs1, softfloat_roundingMode, true);
      else if (rs2 == 1)
        i_res = f32_to_ui32(f_rs1, softfloat_roundingMode, true);
      else
        illegal_exception = true;
      break;

    case 0x68: // FCVT.S.W (Int32 to Float)
      if (rs2 == 0)
        f_res = i32_to_f32((int32_t)val_rs1);
      else if (rs2 == 1)
        f_res = ui32_to_f32(val_rs1);
      else {
        illegal_exception = true;
        return;
      }
      i_res = from_f32(f_res);
      break;

      // Zfinx 特殊说明：
      // FMV.X.W (Move float bits to int) 和 FMV.W.X 在 Zfinx 中通常是 NOP
      // 或者普通的整数 MV， 因为 float 和 int 在同一个寄存器堆里。
      // opcode 0x70,
      // funct3 0 (FMV.X.W) -> Class check in standard, move in others. 在Zfinx
      // 中，opcode 0x53, funct7 0x70, rs2=0 是 FMV.X.W opcode 0x53, funct 0x78,
      // rs2=0 是 FMV.W.X
    case 0x70: // FCLASS.S (FMV.X.W 也是这个组，但 rs2=0)
      if (funct3 == 0) {
        // FMV.X.W: 在 Zfinx 中被移除 (Use standard MV instead)
        // 收到此编码应报异常
        illegal_exception = true;
      } else if (funct3 == 1) {
        // FCLASS.S: 必须保留！
        // 这是一个位操作指令，不产生异常，不更新 fflags
        float32_t f = to_f32(val_rs1);
        i_res = f32_classify(f);
      } else {
        illegal_exception = true;
      }
      break;
    default:
      illegal_exception = true;
      return;
    }
    break;

  // Fused Multiply-Add (FMA) 指令集
  // 结果 = (rs1 * rs2) + rs3
  case 0x43: // FMADD.S
    f_res = f32_mulAdd(f_rs1, f_rs2, f_rs3);
    i_res = from_f32(f_res);
    break;
  case 0x47: // FMSUB.S: (rs1 * rs2) - rs3
    // SoftFloat 的 mulAdd 定义通常是 a*b + c。
    // 需将 rs3 的符号位取反来实现减法，或者使用 softfloat 具体 API 变体
    // 标准做法：f32_mulAdd(a, b, c ^ 0x80000000) (如果 softfloat没提供专门的
    // sub) 或者是 f32_mulAdd(f_rs1, f_rs2, to_f32(val_rs3 ^ 0x80000000));
    {
      float32_t f_neg_rs3 = to_f32(val_rs3 ^ 0x80000000);
      f_res = f32_mulAdd(f_rs1, f_rs2, f_neg_rs3);
      i_res = from_f32(f_res);
    }
    break;
  case 0x4B: // FNMSUB.S: -(rs1 * rs2) + rs3
  {
    // 注意 RISC-V 定义：-(rs1*rs2) + rs3
    // SoftFloat 可能并没有直接对应指令，可能需要操作 rs1 或 rs2 符号
    float32_t f_neg_rs1 = to_f32(val_rs1 ^ 0x80000000);
    f_res = f32_mulAdd(f_neg_rs1, f_rs2, f_rs3);
    i_res = from_f32(f_res);
  } break;
  case 0x4F: // FNMADD.S: -(rs1 * rs2) - rs3
  {
    float32_t f_neg_rs1 = to_f32(val_rs1 ^ 0x80000000);
    float32_t f_neg_rs3 = to_f32(val_rs3 ^ 0x80000000);
    f_res = f32_mulAdd(f_neg_rs1, f_rs2, f_neg_rs3);
    i_res = from_f32(f_res);
  } break;

  default:
    illegal_exception = true;
    return;
  }

  // 4. 更新 CSR 状态 (Accumulate Exception Flags)
  // state.csr[CSR_FCSR] |= softfloat_exceptionFlags;

skip_flags_update:
  // 5. 写回结果
  if (write_rd && rd != 0) {
    state.gpr[rd] = i_res;
  }

  state.pc = next_pc;
}

void Ref_cpu::RV32CSR() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;

  // 使用宏直接提取，无需 copy_indice
  uint32_t rd = BITS(Instruction, 11, 7);
  uint32_t rs1 = BITS(Instruction, 19, 15);
  uint32_t uimm = rs1; // 对于立即数CSR指令，rs1字段就是立即数
  uint32_t csr_addr = BITS(Instruction, 31, 20);
  uint32_t funct3 = BITS(Instruction, 14, 12);

  uint32_t reg_rdata1 = state.gpr[rs1];

  bool we = funct3 == 1 || rs1 != 0;
  bool re = funct3 != 1 || rd != 0;
  uint32_t wcmd = funct3 & 0b11;
  uint32_t csr_wdata, wdata;

  if (funct3 & 0b100) {
    wdata = rs1;
  } else {
    wdata = reg_rdata1;
  }

  if (csr_addr != number_mtvec && csr_addr != number_mepc &&
      csr_addr != number_mcause && csr_addr != number_mie &&
      csr_addr != number_mip && csr_addr != number_mtval &&
      csr_addr != number_mscratch && csr_addr != number_mstatus &&
      csr_addr != number_mideleg && csr_addr != number_medeleg &&
      csr_addr != number_sepc && csr_addr != number_stvec &&
      csr_addr != number_scause && csr_addr != number_sscratch &&
      csr_addr != number_stval && csr_addr != number_sstatus &&
      csr_addr != number_sie && csr_addr != number_sip &&
      csr_addr != number_satp && csr_addr != number_mhartid &&
      csr_addr != number_misa && csr_addr != number_time &&
      csr_addr != number_timeh) {
    ;
  } else if (csr_addr == number_time || csr_addr == number_timeh) {
    illegal_exception = true;
    exception(Instruction);
    return;
  } else {

    int csr_idx = cvt_number_to_csr(csr_addr);
    if (re) {
      state.gpr[rd] = state.csr[csr_idx];
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
  uint32_t funct5 = BITS(Instruction, 31, 27);
  uint32_t reg_d_index = BITS(Instruction, 11, 7);
  uint32_t reg_a_index = BITS(Instruction, 19, 15);
  uint32_t reg_b_index = BITS(Instruction, 24, 20);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  uint32_t v_addr = reg_rdata1;
  uint32_t p_addr = v_addr;

  if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
    bool page_fault;

    if (fast_run) {
      if (funct5 == 2) {
        page_fault = !va2pa(p_addr, v_addr, 1);
      } else {
        page_fault = !va2pa(p_addr, v_addr, 2);
      }
    } else {
#ifdef CONFIG_MMU
      if (funct5 == 2) {
        page_fault = !va2pa_fixed(p_addr, v_addr, 1);
      } else {
        page_fault = !va2pa_fixed(p_addr, v_addr, 2);
      }
#else
      if (funct5 == 2) {
        page_fault = !va2pa(p_addr, v_addr, 1);
      } else {
        page_fault = !va2pa(p_addr, v_addr, 2);
      }
#endif
    }

    if (page_fault) {
      if (funct5 == 2) {
        page_fault_load = true;
      } else {
        page_fault_store = true;
      }
    }

    if (page_fault_load || page_fault_store) {
      exception(v_addr);
      return;
    }
  }

  if (funct5 != 2) {
    state.store = true;
    state.store_addr = p_addr;
    state.store_strb = 0b1111;
  }

  switch (funct5) {
  case 0: { // amoadd.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] + reg_rdata2;
    break;
  }
  case 1: { // amoswap.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = reg_rdata2;
    break;
  }
  case 2: { // lr.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    break;
  }
  case 3: { // sc.w
    state.store_data = reg_rdata2;
    state.gpr[reg_d_index] = 0;
    break;
  }
  case 4: { // amoxor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] ^ reg_rdata2;
    break;
  }
  case 8: { // amoor.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] | reg_rdata2;
    break;
  }
  case 12: { // amoand.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = memory[p_addr >> 2] & reg_rdata2;
    break;
  }
  case 16: { // amomin.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
                           ? reg_rdata2
                           : memory[p_addr >> 2];
    break;
  }
  case 20: { // amomax.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((int32_t)memory[p_addr >> 2] > (int32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  case 24: { // amominu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((uint32_t)memory[p_addr >> 2] < (uint32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  case 28: { // amomaxu.w
    state.gpr[reg_d_index] = memory[p_addr >> 2];
    state.store_data = ((uint32_t)memory[p_addr >> 2] > (uint32_t)reg_rdata2)
                           ? memory[p_addr >> 2]
                           : reg_rdata2;
    break;
  }
  default: {
    break;
  }
  }

  store_data();
  state.pc = next_pc;
}

void Ref_cpu::RV32IM() {
  // pc + 4
  uint32_t next_pc = state.pc + 4;
  uint32_t opcode = BITS(Instruction, 6, 0);
  uint32_t funct3 = BITS(Instruction, 14, 12);
  uint32_t funct7 = BITS(Instruction, 31, 25);
  uint32_t reg_d_index = BITS(Instruction, 11, 7);
  uint32_t reg_a_index = BITS(Instruction, 19, 15);
  uint32_t reg_b_index = BITS(Instruction, 24, 20);

  uint32_t reg_rdata1 = state.gpr[reg_a_index];
  uint32_t reg_rdata2 = state.gpr[reg_b_index];

  switch (opcode) {
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
    next_pc = state.pc + immJ(Instruction);
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_3_opcode_jalr: { // jalr
    bool bit_temp[32];
    next_pc = (reg_rdata1 + immI(Instruction)) & 0xFFFFFFFC;
    state.gpr[reg_d_index] = state.pc + 4;
    break;
  }
  case number_4_opcode_beq: { // beq, bne, blt, bge, bltu, bgeu
    switch (funct3) {
    case 0: { // beq
      if (reg_rdata1 == reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 1: { // bne
      if (reg_rdata1 != reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 4: { // blt
      if ((int32_t)reg_rdata1 < (int32_t)reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 5: { // bge
      if ((int32_t)reg_rdata1 >= (int32_t)reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 6: { // bltu
      if ((uint32_t)reg_rdata1 < (uint32_t)reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    case 7: { // bgeu
      if ((uint32_t)reg_rdata1 >= (uint32_t)reg_rdata2) {
        next_pc = (state.pc + immB(Instruction));
      }
      break;
    }
    }
    break;
  }
  case number_5_opcode_lb: { // lb, lh, lw, lbu, lhu
    uint32_t v_addr = reg_rdata1 + immI(Instruction);
    uint32_t p_addr = v_addr;
    if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
      if (fast_run) {
        page_fault_load = !va2pa(p_addr, v_addr, 1);
      } else {
#ifdef CONFIG_MMU
        page_fault_load = !va2pa_fixed(p_addr, v_addr, 1);
#else
        page_fault_load = !va2pa(p_addr, v_addr, 1);
#endif
      }
    }

    if (page_fault_load) {
      exception(v_addr);
      return;

    } else {
      uint32_t data = memory[p_addr >> 2];
      uint32_t offset = p_addr & 0b11;
      uint32_t size = funct3 & 0b11;
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
      if (!(funct3 & 0b100)) {
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

    uint32_t v_addr = reg_rdata1 + immS(Instruction);
    uint32_t p_addr = v_addr;
    if ((state.csr[csr_satp] & 0x80000000) && privilege != 3) {
      if (fast_run) {
        page_fault_store = !va2pa(p_addr, v_addr, 2);
      } else {
#ifdef CONFIG_MMU
        page_fault_store = !va2pa_fixed(p_addr, v_addr, 2);
#else
        page_fault_store = !va2pa(p_addr, v_addr, 2);
#endif
      }
    }

    if (page_fault_store) {
      exception(v_addr);
      return;
    } else {

      state.store = true;
      state.store_addr = p_addr;
      state.store_data = reg_rdata2;
      if (funct3 == 0b00) {
        state.store_strb = 0b1;
        state.store_data &= 0xFF;
      } else if (funct3 == 0b01) {
        state.store_strb = 0b11;
        state.store_data &= 0xFFFF;
      } else {
        state.store_strb = 0b1111;
      }

      store_data();
    }

    break;
  }
  case number_7_opcode_addi: { // addi, slti, sltiu, xori, ori, andi, slli,
                               // srli, srai
    switch (funct3) {
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
      switch (funct7) {
      case 0: { // srli
        state.gpr[reg_d_index] = (uint32_t)reg_rdata1 >> immI(Instruction);
        break;
      }
      case 32: { // srai
        state.gpr[reg_d_index] = (int32_t)reg_rdata1 >> immI(Instruction);
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
    if (funct7 == 1) {        // mul div
      int64_t s1 = (int64_t)(int32_t)reg_rdata1;
      int64_t s2 = (int64_t)(int32_t)reg_rdata2;

      uint64_t u1 = (uint32_t)reg_rdata1;
      uint64_t u2 = (uint32_t)reg_rdata2;

      // 获取 32 位操作数
      int32_t dividend = (int32_t)reg_rdata1;
      int32_t divisor = (int32_t)reg_rdata2;
      uint32_t u_dividend = (uint32_t)reg_rdata1;
      uint32_t u_divisor = (uint32_t)reg_rdata2;

      switch (funct3) {
      case 0: { // mul
        state.gpr[reg_d_index] = (int32_t)(u1 * u2);
        break;
      }
      case 1: { // mulh
        state.gpr[reg_d_index] = (uint32_t)((s1 * s2) >> 32);
        break;
      }
      case 2: { // mulsu
        state.gpr[reg_d_index] = (uint32_t)((s1 * (int64_t)u2) >> 32);
        break;
      }
      case 3: { // mulhu
        state.gpr[reg_d_index] = (uint32_t)((u1 * u2) >> 32);
        break;
      }
      case 4: { // div (signed)
        if (divisor == 0) {
          state.gpr[reg_d_index] = -1; // RISC-V 规定：除以0结果为 -1
        } else if (dividend == INT32_MIN && divisor == -1) {
          state.gpr[reg_d_index] =
              INT32_MIN; // RISC-V 规定：溢出时结果为被除数本身(INT_MIN)
        } else {
          state.gpr[reg_d_index] = dividend / divisor;
        }
        break;
      }
      case 5: { // divu (unsigned)
        if (u_divisor == 0) {
          state.gpr[reg_d_index] = 0xFFFFFFFF; // RISC-V 规定：除以0结果为最大值
        } else {
          state.gpr[reg_d_index] = u_dividend / u_divisor;
        }
        break;
      }
      case 6: { // rem (signed)
        if (divisor == 0) {
          state.gpr[reg_d_index] = dividend; // RISC-V 规定：除以0，余数为被除数
        } else if (dividend == INT32_MIN && divisor == -1) {
          state.gpr[reg_d_index] = 0; // RISC-V 规定：溢出时，余数为 0
        } else {
          state.gpr[reg_d_index] = dividend % divisor;
        }
        break;
      }
      case 7: { // remu (unsigned)
        if (u_divisor == 0) {
          state.gpr[reg_d_index] =
              u_dividend; // RISC-V 规定：除以0，余数为被除数
        } else {
          state.gpr[reg_d_index] = u_dividend % u_divisor;
        }
        break;
      }
      }
    } else {
      switch (funct3) {
      case 0: { // add, sub
        switch (funct7) {
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
        switch (funct7) {
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

void Ref_cpu::store_data() {

  uint32_t p_addr = state.store_addr;
  int offset = p_addr & 0x3;
  uint32_t wstrb = state.store_strb << offset;
  uint32_t wdata = state.store_data << (offset * 8);
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
  /*if ((number_funct3_unsigned == 1 && p_addr % 2 == 1) ||*/
  /*    (number_funct3_unsigned == 2 && p_addr % 4 != 0)) {*/
  /*  cout << "Store Memory Address Align Error!!!" << endl;*/
  /*  cout << "funct3 code: " << dec << number_funct3_unsigned << endl;*/
  /*  cout << "addr: " << hex << p_addr << endl;*/
  /*  exit(-1);*/
  /*}*/

  memory[p_addr / 4] = (mask & wdata) | (~mask & old_data);

  if (p_addr == UART_BASE) {
    char temp;
    temp = wdata & 0x000000ff;
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xffffff00;
    if (fast_run)
      cout << temp;
  }

  if (p_addr == 0x10000001 && (state.store_data & 0x000000ff) == 7) {
    memory[0xc201004 / 4] = 0xa;
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xfff0ffff;

    state.csr[csr_mip] = state.csr[csr_mip] | (1 << 9);
    state.csr[csr_sip] = state.csr[csr_sip] | (1 << 9);
  }

  if (p_addr == 0x10000001 && (state.store_data & 0x000000ff) == 5) {
    memory[0x10000000 / 4] = memory[0x10000000 / 4] & 0xfff0ffff | 0x00030000;
  }

  if (p_addr == 0xc201004 && (state.store_data & 0x000000ff) == 0xa) {
    memory[0xc201004 / 4] = 0x0;
    state.csr[csr_mip] = state.csr[csr_mip] & ~(1 << 9);
    state.csr[csr_sip] = state.csr[csr_sip] & ~(1 << 9);
  }

  state.store_data = state.store_data << offset * 8;
  state.store_strb = state.store_strb << offset * 8;
}

bool Ref_cpu::va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t type) {
  uint32_t mstatus = state.csr[csr_mstatus];
  uint32_t sstatus = state.csr[csr_sstatus];
  uint32_t satp = state.csr[csr_satp];

  // 1. 提取状态位 (直接位运算，极快)
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;

  // 确定有效特权级 (Effective Privilege Mode)
  // 如果 MPRV=1 且不是取指(type!=0)，则使用 MPP 作为特权级进行检查
  int eff_priv = privilege;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  // 2. Level 1 Page Table Walk
  // satp 的 PPN 字段在 SV32 中是低 22 位 (0-21)
  // VPN[1] 是 v_addr 的 [31:22] 位
  // pte1_addr = (satp.ppn << 12) + (vpn1 * 4)
  // 你的原代码逻辑：(satp << 12) | ((v_addr >> 20) & 0xFFC)
  // 等价于下面的位操作：
  uint32_t ppn_root = satp & 0x3FFFFF; // 提取 SATP 中的 PPN
  uint32_t vpn1 = (v_addr >> 22) & 0x3FF;
  uint32_t pte1_addr = (ppn_root << 12) | (vpn1 << 2);

  // 直接读取，注意这里需要确保 memory 是按字寻址还是字节寻址
  uint32_t pte1 = memory[pte1_addr >> 2];

  // 3. 检查 PTE 有效性
  // !V 或者 (!R && W) 都是无效的
  if (!(pte1 & PTE_V) || (!(pte1 & PTE_R) && (pte1 & PTE_W))) {
    return false;
  }

  // 4. 判断是否是叶子节点 (R=1 或 X=1)
  if ((pte1 & PTE_R) || (pte1 & PTE_X)) {
    // --- Superpage (4MB) ---

    // 权限检查 (Permission Check)
    // Fetch (0): 需要 X
    if (type == 0 && !(pte1 & PTE_X))
      return false;
    // Load (1): 需要 R，或者 (MXR=1 且 X=1)
    if (type == 1 && !(pte1 & PTE_R) && !(mxr && (pte1 & PTE_X)))
      return false;
    // Store (2): 需要 W
    if (type == 2 && !(pte1 & PTE_W))
      return false;

    // 用户权限检查 (User/Supervisor Check)
    bool is_user_page = (pte1 & PTE_U) != 0;
    if (eff_priv == 0 && !is_user_page)
      return false; // U-mode 访问 S-page -> Fault
    if (eff_priv == 1 && is_user_page && !sum)
      return false; // S-mode 访问 U-page 且 SUM=0 -> Fault

    // 对齐检查 (Superpage 要求 PPN[0] 为 0)
    // PPN[0] 对应 PTE 的 [19:10] 位
    if ((pte1 >> 10) & 0x3FF)
      return false;

    // A/D 位检查
    if (!(pte1 & PTE_A))
      return false; // Accessed 必须为 1 (硬件不自动设置时需报错)
    if (type == 2 && !(pte1 & PTE_D))
      return false; // 写操作 Dirty 必须为 1

    // 计算物理地址 (Superpage)
    // PA = PPN[1] | VPN[0] | Offset
    // PPN[1] 是 PTE[31:20]，对应 PA[31:22]
    // v_addr & 0x3FFFFF 保留低 22 位 (VPN[0] + Offset)
    p_addr = ((pte1 << 2) & 0xFFC00000) | (v_addr & 0x3FFFFF);
    return true;
  }

  // 5. Level 2 Page Table Walk (非叶子节点，指向下一级页表)
  // PPN 是 PTE 的 [31:10] 位
  uint32_t ppn1 = (pte1 >> 10) & 0x3FFFFF;
  uint32_t vpn0 = (v_addr >> 12) & 0x3FF;
  uint32_t pte2_addr = (ppn1 << 12) | (vpn0 << 2);

  uint32_t pte2 = memory[pte2_addr >> 2];

  // 重复有效性检查
  if (!(pte2 & PTE_V) || (!(pte2 & PTE_R) && (pte2 & PTE_W))) {
    return false;
  }

  // Level 2 必须是叶子节点 (SV32 只有两级)
  if ((pte2 & PTE_R) || (pte2 & PTE_X)) {
    // --- 4KB Page ---

    // 权限检查 (逻辑同上)
    if (type == 0 && !(pte2 & PTE_X))
      return false;
    if (type == 1 && !(pte2 & PTE_R) && !(mxr && (pte2 & PTE_X)))
      return false;
    if (type == 2 && !(pte2 & PTE_W))
      return false;

    // 用户权限检查
    bool is_user_page = (pte2 & PTE_U) != 0;
    if (eff_priv == 0 && !is_user_page)
      return false;
    if (eff_priv == 1 && is_user_page && !sum)
      return false;

    // A/D 位检查
    if (!(pte2 & PTE_A))
      return false;
    if (type == 2 && !(pte2 & PTE_D))
      return false;

    // 计算物理地址 (4KB Page)
    // PA = PPN | Offset
    // PPN 是 PTE[31:10]，对应 PA[31:12]
    // Offset 是 v_addr[11:0]
    p_addr = ((pte2 >> 10) << 12) | (v_addr & 0xFFF);
    return true;
  }

  return false; // 如果 Level 2 还不是叶子节点，则是非法页表
}

/*
 * va2pa_fixed: a fixed version of va2pa
 *
 * 基本功能几乎与 va2pa() 相同；但当 dut.cpu 检测到 page fault 时，
 * 以 dut.cpu 的结果为准，
 *
 * 目的：当 SFENCE.VMA 还没有执行、存在两种合法的页表映射时，保证
 * DUT 与参考模型的页表映射一致，避免 difftest 失败。
 */
bool Ref_cpu::va2pa_fixed(uint32_t &p_addr, uint32_t v_addr, uint32_t type) {
  bool ret = va2pa(p_addr, v_addr, type);
#ifndef CONFIG_LOOSE_VA2PA
  return ret;
#endif
  extern int ren_commit_idx; // extern from Rename.cpp, for difftest debug
  Inst_entry ren_commit_entry = cpu.back.out.commit_entry[ren_commit_idx];
  bool dut_page_fault_inst = ren_commit_entry.uop.page_fault_inst;
  bool dut_page_fault_load = ren_commit_entry.uop.page_fault_load;
  bool dut_page_fault_store = ren_commit_entry.uop.page_fault_store;

  // 1. dut page_fault, ref no page_fault -> allow, ret = false
  // 2. dut no page_fault, ref page_fault -> ERROR
  switch (type) {
  case 0: // instruction fetch
    if (dut_page_fault_inst) {
      ret = false; // 以 DUT MMU 为准
    } else if (!dut_page_fault_inst && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed instruction fetch page fault "
              "mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  case 1: // load
    if (dut_page_fault_load) {
      ret = false;
    } else if (!dut_page_fault_load && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed load page fault mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  case 2: // store
    if (dut_page_fault_store) {
      ret = false;
    } else if (!dut_page_fault_store && !ret) {
      cout << "[va2pa_fixed] Error: va2pa_fixed store page fault mismatch!"
           << endl;
      cout << "VA: " << hex << v_addr << endl;
      cout << "sim_time: " << dec << sim_time << endl;
      exit(1);
    }
    break;
  default:
    cout << "[va2pa_fixed] Error: unknown access type!" << endl;
    exit(1);
  }
  return ret;
}
