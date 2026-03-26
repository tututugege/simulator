#pragma once

#include "IO.h"

class MemReadArbBlock {
public:
  enum class Owner : uint8_t {
    NONE = 0,
    LSU,
    PTW_DTLB,
    PTW_ITLB,
    PTW_WALK,
  };

  struct IssuedTag {
    bool valid = false;
    Owner owner = Owner::NONE;
    uint32_t req_addr = 0;
    size_t req_id = 0;
    MicroOp uop = {};
  };

  struct CombResult {
    LsuDcacheIO dcache_req = {};
    IssuedTag issued_tags[LSU_LDU_COUNT] = {};
    IssuedTag preempted_lsu_tag = {};
    Owner granted_owner = Owner::NONE;
    bool granted = false;
    bool lsu_port0_preempted = false;
    int injected_port = -1;
  };

  static constexpr size_t kPtwReqIdBase = static_cast<size_t>(LDQ_SIZE);
  static constexpr size_t kFirstDynamicPtwReqId =
      static_cast<size_t>(LDQ_SIZE) + 16;

  void init() {
    reset_state(cur_);
    reset_state(nxt_);
    cur_.next_ptw_req_id = kFirstDynamicPtwReqId;
    nxt_.next_ptw_req_id = kFirstDynamicPtwReqId;
    reset_comb_result(comb_);
  }

  void eval_comb(const LsuDcacheIO *lsu_req_io, bool issue_ptw_walk_read,
                 uint32_t ptw_walk_read_addr, bool has_ptw_dtlb,
                 uint32_t ptw_dtlb_addr, bool has_ptw_itlb,
                 uint32_t ptw_itlb_addr) {
    reset_comb_result(comb_);
    comb_.dcache_req = {};
    nxt_ = cur_;

    if (lsu_req_io != nullptr) {
      comb_.dcache_req = *lsu_req_io;
      for (int i = 0; i < LSU_LDU_COUNT; i++) {
        const auto &req = lsu_req_io->req_ports.load_ports[i];
        if (!req.valid) {
          continue;
        }
        comb_.issued_tags[i].valid = true;
        comb_.issued_tags[i].owner = Owner::LSU;
        comb_.issued_tags[i].req_addr = req.addr;
        comb_.issued_tags[i].req_id = req.req_id;
        comb_.issued_tags[i].uop = req.uop;
      }
    }

    Owner ptw_owner = Owner::NONE;
    uint32_t ptw_addr = 0;
    if (issue_ptw_walk_read) {
      ptw_owner = Owner::PTW_WALK;
      ptw_addr = ptw_walk_read_addr;
    } else if (has_ptw_dtlb) {
      ptw_owner = Owner::PTW_DTLB;
      ptw_addr = ptw_dtlb_addr;
    } else if (has_ptw_itlb) {
      ptw_owner = Owner::PTW_ITLB;
      ptw_addr = ptw_itlb_addr;
    }

    if (ptw_owner == Owner::NONE) {
      return;
    }

    int inject_port = -1;
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      if (!comb_.dcache_req.req_ports.load_ports[i].valid) {
        inject_port = i;
        break;
      }
    }
    if (inject_port < 0) {
      inject_port = 0;
      comb_.lsu_port0_preempted = comb_.issued_tags[0].valid;
      if (comb_.lsu_port0_preempted &&
          comb_.issued_tags[0].owner == Owner::LSU) {
        comb_.preempted_lsu_tag = comb_.issued_tags[0];
      }
    }

    auto &ptw_req = comb_.dcache_req.req_ports.load_ports[inject_port];
    const size_t ptw_req_id = cur_.next_ptw_req_id;
    ptw_req = {};
    ptw_req.valid = true;
    ptw_req.addr = ptw_addr;
    ptw_req.req_id = ptw_req_id;
    ptw_req.uop = {};

    comb_.issued_tags[inject_port].valid = true;
    comb_.issued_tags[inject_port].owner = ptw_owner;
    comb_.issued_tags[inject_port].req_addr = ptw_addr;
    comb_.issued_tags[inject_port].req_id = ptw_req_id;
    comb_.issued_tags[inject_port].uop = {};

    comb_.granted = true;
    comb_.granted_owner = ptw_owner;
    comb_.injected_port = inject_port;
    nxt_.next_ptw_req_id = next_ptw_req_id(cur_.next_ptw_req_id);
  }

  void update_seq() { cur_ = nxt_; }

  const CombResult &comb_result() const { return comb_; }

private:
  struct State {
    size_t next_ptw_req_id = kFirstDynamicPtwReqId;
  };

  static void reset_state(State &state) {
    state.next_ptw_req_id = kFirstDynamicPtwReqId;
  }

  static void reset_comb_result(CombResult &result) {
    result.dcache_req = LsuDcacheIO{};
    for (auto &tag : result.issued_tags) {
      tag = IssuedTag{};
    }
    result.preempted_lsu_tag = IssuedTag{};
    result.granted_owner = Owner::NONE;
    result.granted = false;
    result.lsu_port0_preempted = false;
    result.injected_port = -1;
  }

  static size_t next_ptw_req_id(size_t cur_req_id) {
    const size_t next_req_id = cur_req_id + 1;
    if (next_req_id < kFirstDynamicPtwReqId) {
      return kFirstDynamicPtwReqId;
    }
    return next_req_id;
  }

  State cur_{};
  State nxt_{};
  CombResult comb_{};
};
