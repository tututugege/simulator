#pragma once
#include "config.h"
#include <cstring>
#include <iostream>
#include <string>

typedef wire<PRF_IDX_WIDTH> preg_t;
typedef wire<BR_TAG_WIDTH> tag_t;
typedef wire<BR_MASK_WIDTH> mask_t;

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
  WFI,
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

typedef union {
  uint32_t instruction;
  uint32_t pc_next;
} RobExtraData;

typedef struct InstUop {
  wire<32> instruction; // Debug only: raw instruction bits (not for hardware logic)
  wire<32> diag_val;    // Hardware: Shared field for instruction or pc_next

  wire<AREG_IDX_WIDTH> dest_areg, src1_areg, src2_areg;
  wire<PRF_IDX_WIDTH> dest_preg, src1_preg, src2_preg; // log2(PRF_NUM)
  wire<PRF_IDX_WIDTH> old_dest_preg;
  wire<32> src1_rdata, src2_rdata;
  wire<32> result;
  wire<32> paddr;

  // 分支预测信息
  // wire<1> pred_br_taken; // Moved to FTQ
  // wire<1> alt_pred;      // Moved to FTQ
  // wire<8> altpcpn;       // Moved to FTQ
  // wire<8> pcpn;          // Moved to FTQ
  // wire<32> pred_br_pc;   // Moved to FTQ (next_pc)
  // wire<32> tage_idx[4]; // TN_MAX = 4 // Moved to FTQ

  int ftq_idx;
  int ftq_offset;
  bool ftq_is_last;

  // 分支预测更新信息
  wire<1> mispred;
  wire<1> br_taken;

  wire<1> dest_en, src1_en, src2_en;
  wire<1> src1_busy, src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm; // 好像不用32bit 先用着
  wire<32> pc;  // 未来将会优化pc的获取
  wire<BR_TAG_WIDTH> tag;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<STQ_NUM> pre_sta_mask;

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
  bool is_cache_miss;

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
  RobExtraData extra_data;
} InstEntry;

typedef struct {
  wire<1> valid;
  wire<PRF_IDX_WIDTH> preg;
} WakeInfo;

#include "PerfCount.h"

// Added to support Remote icache
enum class ExitReason { NONE, EBREAK, WFI, SIMPOINT };

class SimContext {
public:
  PerfCount perf;
  ExitReason exit_reason = ExitReason::NONE;
  bool is_ckpt;
};
