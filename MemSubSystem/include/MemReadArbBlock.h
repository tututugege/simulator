#pragma once

#include "IO.h"

class MemReadArbBlock {
public:
  enum class Owner : uint8_t {
    NONE,
    LSU,
    PTW_DTLB,
    PTW_ITLB,
    PTW_WALK,
  };

  struct ArbResult {
    Owner owner = Owner::NONE;
    bool granted = false;
    MemReqIO req = {};
  };

  // 仲裁策略：LSU > PTW_WALK > PTW_DTLB > PTW_ITLB
  ArbResult arbitrate(const MemReqIO *lsu_req_io, bool issue_ptw_walk_read,
                      uint32_t ptw_walk_read_addr, bool has_ptw_dtlb,
                      uint32_t ptw_dtlb_addr, bool has_ptw_itlb,
                      uint32_t ptw_itlb_addr) const {
    ArbResult ret{};
    bool issue_lsu_read =
        (lsu_req_io != nullptr && lsu_req_io->en && !lsu_req_io->wen);

    if (issue_lsu_read) {
      ret.owner = Owner::LSU;
      ret.granted = true;
      ret.req = *lsu_req_io;
      return ret;
    }

    if (issue_ptw_walk_read) {
      ret.owner = Owner::PTW_WALK;
      ret.granted = true;
      ret.req.en = true;
      ret.req.wen = false;
      ret.req.addr = ptw_walk_read_addr;
      ret.req.uop = {};
      return ret;
    }

    if (has_ptw_dtlb) {
      ret.owner = Owner::PTW_DTLB;
      ret.granted = true;
      ret.req.en = true;
      ret.req.wen = false;
      ret.req.addr = ptw_dtlb_addr;
      ret.req.uop = {};
      return ret;
    }

    if (has_ptw_itlb) {
      ret.owner = Owner::PTW_ITLB;
      ret.granted = true;
      ret.req.en = true;
      ret.req.wen = false;
      ret.req.addr = ptw_itlb_addr;
      ret.req.uop = {};
      return ret;
    }

    return ret;
  }
};
