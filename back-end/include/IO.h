#pragma once

#include "InstructionBuffer.h"
#include "config.h"
#include "wire_types.h"
#include "util.h"
#include <bitset>
#include <cstdint>
#include <cstring>
#include <sys/types.h>

struct DecRenIO {
  struct DecRenInst {
    wire<32> diag_val;
    wire<AREG_IDX_WIDTH> dest_areg;
    wire<AREG_IDX_WIDTH> src1_areg;
    wire<AREG_IDX_WIDTH> src2_areg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> ftq_is_last;

    wire<INST_TYPE_WIDTH> type;
    wire<1> dest_en;
    wire<1> src1_en;
    wire<1> src2_en;
    wire<1> is_atomic;
    wire<1> src1_is_pc;
    wire<1> src2_is_imm;
    wire<3> func3;
    wire<7> func7;
    wire<32> imm;
    wire<BR_TAG_WIDTH> br_id;
    wire<BR_MASK_WIDTH> br_mask;
    wire<CSR_IDX_WIDTH> csr_idx;

    wire<ROB_CPLT_MASK_WIDTH> expect_mask;
    wire<ROB_CPLT_MASK_WIDTH> cplt_mask;

    wire<1> page_fault_inst;
    wire<1> illegal_inst;

    TmaMeta tma;
    DebugMeta dbg;

    DecRenInst() { std::memset(this, 0, sizeof(DecRenInst)); }
  };

  DecRenInst uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];
  DecRenIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
  }
};

inline bool is_store(const DecRenIO::DecRenInst &uop) {
  return decode_inst_type(uop.type) == STORE ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_load(const DecRenIO::DecRenInst &uop) {
  return decode_inst_type(uop.type) == LOAD ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_exception(const DecRenIO::DecRenInst &uop) {
  return uop.page_fault_inst || uop.illegal_inst ||
         decode_inst_type(uop.type) == ECALL;
}

inline bool is_flush_inst(const DecRenIO::DecRenInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == CSR || type == ECALL || type == MRET || type == SRET ||
         type == SFENCE_VMA || type == FENCE_I || is_exception(uop) ||
         type == EBREAK;
}

struct RenDecIO {

  wire<1> ready;

  RenDecIO() { ready = {}; }
};

// IDU -> PreIduQueue consume handshake (only what PreIduQueue needs).
struct IduConsumeIO {
  wire<1> fire[DECODE_WIDTH];
  IduConsumeIO() {
    for (auto &v : fire)
      v = {};
  }
};

struct PreFrontIO {

  wire<1> fire[FETCH_WIDTH];
  wire<1> ready;

  PreFrontIO() {
    for (auto &v : fire)
      v = {};
    ready = {};
  }
};

struct FrontPreIO {

  wire<32> inst[FETCH_WIDTH];
  wire<32> pc[FETCH_WIDTH];
  wire<1> valid[FETCH_WIDTH];
  wire<1> front_stall;
  wire<1> predict_dir[FETCH_WIDTH];

  wire<1> alt_pred[FETCH_WIDTH];
  pcpn_t altpcpn[FETCH_WIDTH];
  pcpn_t pcpn[FETCH_WIDTH];
  wire<32> predict_next_fetch_address[FETCH_WIDTH];
  tage_idx_t tage_idx[FETCH_WIDTH][4]; // TN_MAX = 4
  tage_tag_t tage_tag[FETCH_WIDTH][4]; // TN_MAX = 4
  wire<1> sc_used[FETCH_WIDTH];
  wire<1> sc_pred[FETCH_WIDTH];
  tage_scl_meta_sum_t sc_sum[FETCH_WIDTH];
  tage_scl_meta_idx_t sc_idx[FETCH_WIDTH][BPU_SCL_META_NTABLE];
  wire<1> loop_used[FETCH_WIDTH];
  wire<1> loop_hit[FETCH_WIDTH];
  wire<1> loop_pred[FETCH_WIDTH];
  tage_loop_meta_idx_t loop_idx[FETCH_WIDTH];
  tage_loop_meta_tag_t loop_tag[FETCH_WIDTH];
  wire<1> page_fault_inst[FETCH_WIDTH];

  FrontPreIO() {
    for (auto &v : inst)
      v = {};
    for (auto &v : pc)
      v = {};
    for (auto &v : valid)
      v = {};
    front_stall = {};
    for (auto &v : predict_dir)
      v = {};
    for (auto &v : alt_pred)
      v = {};
    for (auto &v : altpcpn)
      v = {};
    for (auto &v : pcpn)
      v = {};
    for (auto &v : predict_next_fetch_address)
      v = {};
    for (auto &tage_idx_0 : tage_idx)
      for (auto &idx : tage_idx_0)
        idx = {};
    for (auto &tage_tag_0 : tage_tag)
      for (auto &tag : tage_tag_0)
        tag = {};

    for (auto &v : sc_used)
      v = {};
    for (auto &v : sc_pred)
      v = {};
    for (auto &v : sc_sum)
      v = {};
    for (auto &sc_idx_0 : sc_idx)
      for (auto &idx : sc_idx_0)
        idx = {};
    for (auto &v : loop_used)
      v = {};
    for (auto &v : loop_hit)
      v = {};
    for (auto &v : loop_pred)
      v = {};
    for (auto &v : loop_idx)
      v = {};
    for (auto &v : loop_tag)
      v = {};

    for (auto &v : page_fault_inst)
      v = {};
  }
};

struct DecBroadcastIO {

  wire<1> mispred;
  wire<BR_MASK_WIDTH> br_mask;
  wire<BR_TAG_WIDTH> br_id;
  wire<ROB_IDX_WIDTH> redirect_rob_idx;
  wire<BR_MASK_WIDTH> clear_mask; // Bits to clear from all in-flight br_masks
                                  // (resolved branches)

  DecBroadcastIO() {
    mispred = {};
    br_mask = {};
    br_id = {};
    redirect_rob_idx = {};
    clear_mask = 0;
  }
};

struct FtqPcReadReq {
  wire<1> valid;
  wire<FTQ_IDX_WIDTH> ftq_idx;
  wire<FTQ_OFFSET_WIDTH> ftq_offset;

  FtqPcReadReq() {
    valid = {};
    ftq_idx = {};
    ftq_offset = {};
  }
};

struct FtqPcReadResp {
  wire<1> valid;
  wire<1> entry_valid;
  wire<32> pc;
  wire<1> pred_taken;
  wire<32> next_pc;

  FtqPcReadResp() {
    valid = {};
    entry_valid = {};
    pc = {};
    pred_taken = {};
    next_pc = {};
  }
};

struct FtqPrfPcReqIO {
  FtqPcReadReq req[FTQ_PRF_PC_PORT_NUM];

  FtqPrfPcReqIO() {
    for (auto &v : req)
      v = {};
  }
};

struct FtqPrfPcRespIO {
  FtqPcReadResp resp[FTQ_PRF_PC_PORT_NUM];

  FtqPrfPcRespIO() {
    for (auto &v : resp)
      v = {};
  }
};

struct FtqRobPcReqIO {
  FtqPcReadReq req[FTQ_ROB_PC_PORT_NUM];

  FtqRobPcReqIO() {
    for (auto &v : req)
      v = {};
  }
};

struct FtqRobPcRespIO {
  FtqPcReadResp resp[FTQ_ROB_PC_PORT_NUM];

  FtqRobPcRespIO() {
    for (auto &v : resp)
      v = {};
  }
};

struct PreIssueIO {
  InstructionBufferEntry entries[DECODE_WIDTH];

  PreIssueIO() {
    for (auto &e : entries) {
      e = {};
    }
  }
};

// Backward-compatible alias for existing modules.
using PreIduIssueIO = PreIssueIO;

struct RobCommitIO {
  struct RobCommitInst {
    wire<32> diag_val;
    wire<AREG_IDX_WIDTH> dest_areg;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> old_dest_preg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> ftq_is_last;

    wire<1> mispred;
    wire<1> br_taken;

    wire<1> dest_en;
    wire<7> func7;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<1> rob_flag;
    wire<STQ_IDX_WIDTH> stq_idx;
    wire<1> stq_flag;

    wire<1> page_fault_inst;
    wire<1> page_fault_load;
    wire<1> page_fault_store;
    wire<1> illegal_inst;

    wire<INST_TYPE_WIDTH> type;
    TmaMeta tma;
    DebugMeta dbg;
    wire<1> flush_pipe;

    RobCommitInst() { std::memset(this, 0, sizeof(RobCommitInst)); }

    InstEntry to_inst_entry(wire<1> valid) const {
      InstEntry dst;
      dst.valid = valid;
      dst.uop.diag_val = diag_val;
      dst.uop.dest_areg = dest_areg;
      dst.uop.dest_preg = dest_preg;
      dst.uop.old_dest_preg = old_dest_preg;
      dst.uop.ftq_idx = ftq_idx;
      dst.uop.ftq_offset = ftq_offset;
      dst.uop.ftq_is_last = ftq_is_last;
      dst.uop.mispred = mispred;
      dst.uop.br_taken = br_taken;
      dst.uop.dest_en = dest_en;
      dst.uop.func7 = func7;
      dst.uop.rob_idx = rob_idx;
      dst.uop.rob_flag = rob_flag;
      dst.uop.stq_idx = stq_idx;
      dst.uop.stq_flag = stq_flag;
      dst.uop.page_fault_inst = page_fault_inst;
      dst.uop.page_fault_load = page_fault_load;
      dst.uop.page_fault_store = page_fault_store;
      dst.uop.illegal_inst = illegal_inst;
      dst.uop.type = decode_inst_type(type);
      dst.uop.tma = tma;
      dst.uop.dbg = dbg;
      dst.uop.flush_pipe = flush_pipe;
      return dst;
    }
  };

  struct RobCommitEntry {
    wire<1> valid;
    RobCommitInst uop;
  };

  RobCommitEntry commit_entry[COMMIT_WIDTH];

  RobCommitIO() {
    for (auto &v : commit_entry)
      v = {};
  }
};

inline bool is_store(const RobCommitIO::RobCommitInst &uop) {
  return decode_inst_type(uop.type) == STORE ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_load(const RobCommitIO::RobCommitInst &uop) {
  return decode_inst_type(uop.type) == LOAD ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_exception(const RobCommitIO::RobCommitInst &uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store ||
         uop.illegal_inst || decode_inst_type(uop.type) == ECALL;
}

inline bool is_flush_inst(const RobCommitIO::RobCommitInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == CSR || type == ECALL || type == MRET || type == SRET ||
         type == SFENCE_VMA || type == FENCE_I || is_exception(uop) ||
         type == EBREAK ||
         uop.flush_pipe;
}

struct RobDisIO {
  struct TmaMeta {
    wire<1> head_is_memory;
    wire<1> head_is_miss;
    wire<1> head_not_ready;
  } tma;

  wire<1> ready;
  wire<1> empty;
  wire<1> stall;
  wire<ROB_IDX_WIDTH> enq_idx;
  wire<1> rob_flag;

  RobDisIO() {
    tma.head_is_memory = {};
    tma.head_is_miss = {};
    tma.head_not_ready = {};
    ready = {};
    empty = {};
    stall = {};
    enq_idx = {};
    rob_flag = {};
  }
};

struct DisRobIO {
  struct DisRobInst {
    wire<32> diag_val;
    wire<AREG_IDX_WIDTH> dest_areg;
    wire<AREG_IDX_WIDTH> src1_areg;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> old_dest_preg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> ftq_is_last;

    wire<1> mispred;
    wire<1> br_taken;

    wire<INST_TYPE_WIDTH> type;
    wire<1> dest_en;
    wire<1> is_atomic;
    wire<3> func3;
    wire<7> func7;
    wire<32> imm;
    wire<BR_MASK_WIDTH> br_mask;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<STQ_IDX_WIDTH> stq_idx;
    wire<1> stq_flag;
    wire<LDQ_IDX_WIDTH> ldq_idx;

    wire<ROB_CPLT_MASK_WIDTH> expect_mask;
    wire<ROB_CPLT_MASK_WIDTH> cplt_mask;
    wire<1> rob_flag;

    wire<1> page_fault_inst;
    wire<1> illegal_inst;
    wire<1> flush_pipe;

    TmaMeta tma;
    DebugMeta dbg;

    DisRobInst() { std::memset(this, 0, sizeof(DisRobInst)); }
  };

  DisRobInst uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];
  wire<1> dis_fire[DECODE_WIDTH];

  DisRobIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
    for (auto &v : dis_fire)
      v = {};
  }
};

inline bool is_store(const DisRobIO::DisRobInst &uop) {
  return decode_inst_type(uop.type) == STORE ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_load(const DisRobIO::DisRobInst &uop) {
  return decode_inst_type(uop.type) == LOAD ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_exception(const DisRobIO::DisRobInst &uop) {
  return uop.page_fault_inst || uop.illegal_inst ||
         decode_inst_type(uop.type) == ECALL;
}

inline bool is_flush_inst(const DisRobIO::DisRobInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == CSR || type == ECALL || type == MRET || type == SRET ||
         type == SFENCE_VMA || type == FENCE_I || is_exception(uop) ||
         type == EBREAK ||
         uop.flush_pipe;
}

struct RenDisIO {
  struct RenDisInst {
    wire<32> diag_val;
    wire<AREG_IDX_WIDTH> dest_areg;
    wire<AREG_IDX_WIDTH> src1_areg;
    wire<AREG_IDX_WIDTH> src2_areg;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> src1_preg;
    wire<PRF_IDX_WIDTH> src2_preg;
    wire<PRF_IDX_WIDTH> old_dest_preg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> ftq_is_last;

    wire<INST_TYPE_WIDTH> type;
    wire<1> dest_en;
    wire<1> src1_en;
    wire<1> src2_en;
    wire<1> is_atomic;
    wire<1> src1_busy;
    wire<1> src2_busy;
    wire<1> src1_is_pc;
    wire<1> src2_is_imm;
    wire<3> func3;
    wire<7> func7;
    wire<32> imm;
    wire<BR_TAG_WIDTH> br_id;
    wire<BR_MASK_WIDTH> br_mask;
    wire<CSR_IDX_WIDTH> csr_idx;

    wire<ROB_CPLT_MASK_WIDTH> expect_mask;
    wire<ROB_CPLT_MASK_WIDTH> cplt_mask;

    wire<1> page_fault_inst;
    wire<1> illegal_inst;

    TmaMeta tma;
    DebugMeta dbg;

    RenDisInst() { std::memset(this, 0, sizeof(RenDisInst)); }

    static RenDisInst from_dec_ren_inst(const DecRenIO::DecRenInst &src) {
      RenDisInst dst;
      dst.diag_val = src.diag_val;
      dst.dest_areg = src.dest_areg;
      dst.src1_areg = src.src1_areg;
      dst.src2_areg = src.src2_areg;
      dst.ftq_idx = src.ftq_idx;
      dst.ftq_offset = src.ftq_offset;
      dst.ftq_is_last = src.ftq_is_last;
      dst.dest_en = src.dest_en;
      dst.src1_en = src.src1_en;
      dst.src2_en = src.src2_en;
      dst.is_atomic = src.is_atomic;
      dst.src1_is_pc = src.src1_is_pc;
      dst.src2_is_imm = src.src2_is_imm;
      dst.func3 = src.func3;
      dst.func7 = src.func7;
      dst.imm = src.imm;
      dst.br_id = src.br_id;
      dst.br_mask = src.br_mask;
      dst.csr_idx = src.csr_idx;
      dst.expect_mask = src.expect_mask;
      dst.cplt_mask = src.cplt_mask;
      dst.page_fault_inst = src.page_fault_inst;
      dst.illegal_inst = src.illegal_inst;
      dst.type = src.type;
      dst.tma = src.tma;
      dst.dbg = src.dbg;
      return dst;
    }
  };

  RenDisInst uop[DECODE_WIDTH];
  wire<1> valid[DECODE_WIDTH];

  RenDisIO() {
    for (auto &v : uop)
      v = {};
    for (auto &v : valid)
      v = {};
  }
};

inline bool is_store(const RenDisIO::RenDisInst &uop) {
  return decode_inst_type(uop.type) == STORE ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_load(const RenDisIO::RenDisInst &uop) {
  return decode_inst_type(uop.type) == LOAD ||
         (decode_inst_type(uop.type) == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_exception(const RenDisIO::RenDisInst &uop) {
  return uop.page_fault_inst || uop.illegal_inst ||
         decode_inst_type(uop.type) == ECALL;
}

inline bool is_flush_inst(const RenDisIO::RenDisInst &uop) {
  InstType type = decode_inst_type(uop.type);
  return type == CSR || type == ECALL || type == MRET || type == SRET ||
         type == SFENCE_VMA || type == FENCE_I || is_exception(uop) ||
         type == EBREAK;
}

struct DisRenIO {

  wire<1> ready;

  DisRenIO() { ready = {}; }
};

struct PrfAwakeIO {

  WakeInfo wake[LSU_LOAD_WB_WIDTH];

  PrfAwakeIO() {
    for (auto &w : wake)
      w = {};
  }
};

struct DisIssIO {
  struct DisIssUop {
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> src1_preg;
    wire<PRF_IDX_WIDTH> src2_preg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> is_atomic;

    wire<1> dest_en;
    wire<1> src1_en;
    wire<1> src2_en;
    wire<1> src1_busy;
    wire<1> src2_busy;
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

    wire<1> rob_flag;

    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;

    DisIssUop() { std::memset(this, 0, sizeof(DisIssUop)); }
  };

  struct DisIssReq {
    wire<1> valid;
    DisIssUop uop;
  };

  DisIssReq req[IQ_NUM][MAX_IQ_DISPATCH_WIDTH];
  DisIssIO() {
    for (auto &iq_req : req)
      for (auto &r : iq_req)
        r = {};
  }
};

struct IssDisIO {

  wire<IQ_READY_NUM_WIDTH> ready_num[IQ_NUM];

  IssDisIO() {
    for (auto &v : ready_num)
      v = 0;
  }
};

struct IssAwakeIO {

  WakeInfo wake[MAX_WAKEUP_PORTS];

  IssAwakeIO() {
    for (auto &v : wake)
      v = {};
  }
};

struct RobBroadcastIO {

  wire<1> flush;
  wire<1> mret;
  wire<1> sret;
  wire<1> ecall;
  wire<1> exception;
  wire<1> fence;
  wire<1> fence_i;

  wire<1> page_fault_inst;
  wire<1> page_fault_load;
  wire<1> page_fault_store;
  wire<1> illegal_inst;
  wire<1> interrupt;
  wire<32> trap_val;
  wire<32> pc;

  wire<ROB_IDX_WIDTH> head_rob_idx;
  wire<1> head_valid;
  wire<ROB_IDX_WIDTH> head_incomplete_rob_idx;
  wire<1> head_incomplete_valid;

  RobBroadcastIO() {
    flush = {};
    mret = {};
    sret = {};
    ecall = {};
    exception = {};
    fence = {};
    fence_i = {};
    page_fault_inst = {};
    page_fault_load = {};
    page_fault_store = {};
    illegal_inst = {};
    interrupt = {};
    trap_val = {};
    pc = {};

    head_rob_idx = {};
    head_valid = {};
    head_incomplete_rob_idx = {};
    head_incomplete_valid = {};
  }
};

struct IssPrfIO {
  struct IssPrfUop {
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> src1_preg;
    wire<PRF_IDX_WIDTH> src2_preg;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> is_atomic;
    wire<1> dest_en;
    wire<1> src1_en;
    wire<1> src2_en;
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

    wire<1> rob_flag;

    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;

    IssPrfUop() { std::memset(this, 0, sizeof(IssPrfUop)); }
  };

  struct IssPrfEntry {
    wire<1> valid;
    IssPrfUop uop;
  };

  IssPrfEntry iss_entry[ISSUE_WIDTH];

  IssPrfIO() {
    for (auto &v : iss_entry)
      v = {};
  }
};

struct PrfExeIO {
  struct PrfExeUop {
    wire<32> pc;
    wire<1> ftq_resp_valid;
    wire<1> ftq_pred_taken;
    wire<32> ftq_next_pc;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<PRF_IDX_WIDTH> src1_preg;
    wire<PRF_IDX_WIDTH> src2_preg;
    wire<32> src1_rdata;
    wire<32> src2_rdata;

    wire<FTQ_IDX_WIDTH> ftq_idx;
    wire<FTQ_OFFSET_WIDTH> ftq_offset;
    wire<1> is_atomic;
    wire<1> dest_en;
    wire<1> src1_en;
    wire<1> src2_en;
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

    wire<1> rob_flag;

    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;

    PrfExeUop() { std::memset(this, 0, sizeof(PrfExeUop)); }

    static PrfExeUop from_iss_prf_uop(const IssPrfIO::IssPrfUop &src) {
      PrfExeUop dst;
      dst.dest_preg = src.dest_preg;
      dst.src1_preg = src.src1_preg;
      dst.src2_preg = src.src2_preg;
      dst.ftq_idx = src.ftq_idx;
      dst.ftq_offset = src.ftq_offset;
      dst.is_atomic = src.is_atomic;
      dst.dest_en = src.dest_en;
      dst.src1_en = src.src1_en;
      dst.src2_en = src.src2_en;
      dst.src1_is_pc = src.src1_is_pc;
      dst.src2_is_imm = src.src2_is_imm;
      dst.func3 = src.func3;
      dst.func7 = src.func7;
      dst.imm = src.imm;
      dst.br_id = src.br_id;
      dst.br_mask = src.br_mask;
      dst.csr_idx = src.csr_idx;
      dst.rob_idx = src.rob_idx;
      dst.stq_idx = src.stq_idx;
      dst.stq_flag = src.stq_flag;
      dst.ldq_idx = src.ldq_idx;
      dst.rob_flag = src.rob_flag;
      dst.op = src.op;
      dst.dbg = src.dbg;
      return dst;
    }
  };

  struct PrfExeEntry {
    wire<1> valid;
    PrfExeUop uop;
  };

  PrfExeEntry iss_entry[ISSUE_WIDTH];

  PrfExeIO() {
    for (auto &v : iss_entry)
      v = {};
  }
};

struct ExePrfIO {
  struct ExePrfWbUop {
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<32> result;
    wire<BR_MASK_WIDTH> br_mask;
    wire<1> dest_en;
    wire<UOP_TYPE_WIDTH> op;

    ExePrfWbUop() { std::memset(this, 0, sizeof(ExePrfWbUop)); }

    static ExePrfWbUop from_micro_op(const MicroOp &src) {
      ExePrfWbUop dst;
      dst.dest_preg = src.dest_preg;
      dst.result = src.result;
      dst.br_mask = src.br_mask;
      dst.dest_en = src.dest_en;
      dst.op = encode_uop_type(src.op);
      return dst;
    }
  };

  struct ExePrfEntry {
    wire<1> valid;
    ExePrfWbUop uop;
  };

  ExePrfEntry entry[ISSUE_WIDTH];
  ExePrfEntry bypass[TOTAL_FU_COUNT];

  ExePrfIO() {
    for (auto &v : entry)
      v = {};

    for (auto &v : bypass)
      v = {};
  }
};

struct ExeIssIO {

  wire<MAX_UOP_TYPE> fu_ready_mask[ISSUE_WIDTH];

  ExeIssIO() {
    for (auto &v : fu_ready_mask)
      v = {};
  }
};

struct ExuRobIO {
  struct ExuRobUop {
    wire<32> diag_val;
    wire<32> result;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<1> mispred;
    wire<1> br_taken;
    wire<1> page_fault_inst;
    wire<1> page_fault_load;
    wire<1> page_fault_store;
    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;
    wire<1> flush_pipe;

    ExuRobUop() { std::memset(this, 0, sizeof(ExuRobUop)); }

    static ExuRobUop from_micro_op(const MicroOp &src) {
      ExuRobUop dst;
      dst.diag_val = src.diag_val;
      dst.result = src.result;
      dst.rob_idx = src.rob_idx;
      dst.mispred = src.mispred;
      dst.br_taken = src.br_taken;
      dst.page_fault_inst = src.page_fault_inst;
      dst.page_fault_load = src.page_fault_load;
      dst.page_fault_store = src.page_fault_store;
      dst.op = encode_uop_type(src.op);
      dst.dbg = src.dbg;
      dst.flush_pipe = src.flush_pipe;
      return dst;
    }
  };

  struct ExuRobEntry {
    wire<1> valid;
    ExuRobUop uop;
  };

  ExuRobEntry entry[ISSUE_WIDTH];

  ExuRobIO() {
    for (auto &v : entry)
      v = {};
  }
};

struct ExuIdIO {

  wire<1> mispred;
  wire<32> redirect_pc;
  wire<ROB_IDX_WIDTH> redirect_rob_idx;
  wire<BR_TAG_WIDTH> br_id;
  wire<FTQ_IDX_WIDTH>
      ftq_idx; // FTQ index of mispredicting branch, for tail recovery
  wire<BR_MASK_WIDTH>
      clear_mask; // OR of all resolved branches' (1 << br_id) this cycle

  ExuIdIO() {
    mispred = {};
    redirect_pc = {};
    redirect_rob_idx = {};
    br_id = {};
    ftq_idx = 0;
    clear_mask = 0;
  }
};

struct ExeCsrIO {

  wire<1> we;
  wire<1> re;
  wire<12> idx;
  wire<32> wdata;
  wire<32> wcmd;

  ExeCsrIO() {
    we = {};
    re = {};
    idx = {};
    wdata = {};
    wcmd = {};
  }
};

struct CsrExeIO {

  wire<32> rdata;

  CsrExeIO() { rdata = {}; }
};

struct CsrRobIO {

  wire<1> interrupt_req;

  CsrRobIO() { interrupt_req = {}; }
};

struct CsrFrontIO {

  wire<32> epc;
  wire<32> trap_pc;

  CsrFrontIO() {
    epc = {};
    trap_pc = {};
  }
};

struct CsrStatusIO {

  wire<32> sstatus;
  wire<32> mstatus;
  wire<32> satp;
  wire<2> privilege;

  CsrStatusIO() {
    sstatus = {};
    mstatus = {};
    satp = {};
    privilege = {};
  }
};

struct RobCsrIO {

  wire<1> interrupt_resp;
  wire<1> commit;

  RobCsrIO() {
    interrupt_resp = {};
    commit = {};
  }
};

struct PtwWalkReq {
  wire<32> vaddr;
  wire<32> satp;
  wire<32> access_type; // 0=Fetch, 1=Load, 2=Store

  PtwWalkReq() {
    vaddr = {};
    satp = {};
    access_type = {};
  }
};

struct PtwWalkResp {
  wire<1> fault;
  wire<32> vaddr;
  wire<32> leaf_pte;
  wire<8> leaf_level; // 1: L1 leaf, 0: L0 leaf

  PtwWalkResp() {
    fault = {};
    vaddr = {};
    leaf_pte = {};
    leaf_level = {};
  }
};

struct PtwMemRespIO {
  wire<1> valid;
  wire<32> data;

  PtwMemRespIO() {
    valid = {};
    data = {};
  }
};

struct PtwWalkRespIO {
  wire<1> valid;
  PtwWalkResp resp;

  PtwWalkRespIO() {
    valid = {};
    resp = {};
  }
};

struct MemReqMeta {
  wire<ROB_IDX_WIDTH> rob_idx;

  MemReqMeta() { std::memset(this, 0, sizeof(MemReqMeta)); }
};

struct MemRespMeta {
  wire<ROB_IDX_WIDTH> rob_idx;
  bool is_cache_miss;
  bool difftest_skip;

  MemRespMeta() { std::memset(this, 0, sizeof(MemRespMeta)); }
};

struct MemReqIO {

  wire<1> en;
  wire<1> wen;
  wire<32> addr;
  wire<32> wdata;
  wire<8> wstrb;

  MemReqMeta meta;

  MemReqIO() {
    en = {};
    wen = {};
    addr = {};
    wdata = {};
    wstrb = {};
    meta = {};
  }
};

struct MemReadyIO {

  wire<1> ready;

  MemReadyIO() { ready = {}; }
};

struct MemRespIO {

  wire<1> wen;
  wire<1> valid;
  wire<32> data;

  wire<32> addr;
  MemRespMeta meta;

  MemRespIO() {
    wen = {};
    valid = {};
    data = {};
    addr = {};
    meta = {};
  }
};

struct PeripheralReqIO {
  wire<1> is_mmio;
  wire<1> wen;
  wire<32> mmio_addr;
  wire<32> mmio_wdata;
  wire<3> mmio_fun3;

  PeripheralReqIO() {
    is_mmio = {};
    wen = {};
    mmio_addr = {};
    mmio_wdata = {};
    mmio_fun3 = {};
  }
};
struct PeripheralRespIO {
  wire<1> is_mmio;
  wire<1> ready;
  wire<32> mmio_rdata;

  PeripheralRespIO() {
    is_mmio = {};
    ready = {};
    mmio_rdata = {};
  }
};
enum class ReplayType : wire<2> {
  HIT = 0,
  CONFLICT = 1,//mshr conflict, fill confilct
  MSHR_HIT = 2,
  MSHR_FULL = 3,
};

enum class StoreState : uint8_t {
  Empty,
  Allocated,
  WaitAddr,
  WaitData,
  WaitTlb,
  Ready,
  Committed,
  WaitDcacheResp,
  WaitMmioResp,
  Replaying,
  PageFault,
  Done
};
// STQ 条目结构（定义在此以供 StoreReq 使用）
struct StqEntry {

  wire<1> data_valid = false;
  wire<32> data = 0;
  wire<3>  func3 = 0;



  wire<1> vaddr_valid = false;
  wire<32> vaddr = 0;

  wire<1> paddr_valid = false;
  wire<32> paddr = 0;
  wire<1> page_fault = false;
  
  wire<1> suppress_write = 0; // For MMIO: bits to suppress in the write (e.g., for LR/SC)

  wire<1> is_mmio = false;
  wire<1> is_lrsc = false;
  wire<1> sc_pass = false; // For SC: whether the store-conditional succeeded
  wire<PRF_IDX_WIDTH> dest_preg = 0; // SC returns 0/1 through STA writeback

  StoreState store_state = StoreState::Empty;

  ReplayType replay_type = ReplayType::HIT;


  wire<BR_MASK_WIDTH> br_mask = {};
  wire<ROB_IDX_WIDTH> rob_idx = 0;
  wire<1> rob_flag = 0;
  wire<1> stq_flag = 0;
};

struct LoadReq {
  wire<1> valid;
  wire<32> addr;
  wire<32> req_id;
};

struct StoreReq {
  wire<1> valid;
  wire<32> addr;
  wire<32> data;
  wire<8> strb;
  wire<32> req_id;
};


// Load响应结构
struct LoadResp {
  wire<1> valid;
  wire<32> data;
  wire<32> req_id;
  ReplayType replay;
};

// Store响应结构
struct StoreResp {
  wire<1> valid;
  ReplayType replay;
  wire<32>  req_id;
};

// 请求端口集合（支持4个Load + 4个Store）
struct DCacheReqPorts {

  LoadReq load_ports[LSU_LDU_COUNT];
  StoreReq store_ports[LSU_STA_COUNT];

  void clear() {
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      load_ports[i].valid = false;
    }
    for (int i = 0; i < LSU_STA_COUNT; i++) {
      store_ports[i].valid = false;
    }
  }
};

// 响应端口集合
struct DCacheRespPorts {
  LoadResp load_resps[LSU_LDU_COUNT];
  StoreResp store_resps[LSU_STA_COUNT];

  void clear() {
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      load_resps[i].valid = false;
    }
    for (int i = 0; i < LSU_STA_COUNT; i++) {
      store_resps[i].valid = false;
    }
  }
};
enum class MMUResultType : wire<2> {
  MISS = 0,
  HIT = 1,
  PAGE_FAULT = 2,
};
struct MMUReq{
  wire<1> valid;
  wire<32> vaddr;
};
struct MMUResp{
  wire<1> valid;
  wire<32> paddr;
  MMUResultType result;
};
struct MMULsuIO{
  MMUResp ldq_resp[LSU_LDU_COUNT];
  MMUResp stq_resp[LSU_STA_COUNT];
};

struct LsuMMUIO{
  MMUReq ldq_req[LSU_LDU_COUNT];
  MMUReq stq_req[LSU_STA_COUNT];
  CsrStatusIO csr_status;
};

struct LsuDcacheIO {
  DCacheReqPorts req_ports;
  wire<LSU_LDU_WIDTH+1> icache_req;

  LsuDcacheIO() { icache_req = LSU_LDU_COUNT; }
};

struct DcacheLsuIO {
  DCacheRespPorts resp_ports;
  wire<1> mshr_fill;
};

struct LsuDisIO {

  wire<STQ_IDX_WIDTH> stq_tail;                     // 当前分配指针
  wire<1> stq_tail_flag;                            // stq_tail 对应 ring 代际位
  wire<bit_width_for_count(STQ_SIZE + 1)> stq_free; // 剩余空闲条目数
  wire<bit_width_for_count(LDQ_SIZE + 1)> ldq_free; // 剩余 Load 队列空闲数
  wire<LDQ_IDX_WIDTH> ldq_alloc_idx[MAX_LDQ_DISPATCH_WIDTH];
  wire<1> ldq_alloc_valid[MAX_LDQ_DISPATCH_WIDTH];

  LsuDisIO() {
    stq_tail = 0;
    stq_tail_flag = 0;
    stq_free = 0;
    ldq_free = 0;
    for (auto &v : ldq_alloc_idx)
      v = 0;
    for (auto &v : ldq_alloc_valid)
      v = 0;
  }
};

struct LsuRobIO {
  struct TmaMeta {
    std::bitset<ROB_NUM> miss_mask;
  } tma;
  wire<1> committed_store_pending;

  LsuRobIO() {
    tma.miss_mask.reset();
    committed_store_pending = 0;
  }
};

struct LsuExeIO {
  struct LsuExeRespUop {
    wire<32> diag_val;
    wire<32> result;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<BR_MASK_WIDTH> br_mask;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<1> dest_en;
    wire<1> page_fault_load;
    wire<1> page_fault_store;
    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;
    wire<1> flush_pipe;

    LsuExeRespUop() { std::memset(this, 0, sizeof(LsuExeRespUop)); }

    static LsuExeRespUop from_micro_op(const MicroOp &src) {
      LsuExeRespUop dst;
      dst.diag_val = src.diag_val;
      dst.result = src.result;
      dst.dest_preg = src.dest_preg;
      dst.br_mask = src.br_mask;
      dst.rob_idx = src.rob_idx;
      dst.dest_en = src.dest_en;
      dst.page_fault_load = src.page_fault_load;
      dst.page_fault_store = src.page_fault_store;
      dst.op = encode_uop_type(src.op);
      dst.dbg = src.dbg;
      dst.flush_pipe = src.flush_pipe;
      return dst;
    }

    MicroOp to_micro_op() const {
      MicroOp dst;
      dst.diag_val = diag_val;
      dst.result = result;
      dst.dest_preg = dest_preg;
      dst.br_mask = br_mask;
      dst.rob_idx = rob_idx;
      dst.dest_en = dest_en;
      dst.page_fault_load = page_fault_load;
      dst.page_fault_store = page_fault_store;
      dst.op = decode_uop_type(op);
      dst.dbg = dbg;
      dst.flush_pipe = flush_pipe;
      return dst;
    }
  };

  struct LsuExeRespEntry {
    wire<1> valid;
    LsuExeRespUop uop;
  };

  LsuExeRespEntry wb_req[LSU_LOAD_WB_WIDTH];
  LsuExeRespEntry sta_wb_req[LSU_STA_COUNT];

  LsuExeIO() {
    for (auto &v : wb_req)
      v = {};
    for (auto &v : sta_wb_req)
      v = {};
  }
};

struct DisLsuIO {

  wire<1> alloc_req[MAX_STQ_DISPATCH_WIDTH];
  wire<BR_MASK_WIDTH> br_mask[MAX_STQ_DISPATCH_WIDTH];
  wire<3> func3[MAX_STQ_DISPATCH_WIDTH];
  wire<ROB_IDX_WIDTH> rob_idx[MAX_STQ_DISPATCH_WIDTH];
  wire<1> rob_flag[MAX_STQ_DISPATCH_WIDTH];
  wire<1> stq_flag[MAX_STQ_DISPATCH_WIDTH];

  wire<1> ldq_alloc_req[MAX_LDQ_DISPATCH_WIDTH];
  wire<LDQ_IDX_WIDTH> ldq_idx[MAX_LDQ_DISPATCH_WIDTH];
  wire<BR_MASK_WIDTH> ldq_br_mask[MAX_LDQ_DISPATCH_WIDTH];
  wire<ROB_IDX_WIDTH> ldq_rob_idx[MAX_LDQ_DISPATCH_WIDTH];
  wire<1> ldq_rob_flag[MAX_LDQ_DISPATCH_WIDTH];

  DisLsuIO() {
    for (auto &v : alloc_req)
      v = 0;
    for (auto &v : br_mask)
      v = 0;
    for (auto &v : func3)
      v = 0;
    for (auto &v : rob_idx)
      v = 0;
    for (auto &v : rob_flag)
      v = 0;
    for (auto &v : stq_flag)
      v = 0;
    for (auto &v : ldq_alloc_req)
      v = 0;
    for (auto &v : ldq_idx)
      v = 0;
    for (auto &v : ldq_br_mask)
      v = 0;
    for (auto &v : ldq_rob_idx)
      v = 0;
    for (auto &v : ldq_rob_flag)
      v = 0;
  }
};

struct ExeLsuIO {
  struct ExeLsuReqUop {
    wire<32> result;
    wire<PRF_IDX_WIDTH> dest_preg;
    wire<3> func3;
    wire<7> func7;
    wire<1> is_atomic;
    wire<BR_MASK_WIDTH> br_mask;
    wire<ROB_IDX_WIDTH> rob_idx;
    wire<STQ_IDX_WIDTH> stq_idx;
    wire<1> stq_flag;
    wire<LDQ_IDX_WIDTH> ldq_idx;
    wire<1> rob_flag;
    wire<1> dest_en;
    wire<UOP_TYPE_WIDTH> op;
    DebugMeta dbg;

    ExeLsuReqUop() { std::memset(this, 0, sizeof(ExeLsuReqUop)); }

    MicroOp to_micro_op() const {
      MicroOp dst;
      dst.result = result;
      dst.dest_preg = dest_preg;
      dst.func3 = func3;
      dst.func7 = func7;
      dst.is_atomic = is_atomic;
      dst.br_mask = br_mask;
      dst.rob_idx = rob_idx;
      dst.stq_idx = stq_idx;
      dst.stq_flag = stq_flag;
      dst.ldq_idx = ldq_idx;
      dst.rob_flag = rob_flag;
      dst.dest_en = dest_en;
      dst.op = decode_uop_type(op);
      dst.dbg = dbg;
      return dst;
    }
  };

  struct ExeLsuReqEntry {
    wire<1> valid;
    ExeLsuReqUop uop;
  };

  ExeLsuReqEntry agu_req[LSU_AGU_COUNT];
  ExeLsuReqEntry sdu_req[LSU_SDU_COUNT];

  ExeLsuIO() {
    for (auto &v : agu_req)
      v = {};
    for (auto &v : sdu_req)
      v = {};
  }
};
