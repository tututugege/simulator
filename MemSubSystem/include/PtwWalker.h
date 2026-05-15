#pragma once

#include "PtwMemPort.h"
#include <cstdint>

class PtwWalker {
public:
  enum class State : uint8_t {
    IDLE,
    L1_REQ,
    L1_RESP,
    L2_REQ,
    L2_RESP,
    DONE,
    FAULT,
  };

  explicit PtwWalker(PtwMemPort *port = nullptr);

  void set_mem_port(PtwMemPort *port) { mem_port = port; }
  void flush();
  bool busy() const { return state != State::IDLE; }
  bool start(uint32_t v_addr, uint32_t satp);
  State tick();

  uint32_t leaf_pte() const { return leaf_pte_reg; }
  uint8_t leaf_level() const { return leaf_level_reg; }

private:
  PtwMemPort *mem_port;
  State state;

  uint32_t req_v_addr;
  uint32_t req_satp;
  uint32_t l1_pte;
  uint32_t leaf_pte_reg;
  uint8_t leaf_level_reg;
};

