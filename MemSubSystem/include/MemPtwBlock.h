#pragma once

#include "PtwWalkPort.h"
#include "ref.h"
#include "types.h"
#include <array>

// PTW 子模块（合并 client 状态 + shared walk FSM）。
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

  explicit MemPtwBlock(SimContext *ctx = nullptr) : ctx(ctx) {}

  void bind_context(SimContext *c) { ctx = c; }
  void init() {
    ptw_clients = {};
    walk_clients = {};
    walk_state = WalkState::IDLE;
    walk_active = false;
    walk_owner = Client::DTLB;
    walk_rr_next = Client::DTLB;
    walk_l1_pte = 0;
    walk_drop_resp_credit = 0;
  }
  void comb_select_walk_owner() {
    if (!walk_active && walk_state == WalkState::IDLE) {
      auto grant_walk_owner = [&](Client owner) {
        auto &wc = walk_clients[client_idx(owner)];
        wc.req_pending = false;
        wc.req_inflight = true;
        walk_owner = owner;
        walk_active = true;
        walk_state = WalkState::L1_REQ;
        walk_l1_pte = 0;
        walk_rr_next = (owner == Client::DTLB) ? Client::ITLB : Client::DTLB;
      };

      Client first = walk_rr_next;
      Client second = (first == Client::DTLB) ? Client::ITLB : Client::DTLB;
      auto &first_wc = walk_clients[client_idx(first)];
      auto &second_wc = walk_clients[client_idx(second)];
      if (first_wc.req_pending) {
        grant_walk_owner(first);
      } else if (second_wc.req_pending) {
        grant_walk_owner(second);
      }
    }
  }
  void count_wait_cycles() {
    if (ctx == nullptr) {
      return;
    }
    if (ptw_clients[client_idx(Client::DTLB)].req_pending ||
        ptw_clients[client_idx(Client::DTLB)].req_inflight ||
        walk_clients[client_idx(Client::DTLB)].req_pending ||
        walk_clients[client_idx(Client::DTLB)].req_inflight) {
      ctx->perf.ptw_dtlb_wait_cycle++;
    }
    if (ptw_clients[client_idx(Client::ITLB)].req_pending ||
        ptw_clients[client_idx(Client::ITLB)].req_inflight ||
        walk_clients[client_idx(Client::ITLB)].req_pending ||
        walk_clients[client_idx(Client::ITLB)].req_inflight) {
      ctx->perf.ptw_itlb_wait_cycle++;
    }
  }

  bool walk_read_req(uint32_t &addr) const {
    if (walk_active && walk_state == WalkState::L1_REQ) {
      const auto &req = walk_clients[client_idx(walk_owner)].req;
      uint32_t root_ppn = req.satp & 0x3FFFFF;
      uint32_t vpn1 = (req.vaddr >> 22) & 0x3FF;
      addr = (root_ppn << 12) + (vpn1 << 2);
      return true;
    }
    if (walk_active && walk_state == WalkState::L2_REQ) {
      const auto &req = walk_clients[client_idx(walk_owner)].req;
      uint32_t ppn = (walk_l1_pte >> 10) & 0x3FFFFF;
      uint32_t vpn0 = (req.vaddr >> 12) & 0x3FF;
      addr = (ppn << 12) + (vpn0 << 2);
      return true;
    }
    return false;
  }

  void on_walk_read_granted() {
    if (walk_state == WalkState::L1_REQ) {
      walk_state = WalkState::L1_WAIT_RESP;
    } else if (walk_state == WalkState::L2_REQ) {
      walk_state = WalkState::L2_WAIT_RESP;
    }
    if (ctx != nullptr) {
      if (walk_owner == Client::DTLB) {
        ctx->perf.ptw_dtlb_grant++;
      } else {
        ctx->perf.ptw_itlb_grant++;
      }
    }
  }

  bool has_pending_mem_req(Client client) const {
    return ptw_clients[client_idx(client)].req_pending;
  }

  uint32_t pending_mem_addr(Client client) const {
    return ptw_clients[client_idx(client)].req_addr;
  }

  void on_mem_read_granted(Client client) {
    auto &s = ptw_clients[client_idx(client)];
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
    auto &s = ptw_clients[client_idx(client)];
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

  WalkRespResult on_walk_mem_resp(uint32_t pte) {
    if (walk_drop_resp_credit > 0) {
      walk_drop_resp_credit--;
      return WalkRespResult::DROPPED;
    }
    if (!walk_active) {
      return WalkRespResult::IGNORED;
    }
    auto &wc = walk_clients[client_idx(walk_owner)];
    bool v = (pte & PTE_V) != 0;
    bool r = (pte & PTE_R) != 0;
    bool w = (pte & PTE_W) != 0;
    bool x = (pte & PTE_X) != 0;

    auto publish_fault = [&]() {
      wc.req_inflight = false;
      wc.resp_valid = true;
      wc.resp = {};
      wc.resp.fault = true;
      walk_active = false;
      walk_state = WalkState::IDLE;
    };
    auto publish_leaf = [&](uint8_t leaf_level) {
      wc.req_inflight = false;
      wc.resp_valid = true;
      wc.resp = {};
      wc.resp.fault = false;
      wc.resp.leaf_pte = pte;
      wc.resp.leaf_level = leaf_level;
      walk_active = false;
      walk_state = WalkState::IDLE;
    };

    if (walk_state == WalkState::L1_WAIT_RESP) {
      if (!v || (!r && w)) {
        publish_fault();
      } else if (r || x) {
        if (((pte >> 10) & 0x3FFu) != 0) {
          publish_fault();
        } else {
          publish_leaf(1);
        }
      } else {
        walk_l1_pte = pte;
        walk_state = WalkState::L2_REQ;
      }
    } else if (walk_state == WalkState::L2_WAIT_RESP) {
      if (!v || (!r && w) || !(r || x)) {
        publish_fault();
      } else {
        publish_leaf(0);
      }
    } else {
      publish_fault();
    }

    if (ctx != nullptr && wc.resp_valid) {
      if (walk_owner == Client::DTLB) {
        ctx->perf.ptw_dtlb_resp++;
      } else {
        ctx->perf.ptw_itlb_resp++;
      }
    }
    return WalkRespResult::HANDLED;
  }

  bool client_send_read_req(Client client, uint32_t paddr) {
    auto &s = ptw_clients[client_idx(client)];
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_req++;
      } else {
        ctx->perf.ptw_itlb_req++;
      }
    }
    if (s.resp_valid) {
      s.resp_valid = false;
    }
    if (s.req_pending || s.req_inflight) {
      if (ctx != nullptr) {
        if (client == Client::DTLB) {
          ctx->perf.ptw_dtlb_blocked++;
        } else {
          ctx->perf.ptw_itlb_blocked++;
        }
      }
      return false;
    }
    s.req_pending = true;
    s.req_addr = paddr;
    return true;
  }

  bool client_resp_valid(Client client) const {
    return ptw_clients[client_idx(client)].resp_valid;
  }
  uint32_t client_resp_data(Client client) const {
    return ptw_clients[client_idx(client)].resp_data;
  }
  void client_consume_resp(Client client) {
    ptw_clients[client_idx(client)].resp_valid = false;
  }

  bool walk_client_send_req(Client client, const PtwWalkReq &req) {
    auto &s = walk_clients[client_idx(client)];
    if (ctx != nullptr) {
      if (client == Client::DTLB) {
        ctx->perf.ptw_dtlb_req++;
      } else {
        ctx->perf.ptw_itlb_req++;
      }
    }
    if (s.resp_valid) {
      s.resp_valid = false;
    }
    if (s.req_pending || s.req_inflight) {
      if (ctx != nullptr) {
        if (client == Client::DTLB) {
          ctx->perf.ptw_dtlb_blocked++;
        } else {
          ctx->perf.ptw_itlb_blocked++;
        }
      }
      return false;
    }
    s.req_pending = true;
    s.req = req;
    return true;
  }

  bool walk_client_resp_valid(Client client) const {
    return walk_clients[client_idx(client)].resp_valid;
  }
  PtwWalkResp walk_client_resp(Client client) const {
    return walk_clients[client_idx(client)].resp;
  }
  void walk_client_consume_resp(Client client) {
    walk_clients[client_idx(client)].resp_valid = false;
  }
  void walk_client_flush(Client client) {
    auto &s = walk_clients[client_idx(client)];
    s.req_pending = false;
    s.req_inflight = false;
    s.resp_valid = false;

    if (walk_active && walk_owner == client) {
      if (walk_state == WalkState::L1_WAIT_RESP ||
          walk_state == WalkState::L2_WAIT_RESP) {
        walk_drop_resp_credit++;
      }
      walk_active = false;
      walk_state = WalkState::IDLE;
      walk_l1_pte = 0;
    }
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

  static size_t client_idx(Client c) { return static_cast<size_t>(c); }

  SimContext *ctx = nullptr;
  std::array<PtwClientState, kClientCount> ptw_clients{};
  std::array<WalkClientState, kClientCount> walk_clients{};
  WalkState walk_state = WalkState::IDLE;
  bool walk_active = false;
  Client walk_owner = Client::DTLB;
  Client walk_rr_next = Client::DTLB;
  uint32_t walk_l1_pte = 0;
  uint32_t walk_drop_resp_credit = 0;
};
