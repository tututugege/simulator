#pragma once

#include "config.h"
#include "IO.h" // For CsrStatusIO
#include <cstdio>
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
  // Optional sequential hook. flush()/other control inputs may be sampled in
  // comb and committed here at the cycle edge.
  virtual void seq() {}
  // Optional hook to cancel only in-flight translation walks without dropping
  // cached TLB entries.
  virtual void cancel_pending_walk() {}
  // Optional visibility hook for fence/quiesce logic.
  virtual bool translation_pending() const { return false; }
  virtual void dump_debug(FILE *out) const { (void)out; }

  // Optional binding hook for PTW memory access path.
  virtual void set_ptw_mem_port(PtwMemPort *port) { (void)port; }
  // Optional binding hook for shared PTW walk service.
  virtual void set_ptw_walk_port(PtwWalkPort *port) { (void)port; }
};
