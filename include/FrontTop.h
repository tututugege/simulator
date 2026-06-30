#pragma once

#include "front_IO.h"
#include <memory>

class PtwMemPort;
class PtwWalkPort;
class SimContext;
struct FrontReadData;
struct FrontUpdateRequest;
namespace axi_interconnect {
struct ReadMasterPort_t;
}

struct ICacheMemPortReq{
  bool req_valid = false;
  uint32_t req_addr = 0;
};
struct ICacheMemPortResp{
  bool resp_valid = false;
  bool resp_miss = false;
  uint32_t resp_data;
};

class FrontTop {
public:
  front_top_in in;
  front_top_out out;
  SimContext *ctx = nullptr;
  PtwMemPort *icache_ptw_mem_port = nullptr;
  PtwWalkPort *icache_ptw_walk_port = nullptr;
  ICacheMemPortReq *icache_mem_req_port = nullptr;
  ICacheMemPortResp *icache_mem_resp_port = nullptr;
  axi_interconnect::ReadMasterPort_t *icache_mem_read_port = nullptr;

  FrontTop();
  ~FrontTop();
  void init();
  void seq_read_bpu();
  void comb_bpu();
  void comb_apply_front2back_accept(bool accept);
  void seq_write_bpu();
  void step_bpu();
  void step_oracle();
  void dump_debug_state() const;

private:
  std::unique_ptr<FrontReadData> read_data_;
  std::unique_ptr<FrontUpdateRequest> update_req_;
};
