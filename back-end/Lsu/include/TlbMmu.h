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
  enum class Kind : uint8_t {
    DTLB,
    ITLB,
  };

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

  enum class TraceSource : uint8_t {
    NONE = 0,
    IDENTITY,
    TLB_HIT,
    TLB_PERM_FAULT,
    WALK_SHARED,
    WALK_LOCAL,
    FLUSH,
  };

  static constexpr int kItlbTopPortCount = 1;
  static constexpr int kDtlbTopPortCount = LSU_LDU_COUNT + LSU_STA_COUNT;
  static constexpr int kMaxTopPortCount =
      (kDtlbTopPortCount > kItlbTopPortCount) ? kDtlbTopPortCount
                                              : kItlbTopPortCount;

  struct TranslateContext {
    wire<32> satp = 0;
    wire<2> eff_priv = 0;
    wire<1> mxr = false;
    wire<1> sum = false;
  };

  struct TlbEntry {
    reg<1> valid = false;
    reg<1> global = false;
    reg<9> asid = 0; // Sv32 satp ASID is 9 bits: satp[30:22]
    reg<1> level = 0; // 1: megapage (L1 leaf), 0: normal page (L0 leaf)
    reg<10> vpn1 = 0;
    reg<10> vpn0 = 0;
    reg<22> ppn = 0; // Sv32 PPN is 22 bits
    reg<8> perm = 0; // bit[0:7]=V,R,W,X,U,G,A,D
  };

  struct WalkRegs {
    reg<1> active = false;
    reg<1> req_sent = false;
    reg<32> v_addr = 0;
    reg<2> type = 0;
    reg<32> satp = 0;
    reg<2> eff_priv = 0;
    reg<1> mxr = false;
    reg<1> sum = false;
  };

  static constexpr int kMaxTlbEntries =
      (ITLB_ENTRIES > DTLB_ENTRIES) ? ITLB_ENTRIES : DTLB_ENTRIES;
  static constexpr int kMaxSlotBits =
      kMaxTlbEntries <= 1 ? 1 : clog2(static_cast<uint64_t>(kMaxTlbEntries));

  struct PendingRefill {
    reg<1> valid = false;
    TlbEntry entry{};
    reg<kMaxSlotBits> slot = 0;
    reg<kMaxSlotBits> next_repl_ptr = 0;
  };

  struct LookupInput {
    wire<1> valid = false;
    wire<32> v_addr = 0;
    wire<9> asid = 0;
  };

  struct LookupOutput {
    wire<1> hit = false;
    TlbEntry entry{};
  };

  struct CoreInput {
    wire<1> translate_valid = false;
    wire<32> v_addr = 0;
    wire<2> type = 0;
    wire<32> satp = 0;
    wire<2> privilege = 0;
    wire<1> mstatus_mxr = false;
    wire<1> mstatus_sum = false;
    wire<1> mstatus_mprv = false;
    wire<2> mstatus_mpp = 0;

    LookupOutput lookup{};
    WalkRegs walk{};
    PendingRefill refill{};
    int repl_ptr = 0;
    int tlb_capacity = 1;

    wire<1> walk_req_ready = false;
    wire<1> walk_resp_valid = false;
    PtwWalkResp walk_resp{};
  };

  struct CoreOutput {
    wire<1> resp_valid = false;
    wire<2> result = 0;
    wire<32> p_addr = 0;
    wire<3> retry_reason = 0;

    WalkRegs walk_next{};
    PendingRefill refill_next{};

    wire<1> walk_req_valid = false;
    PtwWalkReq walk_req{};
    wire<1> walk_resp_consumed = false;

    wire<3> trace_source = 0;
    wire<8> trace_level = 0xff;
    wire<8> trace_perm = 0;
  };

  struct TopPortInput {
    wire<1> valid = false;
    wire<32> v_addr = 0;
    wire<2> type = 0;
    LookupOutput lookup{};
  };

  struct TopPortOutput {
    wire<1> resp_valid = false;
    wire<2> result = 0;
    wire<32> p_addr = 0;
    wire<3> retry_reason = 0;
    wire<3> trace_source = 0;
    wire<8> trace_level = 0xff;
    wire<8> trace_perm = 0;
  };

  struct TopInput {
    wire<1> flush = false;
    wire<1> cancel = false;
    wire<32> satp = 0;
    wire<2> privilege = 0;
    wire<1> mstatus_mxr = false;
    wire<1> mstatus_sum = false;
    wire<1> mstatus_mprv = false;
    wire<2> mstatus_mpp = 0;

    WalkRegs walk{};
    PendingRefill refill{};
    int repl_ptr = 0;
    int tlb_capacity = 1;

    wire<1> walk_req_ready = false;
    wire<1> walk_resp_valid = false;
    PtwWalkResp walk_resp{};

    std::array<TopPortInput, kMaxTopPortCount> port{};
    int port_count = 0;
  };

  struct TopOutput {
    std::array<TopPortOutput, kMaxTopPortCount> port{};

    wire<1> table_flush = false;
    WalkRegs walk_next{};
    PendingRefill refill_next{};

    wire<1> walk_req_valid = false;
    PtwWalkReq walk_req{};
    wire<1> walk_resp_consumed = false;
    wire<1> walk_client_flush = false;
  };

  static constexpr int kAsidBits = 9;
  static constexpr int kVpnPartBits = 10;
  static constexpr int kVpnBits = 20;
  static constexpr int kPpnBits = 22;
  static constexpr int kPermBits = 8;
  static constexpr int kResultBits = 2;
  static constexpr int kRetryBits = 3;
  static constexpr int kTraceSourceBits = 3;
  static constexpr int kPackedEntryWidth =
      1 + 1 + kAsidBits + 1 + kVpnPartBits + kVpnPartBits + kPpnBits +
      kPermBits;
  static constexpr int kPackedLookupRespWidth = 1 + kPackedEntryWidth;
  static constexpr int kPackedWalkRegsWidth =
      1 + 1 + 32 + 2 + 32 + 2 + 1 + 1;
  static constexpr int kPackedWalkPortInWidth =
      1 + 1 + 1 + kVpnBits + (kPpnBits + kPermBits) + 1;
  static constexpr int kPackedWalkReqWidth = 1 + 32 + 32 + 2;
  static constexpr int kLookupReqWidth =
      1 + kVpnPartBits + kVpnPartBits + kAsidBits;
  static constexpr int kLookupRespWidth = kPackedLookupRespWidth;

  static constexpr int slot_width(int entries) {
    return entries <= 1 ? 0 : clog2(static_cast<uint64_t>(entries));
  }
  static constexpr int packed_refill_width(int entries) {
    return 1 + kPackedEntryWidth + 2 * slot_width(entries);
  }
  static constexpr int lookup_req_width() { return kLookupReqWidth; }
  static constexpr int lookup_resp_width() { return kLookupRespWidth; }
  static constexpr int core_pi_width(int entries) {
    return 1 + 32 + 2 + 32 + 2 + 1 + 1 + 1 + 2 +
           kPackedLookupRespWidth + kPackedWalkRegsWidth +
           packed_refill_width(entries) + slot_width(entries) +
           kPackedWalkPortInWidth;
  }
  static constexpr int core_po_width(int entries) {
    return 1 + kResultBits + 32 + kRetryBits + kPackedWalkRegsWidth +
           packed_refill_width(entries) + kPackedWalkReqWidth + 1 +
           kTraceSourceBits + 8 + 8;
  }
  static constexpr int top_common_pi_width(int entries) {
    return 1 + 1 + 32 + 2 + 1 + 1 + 1 + 2 + kPackedWalkRegsWidth +
           packed_refill_width(entries) + slot_width(entries) +
           kPackedWalkPortInWidth;
  }
  static constexpr int top_port_pi_width() {
    return 1 + 32 + kPackedLookupRespWidth;
  }
  static constexpr int top_pi_width(int entries, int ports) {
    return top_common_pi_width(entries) + ports * top_port_pi_width();
  }
  static constexpr int top_common_po_width(int entries) {
    return 1 + kPackedWalkRegsWidth + packed_refill_width(entries) +
           kPackedWalkReqWidth + 1 + 1;
  }
  static constexpr int top_port_po_width() {
    return 1 + kResultBits + 32 + kRetryBits + kTraceSourceBits + 8 + 8;
  }
  static constexpr int top_po_width(int entries, int ports) {
    return top_common_po_width(entries) + ports * top_port_po_width();
  }

  static constexpr int kItlbSlotBits =
      ITLB_ENTRIES <= 1 ? 0 : clog2(static_cast<uint64_t>(ITLB_ENTRIES));
  static constexpr int kDtlbSlotBits =
      DTLB_ENTRIES <= 1 ? 0 : clog2(static_cast<uint64_t>(DTLB_ENTRIES));
  static constexpr int kItlbPackedRefillWidth =
      1 + kPackedEntryWidth + 2 * kItlbSlotBits;
  static constexpr int kDtlbPackedRefillWidth =
      1 + kPackedEntryWidth + 2 * kDtlbSlotBits;

  static constexpr int ITLB_LOOKUP_REQ_WIDTH = kLookupReqWidth;
  static constexpr int ITLB_LOOKUP_RESP_WIDTH = kLookupRespWidth;
  static constexpr int kTopPortPiWidth = 1 + 32 + kPackedLookupRespWidth;
  static constexpr int kTopPortPoWidth =
      1 + kResultBits + 32 + kRetryBits + kTraceSourceBits + 8 + 8;
  static constexpr int kItlbTopCommonPiWidth =
      1 + 1 + 32 + 2 + 1 + 1 + 1 + 2 + kPackedWalkRegsWidth +
      kItlbPackedRefillWidth + kItlbSlotBits + kPackedWalkPortInWidth;
  static constexpr int kItlbTopCommonPoWidth =
      1 + kPackedWalkRegsWidth + kItlbPackedRefillWidth +
      kPackedWalkReqWidth + 1 + 1;
  static constexpr int ITLB_TOP_PI_WIDTH =
      kItlbTopCommonPiWidth + kItlbTopPortCount * kTopPortPiWidth;
  static constexpr int ITLB_TOP_PO_WIDTH =
      kItlbTopCommonPoWidth + kItlbTopPortCount * kTopPortPoWidth;

  static constexpr int DTLB_LOOKUP_REQ_WIDTH = kLookupReqWidth;
  static constexpr int DTLB_LOOKUP_RESP_WIDTH = kLookupRespWidth;
  static constexpr int kDtlbTopCommonPiWidth =
      1 + 1 + 32 + 2 + 1 + 1 + 1 + 2 + kPackedWalkRegsWidth +
      kDtlbPackedRefillWidth + kDtlbSlotBits + kPackedWalkPortInWidth;
  static constexpr int kDtlbTopCommonPoWidth =
      1 + kPackedWalkRegsWidth + kDtlbPackedRefillWidth +
      kPackedWalkReqWidth + 1 + 1;
  static constexpr int DTLB_TOP_PI_WIDTH =
      kDtlbTopCommonPiWidth + kDtlbTopPortCount * kTopPortPiWidth;
  static constexpr int DTLB_TOP_PO_WIDTH =
      kDtlbTopCommonPoWidth + kDtlbTopPortCount * kTopPortPoWidth;

  TlbMmu(SimContext *ctx, PtwMemPort *port = nullptr,
         int tlb_entries = DTLB_ENTRIES, Kind kind = Kind::DTLB);

  Result translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                   CsrStatusIO *status);
  void translate_lsu_ports(const LsuMMUIO &req, MMULsuIO &resp);
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

  static LookupOutput lookup_table_comb(const LookupInput &in,
                                        const TlbEntry *entries,
                                        int entry_count);
  static CoreOutput core_comb(const CoreInput &in);
  static TopOutput top_comb(const TopInput &in, int entries);
  static void tlb_core_io_generator(const bool *pi, bool *po, int entries);
  static void itlb_top_io_generator(const bool *pi, bool *po);
  static void dtlb_top_io_generator(const bool *pi, bool *po);

private:
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

  SimContext *ctx;
  Kind kind_;
  PtwWalkPort *walk_port = nullptr;
  PtwWalker walker;
  // Stable TLB register file. translate() only reads from this array; any
  // refill first lands in refill_comb_ and is committed in seq().
  std::vector<TlbEntry> tlb_entries_;
  int tlb_capacity_;
  int repl_ptr_;
  RetryReason last_retry_reason_ = RetryReason::NONE;

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
  static TranslateContext build_translate_context(uint32_t type, uint32_t satp,
                                                  uint32_t privilege,
                                                  bool mstatus_mxr,
                                                  bool mstatus_sum,
                                                  bool mstatus_mprv,
                                                  uint32_t mstatus_mpp);
  static bool entry_matches(const TlbEntry &entry, uint32_t v_addr,
                            uint16_t asid);
  bool lookup(uint32_t v_addr, uint16_t asid, TlbEntry &hit) const;
  LookupOutput lookup_table_access(uint32_t v_addr, uint16_t asid) const;
  static uint32_t compose_paddr(uint32_t v_addr, const TlbEntry &e);
  static bool check_perm(uint8_t perm, uint32_t type, int eff_priv, bool sum,
                         bool mxr);
  Result translate_shared_via_io_generator(uint32_t &p_addr, uint32_t v_addr,
                                           uint32_t type,
                                           CsrStatusIO *status);
  TopInput build_top_common_input(const CsrStatusIO *status) const;
  TopOutput run_top_via_io_generator(const TopInput &in) const;
  void apply_top_output(const TopOutput &out);
  void run_control_via_top(bool flush, bool cancel);
  Result walk_and_refill_shared(uint32_t &p_addr, uint32_t v_addr,
                                uint32_t type, CsrStatusIO *status);
  Result walk_and_refill(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                         CsrStatusIO *status);
  void schedule_refill(uint32_t v_addr, uint16_t asid, uint8_t level,
                       uint32_t pte, bool global);
};
