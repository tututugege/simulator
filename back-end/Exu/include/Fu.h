#pragma once
#include "AbstractFU.h" // for __builtin_clz
#include "IO.h"
#include "config.h"
// #include <cassert>
#include <climits>

// ==========================================
// ALU: 算术逻辑单元
// ==========================================
class AluUnit : public SingleCycleFU {
  static constexpr int SUB = 0b000;
  static constexpr int SLL = 0b001;
  static constexpr int SLT = 0b010;
  static constexpr int SLTU = 0b011;
  static constexpr int XOR = 0b100;
  static constexpr int SRL = 0b101;
  static constexpr int OR = 0b110;
  static constexpr int AND = 0b111;

public:
  AluUnit(std::string name = "ALU", int port_idx = 0)
      : SingleCycleFU(name, port_idx) {}

protected:
  void impl_compute(ExuInst &inst) override {
    uint32_t operand1, operand2;
    if (inst.src1_is_pc) {
      operand1 = inst.pc;
    } else
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
        if (((inst.func7 >> 5) & 1) && !inst.src2_is_imm)
          inst.result = operand1 - operand2;
        else
          inst.result = operand1 + operand2;
        break;
      case SLL: {
        uint32_t shift = operand2 & 0x1F;
        if (inst.func7 == 0) // sll
          inst.result = operand1 << shift;
        else if (inst.func7 == 0x30 && !inst.src2_is_imm) // rol (Zbb)
          inst.result = (operand1 << shift) | (operand1 >> (32 - shift));
        else if (inst.func7 == 0x24) // bclr (Zbs)
          inst.result = operand1 & ~(1u << shift);
        else if (inst.func7 == 0x14) // bset (Zbs)
          inst.result = operand1 | (1u << shift);
        else if (inst.func7 == 0x34) // binv (Zbs)
          inst.result = operand1 ^ (1u << shift);
        else if (inst.func7 == 0x05) { // clmul (Zbc)
          uint32_t output = 0;
          for (int i = 0; i < 32; i++) {
            if ((operand2 >> i) & 1)
              output ^= (operand1 << i);
          }
          inst.result = output;
        } else if (inst.func7 == 0x30 && inst.src2_is_imm) { // OP-IMM Zbb Unary
          uint32_t sub_op = inst.imm & 0x1F; // rs2 is in imm field
          if (sub_op == 0)                   // clz
            inst.result = (operand1 == 0) ? 32 : __builtin_clz(operand1);
          else if (sub_op == 1) // ctz
            inst.result = (operand1 == 0) ? 32 : __builtin_ctz(operand1);
          else if (sub_op == 2) // pcnt
            inst.result = __builtin_popcount(operand1);
          else if (sub_op == 4) // sext.b
            inst.result = (uint32_t)(int32_t)(int8_t)(operand1 & 0xFF);
          else if (sub_op == 5) // sext.h
            inst.result = (uint32_t)(int32_t)(int16_t)(operand1 & 0xFFFF);
        }
        break;
      }
      case SLT: {
        if (inst.func7 == 0) // slt
          inst.result = ((signed)operand1 < (signed)operand2);
        else if (inst.func7 == 0x10 && !inst.src2_is_imm) // sh1add (Zba)
          inst.result = operand2 + (operand1 << 1);
        else if (inst.func7 == 0x05 && !inst.src2_is_imm) { // clmulr (Zbc)
          uint32_t output = 0;
          for (int i = 0; i < 32; i++) {
            if ((operand2 >> i) & 1)
              output ^= (operand1 >> (31 - i));
          }
          inst.result = output;
        } else {
          inst.result = ((signed)operand1 < (signed)operand2);
        }
        break;
      }
      case SLTU: {
        if (inst.func7 == 0) // sltu
          inst.result = ((unsigned)operand1 < (unsigned)operand2);
        else if (inst.func7 == 0x05 && !inst.src2_is_imm) { // clmulh (Zbc)
          uint32_t output = 0;
          for (int i = 1; i < 32; i++) {
            if ((operand2 >> i) & 1)
              output ^= (operand1 >> (32 - i));
          }
          inst.result = output;
        } else {
          inst.result = ((unsigned)operand1 < (unsigned)operand2);
        }
        break;
      }
      case XOR: {
        if (inst.func7 == 0) // xor
          inst.result = (operand1 ^ operand2);
        else if (inst.func7 == 0x20 && !inst.src2_is_imm) // xnor (Zbb)
          inst.result = ~(operand1 ^ operand2);
        else if (inst.func7 == 0x10 && !inst.src2_is_imm) // sh2add (Zba)
          inst.result = operand2 + (operand1 << 2);
        else if (inst.func7 == 0x05 && !inst.src2_is_imm) // min (Zbb)
          inst.result =
              ((int32_t)operand1 < (int32_t)operand2) ? operand1 : operand2;
        else if (inst.func7 == 0x04 && !inst.src2_is_imm) // pack (Zbb)
          inst.result = (operand1 & 0x0000FFFF) | (operand2 << 16);
        else
          inst.result = (operand1 ^ operand2);
        break;
      }
      case SRL: {
        uint32_t shift = operand2 & 0x1F;
        if (inst.func7 == 0) // srl
          inst.result = (uint32_t)operand1 >> shift;
        else if ((inst.func7 == 0x20) && !inst.src2_is_imm) // sra
          inst.result = (int32_t)operand1 >> shift;
        else if (((inst.func7 >> 5) & 1) && inst.src2_is_imm &&
                 (inst.func7 != 0x30) && (inst.func7 != 0x24) &&
                 (inst.func7 != 0x34) && (inst.func7 != 0x14)) // srai
          inst.result = (int32_t)operand1 >> shift;
        else if (inst.func7 == 0x30) { // ror (Zbb)
          inst.result = (operand1 >> shift) | (operand1 << (32 - shift));
        } else if (inst.func7 == 0x24) { // bext (Zbs)
          inst.result = (operand1 >> shift) & 1;
        } else if (inst.func7 == 0x34 && inst.src2_is_imm) { // rev8 (Zbb)
          if ((inst.imm & 0x1F) == 24) {                     // shamt = 24
            uint32_t x = operand1;
            inst.result = ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
                          ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
          }
        } else if (inst.func7 == 0x14 && inst.src2_is_imm) { // orcb (Zbb)
          if ((inst.imm & 0x1F) == 7) {                      // shamt = 7
            uint32_t x = operand1;
            uint32_t res = 0;
            if (x & 0xFF)
              res |= 0xFF;
            if (x & 0xFF00)
              res |= 0xFF00;
            if (x & 0xFF0000)
              res |= 0xFF0000;
            if (x & 0xFF000000)
              res |= 0xFF000000;
            inst.result = res;
          }
        } else if (inst.func7 == 0x05 && !inst.src2_is_imm) { // minu (Zbb)
          inst.result =
              ((uint32_t)operand1 < (uint32_t)operand2) ? operand1 : operand2;
        } else {
          inst.result = (uint32_t)operand1 >> shift;
        }
        break;
      }
      case OR: {
        if (inst.func7 == 0) // or
          inst.result = (operand1 | operand2);
        else if (inst.func7 == 0x20 && !inst.src2_is_imm) // orn (Zbb)
          inst.result = (operand1 | ~operand2);
        else if (inst.func7 == 0x05 && !inst.src2_is_imm) // max (Zbb)
          inst.result =
              ((int32_t)operand1 > (int32_t)operand2) ? operand1 : operand2;
        else if (inst.func7 == 0x10 && !inst.src2_is_imm) // sh3add (Zba)
          inst.result = operand2 + (operand1 << 3);
        else
          inst.result = (operand1 | operand2);
        break;
      }
      case AND: {
        if (inst.func7 == 0) // and
          inst.result = (operand1 & operand2);
        else if (inst.func7 == 0x20 && !inst.src2_is_imm) // andn (Zbb)
          inst.result = (operand1 & ~operand2);
        else if (inst.func7 == 0x05 && !inst.src2_is_imm) // maxu (Zbb)
          inst.result =
              ((uint32_t)operand1 > (uint32_t)operand2) ? operand1 : operand2;
        else if (inst.func7 == 0x04 && !inst.src2_is_imm) // packh (Zbb)
          inst.result =
              (operand1 & 0x000000FF) | ((operand2 & 0x000000FF) << 8);
        else
          inst.result = (operand1 & operand2);
        break;
      }
      default:
        Assert(0);
      }
      break;
    }
    case UOP_WFI:
      inst.result = 0; // WFI doesn't produce result, treated as NOP here
      break;
    default: {
      inst.result = operand1 + operand2;
      break;
    }
    }
  }
};

// ==========================================
// AGU: 地址生成单元 (IO 精确修改版)
// ==========================================
class AguUnit : public SingleCycleFU {
public:
  AguUnit(std::string name, int port_idx) : SingleCycleFU(name, port_idx) {}

protected:
  void impl_compute(ExuInst &inst) override {
    // 1. 计算虚拟地址 (Common logic)
    // Load 和 Store (STA) 都需要计算地址： Base + Offset
    uint64_t vaddr = inst.src1_rdata + inst.imm;
    inst.result =
        vaddr; // 记录结果，如果是 Load，这个值是地址；如果是 STA，也是地址
  }
};

// ==========================================
// SDU: 存数数据单元 (IO 精确修改版)
// ==========================================
class SduUnit : public SingleCycleFU {
public:
  SduUnit(std::string name, int port_idx) : SingleCycleFU(name, port_idx) {}

protected:
  void impl_compute(ExuInst &inst) override {
    uint32_t result_data = 0;

    // === 1. 获取操作数 & 计算 Store Data ===
    uint32_t op1 = inst.src1_rdata;
    uint32_t op2 = inst.src2_rdata;
    bool is_atomic = inst.is_atomic;

    if (!is_atomic) {
      // 普通 Store: src2_rdata 是要存的数据 (对应 RS2)
      // 注意：根据你的架构定义，这里可能需要确认数据在 src1 还是 src2
      // 通常 Store: Mem[RS1+Imm] = RS2. 所以数据在 src2_rdata.
      // 但你的旧代码用了 src1_rdata 作为 op1 赋值给 result_data，请根据你的
      // decode 逻辑确认。 假设 decode 将 rs2 的值放到了 src2_rdata：
      result_data = inst.src2_rdata;
    } else {
      // AMO 运算逻辑
      switch (inst.func7 >> 2) {
      case AmoOp::SWAP:
        result_data = op2;
        break;
      case AmoOp::ADD:
        result_data = op1 + op2;
        break;
      case AmoOp::XOR:
        result_data = op1 ^ op2;
        break;
      case AmoOp::AND:
        result_data = op1 & op2;
        break;
      case AmoOp::OR:
        result_data = op1 | op2;
        break;
      case AmoOp::MIN:
        result_data = ((int32_t)op1 < (int32_t)op2) ? op1 : op2;
        break;
      case AmoOp::MAX:
        result_data = ((int32_t)op1 > (int32_t)op2) ? op1 : op2;
        break;
      case AmoOp::MINU:
        result_data = (op1 < op2) ? op1 : op2;
        break;
      case AmoOp::MAXU:
        result_data = (op1 > op2) ? op1 : op2;
        break;
      default:
        result_data = op2;
        break;
      }
    }

    // 更新 uop 中的 result，以便传输给 LSU
    inst.result = result_data; // 这里 result 存的是 Data
  }
};

// ==========================================
// MUL: 乘法单元
// ==========================================
class MulUnit : public FixedLatencyFU {
public:
  MulUnit(std::string name = "MUL", int port_idx = 0, int lat = MUL_MAX_LATENCY)
      : FixedLatencyFU(name, port_idx, lat) {}

protected:
  void impl_compute(ExuInst &inst) override {
    int64_t s1 = (int64_t)(int32_t)inst.src1_rdata;
    int64_t s2 = (int64_t)(int32_t)inst.src2_rdata;
    uint64_t u1 = (uint32_t)inst.src1_rdata;
    uint64_t u2 = (uint32_t)inst.src2_rdata;

    switch (inst.func3) {
    case 0:
      inst.result = (int32_t)(u1 * u2);
      break; // mul
    case 1:
      inst.result = (uint32_t)((s1 * s2) >> 32);
      break; // mulh
    case 2:
      inst.result = (uint32_t)((s1 * (int64_t)u2) >> 32);
      break; // mulhsu
    case 3:
      inst.result = (uint32_t)((u1 * u2) >> 32);
      break; // mulhu
    default:
      Assert(0);
      break;
    }
  }
};

// ==========================================
// BRU: 分支单元
// ==========================================
class BruUnit : public SingleCycleFU {
  static constexpr int BEQ = 0b000;
  static constexpr int BNE = 0b001;
  static constexpr int BLT = 0b100;
  static constexpr int BGE = 0b101;
  static constexpr int BLTU = 0b110;
  static constexpr int BGEU = 0b111;
public:
  BruUnit(std::string name, int port_idx) : SingleCycleFU(name, port_idx) {}

protected:
  void impl_compute(ExuInst &inst) override {
    uint32_t operand1 = inst.src1_rdata;
    uint32_t operand2 = inst.src2_rdata;
    uint32_t inst_pc = inst.pc;
    uint32_t pc_br = inst_pc + inst.imm;
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

#if defined(CONFIG_ORACLE_STEADY_FETCH_WIDTH) && !defined(CONFIG_BPU)
    // Oracle steady-fetch stress mode intentionally relaxes FTQ block semantics.
    // In no-BPU builds, treat branch prediction as always-correct and avoid
    // recovery redirects from BRU.
    inst.mispred = false;
#else
    // FTQ lookup
    bool pred_taken = inst.ftq_pred_taken;
    uint32_t pred_target = 0;

    if (pred_taken) {
      pred_target = inst.ftq_next_pc;
    } else {
      pred_target = inst_pc + 4;
    }

    // Verify
    if ((br_taken && pred_taken && pred_target == pc_br) ||
        (!br_taken && !pred_taken)) {
      inst.mispred = false;
    } else {
      inst.mispred = true;
    }
#endif

    inst.br_taken = br_taken;
    inst.diag_val = br_taken ? pc_br : (inst_pc + 4);
  }
};

// ==========================================
// DIV: 除法单元 (Fixed Latency for simulation correctness)
// Note: While real dividers are non-pipelined (IterativeFU), using
// FixedLatencyFU eliminates race conditions with register recycling in this
// implementation
// ==========================================
class DivUnit : public IterativeFU {
public:
  DivUnit(std::string name, int port_idx, int lat)
      : IterativeFU(name, port_idx, lat) {}

protected:
  void impl_compute(ExuInst &inst) override {
    int32_t dividend = (int32_t)inst.src1_rdata;
    int32_t divisor = (int32_t)inst.src2_rdata;
    uint32_t u_dividend = (uint32_t)inst.src1_rdata;
    uint32_t u_divisor = (uint32_t)inst.src2_rdata;

    switch (inst.func3) {
    case 4: // div
      if (divisor == 0)
        inst.result = -1;
      else if (dividend == INT32_MIN && divisor == -1)
        inst.result = INT32_MIN;
      else
        inst.result = dividend / divisor;
      break;
    case 5: // divu
      if (u_divisor == 0)
        inst.result = 0xFFFFFFFF;
      else
        inst.result = u_dividend / u_divisor;
      break;
    case 6: // rem (Rem is my waifu)
      if (divisor == 0)
        inst.result = dividend;
      else if (dividend == INT32_MIN && divisor == -1)
        inst.result = 0;
      else
        inst.result = dividend % divisor;
      break;
    case 7: // remu
      if (u_divisor == 0)
        inst.result = u_dividend;
      else
        inst.result = u_dividend % u_divisor;
      break;
    default:
      Assert(0);
      break;
    }
  }

};

// ==========================================
// CSR: 控制状态寄存器单元
// ==========================================
class CsrUnit : public SingleCycleFU {
public:
  CsrUnit(std::string name, int port_idx) : SingleCycleFU(name, port_idx) {}

protected:
  void impl_compute(ExuInst &inst) override { (void)inst; }
};
