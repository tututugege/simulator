#pragma once

#include "DcacheConfig.h"
#include "FrontTop.h"
#include "IO.h"
#include <cstdint>
#include <cstdio>
constexpr uint32_t DcacheReqIdRouteBit = 1u << 31;
constexpr uint32_t MemRouteBlockReqID = DcacheReqIdRouteBit;

static inline bool dcache_req_id_is_route(uint32_t req_id) {
  return (req_id & DcacheReqIdRouteBit) != 0;
}

static inline uint32_t next_route_req_id(uint32_t req_id) {
  uint32_t next = req_id + 1;
  return dcache_req_id_is_route(next) ? next : MemRouteBlockReqID;
}

enum class Owner : uint8_t {
  NONE = 0,
  LSU,
  ICACHE,
  PTW_DTLB,
  PTW_ITLB,
  PTW_WALK,
};
struct PtwReq {
  wire<1> valid;
  wire<32> addr;
};
struct PtwGrant {
  wire<1> valid;
  Owner owner;
  uint32_t req_id;
};
struct PtwEvent {
  wire<1> valid;
  Owner owner;
  uint32_t data;
  uint8_t replay;
  uint32_t req_addr;
  uint32_t req_id;
};
struct ReplayWakeup {
  wire<1> dtlb;
  wire<1> itlb;
  wire<1> walk;
};
struct MemRouteIn {
  DcacheLsuIO *dcache_resp;

  DCacheReqPorts *lsu_req;
  ICacheMemPortReq *icache_req;

  PtwReq *ptw_walk_req;
  PtwReq *ptw_dtlb_req;
  PtwReq *ptw_itlb_req;
};

struct MemRouteOut {
  LsuDcacheIO *dcache_req;

  DcacheLsuIO *lsu_resp;
  ICacheMemPortResp *icache_resp;

  PtwGrant *ptw_grant;
  PtwEvent *ptw_events;

  ReplayWakeup *wakeup;
};
struct IssueTag {
  Owner owner = Owner::NONE;
  uint32_t req_addr = 0;
  uint32_t req_id = 0;
};
struct MemRouteBlockState {
  IssueTag issued_tags;
  ReplayWakeup ptw_wait;

  wire<1> lsu_replay_valid;
  uint32_t lsu_replay_req_id;

  uint32_t next_req_id;
};

class MemRouteBlock {
public:
  MemRouteBlockState cur, nxt;
  MemRouteIn in;
  MemRouteOut out;

  void init();
  void comb_response();
  void comb_request();
  void seq();

  void dump_debug_state(FILE *out) const;

};
