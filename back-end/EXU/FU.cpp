#include "config.h"
#include <SimCpu.h>
#include <cstdint>
#include <cvt.h>
#include <util.h>

extern uint32_t *p_memory;
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

enum STATE { IDLE, RECV };

#define SUB 0b000
#define SLL 0b001
#define SLT 0b010
#define SLTU 0b011
#define XOR 0b100
#define SRL 0b101
#define OR 0b110
#define AND 0b111

#define BEQ 0b000
#define BNE 0b001
#define BLT 0b100
#define BGE 0b101
#define BLTU 0b110
#define BGEU 0b111

void mul(Inst_uop &inst) {
  // 提取 32 位数据并进行符号处理
  // 必须先转 int32_t 再转 int64_t 才能正确进行符号扩展
  int64_t s1 = (int64_t)(int32_t)inst.src1_rdata;
  int64_t s2 = (int64_t)(int32_t)inst.src2_rdata;

  // 对于无符号数，直接零扩展
  uint64_t u1 = (uint32_t)inst.src1_rdata;
  uint64_t u2 = (uint32_t)inst.src2_rdata;

  switch (inst.func3) {
  case 0: { // mul
    // 结果取低 32 位。
    // 使用无符号乘法避免 C 语言中有符号溢出的未定义行为。
    // 补码表示下，有符号和无符号乘法的低 32 位结果是一样的。
    inst.result = (int32_t)(u1 * u2);
    break;
  }
  case 1: { // mulh (signed * signed, get high)
    inst.result = (uint32_t)((s1 * s2) >> 32);
    break;
  }
  case 2: { // mulhsu (signed * unsigned, get high)
    // 必须使用 64 位乘法，且 s1 带符号，u2 无符号
    // 注意：C语言中 int64 * uint64 会提升为 uint64，
    // 但这里需要保持 s1 的符号性参与运算，通常转换为 int64 计算比较安全，
    // 或者利用 (int64_t)u2 将无符号数视为正的 int64 (因 u2
    // 高32位为0，不会变负)。
    inst.result = (uint32_t)((s1 * (int64_t)u2) >> 32);
    break;
  }
  case 3: { // mulhu (unsigned * unsigned, get high)
    inst.result = (uint32_t)((u1 * u2) >> 32);
    break;
  }
  default:
    assert(0);
    break;
  }
}

void div(Inst_uop &inst) {
  // 获取 32 位操作数
  int32_t dividend = (int32_t)inst.src1_rdata;
  int32_t divisor = (int32_t)inst.src2_rdata;
  uint32_t u_dividend = (uint32_t)inst.src1_rdata;
  uint32_t u_divisor = (uint32_t)inst.src2_rdata;

  switch (inst.func3) {
  case 4: { // div (signed)
    if (divisor == 0) {
      inst.result = -1; // RISC-V 规定：除以0结果为 -1
    } else if (dividend == INT32_MIN && divisor == -1) {
      inst.result = INT32_MIN; // RISC-V 规定：溢出时结果为被除数本身(INT_MIN)
    } else {
      inst.result = dividend / divisor;
    }
    break;
  }
  case 5: { // divu (unsigned)
    if (u_divisor == 0) {
      inst.result = 0xFFFFFFFF; // RISC-V 规定：除以0结果为最大值
    } else {
      inst.result = u_dividend / u_divisor;
    }
    break;
  }
  case 6: { // rem (signed)
    if (divisor == 0) {
      inst.result = dividend; // RISC-V 规定：除以0，余数为被除数
    } else if (dividend == INT32_MIN && divisor == -1) {
      inst.result = 0; // RISC-V 规定：溢出时，余数为 0
    } else {
      inst.result = dividend % divisor;
    }
    break;
  }
  case 7: { // remu (unsigned)
    if (u_divisor == 0) {
      inst.result = u_dividend; // RISC-V 规定：除以0，余数为被除数
    } else {
      inst.result = u_dividend % u_divisor;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
}

void bru(Inst_uop &inst) {
  uint32_t operand1, operand2;
  operand1 = inst.src1_rdata;
  operand2 = inst.src2_rdata;

  uint32_t pc_br = inst.pc + inst.imm;
  bool br_taken = true;

  if (inst.op == UOP_BR) {
    switch (inst.func3) {
    case BEQ:
      br_taken = (operand1 == operand2);
      break;
    case BNE:
      br_taken = (operand1 != operand2);
      break;
    case BGE:
      br_taken = ((signed)operand1 >= (signed)operand2);
      break;
    case BLT:
      br_taken = ((signed)operand1 < (signed)operand2);
      break;
    case BGEU:
      br_taken = ((unsigned)operand1 >= (unsigned)operand2);
      break;
    case BLTU:
      br_taken = ((unsigned)operand1 < (unsigned)operand2);
      break;
    }
  }

  switch (inst.op) {
  case UOP_BR:
    break;
  case UOP_JUMP:
    br_taken = true;
    if (inst.src1_en)
      pc_br = (inst.src1_rdata + inst.imm) & (~0x1);
    break;
  default:
    br_taken = false;
  }

  if (br_taken && inst.pred_br_taken && inst.pred_br_pc == pc_br ||
      !br_taken && !inst.pred_br_taken) {
    inst.mispred = false;
  } else {
    inst.mispred = true;
  }

  inst.br_taken = br_taken;

  if (br_taken)
    inst.pc_next = pc_br;
  else
    inst.pc_next = inst.pc + 4;
}

void alu(Inst_uop &inst) {
  uint32_t operand1, operand2;
  if (inst.src1_is_pc)
    operand1 = inst.pc;
  else
    operand1 = inst.src1_rdata;

  if (inst.src2_is_imm) {
    operand2 = inst.imm;
  } else if (inst.op == UOP_JUMP) {
    operand2 = 4;
  } else {
    operand2 = inst.src2_rdata;
  }

  switch (inst.op) {
  case UOP_ADD: {
    switch (inst.func3) {
    case SUB:
      if (inst.func7_5 && !inst.src2_is_imm)
        inst.result = operand1 - operand2;
      else
        inst.result = operand1 + operand2;
      break;
    case SLL:
      inst.result = operand1 << operand2;
      break;
    case SLT:
      inst.result = ((signed)operand1 < (signed)operand2);
      break;
    case SLTU:
      inst.result = ((unsigned)operand1 < (unsigned)operand2);
      break;
    case XOR:
      inst.result = (operand1 ^ operand2);
      break;
    case SRL:
      if (inst.func7_5)
        inst.result = ((signed)operand1 >> operand2);
      else
        inst.result = ((unsigned)operand1 >> operand2);
      break;
    case OR:
      inst.result = (operand1 | operand2);
      break;
    case AND:
      inst.result = (operand1 & operand2);
      break;
    default:
      assert(0);
    }
    break;
  }
  default: {
    inst.result = operand1 + operand2;
    break;
  }
  }
}

#ifdef CONFIG_MMU
// return: 1 - stall load; 0 - load ok
bool ldu(Inst_uop &inst, bool mmu_page_fault, uint32_t mmu_ppn) {
  bool stall_load = false;
  uint32_t addr = inst.src1_rdata + inst.imm;

  if (addr == 0x1fd0e000) {
    inst.difftest_skip = true;
  }

  int size = inst.func3 & 0b11;
  int offset = addr & 0b11;
  uint32_t mask = 0;
  uint32_t sign = 0;

  if (inst.amoop != AMONONE) {
    size = 0b10;
    offset = 0b0;
  }

  uint32_t data;
  bool page_fault = !cpu.back.load_data(data, addr, inst.rob_idx,
                                        mmu_page_fault, mmu_ppn, stall_load);

  if (!page_fault) {
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
    if (!(inst.func3 & 0b100)) {
      data = data | sign;
    }
    inst.result = data;
  } else {
    inst.page_fault_load = true;
    inst.result = addr;
  }
  return stall_load;
}

void stu_addr(Inst_uop &inst, bool page_fault, uint32_t mmu_ppn) {

  uint32_t v_addr = inst.src1_rdata + inst.imm;

  uint32_t p_addr = v_addr;
  p_addr = (mmu_ppn << 12) | (v_addr & 0xFFF);
  if (page_fault) {
    inst.page_fault_store = true;
    inst.result = v_addr;
  } else {
    inst.result = p_addr;
  }
}
#else
void ldu(Inst_uop &inst) {
  uint32_t addr = inst.src1_rdata + inst.imm;

  if (addr == 0x1fd0e000) {
    inst.difftest_skip = true;
  }

  int size = inst.func3 & 0b11;
  int offset = addr & 0b11;
  uint32_t mask = 0;
  uint32_t sign = 0;

  if (inst.amoop != AMONONE) {
    size = 0b10;
    offset = 0b0;
  }

  uint32_t data;
  bool page_fault = !cpu.back.load_data(data, addr, inst.rob_idx);

  if (!page_fault) {
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
    if (!(inst.func3 & 0b100)) {
      data = data | sign;
    }
    inst.result = data;
  } else {
    inst.page_fault_load = true;
    inst.result = addr;
  }
}

void stu_addr(Inst_uop &inst) {

  uint32_t v_addr = inst.src1_rdata + inst.imm;

  uint32_t p_addr = v_addr;
  bool page_fault = false;

  if (cpu.back.csr.CSR_RegFile[csr_satp] & 0x80000000 &&
      cpu.back.csr.privilege != 3) {
    bool mstatus[32], sstatus[32];
    cvt_number_to_bit_unsigned(mstatus, cpu.back.csr.CSR_RegFile[csr_mstatus],
                               32);

    cvt_number_to_bit_unsigned(sstatus, cpu.back.csr.CSR_RegFile[csr_sstatus],
                               32);

    page_fault = !va2pa(p_addr, v_addr, cpu.back.csr.CSR_RegFile[csr_satp], 2,
                        mstatus, sstatus, cpu.back.csr.privilege, p_memory);
  }

  if (page_fault) {
    inst.page_fault_store = true;
    inst.result = v_addr;
  } else {
    inst.result = p_addr;
  }
}

#endif

void stu_data(Inst_uop &inst) {

  switch (inst.amoop) {
  case AMOADD: { // amoadd.w
    inst.result = inst.src1_rdata + inst.src2_rdata;
    break;
  }
  case AMOSWAP: { // amoswap.w
    inst.result = inst.src2_rdata;
    break;
  }
  case AMOXOR: { // amoxor.w
    inst.result = inst.src1_rdata ^ inst.src2_rdata;
    break;
  }
  case AMOOR: { // amoor.w

    inst.result = inst.src1_rdata | inst.src2_rdata;
    break;
  }
  case AMOAND: { // amoand.w
    inst.result = inst.src1_rdata & inst.src2_rdata;
    break;
  }
  case AMOMIN: { // amomin.w
    inst.result = ((int)inst.src1_rdata > (int)inst.src2_rdata)
                      ? inst.src2_rdata
                      : inst.src1_rdata;
    break;
  }
  case AMOMAX: { // amomax.w
    inst.result = ((int)inst.src1_rdata > (int)inst.src2_rdata)
                      ? inst.src1_rdata
                      : inst.src2_rdata;
    break;
  }
  case AMOMINU: { // amominu.w
    inst.result = ((uint32_t)inst.src1_rdata > (uint32_t)inst.src2_rdata)
                      ? inst.src2_rdata
                      : inst.src1_rdata;
    break;
  }
  case AMOMAXU: { // amomaxu.w
    inst.result = ((uint32_t)inst.src1_rdata > (uint32_t)inst.src2_rdata)
                      ? inst.src1_rdata
                      : inst.src2_rdata;
    break;
  }
  default:
    inst.result = inst.src2_rdata;
    break;
  }
}
