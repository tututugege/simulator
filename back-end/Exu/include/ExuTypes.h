#pragma once

#include "IO.h"

struct ExuInst : public PrfExeIO::PrfExeUop {
  wire<32> diag_val;
  wire<32> result;
  wire<1> mispred;
  wire<1> br_taken;
  wire<1> page_fault_inst;
  wire<1> flush_pipe;
  wire<1> ftq_entry_valid;

  ExuInst()
      : diag_val(0), result(0), mispred(0), br_taken(0), page_fault_inst(0),
        flush_pipe(0), ftq_entry_valid(0) {}

  static ExuInst from_prf_exe_uop(const PrfExeIO::PrfExeUop &src) {
    ExuInst dst;
    static_cast<PrfExeIO::PrfExeUop &>(dst) = src;
    return dst;
  }

  ExePrfIO::ExePrfWbUop to_exe_prf_wb_uop() const {
    ExePrfIO::ExePrfWbUop dst;
    dst.dest_preg = dest_preg;
    dst.result = result;
    dst.br_mask = br_mask;
    dst.dest_en = dest_en;
    dst.op = op;
    return dst;
  }

  ExuRobIO::ExuRobUop to_exu_rob_uop() const {
    ExuRobIO::ExuRobUop dst;
    dst.diag_val = diag_val;
    dst.result = result;
    dst.rob_idx = rob_idx;
    dst.mispred = mispred;
    dst.br_taken = br_taken;
    dst.page_fault_inst = page_fault_inst;
    dst.page_fault_load = false;
    dst.page_fault_store = false;
    dst.op = op;
    dst.dbg = dbg;
    dst.flush_pipe = flush_pipe;
    return dst;
  }

  ExeLsuIO::ExeLsuReqUop to_exe_lsu_req_uop() const {
    ExeLsuIO::ExeLsuReqUop dst;
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
    dst.op = op;
    dst.dbg = dbg;
    return dst;
  }
};

struct ExuEntry {
  wire<1> valid;
  ExuInst uop;
};
