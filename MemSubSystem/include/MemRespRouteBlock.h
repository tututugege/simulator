#pragma once

#include "DcacheConfig.h"
#include "MemPtwBlock.h"
#include "MemReadArbBlock.h"
#include "IO.h"
#include "util.h"

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
  static constexpr size_t kPtwTrackCount = 3;
  static constexpr size_t kLsuReqTrackCount = static_cast<size_t>(LDQ_SIZE);

  struct PtwRouteEvent {
    bool valid = false;
    Owner owner = Owner::NONE;
    uint32_t data = 0;
    uint8_t replay = 0;
    uint32_t req_addr = 0;
    size_t req_id = 0;
  };

  struct ReplayWakeup {
    bool dtlb = false;
    bool itlb = false;
    bool walk = false;
  };

  struct CombOutputs {
    DcacheLsuIO lsu_resp = {};
    PtwRouteEvent ptw_event = {};
    PtwRouteEvent ptw_events[LSU_LDU_COUNT] = {};
    uint8_t ptw_event_count = 0;
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

    struct PtwReqTrackState {
      bool valid = false;
      uint8_t owner = 0;
      size_t req_id = 0;
      uint32_t req_addr = 0;
    };

    IssueTag issued_tags[LSU_LDU_COUNT] = {};
    TrackerState dtlb = {};
    TrackerState itlb = {};
    TrackerState walk = {};
    PtwReqTrackState ptw_tracks[kPtwTrackCount] = {};
    PtwRouteEvent ptw_event = {};
    uint8_t ptw_event_count = 0;
    ReplayWakeup wakeup = {};
    bool ptw_occupies_port0 = false;
    bool lsu_port0_replayed = false;
  };

  void init() {
    reset_state(cur_);
    reset_state(nxt_);
    reset_comb_outputs(comb_);
  }

  void eval_comb(const DcacheLsuIO *dcache_resp_io,
                 const IssueTag (&issue_tags)[LSU_LDU_COUNT],
                 const replay_resp &replay_bcast) {
    reset_comb_outputs(comb_);
    nxt_ = cur_;
    register_ptw_issues(nxt_, issue_tags);
    register_lsu_issues(nxt_, issue_tags);

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
    for (size_t i = 0; i < kPtwTrackCount; i++) {
      d.ptw_tracks[i].valid = cur_.ptw_tracks[i].valid;
      d.ptw_tracks[i].owner = static_cast<uint8_t>(cur_.ptw_tracks[i].owner);
      d.ptw_tracks[i].req_id = cur_.ptw_tracks[i].req_id;
      d.ptw_tracks[i].req_addr = cur_.ptw_tracks[i].req_addr;
    }
    d.ptw_event = comb_.ptw_event;
    d.ptw_event_count = comb_.ptw_event_count;
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

  void apply_ptw_events(MemPtwBlock *ptw_block) const {
    if (ptw_block == nullptr) {
      return;
    }
    for (uint8_t i = 0; i < comb_.ptw_event_count; i++) {
      const auto &evt = comb_.ptw_events[i];
      if (!evt.valid) {
        continue;
      }
      switch (evt.owner) {
      case Owner::PTW_DTLB:
        if (evt.replay == 0) {
          ptw_block->on_mem_resp_client(MemPtwBlock::Client::DTLB, evt.data);
        }
        break;
      case Owner::PTW_ITLB:
        if (evt.replay == 0) {
          ptw_block->on_mem_resp_client(MemPtwBlock::Client::ITLB, evt.data);
        }
        break;
      case Owner::PTW_WALK:
        if (evt.replay == 0) {
          (void)ptw_block->on_walk_mem_resp(evt.req_id, evt.req_addr, evt.data);
        } else {
          (void)ptw_block->on_walk_mem_replay(evt.req_id, evt.replay);
        }
        break;
      default:
        break;
      }
    }
  }

  void apply_ptw_event(MemPtwBlock *ptw_block) const {
    apply_ptw_events(ptw_block);
  }

private:
  static constexpr uint8_t REPLAY_NONE = 0;
  static constexpr uint8_t REPLAY_MSHR_FULL = 1;
  static constexpr uint8_t REPLAY_WAIT_FILL = 2;
  static constexpr uint8_t REPLAY_STRUCT = 3;

  struct ReplayTracker {
    bool blocked = false;
    uint8_t reason = REPLAY_NONE;
    uint32_t req_addr = 0;
  };

  struct PtwReqTrack {
    bool valid = false;
    size_t req_id = 0;
    Owner owner = Owner::NONE;
    uint32_t req_addr = 0;
  };

  struct LsuReqTrack {
    bool valid = false;
    size_t req_id = 0;
    uint32_t req_addr = 0;
  };

  struct State {
    IssueTag issued_tags[LSU_LDU_COUNT] = {};
    ReplayTracker dtlb = {};
    ReplayTracker itlb = {};
    ReplayTracker walk = {};
    PtwReqTrack ptw_tracks[kPtwTrackCount] = {};
    LsuReqTrack lsu_tracks[kLsuReqTrackCount] = {};
  };

  static void reset_state(State &state) {
    for (auto &tag : state.issued_tags) {
      tag = IssueTag{};
    }
    state.dtlb = ReplayTracker{};
    state.itlb = ReplayTracker{};
    state.walk = ReplayTracker{};
    for (auto &track : state.ptw_tracks) {
      track = PtwReqTrack{};
    }
    for (auto &track : state.lsu_tracks) {
      track = LsuReqTrack{};
    }
  }

  static void reset_comb_outputs(CombOutputs &outputs) {
    outputs.lsu_resp = DcacheLsuIO{};
    outputs.ptw_event = PtwRouteEvent{};
    for (auto &event : outputs.ptw_events) {
      event = PtwRouteEvent{};
    }
    outputs.ptw_event_count = 0;
    outputs.wakeup = ReplayWakeup{};
    outputs.ptw_occupies_port0 = false;
    outputs.lsu_port0_replayed = false;
  }

  static bool is_ptw_owner(Owner owner) {
    return owner == Owner::PTW_DTLB || owner == Owner::PTW_ITLB ||
           owner == Owner::PTW_WALK;
  }

  static bool issue_tag_matches_req(const IssueTag &tag, bool req_is_lsu) {
    if (!tag.valid) {
      return false;
    }
    if (req_is_lsu) {
      return tag.owner == Owner::LSU;
    }
    return is_ptw_owner(tag.owner);
  }

  static int owner_track_index(Owner owner) {
    switch (owner) {
    case Owner::PTW_DTLB:
      return 0;
    case Owner::PTW_ITLB:
      return 1;
    case Owner::PTW_WALK:
      return 2;
    default:
      return -1;
    }
  }

  static void register_one_ptw_issue(State &state, const IssueTag &tag) {
    if (!tag.valid || !is_ptw_owner(tag.owner)) {
      return;
    }

    const int slot = owner_track_index(tag.owner);
    if (slot < 0) {
      return;
    }

    state.ptw_tracks[slot].valid = true;
    state.ptw_tracks[slot].req_id = tag.req_id;
    state.ptw_tracks[slot].owner = tag.owner;
    state.ptw_tracks[slot].req_addr = tag.req_addr;
  }

  static void register_ptw_issues(State &state,
                                  const IssueTag (&issue_tags)[LSU_LDU_COUNT]) {
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      register_one_ptw_issue(state, issue_tags[i]);
    }
  }

  static void register_one_lsu_issue(State &state, const IssueTag &tag) {
    if (!tag.valid || tag.owner != Owner::LSU ||
        tag.req_id >= kLsuReqTrackCount) {
      return;
    }

    auto &track = state.lsu_tracks[tag.req_id];
    track.valid = true;
    track.req_id = tag.req_id;
    track.req_addr = tag.req_addr;
  }

  static void register_lsu_issues(State &state,
                                  const IssueTag (&issue_tags)[LSU_LDU_COUNT]) {
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      register_one_lsu_issue(state, issue_tags[i]);
    }
  }

  static bool lookup_ptw_track(const State &state, size_t req_id,
                               IssueTag &tag) {
    int slot = -1;
    for (size_t i = 0; i < kPtwTrackCount; i++) {
      if (!state.ptw_tracks[i].valid) {
        continue;
      }
      if (state.ptw_tracks[i].req_id == req_id) {
        slot = static_cast<int>(i);
        break;
      }
    }
    if (slot < 0) {
      return false;
    }

    tag = {};
    tag.valid = true;
    tag.owner = state.ptw_tracks[slot].owner;
    tag.req_id = state.ptw_tracks[slot].req_id;
    tag.req_addr = state.ptw_tracks[slot].req_addr;
    return true;
  }

  static bool lookup_issue_tag_by_req_id(
      const IssueTag (&issue_tags)[LSU_LDU_COUNT], size_t req_id,
      IssueTag &tag) {
    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      if (!issue_tags[i].valid) {
        continue;
      }
      if (issue_tags[i].req_id != req_id) {
        continue;
      }
      tag = issue_tags[i];
      return true;
    }
    return false;
  }

  static void clear_ptw_track(State &state, size_t req_id) {
    for (size_t i = 0; i < kPtwTrackCount; i++) {
      if (!state.ptw_tracks[i].valid) {
        continue;
      }
      if (state.ptw_tracks[i].req_id == req_id) {
        state.ptw_tracks[i] = {};
        return;
      }
    }
  }

  static bool lookup_lsu_track(const State &state, size_t req_id,
                               IssueTag &tag) {
    if (req_id >= kLsuReqTrackCount) {
      return false;
    }
    const auto &track = state.lsu_tracks[req_id];
    if (!track.valid) {
      return false;
    }

    tag = {};
    tag.valid = true;
    tag.owner = Owner::LSU;
    tag.req_id = track.req_id;
    tag.req_addr = track.req_addr;
    return true;
  }

  static void clear_lsu_track(State &state, size_t req_id) {
    if (req_id >= kLsuReqTrackCount) {
      return;
    }
    state.lsu_tracks[req_id] = {};
  }

  void route_load_responses(const DcacheLsuIO *dcache_resp_io,
                            const IssueTag (&issue_tags)[LSU_LDU_COUNT]) {
    if (dcache_resp_io == nullptr) {
      return;
    }

    load_resp lsu_candidates[LSU_LDU_COUNT] = {};
    bool has_ptw_event = false;
    bool ptw_resp_on_port0 = false;

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      const auto &raw = dcache_resp_io->resp_ports.load_resps[i];
      if (!raw.valid) {
        continue;
      }
      const bool req_is_lsu = (raw.req_id < (size_t)LDQ_SIZE);

      IssueTag routed_tag{};
      if (!req_is_lsu) {
        if (lookup_ptw_track(cur_, raw.req_id, routed_tag)) {
        } else if (lookup_issue_tag_by_req_id(issue_tags, raw.req_id,
                                              routed_tag)) {
        }
      } else if (issue_tag_matches_req(cur_.issued_tags[i], true) &&
                 cur_.issued_tags[i].req_id == raw.req_id) {
        routed_tag = cur_.issued_tags[i];
      } else if (issue_tag_matches_req(issue_tags[i], true) &&
                 issue_tags[i].req_id == raw.req_id) {
        // Support same-cycle request+response path (e.g., dcache hit/replay).
        routed_tag = issue_tags[i];
      } else if (lookup_issue_tag_by_req_id(cur_.issued_tags, raw.req_id,
                                            routed_tag)) {
      } else if (lookup_issue_tag_by_req_id(issue_tags, raw.req_id,
                                            routed_tag)) {
      } else if (lookup_lsu_track(cur_, raw.req_id, routed_tag)) {
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
        if (raw.replay == 0) {
          clear_lsu_track(nxt_, raw.req_id);
        }
        break;
      case Owner::PTW_WALK:
      case Owner::PTW_DTLB:
      case Owner::PTW_ITLB:
        clear_ptw_track(nxt_, raw.req_id);
        if (i == 0) {
          ptw_resp_on_port0 = true;
        }
        if (comb_.ptw_event_count < LSU_LDU_COUNT) {
          auto &evt = comb_.ptw_events[comb_.ptw_event_count++];
          evt.valid = true;
          evt.owner = routed_tag.owner;
          evt.data = tagged.data;
          evt.replay = tagged.replay;
          evt.req_addr = tagged.req_addr;
          evt.req_id = routed_tag.req_id;
          if (!comb_.ptw_event.valid) {
            comb_.ptw_event = evt;
          }
          capture_ptw_replay(evt);
          has_ptw_event = true;
        }
        break;
      default:
        break;
      }
    }

    if (has_ptw_event) {
      comb_.ptw_occupies_port0 = ptw_resp_on_port0;
    }

    for (int i = 1; i < LSU_LDU_COUNT; i++) {
      comb_.lsu_resp.resp_ports.load_resps[i] = lsu_candidates[i].to_io();
    }

    if (has_ptw_event && ptw_resp_on_port0 && lsu_candidates[0].valid) {
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

    if (evt.owner == Owner::PTW_WALK && evt.replay == REPLAY_WAIT_FILL) {
      // PTW walk replay=2 is retried directly by MemPtwBlock on the next
      // cycle. Do not leave a stale wait-fill tracker behind.
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
    eval_walk_tracker(cur_.walk, nxt_.walk, comb_.wakeup.walk, replay_bcast);
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

  static void eval_walk_tracker(const ReplayTracker &cur, ReplayTracker &nxt,
                                bool &wakeup,
                                const replay_resp &replay_bcast) {
    wakeup = false;
    nxt = cur;

    if (!cur.blocked) {
      return;
    }

    if (cur.reason == REPLAY_STRUCT) {
      wakeup = true;
    } else if (cur.reason == REPLAY_MSHR_FULL) {
      wakeup = replay_bcast.replay;
    } else if (cur.reason == REPLAY_WAIT_FILL) {
      // PTW walk has only one active shared walker. After a replayed walk
      // request parks the FSM in WAIT_RESP with no live req_id, requiring an
      // exact matching fill broadcast can lose forward progress if the line's
      // wakeup was consumed before the tracker armed. Any subsequent fill
      // completion is a safe coarse retry trigger for the walker.
      wakeup = replay_bcast.replay;
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

  State cur_{};
  State nxt_{};
  CombOutputs comb_{};
};
