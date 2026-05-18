#pragma once

#include "PtwWalkPort.h"
#include "ref.h"
#include "types.h"
#include <array>
#include <cstddef>
#include <cstdio>

class SimContext;

// Shared PTW submodule. Port-side requests are latched in comb_begin(), memory
// feedback is applied in comb_finish(), and seq() is the only state commit.
class MemPtwBlock {
public:
  enum class Client : uint8_t {
    DTLB = 0,
    ITLB = 1,
    NUM_CLIENTS = 2,
  };

  enum class WalkRespResult : uint8_t {
    HANDLED,
    DROPPED,
    IGNORED,
  };

  enum class GrantOwner : uint8_t {
    NONE = 0,
    MEM_DTLB,
    MEM_ITLB,
    WALK,
  };

  enum class RoutedEventOwner : uint8_t {
    NONE = 0,
    MEM_DTLB,
    MEM_ITLB,
    WALK,
  };

  struct MemClientIn {
    bool req_valid = false;
    uint32_t req_addr = 0;
    bool resp_consumed = false;
  };

  struct MemClientOut {
    bool req_ready = false;
    bool resp_valid = false;
    uint32_t resp_data = 0;
  };

  struct WalkClientIn {
    bool req_valid = false;
    PtwWalkReq req = {};
    bool resp_consumed = false;
    bool flush = false;
  };

  struct WalkClientOut {
    bool req_ready = false;
    bool resp_valid = false;
    PtwWalkResp resp = {};
  };

  struct PortIn {
    std::array<MemClientIn, static_cast<size_t>(Client::NUM_CLIENTS)>
        mem_clients{};
    std::array<WalkClientIn, static_cast<size_t>(Client::NUM_CLIENTS)>
        walk_clients{};
  };

  struct SeqIn {
    std::array<bool, static_cast<size_t>(Client::NUM_CLIENTS)>
        walk_client_flush{};
  };

  struct RoutedEvent {
    bool valid = false;
    RoutedEventOwner owner = RoutedEventOwner::NONE;
    // Memory-side resolved 32-bit word presented to PTW for this event. The
    // surrounding memory subsystem may rewrite the raw routed response into the
    // PTW-visible value before filling FeedbackIn.
    uint32_t data = 0;
    uint8_t replay = 0;
    uint32_t req_addr = 0;
    size_t req_id = 0;
  };

  struct FeedbackIn {
    bool grant_valid = false;
    GrantOwner grant_owner = GrantOwner::NONE;
    size_t grant_req_id = 0;
    std::array<RoutedEvent, LSU_LDU_COUNT> events{};
    uint8_t event_count = 0;
    bool wakeup_dtlb = false;
    bool wakeup_itlb = false;
    bool wakeup_walk = false;
  };

  struct CombOut {
    std::array<MemClientOut, static_cast<size_t>(Client::NUM_CLIENTS)>
        mem_clients{};
    std::array<WalkClientOut, static_cast<size_t>(Client::NUM_CLIENTS)>
        walk_clients{};
    bool issue_walk_read = false;
    uint32_t walk_read_addr = 0;
    bool mem_req_pending[2] = {false, false};
    uint32_t mem_req_addr[2] = {0, 0};
  };

  // Packed pure-combinational wrapper layout.
  // `pi` = current PTW state + current-cycle inputs.
  // `po` = next PTW state + visible combinational outputs.
  //
  // The routed-event slot count is tied to LSU_LDU_COUNT because the memory
  // response router can produce at most one PTW-routed event per load port in
  // a cycle. Under the current large configuration LSU_LDU_COUNT == 3, so the
  // packed interface exposes 3 event slots.
  static constexpr int kPackedEventSlots = LSU_LDU_COUNT;
  static constexpr int kPackedReqIdWidth = static_cast<int>(sizeof(size_t) * 8);
  static constexpr int kPackedCoreStateWidth = 71 + kPackedReqIdWidth;
  static constexpr int kPackedMemClientStateWidth = 67;
  static constexpr int kPackedWalkClientStateWidth = 172;
  static constexpr int kPackedStateWidth =
      kPackedCoreStateWidth +
      2 * kPackedMemClientStateWidth +
      2 * kPackedWalkClientStateWidth;
  static constexpr int kPackedMemClientInWidth = 34;
  static constexpr int kPackedWalkClientInWidth = 99;
  static constexpr int kPackedGrantInWidth = 3 + kPackedReqIdWidth;
  static constexpr int kPackedEventCountWidth = 8;
  static constexpr int kPackedEventInWidth = 69 + kPackedReqIdWidth;
  static constexpr int kPackedWakeupInWidth = 3;
  static constexpr int kPackedPiWidth =
      kPackedStateWidth +
      2 * kPackedMemClientInWidth +
      2 * kPackedWalkClientInWidth +
      kPackedGrantInWidth +
      kPackedEventCountWidth +
      kPackedEventSlots * kPackedEventInWidth +
      kPackedWakeupInWidth;
  static constexpr int kPackedMemClientOutWidth = 34;
  static constexpr int kPackedWalkClientOutWidth = 75;
  static constexpr int kPackedArbOutWidth = 99;
  static constexpr int kPackedPoWidth =
      kPackedStateWidth +
      2 * kPackedMemClientOutWidth +
      2 * kPackedWalkClientOutWidth +
      kPackedArbOutWidth;

  explicit MemPtwBlock(SimContext *ctx = nullptr) : ctx(ctx) { init(); }

  void bind_context(SimContext *c) { ctx = c; }

  void init() {
    reset_state(cur_);
    reset_state(nxt_);
    reset_outputs(comb_);
    refresh_outputs();
  }

  void comb_begin(const PortIn &in) {
    nxt_ = cur_;
    reset_outputs(comb_);
    apply_port_inputs(in);
    count_wait_cycles();
    select_walk_owner();
    refresh_outputs();
  }

  void comb_finish(const FeedbackIn &in) {
    apply_grant(in);
    apply_events(in);
    apply_wakeups(in);
    refresh_outputs();
  }

  void seq() {
    SeqIn in{};
    seq(in);
  }

  void seq(const SeqIn &in) {
    cur_ = nxt_;
    apply_seq_inputs(cur_, in);
    nxt_ = cur_;
    refresh_outputs();
  }

  const CombOut &comb_outputs() const { return comb_; }

  void dump_debug_state(FILE *out) const {
    if (out == nullptr) {
      return;
    }
    std::fprintf(out,
                 "[MEM DEBUG][PTW] walk_active=%d state=%u owner=%u "
                 "req_id_valid=%d req_id=%zu ",
                 static_cast<int>(cur_.walk_active),
                 static_cast<unsigned>(cur_.walk_state),
                 static_cast<unsigned>(cur_.walk_owner),
                 static_cast<int>(cur_.walk_req_id_valid), cur_.walk_req_id);
    for (size_t i = 0; i < kClientCount; i++) {
      const auto &walk = cur_.walk_clients[i];
      const auto &mem = cur_.mem_clients[i];
      std::fprintf(out,
                   i == 0
                       ? "dtlb(req_p=%d req_i=%d resp=%d mem_p=%d mem_i=%d) "
                       : "itlb(req_p=%d req_i=%d resp=%d mem_p=%d mem_i=%d)",
                   static_cast<int>(walk.req_pending),
                   static_cast<int>(walk.req_inflight),
                   static_cast<int>(walk.resp_valid),
                   static_cast<int>(mem.req_pending),
                   static_cast<int>(mem.req_inflight));
    }
    std::fprintf(out, "\n");
  }

  static void eval_packed(const bool *pi, bool *po) {
    MemPtwBlock block(nullptr);
    PortIn port_in{};
    SeqIn seq_in{};
    FeedbackIn feedback{};
    decode_packed_input(pi, block.cur_, port_in, seq_in, feedback);

    block.nxt_ = block.cur_;
    reset_outputs(block.comb_);
    block.apply_port_inputs(port_in);
    block.select_walk_owner();
    block.refresh_outputs();
    block.apply_grant(feedback);
    block.apply_events(feedback);
    block.apply_wakeups(feedback);
    block.apply_seq_inputs(block.nxt_, seq_in);
    block.refresh_outputs();

    encode_packed_output(po, block.nxt_, block.comb_);
  }

private:
  struct PtwClientState {
    bool req_pending = false;
    bool req_inflight = false;
    uint32_t req_addr = 0;
    bool resp_valid = false;
    uint32_t resp_data = 0;
  };

  struct WalkClientState {
    bool req_pending = false;
    bool req_inflight = false;
    PtwWalkReq req = {};
    bool resp_valid = false;
    PtwWalkResp resp = {};
  };

  enum class WalkState : uint8_t {
    IDLE,
    L1_REQ,
    L1_WAIT_RESP,
    L2_REQ,
    L2_WAIT_RESP,
  };

  static constexpr size_t kClientCount =
      static_cast<size_t>(Client::NUM_CLIENTS);

  struct State {
    std::array<PtwClientState, kClientCount> mem_clients{};
    std::array<WalkClientState, kClientCount> walk_clients{};
    WalkState walk_state = WalkState::IDLE;
    bool walk_active = false;
    Client walk_owner = Client::DTLB;
    Client walk_rr_next = Client::DTLB;
    uint32_t walk_l1_pte = 0;
    uint32_t walk_drop_resp_credit = 0;
    bool walk_req_id_valid = false;
    size_t walk_req_id = 0;
  };

  static size_t client_idx(Client c) { return static_cast<size_t>(c); }

  static void reset_state(State &state) {
    state = {};
    state.walk_state = WalkState::IDLE;
    state.walk_owner = Client::DTLB;
    state.walk_rr_next = Client::DTLB;
  }

  static void reset_outputs(CombOut &out) { out = {}; }

  static unsigned read_packed_uint(const bool *bits, int lsb, int width) {
    unsigned value = 0;
    for (int i = 0; i < width && i < 32; i++) {
      if (bits[lsb + i]) {
        value |= (1u << i);
      }
    }
    return value;
  }

  static size_t read_packed_size_t(const bool *bits, int lsb, int width) {
    size_t value = 0;
    for (int i = 0; i < width && i < static_cast<int>(sizeof(size_t) * 8); i++) {
      if (bits[lsb + i]) {
        value |= (size_t(1) << i);
      }
    }
    return value;
  }

  static bool read_packed_bit(const bool *bits, int bit) { return bits[bit]; }

  static void write_packed_uint(bool *bits, int lsb, int width, unsigned value) {
    for (int i = 0; i < width; i++) {
      bits[lsb + i] = ((value >> i) & 1u) != 0u;
    }
  }

  static void write_packed_size_t(bool *bits, int lsb, int width, size_t value) {
    for (int i = 0; i < width; i++) {
      bits[lsb + i] = ((value >> i) & size_t(1)) != 0;
    }
  }

  static void write_packed_bit(bool *bits, int bit, bool value) {
    bits[bit] = value;
  }

  static void clear_packed_bits(bool *bits, int width) {
    for (int i = 0; i < width; i++) {
      bits[i] = false;
    }
  }

  static int packed_state_mem_base(int client) {
    return kPackedCoreStateWidth + client * kPackedMemClientStateWidth;
  }

  static int packed_state_walk_base(int client) {
    return kPackedCoreStateWidth + 2 * kPackedMemClientStateWidth +
           client * kPackedWalkClientStateWidth;
  }

  static int packed_pi_mem_in_base(int client) {
    return kPackedStateWidth + client * kPackedMemClientInWidth;
  }

  static int packed_pi_walk_in_base(int client) {
    return kPackedStateWidth + 2 * kPackedMemClientInWidth +
           client * kPackedWalkClientInWidth;
  }

  static int packed_pi_grant_base() {
    return kPackedStateWidth + 2 * kPackedMemClientInWidth +
           2 * kPackedWalkClientInWidth;
  }

  static int packed_pi_event_count_base() {
    return packed_pi_grant_base() + kPackedGrantInWidth;
  }

  static int packed_pi_event_base(int slot) {
    return packed_pi_event_count_base() + kPackedEventCountWidth +
           slot * kPackedEventInWidth;
  }

  static int packed_pi_wakeup_base() {
    return packed_pi_event_count_base() + kPackedEventCountWidth +
           kPackedEventSlots * kPackedEventInWidth;
  }

  static int packed_po_mem_out_base(int client) {
    return kPackedStateWidth + client * kPackedMemClientOutWidth;
  }

  static int packed_po_walk_out_base(int client) {
    return kPackedStateWidth + 2 * kPackedMemClientOutWidth +
           client * kPackedWalkClientOutWidth;
  }

  static int packed_po_arb_base() {
    return kPackedStateWidth + 2 * kPackedMemClientOutWidth +
           2 * kPackedWalkClientOutWidth;
  }

  static void decode_packed_state(const bool *pi, State &state) {
    reset_state(state);
    state.walk_active = read_packed_bit(pi, 0);
    state.walk_state =
        static_cast<WalkState>(read_packed_uint(pi, 1, 3));
    state.walk_owner = read_packed_bit(pi, 4) ? Client::ITLB : Client::DTLB;
    state.walk_rr_next = read_packed_bit(pi, 5) ? Client::ITLB : Client::DTLB;
    state.walk_req_id_valid = read_packed_bit(pi, 6);
    state.walk_req_id = read_packed_size_t(pi, 7, kPackedReqIdWidth);
    state.walk_l1_pte = read_packed_uint(pi, 7 + kPackedReqIdWidth, 32);
    state.walk_drop_resp_credit =
        read_packed_uint(pi, 39 + kPackedReqIdWidth, 32);

    for (size_t i = 0; i < kClientCount; i++) {
      const int mem_base = packed_state_mem_base(static_cast<int>(i));
      auto &mem = state.mem_clients[i];
      mem.req_pending = read_packed_bit(pi, mem_base + 0);
      mem.req_inflight = read_packed_bit(pi, mem_base + 1);
      mem.req_addr = read_packed_uint(pi, mem_base + 2, 32);
      mem.resp_valid = read_packed_bit(pi, mem_base + 34);
      mem.resp_data = read_packed_uint(pi, mem_base + 35, 32);

      const int walk_base = packed_state_walk_base(static_cast<int>(i));
      auto &walk = state.walk_clients[i];
      walk.req_pending = read_packed_bit(pi, walk_base + 0);
      walk.req_inflight = read_packed_bit(pi, walk_base + 1);
      walk.req.vaddr = read_packed_uint(pi, walk_base + 2, 32);
      walk.req.satp = read_packed_uint(pi, walk_base + 34, 32);
      walk.req.access_type = read_packed_uint(pi, walk_base + 66, 32);
      walk.resp_valid = read_packed_bit(pi, walk_base + 98);
      walk.resp.fault = read_packed_bit(pi, walk_base + 99);
      walk.resp.vaddr = read_packed_uint(pi, walk_base + 100, 32);
      walk.resp.leaf_pte = read_packed_uint(pi, walk_base + 132, 32);
      walk.resp.leaf_level = read_packed_uint(pi, walk_base + 164, 8);
    }
  }

  static void encode_packed_state(bool *po, const State &state) {
    write_packed_bit(po, 0, state.walk_active);
    write_packed_uint(po, 1, 3, static_cast<unsigned>(state.walk_state));
    write_packed_bit(po, 4, state.walk_owner != Client::DTLB);
    write_packed_bit(po, 5, state.walk_rr_next != Client::DTLB);
    write_packed_bit(po, 6, state.walk_req_id_valid);
    write_packed_size_t(po, 7, kPackedReqIdWidth, state.walk_req_id);
    write_packed_uint(po, 7 + kPackedReqIdWidth, 32, state.walk_l1_pte);
    write_packed_uint(po, 39 + kPackedReqIdWidth, 32,
                      state.walk_drop_resp_credit);

    for (size_t i = 0; i < kClientCount; i++) {
      const int mem_base = packed_state_mem_base(static_cast<int>(i));
      const auto &mem = state.mem_clients[i];
      write_packed_bit(po, mem_base + 0, mem.req_pending);
      write_packed_bit(po, mem_base + 1, mem.req_inflight);
      write_packed_uint(po, mem_base + 2, 32, mem.req_addr);
      write_packed_bit(po, mem_base + 34, mem.resp_valid);
      write_packed_uint(po, mem_base + 35, 32, mem.resp_data);

      const int walk_base = packed_state_walk_base(static_cast<int>(i));
      const auto &walk = state.walk_clients[i];
      write_packed_bit(po, walk_base + 0, walk.req_pending);
      write_packed_bit(po, walk_base + 1, walk.req_inflight);
      write_packed_uint(po, walk_base + 2, 32, walk.req.vaddr);
      write_packed_uint(po, walk_base + 34, 32, walk.req.satp);
      write_packed_uint(po, walk_base + 66, 32, walk.req.access_type);
      write_packed_bit(po, walk_base + 98, walk.resp_valid);
      write_packed_bit(po, walk_base + 99, walk.resp.fault);
      write_packed_uint(po, walk_base + 100, 32, walk.resp.vaddr);
      write_packed_uint(po, walk_base + 132, 32, walk.resp.leaf_pte);
      write_packed_uint(po, walk_base + 164, 8, walk.resp.leaf_level);
    }
  }

  static void decode_packed_input(const bool *pi, State &state, PortIn &port_in,
                                  SeqIn &seq_in, FeedbackIn &feedback) {
    decode_packed_state(pi, state);

    for (size_t i = 0; i < kClientCount; i++) {
      const int mem_base = packed_pi_mem_in_base(static_cast<int>(i));
      port_in.mem_clients[i].req_valid = read_packed_bit(pi, mem_base + 0);
      port_in.mem_clients[i].req_addr = read_packed_uint(pi, mem_base + 1, 32);
      port_in.mem_clients[i].resp_consumed =
          read_packed_bit(pi, mem_base + 33);

      const int walk_base = packed_pi_walk_in_base(static_cast<int>(i));
      auto &walk_in = port_in.walk_clients[i];
      walk_in.req_valid = read_packed_bit(pi, walk_base + 0);
      walk_in.req.vaddr = read_packed_uint(pi, walk_base + 1, 32);
      walk_in.req.satp = read_packed_uint(pi, walk_base + 33, 32);
      walk_in.req.access_type = read_packed_uint(pi, walk_base + 65, 32);
      walk_in.resp_consumed = read_packed_bit(pi, walk_base + 97);
      seq_in.walk_client_flush[i] = read_packed_bit(pi, walk_base + 98);
    }

    const int grant_base = packed_pi_grant_base();
    feedback.grant_valid = read_packed_bit(pi, grant_base + 0);
    feedback.grant_owner = static_cast<GrantOwner>(
        read_packed_uint(pi, grant_base + 1, 2));
    feedback.grant_req_id =
        read_packed_size_t(pi, grant_base + 3, kPackedReqIdWidth);

    feedback.event_count = static_cast<uint8_t>(
        read_packed_uint(pi, packed_pi_event_count_base(),
                         kPackedEventCountWidth));
    if (feedback.event_count > kPackedEventSlots) {
      feedback.event_count = static_cast<uint8_t>(kPackedEventSlots);
    }

    for (int i = 0; i < kPackedEventSlots; i++) {
      const int event_base = packed_pi_event_base(i);
      auto &evt = feedback.events[static_cast<size_t>(i)];
      evt.valid = read_packed_bit(pi, event_base + 0);
      evt.owner = static_cast<RoutedEventOwner>(
          read_packed_uint(pi, event_base + 1, 2));
      evt.data = read_packed_uint(pi, event_base + 3, 32);
      evt.replay = static_cast<uint8_t>(read_packed_uint(pi, event_base + 35, 2));
      evt.req_addr = read_packed_uint(pi, event_base + 37, 32);
      evt.req_id = read_packed_size_t(pi, event_base + 69, kPackedReqIdWidth);
    }

    const int wake_base = packed_pi_wakeup_base();
    feedback.wakeup_dtlb = read_packed_bit(pi, wake_base + 0);
    feedback.wakeup_itlb = read_packed_bit(pi, wake_base + 1);
    feedback.wakeup_walk = read_packed_bit(pi, wake_base + 2);
  }

  static void encode_packed_output(bool *po, const State &state,
                                   const CombOut &comb) {
    clear_packed_bits(po, kPackedPoWidth);
    encode_packed_state(po, state);

    for (size_t i = 0; i < kClientCount; i++) {
      const int mem_base = packed_po_mem_out_base(static_cast<int>(i));
      const auto &mem = comb.mem_clients[i];
      write_packed_bit(po, mem_base + 0, mem.req_ready);
      write_packed_bit(po, mem_base + 1, mem.resp_valid);
      write_packed_uint(po, mem_base + 2, 32, mem.resp_data);

      const int walk_base = packed_po_walk_out_base(static_cast<int>(i));
      const auto &walk = comb.walk_clients[i];
      write_packed_bit(po, walk_base + 0, walk.req_ready);
      write_packed_bit(po, walk_base + 1, walk.resp_valid);
      write_packed_bit(po, walk_base + 2, walk.resp.fault);
      write_packed_uint(po, walk_base + 3, 32, walk.resp.vaddr);
      write_packed_uint(po, walk_base + 35, 32, walk.resp.leaf_pte);
      write_packed_uint(po, walk_base + 67, 8, walk.resp.leaf_level);
    }

    const int arb_base = packed_po_arb_base();
    write_packed_bit(po, arb_base + 0, comb.issue_walk_read);
    write_packed_uint(po, arb_base + 1, 32, comb.walk_read_addr);
    write_packed_bit(po, arb_base + 33, comb.mem_req_pending[0]);
    write_packed_bit(po, arb_base + 34, comb.mem_req_pending[1]);
    write_packed_uint(po, arb_base + 35, 32, comb.mem_req_addr[0]);
    write_packed_uint(po, arb_base + 67, 32, comb.mem_req_addr[1]);
  }

  void apply_port_inputs(const PortIn &in) {
    for (size_t i = 0; i < kClientCount; i++) {
      const Client client = static_cast<Client>(i);
      auto &mem_state = nxt_.mem_clients[i];
      if (in.mem_clients[i].resp_consumed) {
        mem_state.resp_valid = false;
      }
      if (in.mem_clients[i].req_valid) {
        send_mem_req(client, in.mem_clients[i].req_addr);
      }

      auto &walk_state = nxt_.walk_clients[i];
      if (in.walk_clients[i].resp_consumed) {
        walk_state.resp_valid = false;
      }
      if (in.walk_clients[i].req_valid) {
        send_walk_req(client, in.walk_clients[i].req);
      }
    }
  }

  static void apply_seq_inputs(State &state, const SeqIn &in) {
    for (size_t i = 0; i < kClientCount; i++) {
      if (in.walk_client_flush[i]) {
        flush_walk_client_state(state, static_cast<Client>(i));
      }
    }
  }

  void count_wait_cycles() const {
    if (ctx == nullptr) {
      return;
    }
    if (nxt_.mem_clients[client_idx(Client::DTLB)].req_pending ||
        nxt_.mem_clients[client_idx(Client::DTLB)].req_inflight ||
        nxt_.walk_clients[client_idx(Client::DTLB)].req_pending ||
        nxt_.walk_clients[client_idx(Client::DTLB)].req_inflight) {
      ctx->perf.ptw_dtlb_wait_cycle++;
    }
    if (nxt_.mem_clients[client_idx(Client::ITLB)].req_pending ||
        nxt_.mem_clients[client_idx(Client::ITLB)].req_inflight ||
        nxt_.walk_clients[client_idx(Client::ITLB)].req_pending ||
        nxt_.walk_clients[client_idx(Client::ITLB)].req_inflight) {
      ctx->perf.ptw_itlb_wait_cycle++;
    }
  }

  void select_walk_owner() {
    if (nxt_.walk_active || nxt_.walk_state != WalkState::IDLE) {
      return;
    }

    auto grant_walk_owner = [&](Client owner) {
      auto &wc = nxt_.walk_clients[client_idx(owner)];
      wc.req_pending = false;
      wc.req_inflight = true;
      nxt_.walk_owner = owner;
      nxt_.walk_active = true;
      nxt_.walk_state = WalkState::L1_REQ;
      nxt_.walk_l1_pte = 0;
      nxt_.walk_rr_next = (owner == Client::DTLB) ? Client::ITLB
                                                  : Client::DTLB;
    };

    const Client first = nxt_.walk_rr_next;
    const Client second = (first == Client::DTLB) ? Client::ITLB
                                                  : Client::DTLB;
    if (nxt_.walk_clients[client_idx(first)].req_pending) {
      grant_walk_owner(first);
    } else if (nxt_.walk_clients[client_idx(second)].req_pending) {
      grant_walk_owner(second);
    }
  }

  void refresh_outputs() {
    reset_outputs(comb_);
    for (size_t i = 0; i < kClientCount; i++) {
      const auto &mem_state = nxt_.mem_clients[i];
      comb_.mem_clients[i].req_ready =
          !mem_state.req_pending && !mem_state.req_inflight;
      comb_.mem_clients[i].resp_valid = mem_state.resp_valid;
      comb_.mem_clients[i].resp_data = mem_state.resp_data;
      comb_.mem_req_pending[i] = mem_state.req_pending;
      comb_.mem_req_addr[i] = mem_state.req_addr;

      const auto &walk_state = nxt_.walk_clients[i];
      comb_.walk_clients[i].req_ready =
          !walk_state.req_pending && !walk_state.req_inflight;
      comb_.walk_clients[i].resp_valid = walk_state.resp_valid;
      comb_.walk_clients[i].resp = walk_state.resp;
    }

    if (!nxt_.walk_active) {
      return;
    }

    const auto &req = nxt_.walk_clients[client_idx(nxt_.walk_owner)].req;
    if (nxt_.walk_state == WalkState::L1_REQ) {
      const uint32_t root_ppn = req.satp & 0x3FFFFF;
      const uint32_t vpn1 = (req.vaddr >> 22) & 0x3FF;
      comb_.issue_walk_read = true;
      comb_.walk_read_addr = (root_ppn << 12) + (vpn1 << 2);
    } else if (nxt_.walk_state == WalkState::L2_REQ) {
      const uint32_t ppn = (nxt_.walk_l1_pte >> 10) & 0x3FFFFF;
      const uint32_t vpn0 = (req.vaddr >> 12) & 0x3FF;
      comb_.issue_walk_read = true;
      comb_.walk_read_addr = (ppn << 12) + (vpn0 << 2);
    }
  }

  void apply_grant(const FeedbackIn &in) {
    if (!in.grant_valid) {
      return;
    }

    switch (in.grant_owner) {
    case GrantOwner::MEM_DTLB:
      on_mem_read_granted(Client::DTLB);
      break;
    case GrantOwner::MEM_ITLB:
      on_mem_read_granted(Client::ITLB);
      break;
    case GrantOwner::WALK:
      on_walk_read_granted(in.grant_req_id);
      break;
    default:
      break;
    }
  }

  void apply_events(const FeedbackIn &in) {
    for (uint8_t i = 0; i < in.event_count; i++) {
      const auto &evt = in.events[i];
      if (!evt.valid) {
        continue;
      }
      switch (evt.owner) {
      case RoutedEventOwner::MEM_DTLB:
        if (evt.replay == 0) {
          on_mem_resp_client(Client::DTLB, evt.data);
        }
        break;
      case RoutedEventOwner::MEM_ITLB:
        if (evt.replay == 0) {
          on_mem_resp_client(Client::ITLB, evt.data);
        }
        break;
      case RoutedEventOwner::WALK:
        if (evt.replay == 0) {
          (void)on_walk_mem_resp(evt.req_id, evt.data);
        } else {
          (void)on_walk_mem_replay(evt.req_id, evt.replay);
        }
        break;
      default:
        break;
      }
    }
  }

  void apply_wakeups(const FeedbackIn &in) {
    if (in.wakeup_dtlb) {
      retry_mem_req(Client::DTLB);
    }
    if (in.wakeup_itlb) {
      retry_mem_req(Client::ITLB);
    }
    if (in.wakeup_walk) {
      retry_active_walk();
    }
  }

  void send_mem_req(Client client, uint32_t paddr) {
    auto &s = nxt_.mem_clients[client_idx(client)];
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_req++;
      } else {
        ctx->perf.ptw_itlb_req++;
      }
    }
    s.resp_valid = false;
    if (s.req_pending || s.req_inflight) {
      if (ctx != nullptr) {
        if (client == Client::DTLB) {
          ctx->perf.ptw_dtlb_blocked++;
        } else {
          ctx->perf.ptw_itlb_blocked++;
        }
      }
      return;
    }
    s.req_pending = true;
    s.req_addr = paddr;
  }

  void send_walk_req(Client client, const PtwWalkReq &req) {
    auto &s = nxt_.walk_clients[client_idx(client)];
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_req++;
      } else {
        ctx->perf.ptw_itlb_req++;
      }
    }
    s.resp_valid = false;
    if (s.req_pending || s.req_inflight) {
      if (ctx != nullptr) {
        if (client == Client::DTLB) {
          ctx->perf.ptw_dtlb_blocked++;
        } else {
          ctx->perf.ptw_itlb_blocked++;
        }
      }
      return;
    }
    s.req_pending = true;
    s.req = req;
  }

  void on_mem_read_granted(Client client) {
    auto &s = nxt_.mem_clients[client_idx(client)];
    s.req_pending = false;
    s.req_inflight = true;
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_grant++;
      } else {
        ctx->perf.ptw_itlb_grant++;
      }
    }
  }

  void on_mem_resp_client(Client client, uint32_t data) {
    auto &s = nxt_.mem_clients[client_idx(client)];
    s.resp_valid = true;
    s.resp_data = data;
    s.req_inflight = false;
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_resp++;
      } else {
        ctx->perf.ptw_itlb_resp++;
      }
    }
  }

  void on_walk_read_granted(size_t req_id) {
    if (nxt_.walk_state == WalkState::L1_REQ) {
      nxt_.walk_state = WalkState::L1_WAIT_RESP;
    } else if (nxt_.walk_state == WalkState::L2_REQ) {
      nxt_.walk_state = WalkState::L2_WAIT_RESP;
    }
    nxt_.walk_req_id_valid = true;
    nxt_.walk_req_id = req_id;
    if (ctx != nullptr) {
      if (nxt_.walk_owner == Client::DTLB) {
        ctx->perf.ptw_dtlb_grant++;
      } else {
        ctx->perf.ptw_itlb_grant++;
      }
    }
  }

  WalkRespResult on_walk_mem_resp(size_t req_id, uint32_t pte) {
    const bool is_active_req =
        nxt_.walk_active && nxt_.walk_req_id_valid && (nxt_.walk_req_id == req_id);
    if (nxt_.walk_drop_resp_credit > 0 && !is_active_req) {
      nxt_.walk_drop_resp_credit--;
      return WalkRespResult::DROPPED;
    }
    if (!nxt_.walk_active || !nxt_.walk_req_id_valid || !is_active_req) {
      return WalkRespResult::IGNORED;
    }

    auto &wc = nxt_.walk_clients[client_idx(nxt_.walk_owner)];
    const bool v = (pte & PTE_V) != 0;
    const bool r = (pte & PTE_R) != 0;
    const bool w = (pte & PTE_W) != 0;
    const bool x = (pte & PTE_X) != 0;

    auto publish_fault = [&]() {
      wc.req_inflight = false;
      wc.resp_valid = true;
      wc.resp = {};
      wc.resp.fault = true;
      wc.resp.vaddr = wc.req.vaddr;
      nxt_.walk_active = false;
      nxt_.walk_state = WalkState::IDLE;
      nxt_.walk_req_id_valid = false;
      nxt_.walk_req_id = 0;
    };
    auto publish_leaf = [&](uint8_t leaf_level) {
      wc.req_inflight = false;
      wc.resp_valid = true;
      wc.resp = {};
      wc.resp.fault = false;
      wc.resp.vaddr = wc.req.vaddr;
      wc.resp.leaf_pte = pte;
      wc.resp.leaf_level = leaf_level;
      nxt_.walk_active = false;
      nxt_.walk_state = WalkState::IDLE;
      nxt_.walk_req_id_valid = false;
      nxt_.walk_req_id = 0;
    };

    if (nxt_.walk_state == WalkState::L1_WAIT_RESP) {
      if (!v || (!r && w)) {
        publish_fault();
      } else if (r || x) {
        if (((pte >> 10) & 0x3FFu) != 0) {
          publish_fault();
        } else {
          publish_leaf(1);
        }
      } else {
        nxt_.walk_l1_pte = pte;
        nxt_.walk_state = WalkState::L2_REQ;
        nxt_.walk_req_id_valid = false;
        nxt_.walk_req_id = 0;
      }
    } else if (nxt_.walk_state == WalkState::L2_WAIT_RESP) {
      if (!v || (!r && w) || !(r || x)) {
        publish_fault();
      } else {
        publish_leaf(0);
      }
    } else {
      publish_fault();
    }

    if (ctx != nullptr && wc.resp_valid) {
      if (nxt_.walk_owner == Client::DTLB) {
        ctx->perf.ptw_dtlb_resp++;
      } else {
        ctx->perf.ptw_itlb_resp++;
      }
    }
    return WalkRespResult::HANDLED;
  }

  WalkRespResult on_walk_mem_replay(size_t req_id, uint8_t replay_reason) {
    const bool is_active_req =
        nxt_.walk_active && nxt_.walk_req_id_valid && (nxt_.walk_req_id == req_id);
    if (nxt_.walk_drop_resp_credit > 0 && !is_active_req) {
      nxt_.walk_drop_resp_credit--;
      return WalkRespResult::DROPPED;
    }
    if (!nxt_.walk_active || !is_active_req) {
      return WalkRespResult::IGNORED;
    }

    auto &wc = nxt_.walk_clients[client_idx(nxt_.walk_owner)];
    wc.req_inflight = false;

    static constexpr uint8_t kReplayWaitFill = 2;
    if (replay_reason == kReplayWaitFill) {
      if (nxt_.walk_state == WalkState::L1_WAIT_RESP) {
        nxt_.walk_state = WalkState::L1_REQ;
      } else if (nxt_.walk_state == WalkState::L2_WAIT_RESP) {
        nxt_.walk_state = WalkState::L2_REQ;
      }
      nxt_.walk_req_id_valid = false;
      nxt_.walk_req_id = 0;
      return WalkRespResult::HANDLED;
    }

    nxt_.walk_req_id_valid = false;
    nxt_.walk_req_id = 0;
    return WalkRespResult::HANDLED;
  }

  void retry_mem_req(Client client) {
    auto &s = nxt_.mem_clients[client_idx(client)];
    if (!s.req_inflight) {
      return;
    }
    s.req_inflight = false;
    s.req_pending = true;
  }

  void retry_active_walk() {
    if (!nxt_.walk_active) {
      return;
    }
    if (nxt_.walk_state == WalkState::L1_WAIT_RESP) {
      nxt_.walk_state = WalkState::L1_REQ;
    } else if (nxt_.walk_state == WalkState::L2_WAIT_RESP) {
      nxt_.walk_state = WalkState::L2_REQ;
    }
    nxt_.walk_req_id_valid = false;
    nxt_.walk_req_id = 0;
  }

  static void flush_walk_client_state(State &state, Client client) {
    auto &s = state.walk_clients[client_idx(client)];
    s.req_pending = false;
    s.req_inflight = false;
    s.resp_valid = false;

    if (state.walk_active && state.walk_owner == client) {
      if (state.walk_state == WalkState::L1_WAIT_RESP ||
          state.walk_state == WalkState::L2_WAIT_RESP) {
        state.walk_drop_resp_credit++;
      }
      state.walk_active = false;
      state.walk_state = WalkState::IDLE;
      state.walk_l1_pte = 0;
      state.walk_req_id_valid = false;
      state.walk_req_id = 0;
    }
  }

  SimContext *ctx = nullptr;
  State cur_{};
  State nxt_{};
  CombOut comb_{};
};
