#pragma once
#include "config.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

// Standard library usings (kept for compatibility)
using std::cin;
using std::cout;
using std::dec;
using std::endl;
using std::hex;
using std::string;

// Legacy branch-mask alias used across LSU/dispatch paths.
using mask_t = wire<BR_MASK_WIDTH>;

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
  AMO,
  FP
};

constexpr int INST_TYPE_COUNT = FP + 1;
constexpr int INST_TYPE_WIDTH = bit_width_for_count(INST_TYPE_COUNT);

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

// Debug sideband shared by InstInfo/MicroOp. Keep diag_val in hardware path.
struct DebugMeta {
  wire<32> instruction;
  wire<32> pc;
  uint8_t mem_align_mask;
  bool difftest_skip;
  int64_t inst_idx;
};

struct TmaMeta {
  bool is_cache_miss;
  bool is_ret;
  bool mem_commit_is_load;
  bool mem_commit_is_store;
};

struct InstInfo {
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
  wire<BR_TAG_WIDTH> br_id;
  wire<BR_MASK_WIDTH> br_mask;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<1> stq_flag;
  wire<LDQ_IDX_WIDTH> ldq_idx;

  // ROB completion 信息
  wire<ROB_CPLT_MASK_WIDTH> expect_mask;
  wire<ROB_CPLT_MASK_WIDTH> cplt_mask;
  wire<1> rob_flag; // 用于对比指令年龄

  // 异常信息
  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;
  wire<1> is_atomic;
  wire<1> flush_pipe;

  InstType type;
  TmaMeta tma;
  DebugMeta dbg;

  InstInfo() { std::memset(this, 0, sizeof(InstInfo)); }
};

struct MicroOp {
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
  wire<BR_TAG_WIDTH> br_id;
  wire<BR_MASK_WIDTH> br_mask;
  wire<CSR_IDX_WIDTH> csr_idx;
  wire<ROB_IDX_WIDTH> rob_idx;
  wire<STQ_IDX_WIDTH> stq_idx;
  wire<1> stq_flag;
  wire<LDQ_IDX_WIDTH> ldq_idx;

  // ROB completion 信息
  wire<ROB_CPLT_MASK_WIDTH> expect_mask;
  wire<ROB_CPLT_MASK_WIDTH> cplt_mask;
  wire<1> rob_flag; // 用于对比指令年龄

  // 异常信息
  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;

  UopType op;
  TmaMeta tma;
  DebugMeta dbg;
  wire<1> flush_pipe;

  MicroOp() { std::memset(this, 0, sizeof(MicroOp)); }

  // Explicit conversion from InstInfo
  MicroOp(const InstInfo &info) {
    std::memset(this, 0, sizeof(MicroOp));
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
    this->br_id = info.br_id;
    this->br_mask = info.br_mask;
    this->csr_idx = info.csr_idx;
    this->rob_idx = info.rob_idx;
    this->stq_idx = info.stq_idx;
    this->stq_flag = info.stq_flag;
    this->ldq_idx = info.ldq_idx;
    this->expect_mask = info.expect_mask;
    this->cplt_mask = info.cplt_mask;
    this->rob_flag = info.rob_flag;
    this->page_fault_inst = info.page_fault_inst;
    this->page_fault_load = info.page_fault_load;
    this->page_fault_store = info.page_fault_store;
    this->illegal_inst = info.illegal_inst;
    this->tma.is_cache_miss = info.tma.is_cache_miss;
    this->tma.is_ret = info.tma.is_ret;
    this->tma.mem_commit_is_load = info.tma.mem_commit_is_load;
    this->tma.mem_commit_is_store = info.tma.mem_commit_is_store;
    this->is_atomic = info.is_atomic;
    this->dbg.instruction = info.dbg.instruction;
    this->dbg.pc = info.dbg.pc;
    this->dbg.mem_align_mask = info.dbg.mem_align_mask;
    this->dbg.difftest_skip = info.dbg.difftest_skip;
    this->dbg.inst_idx = info.dbg.inst_idx;
    this->flush_pipe = info.flush_pipe;
  }
};

struct InstEntry {
  wire<1> valid;
  InstInfo uop;
};

struct UopEntry {
  wire<1> valid;
  MicroOp uop;
};

struct WakeInfo {
  wire<1> valid;
  wire<PRF_IDX_WIDTH> preg;
};

#include "PerfCount.h"

// Added to support Remote icache
enum class ExitReason { NONE, EBREAK, WFI, SIMPOINT };
class SimCpu;

class SimContext {
public:
  PerfCount perf;
  ExitReason exit_reason = ExitReason::NONE;
  bool is_ckpt = false;
  uint64_t ckpt_warmup_commit_target = 0;
  uint64_t ckpt_measure_commit_target = 0;
  SimCpu *cpu = nullptr;
  void run_commit_inst(InstEntry *inst_entry);
  void run_difftest_inst(InstEntry *inst_entry);
};
