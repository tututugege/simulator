#pragma once

#include "IO.h"

struct PtwWalkPortCombIn {
  bool req_valid = false;
  PtwWalkReq req = {};
  bool resp_consumed = false;
  bool flush = false;
};

struct PtwWalkPortSeqIn {
  bool flush = false;
};

struct PtwWalkPortCombOut {
  bool req_ready = false;
  bool resp_valid = false;
  PtwWalkResp resp = {};
};

class PtwWalkPort {
public:
  virtual ~PtwWalkPort() {}
  virtual bool send_walk_req(const PtwWalkReq &req) = 0;
  virtual bool resp_valid() const = 0;
  virtual PtwWalkResp resp() const = 0;
  virtual void consume_resp() = 0;
  virtual void flush_client() {}
};