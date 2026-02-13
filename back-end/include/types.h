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


typedef struct InstInfo {
  wire<32>
      instruction; // Debug only: raw instruction bits (not for hardware logic)
  wire<32> diag_val; // Hardware: Shared field for instruction or pc_next

  wire<AREG_IDX_WIDTH> dest_areg, src1_areg, src2_areg;
  wire<PRF_IDX_WIDTH> dest_preg, src1_preg, src2_preg; // log2(PRF_NUM)
  wire<PRF_IDX_WIDTH> old_dest_preg;

  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> ftq_is_last;

  // 分支预测更新信息
  wire<1> mispred;
  wire<1> br_taken;

  wire<1> dest_en, src1_en, src2_en;
  wire<1> src1_busy, src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm;
  wire<32> pc;
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
  wire<1> is_atomic;

  InstType type;
  bool is_cache_miss;

  // Debug
  bool difftest_skip;
  bool flush_pipe;
  int64_t inst_idx;

  InstInfo() { std::memset(this, 0, sizeof(InstInfo)); }
} InstInfo;

typedef struct MicroOp {
  wire<32>
      instruction; // Debug only: raw instruction bits (not for hardware logic)
  wire<32> diag_val; // Hardware: Shared field for instruction or pc_next

  wire<AREG_IDX_WIDTH> dest_areg, src1_areg;
  wire<PRF_IDX_WIDTH> dest_preg, src1_preg, src2_preg; // log2(PRF_NUM)
  wire<32> src1_rdata, src2_rdata;
  wire<32> result;

  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;
  wire<1> ftq_is_last;

  // 分支预测更新信息
  wire<1> mispred;
  wire<1> br_taken;

  wire<1> is_atomic;

  wire<1> dest_en, src1_en, src2_en;
  wire<1> src1_busy, src2_busy;
  wire<1> src1_is_pc;
  wire<1> src2_is_imm;
  wire<3> func3;
  wire<7> func7;
  wire<32> imm;
  wire<32> pc;
  wire<BR_TAG_WIDTH> tag;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<STQ_NUM> pre_sta_mask;

  // ROB 信息
  wire<2> uop_num;
  wire<1> rob_flag; // 用于对比指令年龄

  // 异常信息
  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;

  UopType op;
  bool is_cache_miss;

  // Debug
  bool difftest_skip;
  bool flush_pipe;
  int64_t inst_idx;
  int64_t cplt_time;

  MicroOp() { std::memset(this, 0, sizeof(MicroOp)); }

  // Explicit conversion from InstInfo
  MicroOp(const InstInfo &info) {
    std::memset(this, 0, sizeof(MicroOp));
    this->instruction = info.instruction;
    this->diag_val = info.diag_val;
    this->dest_areg = info.dest_areg;
    this->src1_areg = info.src1_areg;
    this->dest_preg = info.dest_preg;
    this->src1_preg = info.src1_preg;
    this->src2_preg = info.src2_preg;
    this->ftq_idx = info.ftq_idx;
    this->ftq_offset = info.ftq_offset;
    this->ftq_is_last = info.ftq_is_last;
    this->mispred = info.mispred;
    this->br_taken = info.br_taken;
    this->dest_en = info.dest_en;
    this->src1_en = info.src1_en;
    this->src2_en = info.src2_en;
    this->src1_busy = info.src1_busy;
    this->src2_busy = info.src2_busy;
    this->src1_is_pc = info.src1_is_pc;
    this->src2_is_imm = info.src2_is_imm;
    this->func3 = info.func3;
    this->func7 = info.func7;
    this->imm = info.imm;
    this->pc = info.pc;
    this->tag = info.tag;
    this->csr_idx = info.csr_idx;
    this->rob_idx = info.rob_idx;
    this->stq_idx = info.stq_idx;
    this->pre_sta_mask = info.pre_sta_mask;
    this->uop_num = info.uop_num;
    this->rob_flag = info.rob_flag;
    this->page_fault_inst = info.page_fault_inst;
    this->page_fault_load = info.page_fault_load;
    this->page_fault_store = info.page_fault_store;
    this->illegal_inst = info.illegal_inst;
    this->is_cache_miss = info.is_cache_miss;
    this->is_atomic = info.is_atomic;
    this->difftest_skip = info.difftest_skip;
    this->flush_pipe = info.flush_pipe;
    this->inst_idx = info.inst_idx;
  }
} MicroOp;

typedef struct {
  wire<1> valid;
  InstInfo uop;
} InstEntry;

typedef struct {
  wire<1> valid;
  MicroOp uop;
} UopEntry;

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
