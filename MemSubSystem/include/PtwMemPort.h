#pragma once

#include <cstdint>

class PtwMemPort {
public:
  virtual ~PtwMemPort() {}
  virtual bool send_read_req(uint32_t paddr) = 0;
  virtual bool resp_valid() const = 0;
  virtual uint32_t resp_data() const = 0;
  virtual void consume_resp() = 0;
};
