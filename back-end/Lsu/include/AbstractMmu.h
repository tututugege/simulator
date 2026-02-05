#pragma once

#include "config.h"
#include "IO.h" // For CsrStatusIO
#include <cstdint>

class AbstractMmu {
public:
  virtual ~AbstractMmu() {}

  // 核心翻译接口
  // type: 0=Fetch, 1=Load, 2=Store
  virtual bool translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                         CsrStatusIO *status) = 0;
};
