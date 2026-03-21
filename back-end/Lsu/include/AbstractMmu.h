#pragma once

#include "config.h"
#include "IO.h" // For CsrStatusIO
#include <cstdint>

class PtwMemPort;
class PtwWalkPort;

class AbstractMmu {
public:
  enum class Result : uint8_t {
    OK,
    FAULT,
    RETRY,
  };

  virtual ~AbstractMmu() {}

  // 核心翻译接口
  // type: 0=Fetch, 1=Load, 2=Store
  virtual Result translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                           CsrStatusIO *status) = 0;

  // Optional TLB shootdown hook (e.g. sfence.vma).
  virtual void flush() {}
  // Optional hook to cancel only in-flight translation walks without dropping
  // cached TLB entries.
  virtual void cancel_pending_walk() {}

  // Optional binding hook for PTW memory access path.
  virtual void set_ptw_mem_port(PtwMemPort *port) { (void)port; }
  // Optional binding hook for shared PTW walk service.
  virtual void set_ptw_walk_port(PtwWalkPort *port) { (void)port; }
};
