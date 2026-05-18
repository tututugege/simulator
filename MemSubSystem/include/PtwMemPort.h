#pragma once

#include <cstdint>

struct PtwMemPortCombIn {
  bool req_valid = false;
  uint32_t req_addr = 0;
  bool resp_consumed = false;
};

struct PtwMemPortCombOut {
  bool req_ready = false;
  bool resp_valid = false;
  uint32_t resp_data = 0;
};

class PtwMemPort {
public:
  virtual ~PtwMemPort() {}
  virtual bool send_read_req(uint32_t paddr) = 0;
  virtual bool resp_valid() const = 0;
  virtual uint32_t resp_data() const = 0;
  virtual void consume_resp() = 0;
};
