#pragma once
#include "AbstractFU.h" // for __builtin_clz
#include "IO.h"
#include "FTQ.h"
#include "config.h"
// #include <cassert>
#include <climits>

// ==========================================
// ALU: 算术逻辑单元
// ==========================================
class AluUnit : public FixedLatencyFU {
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
      : FixedLatencyFU(name, port_idx, 1) {}

protected:
  void impl_compute(InstUop &inst) override {
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
        if (((inst.func7 >> 5) & 1) && !inst.src2_is_imm)
          inst.result = operand1 - operand2;
        else
          inst.result = operand1 + operand2;
        break;
      case SLL:
        inst.result = operand1 << (operand2 & 0x1F);
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
        if ((inst.func7 >> 5) & 1)
          inst.result = (int32_t)operand1 >> (operand2 & 0x1F);
        else
          inst.result = (uint32_t)operand1 >> (operand2 & 0x1F);
        break;
      case OR:
        inst.result = (operand1 | operand2);
        break;
      case AND:
        inst.result = (operand1 & operand2);
        break;
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
extern long long sim_time;

class AguUnit : public FixedLatencyFU {
  ExeLsuIO *exe2lsu;
  int agu_port_idx; // 标记这是第几个 AGU 端口

public:
  // 构造函数传入 port_id，用于区分写入哪个 LSU 输入端口
  AguUnit(std::string name, int port_idx, ExeLsuIO *exe2lsu, int agu_port_idx)
      : FixedLatencyFU(name, port_idx, 1), exe2lsu(exe2lsu),
        agu_port_idx(agu_port_idx) {}

protected:
  int get_lsu_port_id() override { return this->agu_port_idx; }
  void impl_compute(InstUop &inst) override {
    // 1. 计算虚拟地址 (Common logic)
    // Load 和 Store (STA) 都需要计算地址： Base + Offset
    uint64_t vaddr = inst.src1_rdata + inst.imm;
    inst.result =
        vaddr; // 记录结果，如果是 Load，这个值是地址；如果是 STA，也是地址



    // 2. 驱动 LSU IO 接口
    // 我们不再调用函数，而是模拟“连线”，将请求置为 valid
    // 注意：需要在 Exu 或 Top Level 每一拍开始时清空 backend->in，以防止 latch
    // 效应
    if (agu_port_idx < LSU_AGU_COUNT) {
      exe2lsu->agu_req[agu_port_idx].valid = true;
      exe2lsu->agu_req[agu_port_idx].uop = inst;
      // 这里的 inst 已经包含了刚刚计算好的 result (vaddr)
    }
  }
};

// ==========================================
// SDU: 存数数据单元 (IO 精确修改版)
// ==========================================
class SduUnit : public FixedLatencyFU {
  ExeLsuIO *exe2lsu;
  int sdu_port_idx; // 标记这是第几个 SDU 端口

public:
  SduUnit(std::string name, int port_idx, ExeLsuIO *exe2lsu, int sdu_port_idx)
      : FixedLatencyFU(name, port_idx, 1), exe2lsu(exe2lsu),
        sdu_port_idx(sdu_port_idx) {}

protected:
  int get_lsu_port_id() override { return this->sdu_port_idx; }
  void impl_compute(InstUop &inst) override {
    uint32_t result_data = 0;

    // === 1. 获取操作数 & 计算 Store Data ===
    uint32_t op1 = inst.src1_rdata;
    uint32_t op2 = inst.src2_rdata;
    bool is_atomic = inst.type == AMO;

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
        result_data = ((int64_t)op1 < (int64_t)op2) ? op1 : op2;
        break;
      case AmoOp::MAX:
        result_data = ((int64_t)op1 > (int64_t)op2) ? op1 : op2;
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

    // === 2. 驱动 LSU IO 接口 ===
    if (sdu_port_idx < LSU_SDU_COUNT) {
      exe2lsu->sdu_req[sdu_port_idx].valid = true;
      exe2lsu->sdu_req[sdu_port_idx].uop = inst;
    }
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
  void impl_compute(InstUop &inst) override {
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
class BruUnit : public FixedLatencyFU {
  static constexpr int BEQ = 0b000;
  static constexpr int BNE = 0b001;
  static constexpr int BLT = 0b100;
  static constexpr int BGE = 0b101;
  static constexpr int BLTU = 0b110;
  static constexpr int BGEU = 0b111;
  FTQ *ftq;

public:
  BruUnit(std::string name, int port_idx, FTQ *ftq)
      : FixedLatencyFU(name, port_idx, 1), ftq(ftq) {}

protected:
  void impl_compute(InstUop &inst) override {
    uint32_t operand1 = inst.src1_rdata;
    uint32_t operand2 = inst.src2_rdata;
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

    // FTQ lookup
    bool pred_taken = false;
    uint32_t pred_target = 0;

    FTQEntry &ftq_entry = ftq->get(inst.ftq_idx);
    if (ftq_entry.valid) {
      pred_taken = ftq_entry.pred_taken_mask[inst.ftq_offset];
      if (pred_taken) {
        // If predicted taken, the target MUST be the block's next_pc ??? 
        // Logic: specific branch target logic
        // If it's a conditional branch inside the block, and predicted taken,
        // it usually implies it's the last instruction of the block or redirects flow.
        // Simplified: if pred_taken, we expect target to be ftq_entry.next_pc?
        // Or do we store targets?
        // Current design: FTQ stores next_pc of the BLOCK.
        // If there are multiple branches, only one is taken?
        // Assuming block ends at the taken branch.
        pred_target = ftq_entry.next_pc; 
      } else {
        pred_target = inst.pc + 4; // Check if compressed?
      }
    }

    // Verify
    if ((br_taken && pred_taken && pred_target == pc_br) ||
        (!br_taken && !pred_taken)) {
      inst.mispred = false;
    } else {
      inst.mispred = true;
    }

    inst.br_taken = br_taken;
    inst.diag_val = br_taken ? pc_br : (inst.pc + 4);
  }
};

// ==========================================
// DIV: 除法单元 (Fixed Latency for simulation correctness)
// Note: While real dividers are non-pipelined (IterativeFU), using FixedLatencyFU
// eliminates race conditions with register recycling in this implementation
// ==========================================
class DivUnit : public IterativeFU {
public:
  DivUnit(std::string name, int port_idx, int lat)
      : IterativeFU(name, port_idx, lat) {}

protected:
  void impl_compute(InstUop &inst) override {
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
    case 6: // rem
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


  // int calculate_latency(const InstUop &inst) override {
    // 固定返回DIV_MAX_LATENCY以消除竞态条件
    // 原始的SRT动态延迟计算会导致与唤醒管道不匹配
    // uint64_t divisor = inst.src2_rdata;
    // uint64_t dividend = inst.src1_rdata;
    // if (divisor == 0 || dividend == 0)
    //   return 4;
    // int clz_n = __builtin_clzl(dividend);
    // int clz_d = __builtin_clzl(divisor);
    // int effective_bits = (64 - clz_n) - (64 - clz_d);
    // if (effective_bits < 0)
    //   effective_bits = 0;
    // return (effective_bits / radix_log2) + 4;
    // return DIV_MAX_LATENCY;
  // }
};

// ==========================================
// CSR: 控制状态寄存器单元
// ==========================================
class CsrUnit : public FixedLatencyFU {
  ExeCsrIO *exe2csr;
  CsrExeIO *csr2exe;

public:
  CsrUnit(std::string name, int port_idx, ExeCsrIO *exe2csr, CsrExeIO *csr2exe)
      : FixedLatencyFU(name, port_idx, 1), exe2csr(exe2csr), csr2exe(csr2exe) {}

protected:
  void impl_compute(InstUop &inst) override {
    if (exe2csr->re) {
      inst.result = csr2exe->rdata;
    }
  }
};
