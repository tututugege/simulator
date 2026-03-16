#include "PtwWalker.h"
#include "ref.h"

PtwWalker::PtwWalker(PtwMemPort *port)
    : mem_port(port), state(State::IDLE), req_v_addr(0), req_satp(0),
      l1_pte(0), leaf_pte_reg(0), leaf_level_reg(0) {}

void PtwWalker::flush() {
  state = State::IDLE;
  req_v_addr = 0;
  req_satp = 0;
  l1_pte = 0;
  leaf_pte_reg = 0;
  leaf_level_reg = 0;
}

bool PtwWalker::start(uint32_t v_addr, uint32_t satp) {
  if (busy() || mem_port == nullptr) {
    return false;
  }
  req_v_addr = v_addr;
  req_satp = satp;
  l1_pte = 0;
  leaf_pte_reg = 0;
  leaf_level_reg = 0;
  state = State::L1_REQ;
  return true;
}

PtwWalker::State PtwWalker::tick() {
  if (state == State::IDLE || state == State::DONE || state == State::FAULT) {
    return state;
  }

  if (state == State::L1_REQ) {
    uint32_t root_ppn = req_satp & 0x3FFFFF;
    uint32_t vpn1 = (req_v_addr >> 22) & 0x3FF;
    uint32_t pte_addr = (root_ppn << 12) + (vpn1 << 2);
    if (mem_port->send_read_req(pte_addr)) {
      state = State::L1_RESP;
    }
    return state;
  }

  if (state == State::L1_RESP) {
    if (!mem_port->resp_valid()) {
      return state;
    }
    uint32_t pte = mem_port->resp_data();
    mem_port->consume_resp();

    bool v = pte & PTE_V;
    bool r = pte & PTE_R;
    bool w = pte & PTE_W;
    bool x = pte & PTE_X;
    if (!v || (!r && w)) {
      state = State::FAULT;
      return state;
    }

    if (r || x) {
      if (((pte >> 10) & 0x3FF) != 0) {
        state = State::FAULT;
        return state;
      }
      leaf_pte_reg = pte;
      leaf_level_reg = 1;
      state = State::DONE;
      return state;
    }

    l1_pte = pte;
    state = State::L2_REQ;
    return state;
  }

  if (state == State::L2_REQ) {
    uint32_t ppn = (l1_pte >> 10) & 0x3FFFFF;
    uint32_t vpn0 = (req_v_addr >> 12) & 0x3FF;
    uint32_t pte_addr = (ppn << 12) + (vpn0 << 2);
    if (mem_port->send_read_req(pte_addr)) {
      state = State::L2_RESP;
    }
    return state;
  }

  if (state == State::L2_RESP) {
    if (!mem_port->resp_valid()) {
      return state;
    }
    uint32_t pte = mem_port->resp_data();
    mem_port->consume_resp();

    bool v = pte & PTE_V;
    bool r = pte & PTE_R;
    bool w = pte & PTE_W;
    bool x = pte & PTE_X;
    if (!v || (!r && w)) {
      state = State::FAULT;
      return state;
    }
    if (!(r || x)) {
      state = State::FAULT;
      return state;
    }

    leaf_pte_reg = pte;
    leaf_level_reg = 0;
    state = State::DONE;
    return state;
  }

  return state;
}
