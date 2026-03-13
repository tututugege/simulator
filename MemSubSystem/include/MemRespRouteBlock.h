#pragma once

#include "DcacheConfig.h"
#include "MemPtwBlock.h"
#include "MemReadArbBlock.h"
#include "IO.h"
#include "util.h"
#include <cstdio>

struct load_resp {
  bool valid = false;
  uint32_t data = 0;
  MicroOp uop = {};
  size_t req_id = 0;
  uint8_t replay = 0;
  uint32_t req_addr = 0;

  static load_resp from_io(const LoadResp &resp, uint32_t req_addr = 0) {
    load_resp ret{};
    ret.valid = resp.valid;
    ret.data = resp.data;
    ret.uop = resp.uop;
    ret.req_id = resp.req_id;
    ret.replay = resp.replay;
    ret.req_addr = req_addr;
    return ret;
  }

  LoadResp to_io() const {
    LoadResp ret{};
    ret.valid = valid;
    ret.data = data;
    ret.uop = uop;
    ret.req_id = req_id;
    ret.replay = replay;
    return ret;
  }
};

struct replay_resp {
  bool replay = false;
  uint32_t replay_addr = 0;

  static replay_resp from_io(const ReplayResp &resp) {
    replay_resp ret{};
    ret.replay = resp.replay;
    ret.replay_addr = static_cast<uint32_t>(resp.replay_addr);
    return ret;
  }
};

class MemRespRouteBlock {
public:
  using Owner = MemReadArbBlock::Owner;
  using IssueTag = MemReadArbBlock::IssuedTag;
  static constexpr size_t kPtwReqIdBase = MemReadArbBlock::kPtwReqIdBase;

  struct PtwRouteEvent {
    bool valid = false;
    Owner owner = Owner::NONE;
    uint32_t data = 0;
    uint8_t replay = 0;
    uint32_t req_addr = 0;
  };

  struct ReplayWakeup {
    bool dtlb = false;
    bool itlb = false;
    bool walk = false;
  };

  struct CombOutputs {
    DcacheLsuIO lsu_resp = {};
    PtwRouteEvent ptw_event = {};
    ReplayWakeup wakeup = {};
    bool ptw_occupies_port0 = false;
    bool lsu_port0_replayed = false;
  };

  struct DebugState {
    struct TrackerState {
      bool blocked = false;
      uint8_t reason = 0;
      uint32_t req_addr = 0;
    };

    IssueTag issued_tags[LSU_LDU_COUNT] = {};
    TrackerState dtlb = {};
    TrackerState itlb = {};
    TrackerState walk = {};
    PtwRouteEvent ptw_event = {};
    ReplayWakeup wakeup = {};
    bool ptw_occupies_port0 = false;
    bool lsu_port0_replayed = false;
  };

  void init() {
    cur_ = {};
    nxt_ = {};
    comb_ = {};
  }

  void eval_comb(const DcacheLsuIO *dcache_resp_io,
                 const IssueTag (&issue_tags)[LSU_LDU_COUNT],
                 const replay_resp &replay_bcast) {
    comb_ = {};
    nxt_ = cur_;

    if (dcache_resp_io != nullptr) {
      comb_.lsu_resp.resp_ports.store_resps[0] = dcache_resp_io->resp_ports.store_resps[0];
      for (int i = 1; i < LSU_STA_COUNT; i++) {
        comb_.lsu_resp.resp_ports.store_resps[i] =
            dcache_resp_io->resp_ports.store_resps[i];
      }
      comb_.lsu_resp.resp_ports.replay_resp = dcache_resp_io->resp_ports.replay_resp;
    }

    eval_replay_wakeup(replay_bcast);
    route_load_responses(dcache_resp_io, issue_tags);

    // Keep per-port issued tags sticky until a response appears on that port.
    // This preserves owner attribution when memory latency is multi-cycle.
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      const bool got_resp =
          (dcache_resp_io != nullptr) &&
          dcache_resp_io->resp_ports.load_resps[i].valid;
      if (got_resp) {
        nxt_.issued_tags[i] = {};
        continue;
      }
      if (issue_tags[i].valid) {
        nxt_.issued_tags[i] = issue_tags[i];
        continue;
      }
    }
  }

  void update_seq() { cur_ = nxt_; }

  const CombOutputs &comb_outputs() const { return comb_; }

  DebugState debug_state() const {
    DebugState d{};
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      d.issued_tags[i] = cur_.issued_tags[i];
    }
    d.dtlb.blocked = cur_.dtlb.blocked;
    d.dtlb.reason = cur_.dtlb.reason;
    d.dtlb.req_addr = cur_.dtlb.req_addr;
    d.itlb.blocked = cur_.itlb.blocked;
    d.itlb.reason = cur_.itlb.reason;
    d.itlb.req_addr = cur_.itlb.req_addr;
    d.walk.blocked = cur_.walk.blocked;
    d.walk.reason = cur_.walk.reason;
    d.walk.req_addr = cur_.walk.req_addr;
    d.ptw_event = comb_.ptw_event;
    d.wakeup = comb_.wakeup;
    d.ptw_occupies_port0 = comb_.ptw_occupies_port0;
    d.lsu_port0_replayed = comb_.lsu_port0_replayed;
    return d;
  }

  ReplayWakeup peek_wakeup(const replay_resp &replay_bcast) const {
    ReplayWakeup wake{};
    ReplayTracker tmp{};
    eval_one_tracker(cur_.dtlb, tmp, wake.dtlb, replay_bcast);
    eval_one_tracker(cur_.itlb, tmp, wake.itlb, replay_bcast);
    eval_one_tracker(cur_.walk, tmp, wake.walk, replay_bcast);
    return wake;
  }

  void apply_ptw_event(MemPtwBlock *ptw_block) const {
    if (ptw_block == nullptr || !comb_.ptw_event.valid ||
        comb_.ptw_event.replay != 0) {
      return;
    }

    switch (comb_.ptw_event.owner) {
    case Owner::PTW_DTLB:
      ptw_block->on_mem_resp_client(MemPtwBlock::Client::DTLB,
                                    comb_.ptw_event.data);
      break;
    case Owner::PTW_ITLB:
      ptw_block->on_mem_resp_client(MemPtwBlock::Client::ITLB,
                                    comb_.ptw_event.data);
      break;
    case Owner::PTW_WALK:
      (void)ptw_block->on_walk_mem_resp(comb_.ptw_event.data);
      break;
    default:
      break;
    }
  }

private:
  static inline bool is_dbg_stuck_load_uop(const MicroOp &uop) {
    return uop.pc == 0xc038afd0u || uop.instruction == 0x04c4a783u ||
           uop.pc == 0xc006c620u || uop.instruction == 0x0144a803u;
  }

  static constexpr uint8_t REPLAY_NONE = 0;
  static constexpr uint8_t REPLAY_MSHR_FULL = 1;
  static constexpr uint8_t REPLAY_WAIT_FILL = 2;
  static constexpr uint8_t REPLAY_STRUCT = 3;

  struct ReplayTracker {
    bool blocked = false;
    uint8_t reason = REPLAY_NONE;
    uint32_t req_addr = 0;
  };

  struct State {
    IssueTag issued_tags[LSU_LDU_COUNT] = {};
    ReplayTracker dtlb = {};
    ReplayTracker itlb = {};
    ReplayTracker walk = {};
  };

  void route_load_responses(const DcacheLsuIO *dcache_resp_io,
                            const IssueTag (&issue_tags)[LSU_LDU_COUNT]) {
    if (dcache_resp_io == nullptr) {
      return;
    }

    load_resp lsu_candidates[LSU_LDU_COUNT] = {};
    PtwRouteEvent ptw_candidate{};
    bool ptw_resp_on_port0 = false;

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      const auto &raw = dcache_resp_io->resp_ports.load_resps[i];
      if (!raw.valid) {
        continue;
      }
      const bool req_is_lsu = (raw.req_id < (size_t)LDQ_SIZE);
      const auto tag_matches_req = [&](const IssueTag &tag) {
        if (!tag.valid) {
          return false;
        }
        if (req_is_lsu) {
          return tag.owner == Owner::LSU;
        }
        return tag.owner == Owner::PTW_DTLB || tag.owner == Owner::PTW_ITLB ||
               tag.owner == Owner::PTW_WALK;
      };

      IssueTag routed_tag{};
      if (tag_matches_req(cur_.issued_tags[i])) {
        routed_tag = cur_.issued_tags[i];
      } else if (tag_matches_req(issue_tags[i])) {
        // Support same-cycle request+response path (e.g., dcache hit/replay).
        routed_tag = issue_tags[i];
      } else if (!req_is_lsu) {
        // Last-resort recovery: infer PTW owner from encoded req_id range.
        routed_tag.valid = decode_ptw_owner_from_req_id(raw.req_id, routed_tag.owner);
        routed_tag.req_addr = 0;
      }

      if (!routed_tag.valid) {
        // Fallback recovery: if issued-tag tracking is lost but the response
        // still carries an LSU load token, forward it to LSU instead of
        // dropping and deadlocking the waiting LDQ entry.
        if (req_is_lsu) {
          lsu_candidates[i] = load_resp::from_io(raw, 0);
          continue;
        }
        continue;
      }

      const auto tagged = load_resp::from_io(raw, routed_tag.req_addr);
      switch (routed_tag.owner) {
      case Owner::LSU:
        lsu_candidates[i] = tagged;
        break;
      case Owner::PTW_WALK:
      case Owner::PTW_DTLB:
      case Owner::PTW_ITLB:
        if (i == 0) {
          ptw_resp_on_port0 = true;
        }
        if (!ptw_candidate.valid) {
          ptw_candidate.valid = true;
          ptw_candidate.owner = cur_.issued_tags[i].owner;
          ptw_candidate.data = tagged.data;
          ptw_candidate.replay = tagged.replay;
          ptw_candidate.req_addr = tagged.req_addr;
        }
        break;
      default:
        break;
      }
    }

    if (ptw_candidate.valid) {
      comb_.ptw_occupies_port0 = ptw_resp_on_port0;
      comb_.ptw_event = ptw_candidate;
      capture_ptw_replay(ptw_candidate);
    }

    for (int i = 1; i < LSU_LDU_COUNT; i++) {
      comb_.lsu_resp.resp_ports.load_resps[i] = lsu_candidates[i].to_io();
    }

    if (ptw_candidate.valid && ptw_resp_on_port0 && lsu_candidates[0].valid) {
      // PTW response always owns LSU load response port 0. If LSU also lands on
      // that port in the same cycle, force the LSU side into replay=3 so the
      // request is retried on the next cycle.
      auto replayed = lsu_candidates[0];
      replayed.valid = true;
      replayed.data = 0;
      replayed.replay = REPLAY_STRUCT;
      comb_.lsu_resp.resp_ports.load_resps[0] = replayed.to_io();
      comb_.lsu_port0_replayed = true;
    } else {
      comb_.lsu_resp.resp_ports.load_resps[0] = lsu_candidates[0].to_io();
    }
  }

  void capture_ptw_replay(const PtwRouteEvent &evt) {
    ReplayTracker *tracker = tracker_for_owner(nxt_, evt.owner);
    if (tracker == nullptr) {
      return;
    }

    if (evt.replay == REPLAY_NONE) {
      tracker->blocked = false;
      tracker->reason = REPLAY_NONE;
      tracker->req_addr = 0;
      return;
    }

    tracker->blocked = true;
    tracker->reason = evt.replay;
    tracker->req_addr = evt.req_addr;
  }

  void eval_replay_wakeup(const replay_resp &replay_bcast) {
    eval_one_tracker(cur_.dtlb, nxt_.dtlb, comb_.wakeup.dtlb, replay_bcast);
    eval_one_tracker(cur_.itlb, nxt_.itlb, comb_.wakeup.itlb, replay_bcast);
    eval_one_tracker(cur_.walk, nxt_.walk, comb_.wakeup.walk, replay_bcast);
  }

  static void eval_one_tracker(const ReplayTracker &cur, ReplayTracker &nxt,
                               bool &wakeup, const replay_resp &replay_bcast) {
    wakeup = false;
    nxt = cur;

    if (!cur.blocked) {
      return;
    }

    // replay=3: pure structural conflict (bank/resp-port collision). Wake the
    // request immediately in the next cycle without waiting for any broadcast.
    if (cur.reason == REPLAY_STRUCT) {
      wakeup = true;
      // replay=1: MSHR full. Any fill completion means one entry was freed.
    } else if (cur.reason == REPLAY_MSHR_FULL) {
      wakeup = replay_bcast.replay;
      // replay=2: wait for the specific cache line fill to complete.
    } else if (cur.reason == REPLAY_WAIT_FILL) {
      wakeup = replay_bcast.replay &&
               cache_line_match(cur.req_addr, replay_bcast.replay_addr);
    }

    if (wakeup) {
      nxt.blocked = false;
      nxt.reason = REPLAY_NONE;
      nxt.req_addr = 0;
    }
  }

  static ReplayTracker *tracker_for_owner(State &state, Owner owner) {
    switch (owner) {
    case Owner::PTW_DTLB:
      return &state.dtlb;
    case Owner::PTW_ITLB:
      return &state.itlb;
    case Owner::PTW_WALK:
      return &state.walk;
    default:
      return nullptr;
    }
  }

  static bool decode_ptw_owner_from_req_id(size_t req_id, Owner &owner) {
    if (req_id < kPtwReqIdBase) {
      return false;
    }
    const size_t code = req_id - kPtwReqIdBase;
    if (code == static_cast<size_t>(Owner::PTW_DTLB)) {
      owner = Owner::PTW_DTLB;
      return true;
    }
    if (code == static_cast<size_t>(Owner::PTW_ITLB)) {
      owner = Owner::PTW_ITLB;
      return true;
    }
    if (code == static_cast<size_t>(Owner::PTW_WALK)) {
      owner = Owner::PTW_WALK;
      return true;
    }
    return false;
  }

  State cur_{};
  State nxt_{};
  CombOutputs comb_{};
};
