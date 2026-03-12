#pragma once

#include "MemPtwBlock.h"
#include "IO.h"
#include "util.h"
#include <deque>

class MemRespRouteBlock {
public:
  enum class Owner : uint8_t {
    LSU,
    PTW_DTLB,
    PTW_ITLB,
    PTW_WALK,
  };

  enum class RouteResult : uint8_t {
    NONE,
    HANDLED,
    DROPPED_OR_IGNORED,
  };

  MemRespRouteBlock() = default;
  void init() {
    owner_q.clear();
  }
  void enqueue_owner(Owner o) { owner_q.push_back(o); }

  RouteResult route_resp(const MemRespIO &resp, MemRespIO *lsu_resp_io,
                         MemPtwBlock *ptw_block) {
    if (!resp.valid) {
      return RouteResult::NONE;
    }
    Assert(!owner_q.empty() &&
           "MemRespRouteBlock: read response without owner tag");
    Owner owner = owner_q.front();
    owner_q.pop_front();

    if (owner == Owner::LSU) {
      if (lsu_resp_io != nullptr) {
        *lsu_resp_io = resp;
      }
      return RouteResult::HANDLED;
    }

    Assert(ptw_block != nullptr && "MemRespRouteBlock: ptw_block is null");
    if (owner == Owner::PTW_DTLB) {
      ptw_block->on_mem_resp_client(MemPtwBlock::Client::DTLB, resp.data);
      return RouteResult::HANDLED;
    }
    if (owner == Owner::PTW_ITLB) {
      ptw_block->on_mem_resp_client(MemPtwBlock::Client::ITLB, resp.data);
      return RouteResult::HANDLED;
    }
    auto st = ptw_block->on_walk_mem_resp(resp.data);
    if (st == MemPtwBlock::WalkRespResult::HANDLED) {
      return RouteResult::HANDLED;
    }
    return RouteResult::DROPPED_OR_IGNORED;
  }
private:
  std::deque<Owner> owner_q;
};
