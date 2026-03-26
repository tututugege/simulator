#pragma once

#include "IO.h"

class PtwWalkPort {
public:
  virtual ~PtwWalkPort() {}
  virtual bool send_walk_req(const PtwWalkReq &req) = 0;
  virtual bool resp_valid() const = 0;
  virtual PtwWalkResp resp() const = 0;
  virtual void consume_resp() = 0;
  virtual void flush_client() {}
};
