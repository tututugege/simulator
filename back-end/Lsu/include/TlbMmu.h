#pragma once

#include "IO.h"
#include "PtwWalker.h"
#include "PtwWalkPort.h"
#include "config.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

class SimContext;
class PtwMemPort;

class TlbMmu {
public:
  enum class Result : uint8_t {
    OK,
    FAULT,
    RETRY,
  };

  enum class RetryReason : uint8_t {
    NONE = 0,
    OTHER_WALK_ACTIVE = 1,
    WALK_REQ_BLOCKED = 2,
    WAIT_WALK_RESP = 3,
    LOCAL_WALKER_BUSY = 4,
  };

  TlbMmu(SimContext *ctx, PtwMemPort *port = nullptr,
         int tlb_entries = DTLB_ENTRIES);

  Result translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                   CsrStatusIO *status);
  void flush();
  void seq();
  void cancel_pending_walk();
  void dump_debug(FILE *out) const;
  void set_ptw_mem_port(PtwMemPort *port);
  void set_ptw_walk_port(PtwWalkPort *port);
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
  enum class TraceSource : uint8_t {
    NONE = 0,
    IDENTITY,
    TLB_HIT,
    TLB_PERM_FAULT,
    WALK_SHARED,
    WALK_LOCAL,
    FLUSH,
  };

  struct TraceEntry {
    bool valid = false;
    uint64_t cycle = 0;
    TraceSource source = TraceSource::NONE;
    Result result = Result::OK;
    RetryReason retry = RetryReason::NONE;
    uint32_t v_addr = 0;
    uint32_t p_addr = 0;
    uint32_t satp = 0;
    uint16_t asid = 0;
    uint8_t type = 0;
    uint8_t level = 0xff;
    uint8_t perm = 0;
    bool walk_active = false;
    bool walk_req_sent = false;
  };

  struct TranslateContext {
    uint32_t satp = 0;
    int eff_priv = 0;
    bool mxr = false;
    bool sum = false;
  };

  struct TlbEntry {
    bool valid;
    bool global;
    uint16_t asid; // Sv32 satp ASID is 9 bits: satp[30:22]
    uint8_t level; // 1: megapage (L1 leaf), 0: normal page (L0 leaf)
    uint16_t vpn1;
    uint16_t vpn0;
    uint32_t ppn; // 22 bits for Sv32
    uint8_t perm; // bit[0:7]=V,R,W,X,U,G,A,D
  };

  SimContext *ctx;
  PtwWalkPort *walk_port = nullptr;
  PtwWalker walker;
  // Stable TLB register file. translate() only reads from this array; any
  // refill first lands in refill_comb_ and is committed in seq().
  std::vector<TlbEntry> tlb_entries_;
  int tlb_capacity_;
  int repl_ptr_;
  RetryReason last_retry_reason_ = RetryReason::NONE;

  struct WalkRegs {
    bool active = false;
    bool req_sent = false;
    uint32_t v_addr = 0;
    uint32_t type = 0;
    uint32_t satp = 0;
    int eff_priv = 0;
    bool mxr = false;
    bool sum = false;
  };

  struct PendingRefill {
    bool valid = false;
    TlbEntry entry{};
    int slot = 0;
    int next_repl_ptr = 0;
  };

  WalkRegs walk_regs_{};
  // Per-cycle comb shadow for the in-flight miss record. Multiple translate()
  // calls in the same cycle share and update this view before seq() commits it.
  WalkRegs walk_comb_{};
  bool walk_comb_valid_ = false;
  // Same-cycle refill bypass. This keeps a completed walk visible to later
  // translate() calls in the same cycle without making the TLB regfile itself
  // look combinationally writable.
  PendingRefill refill_comb_{};
  bool flush_pending_ = false;
  std::array<TraceEntry, 16> trace_hist_{};
  size_t trace_hist_head_ = 0;

  const WalkRegs &visible_walk_regs() const;
  WalkRegs &ensure_walk_comb();
  static bool trace_watch_vaddr(uint32_t v_addr);
  void record_trace(TraceSource source, Result result, uint32_t v_addr,
                    uint32_t p_addr, uint32_t satp, uint16_t asid,
                    uint32_t type, uint8_t level, uint8_t perm);
  void clear_tlb_entries();
  static TranslateContext build_translate_context(uint32_t type,
                                                  CsrStatusIO *status);
  bool entry_matches(const TlbEntry &entry, uint32_t v_addr,
                     uint16_t asid) const;
  bool lookup(uint32_t v_addr, uint16_t asid, TlbEntry &hit) const;
  uint32_t compose_paddr(uint32_t v_addr, const TlbEntry &e) const;
  bool check_perm(uint8_t perm, uint32_t type, int eff_priv, bool sum,
                  bool mxr) const;
  Result walk_and_refill_shared(uint32_t &p_addr, uint32_t v_addr,
                                uint32_t type, CsrStatusIO *status);
  Result walk_and_refill(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                         CsrStatusIO *status);
  void schedule_refill(uint32_t v_addr, uint16_t asid, uint8_t level,
                       uint32_t pte, bool global);
};
