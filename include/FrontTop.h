#pragma once

#include "front_IO.h"

class PtwMemPort;
class PtwWalkPort;
class SimContext;
namespace axi_interconnect {
struct ReadMasterPort_t;
}

class FrontTop {
public:
  front_top_in in;
  front_top_out out;
  SimContext *ctx = nullptr;
  PtwMemPort *icache_ptw_mem_port = nullptr;
  PtwWalkPort *icache_ptw_walk_port = nullptr;
  axi_interconnect::ReadMasterPort_t *icache_mem_read_port = nullptr;

  void init();
  void step_bpu();
  void step_oracle();
};
