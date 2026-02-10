#pragma once
#include "config.h"

// =============================================================================
// Module-Specific Instruction Structures (Verilog Reference)
// These structures are "slim" versions of InstUop, containing only the signals
// physically present in the hardware interfaces between stages.
// =============================================================================

// IDU -> Rename (Decode-to-Rename Interface)
typedef struct DecRenUop {
  InstType type;           // 译码核心类型 (Decoded Instruction Type)
  wire<32> pc;             // 程序计数器 (Program Counter - Temporary)
  wire<32> imm;            // 立即数 (Immediate)
  wire<12> csr_idx;        // CSR 索引 (CSR Index)
  wire<3>  func3;          // 辅助功能码 (funct3)
  wire<7>  func7;          // 辅助功能码 (funct7)
  
  // 寄存器索引 (Architectural Register Indices)
  wire<AREG_IDX_WIDTH> dest_areg;
  wire<AREG_IDX_WIDTH> src1_areg;
  wire<AREG_IDX_WIDTH> src2_areg;

  // 控制与元数据 (Control and Metadata)
  wire<BR_TAG_WIDTH> tag;            // 分支预测 Tag (Branch Tag for Squashing)
  wire<2>  uop_num;        // micro-op 数量 (Number of uops)
  
  // Explicit signals (Verilog-friendly)
  wire<1>  dest_en;
  wire<1>  src1_en;
  wire<1>  src2_en;
  wire<1>  src1_is_pc;      // 操作数 1 是否来自 PC (Is src1 from PC)
  wire<1>  src2_is_imm;     // 操作数 2 是否是立即数 (Is src2 immediate)
  wire<1>  page_fault_inst;
  wire<1>  illegal_inst;    // 非法指令异常 (Illegal Instruction)

  // Filter function: Maps full InstUop to DecRenUop
  static DecRenUop filter(const InstUop& full) {
    DecRenUop slim;
    slim.pc        = full.pc;
    slim.type      = full.type;
    slim.imm       = full.imm;
    slim.csr_idx   = full.csr_idx;
    slim.func3     = full.func3;
    slim.func7     = full.func7;
    slim.dest_areg = full.dest_areg;
    slim.src1_areg = full.src1_areg;
    slim.src2_areg = full.src2_areg;
    slim.tag       = full.tag;
    slim.uop_num   = full.uop_num;
    slim.dest_en   = full.dest_en;
    slim.src1_en   = full.src1_en;
    slim.src2_en   = full.src2_en;
    slim.src1_is_pc = full.src1_is_pc;
    slim.src2_is_imm = full.src2_is_imm;
    slim.page_fault_inst = full.page_fault_inst;
    slim.illegal_inst = full.illegal_inst;
    return slim;
  }
} DecRenUop;

// Rename -> Dispatch/ROB (Ren-to-Dis Interface, Instruction Level)
typedef struct RenDisUop {
  wire<32>  pc;            // 程序计数器
  DecRenUop base;          // 核心译码信息 (Includes Type)
  wire<AREG_IDX_WIDTH> dest_areg;
  wire<AREG_IDX_WIDTH> src1_areg;
  wire<AREG_IDX_WIDTH> src2_areg;
  wire<PRF_IDX_WIDTH>  dest_preg;     // 物理目标寄存器
  wire<PRF_IDX_WIDTH>  src1_preg;     // 物理源寄存器 1
  wire<PRF_IDX_WIDTH>  src2_preg;     // 物理源寄存器 2
  wire<PRF_IDX_WIDTH>  old_dest_preg; // 旧物理目标寄存器
  wire<1>   src1_busy;     // 源 1 忙状态
  wire<1>   src2_busy;     // 源 2 忙状态

  static RenDisUop filter(const InstUop& full) {
    RenDisUop slim;
    slim.pc            = full.pc;
    slim.base          = DecRenUop::filter(full);
    slim.dest_preg     = full.dest_preg;
    slim.src1_preg     = full.src1_preg;
    slim.src2_preg     = full.src2_preg;
    slim.old_dest_preg = full.old_dest_preg;
    slim.src1_busy     = full.src1_busy;
    slim.src2_busy     = full.src2_busy;
    return slim;
  }
} RenDisUop;

// Dispatch -> Issue (Dis-to-Iss Interface, Micro-op Level)
typedef struct DisIssUop {
  wire<32>  pc;            // 程序计数器
  UopType   op;            // 微操作码 (Specific Uop Type)
  wire<32>  imm;           // 立即数
  // 重命名后的物理寄存器索引
  wire<PRF_IDX_WIDTH> dest_preg, src1_preg, src2_preg;
  wire<ROB_IDX_WIDTH> rob_idx;       // ROB 索引
  wire<STQ_IDX_WIDTH> stq_idx;       // LD/ST 队列索引
  wire<CSR_IDX_WIDTH> csr_idx;       // CSR 索引 (Critical Fix)
  wire<BR_TAG_WIDTH>  tag;           // Branch Tag (Critical Fix)
  wire<3>   func3;         // 辅助功能码 (funct3)
  wire<7>   func7;         // 辅助功能码 (funct7)
  wire<1>   src1_busy, src2_busy;
  wire<1>   src1_is_pc, src2_is_imm;

  static DisIssUop filter(const InstUop& full) {
    DisIssUop slim;
    slim.pc        = full.pc;
    slim.op        = full.op;
    slim.imm       = full.imm;
    slim.dest_preg = full.dest_preg;
    slim.src1_preg = full.src1_preg;
    slim.src2_preg = full.src2_preg;
    slim.rob_idx   = full.rob_idx;
    slim.stq_idx   = full.stq_idx;
    slim.csr_idx   = full.csr_idx;
    slim.tag       = full.tag;
    slim.func3     = full.func3;
    slim.func7     = full.func7;
    slim.src1_busy = full.src1_busy;
    slim.src2_busy = full.src2_busy;
    slim.src1_is_pc = full.src1_is_pc;
    slim.src2_is_imm = full.src2_is_imm;
    return slim;
  }
} DisIssUop;

// Issue -> Execution (Iss-to-Exe Interface, Micro-op Level)
typedef struct IssExeUop {
  wire<32> pc;             // 程序计数器
  UopType  op;             // 具体操作码 (Specific Uop Type)
  wire<32> imm;            // 立即数
  wire<PRF_IDX_WIDTH>  dest_preg;      // 目标寄存器
  wire<ROB_IDX_WIDTH>  rob_idx;        // ROB 索引
  wire<CSR_IDX_WIDTH>  csr_idx;        // CSR 索引
  wire<BR_TAG_WIDTH>   tag;            // Branch Tag
  wire<3>  func3;          // 辅助功能码
  wire<7>  func7;          // 辅助功能码
  wire<1>  src1_is_pc;     // 操作数 1 Mux
  wire<1>  src2_is_imm;    // 操作数 2 Mux
  wire<1>  illegal_inst;   // 异常透传

  static IssExeUop filter(const InstUop& full) {
    IssExeUop slim;
    slim.pc        = full.pc;
    slim.op        = full.op;
    slim.imm       = full.imm;
    slim.dest_preg = full.dest_preg;
    slim.rob_idx   = full.rob_idx;
    slim.csr_idx   = full.csr_idx;
    slim.tag       = full.tag;
    slim.func3     = full.func3;
    slim.func7     = full.func7;
    slim.src1_is_pc = full.src1_is_pc;
    slim.src2_is_imm = full.src2_is_imm;
    slim.illegal_inst = full.illegal_inst;
    return slim;
  }
} IssExeUop;

// Execution -> Writeback (Exe-to-Wb Interface, Micro-op Level)
typedef struct ExeWbUop {
  UopType  op;             // 具体操作码 (Specific Uop Type)
  wire<32> result;         // 计算结果 (Execution Result)
  wire<PRF_IDX_WIDTH> dest_preg;      // 目标寄存器 (Destination Register)
  wire<ROB_IDX_WIDTH> rob_idx;        // ROB 索引 (ROB Index)
  wire<BR_TAG_WIDTH>  tag;            // Branch Tag
  wire<1>  page_fault_inst;
  wire<1>  page_fault_load;
  wire<1>  page_fault_store;
  wire<1>  flush_pipe;

  static ExeWbUop filter(const InstUop& full) {
    ExeWbUop slim;
    slim.op               = full.op;
    slim.result           = full.result;
    slim.dest_preg        = full.dest_preg;
    slim.rob_idx          = full.rob_idx;
    slim.tag              = full.tag;
    slim.page_fault_inst  = full.page_fault_inst;
    slim.page_fault_load  = full.page_fault_load;
    slim.page_fault_store = full.page_fault_store;
    slim.flush_pipe       = full.flush_pipe;
    return slim;
  }
} ExeWbUop;

// ROB Entry (ROB Level, Instructions with Commit Status)
typedef struct RobUop {
  InstType type;           // 指令类型 (用于提交副作用)
  wire<32> pc;             // 用于异常处理
  wire<32> instruction;    // Debug only: raw instruction (not for hardware logic)
  wire<32> diag_val;       // 诊断信息 (指令或跳转目标)
  
  // 寄存器更新核心字段 (User Requested Optimization)
  wire<PRF_IDX_WIDTH>  dest_preg;      // 目标物理寄存器
  wire<PRF_IDX_WIDTH>  old_dest_preg;  // 需释放的旧物理寄存器
  wire<AREG_IDX_WIDTH> dest_areg;      // 目标逻辑寄存器 (用于更新 RAT)
  wire<1>  dest_en;        // 目标寄存器写使能
  wire<BR_TAG_WIDTH>   tag;            // Branch Tag

  // 状态与异常
  wire<2>  uop_num;        // 该指令拆分的 uop 总数
  wire<2>  cplt_num;       // 已完成的 uop 数量
  wire<1>  page_fault_inst, page_fault_load, page_fault_store, illegal_inst;
  
  // 分支更新
  wire<1>  br_taken;
  wire<1>  mispred;

  static RobUop filter(const InstUop& full) {
    RobUop slim;
    slim.type            = full.type;
    slim.pc              = full.pc;
    slim.instruction     = full.instruction;
    slim.dest_preg       = full.dest_preg;
    slim.old_dest_preg   = full.old_dest_preg;
    slim.dest_areg       = full.dest_areg;
    slim.dest_en         = full.dest_en;
    slim.tag             = full.tag;
    slim.uop_num         = full.uop_num;
    slim.cplt_num        = full.cplt_num;
    slim.page_fault_inst = full.page_fault_inst;
    slim.page_fault_load = full.page_fault_load;
    slim.page_fault_store = full.page_fault_store;
    slim.illegal_inst    = full.illegal_inst;
    slim.diag_val        = full.diag_val;
    slim.br_taken        = full.br_taken;
    slim.mispred         = full.mispred;
    return slim;
  }
} RobUop;

// =============================================================================
// Module IO Envelopes (Hardware Boundary Reference)
// These group the filtered signals into module-level ports.
// =============================================================================

// Private IDU Module Interface
typedef struct IduIO {
  // --- Inputs ---
  struct {
    wire<1>  valid[FETCH_WIDTH];
    wire<32> inst[FETCH_WIDTH];  // Only the raw bits needed for decode
  } from_front;

  struct {
    wire<1>  ready;              // Rename ready signal
  } from_ren;

  struct {
    wire<1>  flush;              // Global pipeline flush
    wire<1>  mispred;            // Misprediction detected from PRF/EXE
    wire<BR_TAG_WIDTH>  br_tag;             // Tag that mispredicted
  } from_back;

  // --- Outputs ---
  struct {
    wire<1>  ready;              // Ready to receive from fetch
    wire<1>  fire[FETCH_WIDTH];  // Fire signal for each slot
  } to_front;

  struct {
    wire<1>     valid[FETCH_WIDTH];
    DecRenUop   uop[FETCH_WIDTH]; // Decoded instructions
  } to_ren;

  struct {
    wire<1>  mispred;            // Broadcast mispred to squash backend
    wire<BR_MASK_WIDTH> br_mask;            // Branch mask to clear (TODO: parameterize)
    wire<BR_TAG_WIDTH>  br_tag;
  } to_back;
} IduIO;

// Private Rename Module Interface
typedef struct RenIO {
  // --- Inputs ---
  struct {
    wire<1>     valid[FETCH_WIDTH];
    DecRenUop   uop[FETCH_WIDTH];
  } from_dec;

  struct {
    wire<1>  ready;              // Dispatch ready signal
  } from_dis;

  struct {
    wire<1>  flush;              // Global pipeline flush
    // Commit info needed for archiving RAT and freeing regs
    wire<1>  commit_valid[COMMIT_WIDTH];
    wire<AREG_IDX_WIDTH>  commit_areg[COMMIT_WIDTH];
    wire<PRF_IDX_WIDTH>   commit_preg[COMMIT_WIDTH];
    wire<1>  commit_dest_en[COMMIT_WIDTH];
  } from_rob;

  struct {
    // Wakeup signals from backend
    wire<1>  wake_valid[MAX_WAKEUP_PORTS];
    wire<PRF_IDX_WIDTH>  wake_preg[MAX_WAKEUP_PORTS];
  } from_back;

  // --- Outputs ---
  struct {
    wire<1>  ready;              // Ready to receive from IDU
  } to_dec;

  struct {
    wire<1>     valid[FETCH_WIDTH];
    RenDisUop   uop[FETCH_WIDTH]; // Renamed instructions
  } to_dis;
} RenIO;

// Private Dispatch Module Interface
typedef struct DispatchIO {
  // --- Inputs ---
  struct {
    wire<1>     valid[FETCH_WIDTH];
    RenDisUop   uop[FETCH_WIDTH];
  } from_ren;

  struct {
    wire<1>  ready;              // ROB ready
    wire<1>  full;               // ROB full/stall
  } from_rob;

  struct {
    int      ready_num[IQ_NUM];  // Free slots in each IQ
  } from_iss;

  struct {
    wire<1>  flush;
  } from_back;

  // --- Outputs ---
  struct {
    wire<1>  ready;              // Ready to receive from Rename
  } to_ren;

  struct {
    wire<1>     valid[FETCH_WIDTH];
    RobUop      uop[FETCH_WIDTH]; // Instructions dispatched to ROB
  } to_rob;

  struct {
    wire<1>     valid[IQ_NUM][MAX_IQ_DISPATCH_WIDTH];
    DisIssUop   uop[IQ_NUM][MAX_IQ_DISPATCH_WIDTH]; // uops to IQs
  } to_iss;
} DispatchIO;

// Private Issue Module Interface
typedef struct IssueIO {
  // --- Inputs ---
  struct {
    wire<1>     valid[IQ_NUM][MAX_IQ_DISPATCH_WIDTH];
    DisIssUop   uop[IQ_NUM][MAX_IQ_DISPATCH_WIDTH];
  } from_dis;

  struct {
    wire<1>  flush;
  } from_back;

  // --- Outputs ---
  struct {
    int      ready_num[IQ_NUM];  // Feedback to Dispatch
  } to_dis;

  struct {
    wire<1>     valid[ISSUE_WIDTH];
    IssExeUop   uop[ISSUE_WIDTH];  // Instructions to EXE (via PRF read)
  } to_exe;

  struct {
    wire<1>  wake_valid[MAX_WAKEUP_PORTS];
    wire<PRF_IDX_WIDTH>  wake_preg[MAX_WAKEUP_PORTS];
  } awake_bus;                   // Internal/External Wakeup
} IssueIO;

// Private Execution Module Interface
typedef struct ExuIO {
  // --- Inputs ---
  struct {
    wire<1>     valid[ISSUE_WIDTH];
    IssExeUop   uop[ISSUE_WIDTH];
    wire<32>    src1_data[ISSUE_WIDTH]; // From PRF
    wire<32>    src2_data[ISSUE_WIDTH]; // From PRF
  } from_iss;

  struct {
    wire<1>  flush;
  } from_back;

  // --- Outputs ---
  struct {
    wire<1>     valid[ISSUE_WIDTH];
    ExeWbUop    uop[ISSUE_WIDTH];  // Writeback to PRF/ROB
  } to_back;
} ExuIO;

// Private ROB Module Interface
typedef struct RobIO {
  // --- Inputs ---
  struct {
    wire<1>     valid[FETCH_WIDTH];
    RobUop      uop[FETCH_WIDTH]; // Instructions from Dispatch
  } from_dis;

  struct {
    wire<1>     valid[ISSUE_WIDTH];
    ExeWbUop    uop[ISSUE_WIDTH]; // Completion from EXE
  } from_exe;

  // --- Outputs ---
  struct {
    wire<1>  stall;              // To Dispatch
  } to_dis;

  struct {
    wire<1>     commit_valid[COMMIT_WIDTH];
    wire<AREG_IDX_WIDTH>     commit_areg[COMMIT_WIDTH];
    wire<PRF_IDX_WIDTH>      commit_preg[COMMIT_WIDTH];
    wire<1>     commit_dest_en[COMMIT_WIDTH];
  } to_ren;

  struct {
    wire<1>  flush;              // Global broadcast
  } to_all;
} RobIO;
