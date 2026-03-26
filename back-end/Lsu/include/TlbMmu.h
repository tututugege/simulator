#pragma once

#include "AbstractMmu.h"
#include "PtwWalker.h"
#include "PtwWalkPort.h"
#include <cstdint>
#include <vector>

class SimContext;
class PtwMemPort;
class AbstractLsu;

class TlbMmu : public AbstractMmu {
public:
  enum class RetryReason : uint8_t {
    NONE = 0,
    OTHER_WALK_ACTIVE = 1,
    WALK_REQ_BLOCKED = 2,
    WAIT_WALK_RESP = 3,
    LOCAL_WALKER_BUSY = 4,
  };

  TlbMmu(SimContext *ctx, PtwMemPort *port = nullptr,
         AbstractLsu *coherent_lsu = nullptr,
         int tlb_entries = DTLB_ENTRIES);

  Result translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                   CsrStatusIO *status) override;
  void flush() override;
  void cancel_pending_walk() override;
  void set_ptw_mem_port(PtwMemPort *port) override;
  void set_ptw_walk_port(PtwWalkPort *port) override;
  RetryReason last_retry_reason() const { return last_retry_reason_; }
  struct DebugState {
    bool walk_active = false;
    bool walk_req_sent = false;
    uint32_t walk_v_addr = 0;
    uint32_t walk_type = 0;
    uint32_t walk_satp = 0;
    int walk_eff_priv = 0;
    bool walk_mxr = false;
    bool walk_sum = false;
    RetryReason last_retry_reason = RetryReason::NONE;
  };
  DebugState debug_state() const;

private:
  struct TlbEntry {
    bool valid;
    bool global;
    uint8_t asid;
    uint8_t level; // 1: megapage (L1 leaf), 0: normal page (L0 leaf)
    uint16_t vpn1;
    uint16_t vpn0;
    uint32_t ppn; // 22 bits for Sv32
    uint8_t perm; // bit[0:7]=V,R,W,X,U,G,A,D
  };

  SimContext *ctx;
  PtwWalkPort *walk_port = nullptr;
  PtwWalker walker;
  std::vector<TlbEntry> dtlb;
  int tlb_entries;
  int repl_ptr;
  bool walk_active = false;
  uint32_t walk_v_addr = 0;
  uint32_t walk_type = 0;
  uint32_t walk_satp = 0;
  int walk_eff_priv = 0;
  bool walk_mxr = false;
  bool walk_sum = false;
  bool walk_req_sent = false;
  RetryReason last_retry_reason_ = RetryReason::NONE;
  AbstractLsu *coherent_lsu_ = nullptr;

  bool lookup(uint32_t v_addr, uint8_t asid, TlbEntry &hit) const;
  uint32_t compose_paddr(uint32_t v_addr, const TlbEntry &e) const;
  bool check_perm(uint8_t perm, uint32_t type, int eff_priv, bool sum,
                  bool mxr) const;
  Result walk_and_refill_coherent(uint32_t &p_addr, uint32_t v_addr,
                                  uint32_t type, CsrStatusIO *status);
  Result walk_and_refill_shared(uint32_t &p_addr, uint32_t v_addr,
                                uint32_t type, CsrStatusIO *status);
  Result walk_and_refill(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                         CsrStatusIO *status);
  void refill(uint32_t v_addr, uint8_t asid, uint8_t level, uint32_t pte);
};
