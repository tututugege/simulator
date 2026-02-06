#pragma once

#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iostream>
#include <string>

// ==========================================
// [Type Definitions & Utilities]
// ==========================================

// Recursive template to find suitable container type
template <int N>
using AutoType = typename std::conditional<
    N == 1, bool,
    typename std::conditional<
        N <= 8, uint8_t,
        typename std::conditional<
            N <= 16, uint16_t,
            typename std::conditional<
                N <= 32, uint32_t,
                typename std::conditional<N <= 64, uint64_t, uint64_t>::type>::
                type>::type>::type>::type;

template <int N> using wire = AutoType<N>;
template <int N> using reg = AutoType<N>;

typedef wire<7> preg_t;
typedef wire<4> tag_t;

// Standard library usings (kept for compatibility)
using std::cin;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::string;

// ==========================================
// [Instruction & Pipeline Definitions]
// ==========================================

enum UopType {
  UOP_JUMP,
  UOP_ADD,
  UOP_BR,
  UOP_LOAD,
  UOP_STA,
  UOP_STD,
  UOP_CSR,
  UOP_ECALL,
  UOP_EBREAK,
  UOP_SFENCE_VMA,
  UOP_FENCE_I,
  UOP_MRET,
  UOP_SRET,
  UOP_MUL,
  UOP_DIV,
  MAX_UOP_TYPE
};

enum IQType {
  IQ_INT,
  IQ_LD,
  IQ_STA,
  IQ_STD,
  IQ_BR,
  IQ_NUM,
};

struct IssuePortConfigInfo {
  int port_idx;          // 物理端口号 (Out.iss2prf 的下标)
  uint64_t support_mask; // 该端口支持的操作掩码 (Capability)
};

struct IQStaticConfig {
  int id;                 // IQ ID
  int size;               // 队列深度
  int dispatch_width;     // 入队宽度 (Dispatch 写端口数)
  uint64_t supported_ops; // IQ 整体接收什么指令 (用于 Dispatch 路由)
  int port_start_idx;
  int port_num;
};

enum InstType {
  NOP,
  JAL,
  JALR,
  ADD,
  BR,
  LOAD,
  STORE,
  CSR,
  ECALL,
  EBREAK,
  SFENCE_VMA,
  FENCE_I,
  MRET,
  SRET,
  MUL,
  DIV,
  AMO
};

// AMO Operations (funct7[6:2])
namespace AmoOp {
constexpr uint8_t ADD = 0b00000;
constexpr uint8_t SWAP = 0b00001;
constexpr uint8_t LR = 0b00010;
constexpr uint8_t SC = 0b00011;
constexpr uint8_t XOR = 0b00100;
constexpr uint8_t OR = 0b01000;
constexpr uint8_t AND = 0b01100;
constexpr uint8_t MIN = 0b10000;
constexpr uint8_t MAX = 0b10100;
constexpr uint8_t MINU = 0b11000;
constexpr uint8_t MAXU = 0b11100;
} // namespace AmoOp

// ==========================================
// [Structs & Classes]
// ==========================================

typedef struct InstUop {
  wire<32> instruction;

  wire<6> dest_areg, src1_areg, src2_areg;
  wire<7> dest_preg, src1_preg, src2_preg; // log2(ROB_NUM)
  wire<7> old_dest_preg;
  wire<32> src1_rdata, src2_rdata;
  wire<32> result;
  wire<32> paddr;

  // 分支预测信息
  wire<1> pred_br_taken;
  wire<1> alt_pred;
  wire<8> altpcpn;
  wire<8> pcpn;
  wire<32> pred_br_pc;
  wire<32> tage_idx[4]; // TN_MAX = 4

  // 分支预测更新信息
  wire<1> mispred;
  wire<1> br_taken;
  wire<32> pc_next;

  wire<1> dest_en, src1_en, src2_en;
  wire<1> src1_busy, src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm; // 好像不用32bit 先用着
  wire<32> pc;  // 未来将会优化pc的获取
  wire<4> tag;
  wire<12> csr_idx;
  wire<7> rob_idx;
  wire<4> stq_idx;
  wire<16> pre_sta_mask;
  wire<16> pre_std_mask;

  // ROB 信息
  wire<2> uop_num;
  wire<2> cplt_num;
  wire<1> rob_flag; // 用于对比指令年龄

  // 异常信息
  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;

  InstType type;
  UopType op;

  // Debug
  bool difftest_skip;
  bool flush_pipe;
  int64_t inst_idx;
  int64_t cplt_time;
  int64_t enqueue_time;

  InstUop() { std::memset(this, 0, sizeof(InstUop)); }
} InstUop;

typedef struct {
  wire<1> valid;
  InstUop uop;
} InstEntry;

typedef struct {
  wire<1> valid;
  wire<7> preg;
} WakeInfo;

#include "PerfCount.h"

// Added to support Remote icache
class SimContext {
public:
  PerfCount perf;
  bool sim_end = false;
  bool is_ckpt;
};
