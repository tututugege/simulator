#include "TlbMmu.h"
#include "TlbMmuKernel.h"
#include "PtwMemPort.h"
#include "ref.h"

namespace {
#ifndef CONFIG_ITLB_FOCUS_VADDR_BEGIN
#define CONFIG_ITLB_FOCUS_VADDR_BEGIN 0u
#endif

#ifndef CONFIG_ITLB_FOCUS_VADDR_END
#define CONFIG_ITLB_FOCUS_VADDR_END 0u
#endif

inline bool itlb_focus_vaddr(uint32_t v_addr) {
  const uint32_t begin = static_cast<uint32_t>(CONFIG_ITLB_FOCUS_VADDR_BEGIN);
  const uint32_t end = static_cast<uint32_t>(CONFIG_ITLB_FOCUS_VADDR_END);
  return end > begin && (v_addr - begin) < (end - begin);
}

constexpr uint32_t kSv32AsidShift = 22u;
constexpr uint32_t kSv32AsidMask = 0x1FFu;

inline uint16_t sv32_asid(uint32_t satp) {
  return static_cast<uint16_t>((satp >> kSv32AsidShift) & kSv32AsidMask);
}

inline const char *mmu_result_name(TlbMmu::Result ret) {
  switch (ret) {
  case TlbMmu::Result::OK:
    return "OK";
  case TlbMmu::Result::RETRY:
    return "RETRY";
  case TlbMmu::Result::FAULT:
    return "FAULT";
  default:
    return "UNKNOWN";
  }
}

struct BitWriter {
  bool *cursor;

  explicit BitWriter(bool *bits) : cursor(bits) {}

  void bit(bool value) { *cursor++ = value; }

  void uint(uint32_t value, int width) {
    for (int i = 0; i < width; ++i) {
      bit(((value >> i) & 1u) != 0);
    }
  }
};

struct BitReader {
  const bool *cursor;

  explicit BitReader(const bool *bits) : cursor(bits) {}

  bool bit() { return *cursor++; }

  uint32_t uint(int width) {
    uint32_t value = 0;
    for (int i = 0; i < width; ++i) {
      if (bit()) {
        value |= (1u << i);
      }
    }
    return value;
  }
};

uint32_t pack_pte_payload(uint32_t pte) {
  return (((pte >> 10) & 0x3FFFFFu) << 8) | (pte & 0xFFu);
}

uint32_t unpack_pte_payload(uint32_t payload) {
  return ((payload >> 8) << 10) | (payload & 0xFFu);
}

void encode_entry(BitWriter &w, const TlbMmu::TlbEntry &entry) {
  w.bit(entry.valid);
  w.bit(entry.global);
  w.uint(entry.asid, TlbMmu::kAsidBits);
  w.bit((entry.level & 1u) != 0);
  w.uint(entry.vpn1, TlbMmu::kVpnPartBits);
  w.uint(entry.vpn0, TlbMmu::kVpnPartBits);
  w.uint(entry.ppn, TlbMmu::kPpnBits);
  w.uint(entry.perm, TlbMmu::kPermBits);
}

TlbMmu::TlbEntry decode_entry(BitReader &r) {
  TlbMmu::TlbEntry entry{};
  entry.valid = r.bit();
  entry.global = r.bit();
  entry.asid = static_cast<uint16_t>(r.uint(TlbMmu::kAsidBits));
  entry.level = r.bit() ? 1 : 0;
  entry.vpn1 = static_cast<uint16_t>(r.uint(TlbMmu::kVpnPartBits));
  entry.vpn0 = static_cast<uint16_t>(r.uint(TlbMmu::kVpnPartBits));
  entry.ppn = r.uint(TlbMmu::kPpnBits);
  entry.perm = static_cast<uint8_t>(r.uint(TlbMmu::kPermBits));
  return entry;
}

void encode_lookup_resp(BitWriter &w, const TlbMmu::LookupOutput &lookup) {
  w.bit(lookup.hit);
  encode_entry(w, lookup.entry);
}

TlbMmu::LookupOutput decode_lookup_resp(BitReader &r) {
  TlbMmu::LookupOutput lookup{};
  lookup.hit = r.bit();
  lookup.entry = decode_entry(r);
  return lookup;
}

void encode_walk_regs(BitWriter &w, const TlbMmu::WalkRegs &walk) {
  w.bit(walk.active);
  w.bit(walk.req_sent);
  w.uint(walk.v_addr, 32);
  w.uint(walk.type, 2);
  w.uint(walk.satp, 32);
  w.uint(static_cast<uint32_t>(walk.eff_priv) & 0x3u, 2);
  w.bit(walk.mxr);
  w.bit(walk.sum);
}

TlbMmu::WalkRegs decode_walk_regs(BitReader &r) {
  TlbMmu::WalkRegs walk{};
  walk.active = r.bit();
  walk.req_sent = r.bit();
  walk.v_addr = r.uint(32);
  walk.type = r.uint(2);
  walk.satp = r.uint(32);
  walk.eff_priv = static_cast<int>(r.uint(2));
  walk.mxr = r.bit();
  walk.sum = r.bit();
  return walk;
}

void encode_refill(BitWriter &w, const TlbMmu::PendingRefill &refill,
                   int entries) {
  w.bit(refill.valid);
  encode_entry(w, refill.entry);
  const int slot_bits = TlbMmu::slot_width(entries);
  w.uint(static_cast<uint32_t>(refill.slot), slot_bits);
  w.uint(static_cast<uint32_t>(refill.next_repl_ptr), slot_bits);
}

TlbMmu::PendingRefill decode_refill(BitReader &r, int entries) {
  TlbMmu::PendingRefill refill{};
  refill.valid = r.bit();
  refill.entry = decode_entry(r);
  const int slot_bits = TlbMmu::slot_width(entries);
  refill.slot = static_cast<int>(r.uint(slot_bits));
  refill.next_repl_ptr = static_cast<int>(r.uint(slot_bits));
  return refill;
}

void encode_walk_port_view(BitWriter &w, bool req_ready, bool resp_valid,
                           const PtwWalkResp &resp) {
  w.bit(req_ready);
  w.bit(resp_valid);
  w.bit(resp.fault);
  w.uint(static_cast<uint32_t>(resp.vaddr) >> 12, TlbMmu::kVpnBits);
  w.uint(pack_pte_payload(resp.leaf_pte), TlbMmu::kPpnBits + TlbMmu::kPermBits);
  w.bit((static_cast<uint32_t>(resp.leaf_level) & 1u) != 0);
}

void decode_walk_port_view(BitReader &r, bool &req_ready, bool &resp_valid,
                           PtwWalkResp &resp) {
  req_ready = r.bit();
  resp_valid = r.bit();
  resp.fault = r.bit();
  resp.vaddr = r.uint(TlbMmu::kVpnBits) << 12;
  resp.leaf_pte = unpack_pte_payload(r.uint(TlbMmu::kPpnBits + TlbMmu::kPermBits));
  resp.leaf_level = r.bit() ? 1 : 0;
}

void encode_walk_req(BitWriter &w, bool valid, const PtwWalkReq &req) {
  w.bit(valid);
  w.uint(req.vaddr, 32);
  w.uint(req.satp, 32);
  w.uint(req.access_type, 2);
}

void decode_walk_req(BitReader &r, bool &valid, PtwWalkReq &req) {
  valid = r.bit();
  req.vaddr = r.uint(32);
  req.satp = r.uint(32);
  req.access_type = r.uint(2);
}

void encode_core_input_bits(bool *pi, const TlbMmu::CoreInput &in,
                            int entries) {
  BitWriter w(pi);
  w.bit(in.translate_valid);
  w.uint(in.v_addr, 32);
  w.uint(in.type, 2);
  w.uint(in.satp, 32);
  w.uint(in.privilege, 2);
  w.bit(in.mstatus_mxr);
  w.bit(in.mstatus_sum);
  w.bit(in.mstatus_mprv);
  w.uint(in.mstatus_mpp, 2);
  encode_lookup_resp(w, in.lookup);
  encode_walk_regs(w, in.walk);
  encode_refill(w, in.refill, entries);
  w.uint(static_cast<uint32_t>(in.repl_ptr), TlbMmu::slot_width(entries));
  encode_walk_port_view(w, in.walk_req_ready, in.walk_resp_valid,
                        in.walk_resp);
}

TlbMmu::CoreInput decode_core_input_bits(const bool *pi, int entries) {
  BitReader r(pi);
  TlbMmu::CoreInput in{};
  in.translate_valid = r.bit();
  in.v_addr = r.uint(32);
  in.type = r.uint(2);
  in.satp = r.uint(32);
  in.privilege = r.uint(2);
  in.mstatus_mxr = r.bit();
  in.mstatus_sum = r.bit();
  in.mstatus_mprv = r.bit();
  in.mstatus_mpp = r.uint(2);
  in.lookup = decode_lookup_resp(r);
  in.walk = decode_walk_regs(r);
  in.refill = decode_refill(r, entries);
  in.repl_ptr = static_cast<int>(r.uint(TlbMmu::slot_width(entries)));
  in.tlb_capacity = entries > 0 ? entries : 1;
  decode_walk_port_view(r, in.walk_req_ready, in.walk_resp_valid,
                        in.walk_resp);
  return in;
}

void encode_core_output_bits(bool *po, const TlbMmu::CoreOutput &out,
                             int entries) {
  BitWriter w(po);
  w.bit(out.resp_valid);
  w.uint(out.result, TlbMmu::kResultBits);
  w.uint(out.p_addr, 32);
  w.uint(out.retry_reason, TlbMmu::kRetryBits);
  encode_walk_regs(w, out.walk_next);
  encode_refill(w, out.refill_next, entries);
  encode_walk_req(w, out.walk_req_valid, out.walk_req);
  w.bit(out.walk_resp_consumed);
  w.uint(out.trace_source, TlbMmu::kTraceSourceBits);
  w.uint(out.trace_level, 8);
  w.uint(out.trace_perm, 8);
}

TlbMmu::CoreOutput decode_core_output_bits(const bool *po, int entries) {
  BitReader r(po);
  TlbMmu::CoreOutput out{};
  out.resp_valid = r.bit();
  out.result = r.uint(TlbMmu::kResultBits);
  out.p_addr = r.uint(32);
  out.retry_reason = r.uint(TlbMmu::kRetryBits);
  out.walk_next = decode_walk_regs(r);
  out.refill_next = decode_refill(r, entries);
  bool walk_req_valid = false;
  decode_walk_req(r, walk_req_valid, out.walk_req);
  out.walk_req_valid = walk_req_valid;
  out.walk_resp_consumed = r.bit();
  out.trace_source = r.uint(TlbMmu::kTraceSourceBits);
  out.trace_level = r.uint(8);
  out.trace_perm = r.uint(8);
  return out;
}

TlbMmu::CoreOutput run_core_lane_via_io_generator(
    const TlbMmu::CoreInput &in, int entries) {
  bool pi[TlbMmu::core_pi_width(TlbMmu::kMaxTlbEntries)] = {};
  bool po[TlbMmu::core_po_width(TlbMmu::kMaxTlbEntries)] = {};
  encode_core_input_bits(pi, in, entries);
  TlbMmu::tlb_core_io_generator(pi, po, entries);
  return decode_core_output_bits(po, entries);
}

void encode_top_input_bits(bool *pi, const TlbMmu::TopInput &in,
                           int entries, int port_count) {
  BitWriter w(pi);
  w.bit(in.flush);
  w.bit(in.cancel);
  w.uint(in.satp, 32);
  w.uint(in.privilege, 2);
  w.bit(in.mstatus_mxr);
  w.bit(in.mstatus_sum);
  w.bit(in.mstatus_mprv);
  w.uint(in.mstatus_mpp, 2);
  encode_walk_regs(w, in.walk);
  encode_refill(w, in.refill, entries);
  w.uint(static_cast<uint32_t>(in.repl_ptr), TlbMmu::slot_width(entries));
  encode_walk_port_view(w, in.walk_req_ready, in.walk_resp_valid,
                        in.walk_resp);
  for (int i = 0; i < port_count; ++i) {
    w.bit(in.port[i].valid);
    w.uint(in.port[i].v_addr, 32);
    encode_lookup_resp(w, in.port[i].lookup);
  }
}

TlbMmu::TopInput decode_top_input_bits(const bool *pi, int entries,
                                       int port_count, bool is_dtlb) {
  BitReader r(pi);
  TlbMmu::TopInput in{};
  in.flush = r.bit();
  in.cancel = r.bit();
  in.satp = r.uint(32);
  in.privilege = r.uint(2);
  in.mstatus_mxr = r.bit();
  in.mstatus_sum = r.bit();
  in.mstatus_mprv = r.bit();
  in.mstatus_mpp = r.uint(2);
  in.walk = decode_walk_regs(r);
  in.refill = decode_refill(r, entries);
  in.repl_ptr = static_cast<int>(r.uint(TlbMmu::slot_width(entries)));
  in.tlb_capacity = entries > 0 ? entries : 1;
  decode_walk_port_view(r, in.walk_req_ready, in.walk_resp_valid,
                        in.walk_resp);
  in.port_count = port_count;
  for (int i = 0; i < port_count; ++i) {
    auto &port = in.port[i];
    port.valid = r.bit();
    port.v_addr = r.uint(32);
    port.type = is_dtlb ? (i < LSU_LDU_COUNT ? 1 : 2) : 0;
    port.lookup = decode_lookup_resp(r);
  }
  return in;
}

void encode_top_port_output(BitWriter &w,
                            const TlbMmu::TopPortOutput &port) {
  w.bit(port.resp_valid);
  w.uint(port.result, TlbMmu::kResultBits);
  w.uint(port.p_addr, 32);
  w.uint(port.retry_reason, TlbMmu::kRetryBits);
  w.uint(port.trace_source, TlbMmu::kTraceSourceBits);
  w.uint(port.trace_level, 8);
  w.uint(port.trace_perm, 8);
}

TlbMmu::TopPortOutput decode_top_port_output(BitReader &r) {
  TlbMmu::TopPortOutput port{};
  port.resp_valid = r.bit();
  port.result = r.uint(TlbMmu::kResultBits);
  port.p_addr = r.uint(32);
  port.retry_reason = r.uint(TlbMmu::kRetryBits);
  port.trace_source = r.uint(TlbMmu::kTraceSourceBits);
  port.trace_level = r.uint(8);
  port.trace_perm = r.uint(8);
  return port;
}

void encode_top_output_bits(bool *po, const TlbMmu::TopOutput &out,
                            int entries, int port_count) {
  BitWriter w(po);
  w.bit(out.table_flush);
  encode_walk_regs(w, out.walk_next);
  encode_refill(w, out.refill_next, entries);
  encode_walk_req(w, out.walk_req_valid, out.walk_req);
  w.bit(out.walk_resp_consumed);
  w.bit(out.walk_client_flush);
  for (int i = 0; i < port_count; ++i) {
    encode_top_port_output(w, out.port[i]);
  }
}

TlbMmu::TopOutput decode_top_output_bits(const bool *po, int entries,
                                         int port_count) {
  BitReader r(po);
  TlbMmu::TopOutput out{};
  out.table_flush = r.bit();
  out.walk_next = decode_walk_regs(r);
  out.refill_next = decode_refill(r, entries);
  bool walk_req_valid = false;
  decode_walk_req(r, walk_req_valid, out.walk_req);
  out.walk_req_valid = walk_req_valid;
  out.walk_resp_consumed = r.bit();
  out.walk_client_flush = r.bit();
  for (int i = 0; i < port_count; ++i) {
    out.port[i] = decode_top_port_output(r);
  }
  return out;
}

MMUResultType to_lsu_mmu_result(TlbMmu::Result result) {
  switch (result) {
  case TlbMmu::Result::OK:
    return MMUResultType::HIT;
  case TlbMmu::Result::FAULT:
    return MMUResultType::PAGE_FAULT;
  case TlbMmu::Result::RETRY:
  default:
    return MMUResultType::MISS;
  }
}
} // namespace

TlbMmu::TlbMmu(SimContext *ctx, PtwMemPort *port, int tlb_entries, Kind kind)
    : ctx(ctx), kind_(kind), walker(port),
      tlb_entries_(tlb_entries > 0 ? tlb_entries : 1),
      tlb_capacity_(tlb_entries > 0 ? tlb_entries : 1), repl_ptr_(0) {
  (void)this->ctx;
  flush_pending_ = true;
  seq();
}

void TlbMmu::set_ptw_mem_port(PtwMemPort *port) { walker.set_mem_port(port); }
void TlbMmu::set_ptw_walk_port(PtwWalkPort *port) { walk_port = port; }

void TlbMmu::cancel_pending_walk() {
  walker.flush();
  run_control_via_top(false, true);
  last_retry_reason_ = RetryReason::NONE;
}

void TlbMmu::flush() {
  const WalkRegs &walk = visible_walk_regs();
  if (walk.active) {
    record_trace(TraceSource::FLUSH, Result::RETRY, walk.v_addr, 0, walk.satp,
                 sv32_asid(walk.satp), walk.type, 0xff, 0);
  }
  walker.flush();
  run_control_via_top(true, false);
  last_retry_reason_ = RetryReason::NONE;
}

void TlbMmu::dump_debug(FILE *out) const {
  if (out == nullptr) {
    return;
  }
  const WalkRegs &walk = visible_walk_regs();
  std::fprintf(out,
               "[PF-MISMATCH][MMU] walk_active=%d walk_req_sent=%d "
               "walk_v=0x%08x walk_type=%u walk_satp=0x%08x retry=%u "
               "flush_pending=%d\n",
               static_cast<int>(walk.active), static_cast<int>(walk.req_sent),
               walk.v_addr, walk.type, walk.satp,
               static_cast<unsigned>(last_retry_reason_),
               static_cast<int>(flush_pending_));
  for (size_t i = 0; i < trace_hist_.size(); i++) {
    const size_t idx = (trace_hist_head_ + i) % trace_hist_.size();
    const auto &e = trace_hist_[idx];
    if (!e.valid) {
      continue;
    }
    const char *src = "none";
    switch (e.source) {
    case TraceSource::IDENTITY:
      src = "identity";
      break;
    case TraceSource::TLB_HIT:
      src = "tlb_hit";
      break;
    case TraceSource::TLB_PERM_FAULT:
      src = "tlb_perm_fault";
      break;
    case TraceSource::WALK_SHARED:
      src = "walk_shared";
      break;
    case TraceSource::WALK_LOCAL:
      src = "walk_local";
      break;
    case TraceSource::FLUSH:
      src = "flush";
      break;
    default:
      break;
    }
    std::fprintf(
        out,
        "[PF-MISMATCH][MMU][TRACE] cyc=%llu src=%s ret=%s retry=%u "
        "v=0x%08x p=0x%08x satp=0x%08x asid=0x%03x type=%u level=%u "
        "perm=0x%02x walk_active=%d walk_req_sent=%d\n",
        static_cast<unsigned long long>(e.cycle), src,
        mmu_result_name(e.result), static_cast<unsigned>(e.retry), e.v_addr,
        e.p_addr, e.satp, e.asid, static_cast<unsigned>(e.type),
        static_cast<unsigned>(e.level), static_cast<unsigned>(e.perm),
        static_cast<int>(e.walk_active), static_cast<int>(e.walk_req_sent));
  }
}

void TlbMmu::seq() {
  if (flush_pending_) {
    clear_tlb_entries();
    repl_ptr_ = 0;
    walk_regs_ = {};
    walk_comb_ = {};
    walk_comb_valid_ = false;
    refill_comb_ = {};
    flush_pending_ = false;
    return;
  }

  if (refill_comb_.valid) {
    tlb_entries_[refill_comb_.slot] = refill_comb_.entry;
    repl_ptr_ = refill_comb_.next_repl_ptr;
    refill_comb_ = {};
  }

  if (walk_comb_valid_) {
    walk_regs_ = walk_comb_;
    walk_comb_valid_ = false;
  }
}

TlbMmu::DebugState TlbMmu::debug_state() const {
  DebugState d{};
  const WalkRegs &walk = visible_walk_regs();
  d.walk_active = walk.active;
  d.walk_req_sent = walk.req_sent;
  d.walk_v_addr = walk.v_addr;
  d.walk_type = walk.type;
  d.walk_satp = walk.satp;
  d.walk_eff_priv = walk.eff_priv;
  d.walk_mxr = walk.mxr;
  d.walk_sum = walk.sum;
  d.last_retry_reason = last_retry_reason_;
  return d;
}

const TlbMmu::WalkRegs &TlbMmu::visible_walk_regs() const {
  return walk_comb_valid_ ? walk_comb_ : walk_regs_;
}

TlbMmu::WalkRegs &TlbMmu::ensure_walk_comb() {
  if (!walk_comb_valid_) {
    walk_comb_ = walk_regs_;
    walk_comb_valid_ = true;
  }
  return walk_comb_;
}

bool TlbMmu::trace_watch_vaddr(uint32_t v_addr) {
  return (v_addr & ~0xFFFu) == 0xA0215000u;
}

void TlbMmu::record_trace(TraceSource source, Result result, uint32_t v_addr,
                          uint32_t p_addr, uint32_t satp, uint16_t asid,
                          uint32_t type, uint8_t level, uint8_t perm) {
  if (!trace_watch_vaddr(v_addr)) {
    return;
  }
  auto &slot = trace_hist_[trace_hist_head_];
  trace_hist_head_ = (trace_hist_head_ + 1) % trace_hist_.size();
  slot = {};
  slot.valid = true;
  slot.cycle = (sim_time >= 0) ? static_cast<uint64_t>(sim_time) : 0;
  slot.source = source;
  slot.result = result;
  slot.retry = last_retry_reason_;
  slot.v_addr = v_addr;
  slot.p_addr = p_addr;
  slot.satp = satp;
  slot.asid = asid;
  slot.type = static_cast<uint8_t>(type);
  slot.level = level;
  slot.perm = perm;
  const WalkRegs &walk = visible_walk_regs();
  slot.walk_active = walk.active;
  slot.walk_req_sent = walk.req_sent;
}

void TlbMmu::clear_tlb_entries() {
  for (auto &entry : tlb_entries_) {
    entry = {};
    entry.valid = false;
  }
}

TlbMmu::TranslateContext TlbMmu::build_translate_context(uint32_t type,
                                                         CsrStatusIO *status) {
  if (status == nullptr) {
    return {};
  }
  return build_translate_context(
      type, static_cast<uint32_t>(status->satp),
      static_cast<uint32_t>(status->privilege),
      (static_cast<uint32_t>(status->mstatus) & MSTATUS_MXR) != 0,
      (static_cast<uint32_t>(status->mstatus) & MSTATUS_SUM) != 0,
      (static_cast<uint32_t>(status->mstatus) & MSTATUS_MPRV) != 0,
      (static_cast<uint32_t>(status->mstatus) >> MSTATUS_MPP_SHIFT) & 0x3u);
}

TlbMmu::TranslateContext
TlbMmu::build_translate_context(uint32_t type, uint32_t satp,
                                uint32_t privilege, bool mstatus_mxr,
                                bool mstatus_sum, bool mstatus_mprv,
                                uint32_t mstatus_mpp) {
  TranslateContext ctx{};
  ctx.satp = satp;
  ctx.eff_priv = static_cast<int>(privilege & 0x3u);
  ctx.mxr = mstatus_mxr;
  ctx.sum = mstatus_sum;
  if (type != 0 && mstatus_mprv) {
    ctx.eff_priv = static_cast<int>(mstatus_mpp & 0x3u);
  }
  return ctx;
}

bool TlbMmu::entry_matches(const TlbEntry &entry, uint32_t v_addr,
                           uint16_t asid) {
  if (!entry.valid) {
    return false;
  }
  if (!(entry.global || entry.asid == asid)) {
    return false;
  }
  const uint16_t vpn1 = (v_addr >> 22) & 0x3FF;
  const uint16_t vpn0 = (v_addr >> 12) & 0x3FF;
  if (entry.vpn1 != vpn1) {
    return false;
  }
  return entry.level == 1 || entry.vpn0 == vpn0;
}

TlbMmu::LookupOutput TlbMmu::lookup_table_comb(const LookupInput &in,
                                               const TlbEntry *entries,
                                               int entry_count) {
  LookupOutput out{};
  if (!in.valid || entries == nullptr || entry_count <= 0) {
    return out;
  }
  for (int i = 0; i < entry_count; ++i) {
    const TlbEntry &entry = entries[i];
    if (entry_matches(entry, in.v_addr, in.asid)) {
      out.hit = true;
      out.entry = entry;
      return out;
    }
  }
  return out;
}

bool TlbMmu::lookup(uint32_t v_addr, uint16_t asid, TlbEntry &hit) const {
  if (refill_comb_.valid && entry_matches(refill_comb_.entry, v_addr, asid)) {
    hit = refill_comb_.entry;
    return true;
  }
  const LookupOutput table_lookup = lookup_table_access(v_addr, asid);
  if (table_lookup.hit) {
    hit = table_lookup.entry;
    return true;
  }
  return false;
}

TlbMmu::LookupOutput
TlbMmu::lookup_table_access(uint32_t v_addr, uint16_t asid) const {
  LookupInput req{};
  req.valid = true;
  req.v_addr = v_addr;
  req.asid = asid;
  const int entry_count = kind_ == Kind::ITLB ? ITLB_ENTRIES : DTLB_ENTRIES;
  return lookup_table_comb(req, tlb_entries_.data(), entry_count);
}

uint32_t TlbMmu::compose_paddr(uint32_t v_addr, const TlbEntry &e) {
  if (e.level == 1) {
    const uint32_t mask = (1u << 22) - 1u;
    return ((e.ppn << 12) & ~mask) | (v_addr & mask);
  }
  return (e.ppn << 12) | (v_addr & 0xFFFu);
}

bool TlbMmu::check_perm(uint8_t perm, uint32_t type, int eff_priv, bool sum,
                        bool mxr) {
  bool r = perm & PTE_R;
  bool w = perm & PTE_W;
  bool x = perm & PTE_X;
  bool u = perm & PTE_U;
  bool a = perm & PTE_A;
  bool d = perm & PTE_D;

  if (type == 0 && !x) {
    return false;
  }
  if (type == 1 && !r && !(mxr && x)) {
    return false;
  }
  if (type == 2 && !w) {
    return false;
  }
  if (eff_priv == 0 && !u) {
    return false;
  }
  if (eff_priv == 1 && u) {
    if (type == 0) {
      return false;
    }
    if (!sum) {
      return false;
    }
  }
  if (!a) {
    return false;
  }
  if (type == 2 && !d) {
    return false;
  }
  return true;
}

TlbMmu::CoreOutput TlbMmu::core_comb(const CoreInput &in) {
  CoreOutput out{};
  out.walk_next = in.walk;
  out.refill_next = in.refill;
  out.result = static_cast<uint32_t>(Result::OK);
  out.retry_reason = static_cast<uint32_t>(RetryReason::NONE);
  out.trace_source = static_cast<uint32_t>(TraceSource::NONE);
  out.trace_level = 0xff;

  if (!in.translate_valid) {
    return out;
  }

  out.resp_valid = true;
  const TranslateContext ctx_view = build_translate_context(
      in.type, in.satp, in.privilege, in.mstatus_mxr, in.mstatus_sum,
      in.mstatus_mprv, in.mstatus_mpp);
  const uint32_t satp = ctx_view.satp;
  const int eff_priv = ctx_view.eff_priv;
  const uint32_t type = in.type;

  if (eff_priv == 3 || ((satp & 0x80000000u) == 0)) {
    out.p_addr = in.v_addr;
    out.trace_source = static_cast<uint32_t>(TraceSource::IDENTITY);
    return out;
  }

  const uint16_t asid = sv32_asid(satp);
  TlbEntry hit{};
  bool hit_valid = false;
  if (in.refill.valid && entry_matches(in.refill.entry, in.v_addr, asid)) {
    hit = in.refill.entry;
    hit_valid = true;
  } else if (in.lookup.hit) {
    hit = in.lookup.entry;
    hit_valid = true;
  }

  if (hit_valid) {
    const uint8_t perm = static_cast<uint8_t>(hit.perm);
    if (!check_perm(perm, type, eff_priv, ctx_view.sum, ctx_view.mxr)) {
      out.result = static_cast<uint32_t>(Result::FAULT);
      out.trace_source = static_cast<uint32_t>(TraceSource::TLB_PERM_FAULT);
      out.trace_level = hit.level;
      out.trace_perm = perm;
      return out;
    }
    out.p_addr = compose_paddr(in.v_addr, hit);
    out.trace_source = static_cast<uint32_t>(TraceSource::TLB_HIT);
    out.trace_level = hit.level;
    out.trace_perm = perm;
    return out;
  }

  WalkRegs walk = in.walk;
  if (!walk.active) {
    walk = {};
    walk.active = true;
    walk.req_sent = false;
    walk.v_addr = in.v_addr;
    walk.type = type;
    walk.satp = satp;
    walk.eff_priv = ctx_view.eff_priv;
    walk.mxr = ctx_view.mxr;
    walk.sum = ctx_view.sum;
  } else {
    const uint32_t walk_page = static_cast<uint32_t>(walk.v_addr) >> 12;
    const uint32_t req_page = static_cast<uint32_t>(in.v_addr) >> 12;
    if (walk_page != req_page || static_cast<uint32_t>(walk.satp) != satp) {
      out.result = static_cast<uint32_t>(Result::RETRY);
      out.retry_reason = static_cast<uint32_t>(RetryReason::OTHER_WALK_ACTIVE);
      out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
      out.walk_next = walk;
      return out;
    }
  }

  if (!walk.req_sent) {
    if (!in.walk_req_ready) {
      out.result = static_cast<uint32_t>(Result::RETRY);
      out.retry_reason = static_cast<uint32_t>(RetryReason::WALK_REQ_BLOCKED);
      out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
      out.walk_next = walk;
      return out;
    }
    PtwWalkReq req{};
    req.vaddr = walk.v_addr;
    req.satp = walk.satp;
    req.access_type = walk.type;
    out.walk_req_valid = true;
    out.walk_req = req;
    walk.req_sent = true;
    out.result = static_cast<uint32_t>(Result::RETRY);
    out.retry_reason = static_cast<uint32_t>(RetryReason::WAIT_WALK_RESP);
    out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
    out.walk_next = walk;
    return out;
  }

  if (!in.walk_resp_valid) {
    out.result = static_cast<uint32_t>(Result::RETRY);
    out.retry_reason = static_cast<uint32_t>(RetryReason::WAIT_WALK_RESP);
    out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
    out.walk_next = walk;
    return out;
  }

  const PtwWalkResp wr = in.walk_resp;
  if ((static_cast<uint32_t>(wr.vaddr) >> 12) !=
      (static_cast<uint32_t>(walk.v_addr) >> 12)) {
    out.walk_resp_consumed = true;
    out.result = static_cast<uint32_t>(Result::RETRY);
    out.retry_reason = static_cast<uint32_t>(RetryReason::WAIT_WALK_RESP);
    out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
    out.walk_next = walk;
    return out;
  }

  out.walk_resp_consumed = true;
  out.walk_next = {};
  out.trace_source = static_cast<uint32_t>(TraceSource::WALK_SHARED);
  out.trace_level = wr.leaf_level;
  out.trace_perm = static_cast<uint8_t>(wr.leaf_pte & 0xFFu);

  if (wr.fault) {
    out.result = static_cast<uint32_t>(Result::FAULT);
    return out;
  }

  const uint8_t perm = static_cast<uint8_t>(wr.leaf_pte & 0xFFu);
  if (!check_perm(perm, type, eff_priv, ctx_view.sum, ctx_view.mxr)) {
    out.result = static_cast<uint32_t>(Result::FAULT);
    return out;
  }

  const int capacity = in.tlb_capacity > 0 ? in.tlb_capacity : 1;
  const int refill_slot = in.repl_ptr % capacity;
  PendingRefill refill{};
  refill.valid = true;
  refill.slot = refill_slot;
  refill.next_repl_ptr = (refill_slot + 1) % capacity;
  refill.entry = {};
  refill.entry.valid = true;
  refill.entry.global = (wr.leaf_pte & PTE_G) != 0;
  refill.entry.asid = asid;
  refill.entry.level = static_cast<uint32_t>(wr.leaf_level) & 1u;
  refill.entry.vpn1 = (static_cast<uint32_t>(in.v_addr) >> 22) & 0x3FFu;
  refill.entry.vpn0 = (static_cast<uint32_t>(in.v_addr) >> 12) & 0x3FFu;
  refill.entry.ppn = (static_cast<uint32_t>(wr.leaf_pte) >> 10) & 0x3FFFFFu;
  refill.entry.perm = perm;
  out.refill_next = refill;
  out.p_addr = compose_paddr(in.v_addr, refill.entry);
  return out;
}

TlbMmu::TopOutput TlbMmu::top_comb(const TopInput &in, int entries) {
  TopOutput out{};
  out.walk_next = in.walk;
  out.refill_next = in.refill;

  if (in.flush) {
    out.table_flush = true;
    out.walk_next = {};
    out.refill_next = {};
    out.walk_client_flush = true;
    return out;
  }
  if (in.cancel) {
    out.walk_next = {};
    out.walk_client_flush = true;
    return out;
  }

  WalkRegs shadow_walk = in.walk;
  PendingRefill shadow_refill = in.refill;
  bool shadow_walk_req_ready = in.walk_req_ready;
  bool shadow_walk_resp_valid = in.walk_resp_valid;
  PtwWalkResp shadow_walk_resp = in.walk_resp;
  const int port_count =
      in.port_count < kMaxTopPortCount ? in.port_count : kMaxTopPortCount;

  for (int i = 0; i < port_count; ++i) {
    const TopPortInput &port = in.port[i];
    CoreInput core_in{};
    core_in.translate_valid = port.valid;
    core_in.v_addr = port.v_addr;
    core_in.type = port.type;
    core_in.satp = in.satp;
    core_in.privilege = in.privilege;
    core_in.mstatus_mxr = in.mstatus_mxr;
    core_in.mstatus_sum = in.mstatus_sum;
    core_in.mstatus_mprv = in.mstatus_mprv;
    core_in.mstatus_mpp = in.mstatus_mpp;
    core_in.lookup = port.lookup;
    core_in.walk = shadow_walk;
    core_in.refill = shadow_refill;
    core_in.repl_ptr = in.repl_ptr;
    core_in.tlb_capacity = in.tlb_capacity;
    core_in.walk_req_ready = shadow_walk_req_ready;
    core_in.walk_resp_valid = shadow_walk_resp_valid;
    core_in.walk_resp = shadow_walk_resp;

    const CoreOutput core_out = run_core_lane_via_io_generator(core_in, entries);
    TopPortOutput &port_out = out.port[i];
    port_out.resp_valid = core_out.resp_valid;
    port_out.result = core_out.result;
    port_out.p_addr = core_out.p_addr;
    port_out.retry_reason = core_out.retry_reason;
    port_out.trace_source = core_out.trace_source;
    port_out.trace_level = core_out.trace_level;
    port_out.trace_perm = core_out.trace_perm;

    shadow_walk = core_out.walk_next;
    shadow_refill = core_out.refill_next;
    if (core_out.walk_req_valid) {
      out.walk_req_valid = true;
      out.walk_req = core_out.walk_req;
      shadow_walk_req_ready = false;
      shadow_walk_resp_valid = false;
    }
    if (core_out.walk_resp_consumed) {
      out.walk_resp_consumed = true;
      shadow_walk_resp_valid = false;
    }
  }

  out.walk_next = shadow_walk;
  out.refill_next = shadow_refill;
  return out;
}

void TlbMmu::tlb_core_io_generator(const bool *pi, bool *po, int entries) {
  tlb_kernel_core_io_generator(pi, po, entries);
}

void TlbMmu::itlb_top_io_generator(const bool *pi, bool *po) {
  tlb_kernel_top_io_generator(pi, po, ITLB_ENTRIES, kItlbTopPortCount,
                              LSU_LDU_COUNT, false);
}

void TlbMmu::dtlb_top_io_generator(const bool *pi, bool *po) {
  tlb_kernel_top_io_generator(pi, po, DTLB_ENTRIES, kDtlbTopPortCount,
                              LSU_LDU_COUNT, true);
}

TlbMmu::Result TlbMmu::translate_shared_via_io_generator(
    uint32_t &p_addr, uint32_t v_addr, uint32_t type, CsrStatusIO *status) {
  TopInput in = build_top_common_input(status);
  int port_idx = 0;
  if (kind_ == Kind::DTLB && type == 2) {
    port_idx = LSU_LDU_COUNT;
  }
  in.port[port_idx].valid = true;
  in.port[port_idx].v_addr = v_addr;
  in.port[port_idx].type = type;
  in.port[port_idx].lookup = lookup_table_access(v_addr, sv32_asid(in.satp));

  const TopOutput out = run_top_via_io_generator(in);
  apply_top_output(out);

  const TopPortOutput &port_out = out.port[port_idx];
  last_retry_reason_ = static_cast<RetryReason>(
      static_cast<uint32_t>(port_out.retry_reason) & 0x7u);

  p_addr = port_out.p_addr;
  const Result result =
      static_cast<Result>(static_cast<uint32_t>(port_out.result) & 0x3u);
  const TraceSource trace_source = static_cast<TraceSource>(
      static_cast<uint32_t>(port_out.trace_source) & 0x7u);
  if (trace_source != TraceSource::NONE) {
    record_trace(trace_source, result, v_addr,
                 result == Result::OK ? p_addr : 0, in.satp,
                 sv32_asid(in.satp), type, port_out.trace_level,
                 port_out.trace_perm);
  }
  return result;
}

TlbMmu::TopInput
TlbMmu::build_top_common_input(const CsrStatusIO *status) const {
  const uint32_t satp =
      status != nullptr ? static_cast<uint32_t>(status->satp) : 0u;
  const uint32_t mstatus =
      status != nullptr ? static_cast<uint32_t>(status->mstatus) : 0u;
  const uint32_t privilege =
      status != nullptr ? static_cast<uint32_t>(status->privilege) : 0u;
  const PtwWalkPortCombOut walk_port_out =
      walk_port != nullptr ? walk_port->comb_output() : PtwWalkPortCombOut{};

  TopInput in{};
  in.satp = satp;
  in.privilege = privilege;
  in.mstatus_mxr = (mstatus & MSTATUS_MXR) != 0;
  in.mstatus_sum = (mstatus & MSTATUS_SUM) != 0;
  in.mstatus_mprv = (mstatus & MSTATUS_MPRV) != 0;
  in.mstatus_mpp = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3u;
  in.walk = visible_walk_regs();
  in.refill = refill_comb_;
  in.repl_ptr = repl_ptr_;
  in.tlb_capacity = tlb_capacity_ > 0 ? tlb_capacity_ : 1;
  in.walk_req_ready = walk_port_out.req_ready;
  in.walk_resp_valid = walk_port_out.resp_valid;
  in.walk_resp = walk_port_out.resp;
  in.port_count = kind_ == Kind::ITLB ? kItlbTopPortCount : kDtlbTopPortCount;
  return in;
}

TlbMmu::TopOutput TlbMmu::run_top_via_io_generator(const TopInput &in) const {
  if (kind_ == Kind::ITLB) {
    bool pi[ITLB_TOP_PI_WIDTH] = {};
    bool po[ITLB_TOP_PO_WIDTH] = {};
    encode_top_input_bits(pi, in, ITLB_ENTRIES, kItlbTopPortCount);
    itlb_top_io_generator(pi, po);
    return decode_top_output_bits(po, ITLB_ENTRIES, kItlbTopPortCount);
  }

  bool pi[DTLB_TOP_PI_WIDTH] = {};
  bool po[DTLB_TOP_PO_WIDTH] = {};
  encode_top_input_bits(pi, in, DTLB_ENTRIES, kDtlbTopPortCount);
  dtlb_top_io_generator(pi, po);
  return decode_top_output_bits(po, DTLB_ENTRIES, kDtlbTopPortCount);
}

void TlbMmu::apply_top_output(const TopOutput &out) {
  if (out.walk_client_flush && walk_port != nullptr) {
    walk_port->flush_client();
  }
  if (out.walk_req_valid && walk_port != nullptr) {
    const bool accepted = walk_port->send_walk_req(out.walk_req);
    Assert(accepted &&
           "TLB top emitted a PTW walk request only when req_ready was set");
  }
  if (out.walk_resp_consumed && walk_port != nullptr) {
    walk_port->consume_resp();
  }

  if (out.table_flush) {
    flush_pending_ = true;
  }
  walk_comb_ = out.walk_next;
  walk_comb_valid_ = true;
  refill_comb_ = out.refill_next;
}

void TlbMmu::run_control_via_top(bool flush, bool cancel) {
  TopInput in = build_top_common_input(nullptr);
  in.flush = flush;
  in.cancel = cancel;
  const TopOutput out = run_top_via_io_generator(in);
  apply_top_output(out);
}

void TlbMmu::translate_lsu_ports(const LsuMMUIO &req, MMULsuIO &resp) {
  resp = {};
  TopInput in = build_top_common_input(&req.csr_status);
  const uint16_t asid = sv32_asid(in.satp);

  for (int i = 0; i < LSU_LDU_COUNT; ++i) {
    const MMUReq &ld_req = req.ldq_req[i];
    auto &port = in.port[i];
    port.valid = ld_req.valid;
    port.v_addr = ld_req.vaddr;
    port.type = 1;
    if (ld_req.valid) {
      port.lookup = lookup_table_access(ld_req.vaddr, asid);
    }
  }
  for (int i = 0; i < LSU_STA_COUNT; ++i) {
    const int port_idx = LSU_LDU_COUNT + i;
    const MMUReq &st_req = req.stq_req[i];
    auto &port = in.port[port_idx];
    port.valid = st_req.valid;
    port.v_addr = st_req.vaddr;
    port.type = 2;
    if (st_req.valid) {
      port.lookup = lookup_table_access(st_req.vaddr, asid);
    }
  }

  const TopOutput out = run_top_via_io_generator(in);
  apply_top_output(out);

  int last_valid_port = -1;
  for (int i = 0; i < LSU_LDU_COUNT; ++i) {
    if (!req.ldq_req[i].valid) {
      continue;
    }
    const TopPortOutput &port = out.port[i];
    MMUResp &ld_resp = resp.ldq_resp[i];
    const Result result =
        static_cast<Result>(static_cast<uint32_t>(port.result) & 0x3u);
    ld_resp.valid = port.resp_valid;
    ld_resp.result = to_lsu_mmu_result(result);
    ld_resp.paddr = result == Result::OK ? port.p_addr : 0;
    last_valid_port = i;
    const TraceSource trace_source = static_cast<TraceSource>(
        static_cast<uint32_t>(port.trace_source) & 0x7u);
    if (trace_source != TraceSource::NONE) {
      record_trace(trace_source, result, req.ldq_req[i].vaddr,
                   result == Result::OK ? port.p_addr : 0, in.satp, asid, 1,
                   port.trace_level, port.trace_perm);
    }
  }
  for (int i = 0; i < LSU_STA_COUNT; ++i) {
    if (!req.stq_req[i].valid) {
      continue;
    }
    const int port_idx = LSU_LDU_COUNT + i;
    const TopPortOutput &port = out.port[port_idx];
    MMUResp &st_resp = resp.stq_resp[i];
    const Result result =
        static_cast<Result>(static_cast<uint32_t>(port.result) & 0x3u);
    st_resp.valid = port.resp_valid;
    st_resp.result = to_lsu_mmu_result(result);
    st_resp.paddr = result == Result::OK ? port.p_addr : 0;
    last_valid_port = port_idx;
    const TraceSource trace_source = static_cast<TraceSource>(
        static_cast<uint32_t>(port.trace_source) & 0x7u);
    if (trace_source != TraceSource::NONE) {
      record_trace(trace_source, result, req.stq_req[i].vaddr,
                   result == Result::OK ? port.p_addr : 0, in.satp, asid, 2,
                   port.trace_level, port.trace_perm);
    }
  }
  if (last_valid_port >= 0) {
    last_retry_reason_ = static_cast<RetryReason>(
        static_cast<uint32_t>(out.port[last_valid_port].retry_reason) & 0x7u);
  }
}

void TlbMmu::schedule_refill(uint32_t v_addr, uint16_t asid, uint8_t level,
                             uint32_t pte, bool global) {
  PendingRefill refill{};
  refill.valid = true;
  refill.slot = repl_ptr_;
  refill.next_repl_ptr = (repl_ptr_ + 1) % tlb_capacity_;
  refill.entry = {};
  refill.entry.valid = true;
  refill.entry.global = global;
  refill.entry.asid = asid;
  refill.entry.level = level;
  refill.entry.vpn1 = (v_addr >> 22) & 0x3FF;
  refill.entry.vpn0 = (v_addr >> 12) & 0x3FF;
  refill.entry.ppn = (pte >> 10) & 0x3FFFFF;
  refill.entry.perm = pte & 0xFF;
  refill_comb_ = refill;
}

TlbMmu::Result TlbMmu::walk_and_refill(uint32_t &p_addr, uint32_t v_addr,
                                       uint32_t type, CsrStatusIO *status) {
  const TranslateContext ctx_view = build_translate_context(type, status);
  WalkRegs &walk = ensure_walk_comb();

  if (!walk.active) {
    if (!walker.start(v_addr, ctx_view.satp)) {
      last_retry_reason_ = RetryReason::LOCAL_WALKER_BUSY;
      return Result::RETRY;
    }
    walk = {};
    walk.active = true;
    walk.v_addr = v_addr;
    walk.type = type;
    walk.satp = ctx_view.satp;
    walk.eff_priv = ctx_view.eff_priv;
    walk.mxr = ctx_view.mxr;
    walk.sum = ctx_view.sum;
  } else {
    if (walk.v_addr != v_addr || walk.type != type || walk.satp != ctx_view.satp) {
      last_retry_reason_ = RetryReason::LOCAL_WALKER_BUSY;
      return Result::RETRY;
    }
  }

  auto st = walker.tick();
  if (st != PtwWalker::State::DONE) {
    if (st == PtwWalker::State::FAULT) {
      walker.flush();
      walk = {};
      last_retry_reason_ = RetryReason::NONE;
      record_trace(TraceSource::WALK_LOCAL, Result::FAULT, v_addr, 0,
                   ctx_view.satp, sv32_asid(ctx_view.satp), type, 0xff, 0);
      return Result::FAULT;
    }
    last_retry_reason_ = RetryReason::WAIT_WALK_RESP;
    record_trace(TraceSource::WALK_LOCAL, Result::RETRY, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, 0xff, 0);
    return Result::RETRY;
  }

  uint32_t leaf_pte = walker.leaf_pte();
  uint8_t leaf_level = walker.leaf_level();
  uint8_t perm = leaf_pte & 0xFF;
  if (!check_perm(perm, walk.type, walk.eff_priv, walk.sum, walk.mxr)) {
    walker.flush();
    walk = {};
    last_retry_reason_ = RetryReason::NONE;
    record_trace(TraceSource::WALK_LOCAL, Result::FAULT, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, leaf_level,
                 perm);
    return Result::FAULT;
  }

  const bool global = (leaf_pte & PTE_G) != 0;
  const uint16_t asid = sv32_asid(walk.satp);
  schedule_refill(walk.v_addr, asid, leaf_level, leaf_pte, global);
  TlbEntry refill_hit{};
  const bool hit = lookup(walk.v_addr, asid, refill_hit);
  Assert(hit && "Local PTW refill must be visible through same-cycle bypass");
  p_addr = compose_paddr(walk.v_addr, refill_hit);
  walker.flush();
  walk = {};
  last_retry_reason_ = RetryReason::NONE;
  record_trace(TraceSource::WALK_LOCAL, Result::OK, v_addr, p_addr,
               ctx_view.satp, asid, type, leaf_level, perm);
  return Result::OK;
}

TlbMmu::Result TlbMmu::walk_and_refill_shared(uint32_t &p_addr, uint32_t v_addr,
                                              uint32_t type,
                                              CsrStatusIO *status) {
  const TranslateContext ctx_view = build_translate_context(type, status);
  WalkRegs &walk = ensure_walk_comb();

  if (!walk.active) {
    walk = {};
    walk.active = true;
    walk.req_sent = false;
    walk.v_addr = v_addr;
    walk.type = type;
    walk.satp = ctx_view.satp;
    walk.eff_priv = ctx_view.eff_priv;
    walk.mxr = ctx_view.mxr;
    walk.sum = ctx_view.sum;
  } else {
    const uint32_t walk_page = walk.v_addr >> 12;
    const uint32_t req_page = v_addr >> 12;
    if (walk_page != req_page || walk.satp != ctx_view.satp) {
      last_retry_reason_ = RetryReason::OTHER_WALK_ACTIVE;
      return Result::RETRY;
    }
  }

  if (!walk.req_sent) {
    PtwWalkReq req{};
    req.vaddr = walk.v_addr;
    req.satp = walk.satp;
    req.access_type = walk.type;
    if (!walk_port->send_walk_req(req)) {
      last_retry_reason_ = RetryReason::WALK_REQ_BLOCKED;
      record_trace(TraceSource::WALK_SHARED, Result::RETRY, v_addr, 0,
                   ctx_view.satp, sv32_asid(ctx_view.satp), type, 0xff, 0);
      return Result::RETRY;
    }
    walk.req_sent = true;
  }

  if (!walk_port->resp_valid()) {
    last_retry_reason_ = RetryReason::WAIT_WALK_RESP;
    record_trace(TraceSource::WALK_SHARED, Result::RETRY, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, 0xff, 0);
    return Result::RETRY;
  }

  PtwWalkResp wr = walk_port->resp();
  if ((static_cast<uint32_t>(wr.vaddr) >> 12) != (walk.v_addr >> 12)) {
    walk_port->consume_resp();
    last_retry_reason_ = RetryReason::WAIT_WALK_RESP;
    record_trace(TraceSource::WALK_SHARED, Result::RETRY, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, 0xff, 0);
    return Result::RETRY;
  }
  walk_port->consume_resp();
  walk = {};

  if (wr.fault) {
    last_retry_reason_ = RetryReason::NONE;
    record_trace(TraceSource::WALK_SHARED, Result::FAULT, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, wr.leaf_level,
                 static_cast<uint8_t>(wr.leaf_pte & 0xFF));
    return Result::FAULT;
  }

  uint8_t perm = wr.leaf_pte & 0xFF;
  if (!check_perm(perm, type, ctx_view.eff_priv, ctx_view.sum, ctx_view.mxr)) {
    last_retry_reason_ = RetryReason::NONE;
    record_trace(TraceSource::WALK_SHARED, Result::FAULT, v_addr, 0,
                 ctx_view.satp, sv32_asid(ctx_view.satp), type, wr.leaf_level,
                 perm);
    return Result::FAULT;
  }

  const bool global = (wr.leaf_pte & PTE_G) != 0;
  const uint16_t asid = sv32_asid(ctx_view.satp);
  schedule_refill(v_addr, asid, wr.leaf_level, wr.leaf_pte, global);
  TlbEntry refill_hit{};
  const bool hit = lookup(v_addr, asid, refill_hit);
  Assert(hit && "Shared PTW refill must be visible through same-cycle bypass");
  p_addr = compose_paddr(v_addr, refill_hit);
  last_retry_reason_ = RetryReason::NONE;
  record_trace(TraceSource::WALK_SHARED, Result::OK, v_addr, p_addr,
               ctx_view.satp, asid, type, wr.leaf_level, perm);
  return Result::OK;
}

TlbMmu::Result TlbMmu::translate(uint32_t &p_addr, uint32_t v_addr,
                                 uint32_t type, CsrStatusIO *status) {
  last_retry_reason_ = RetryReason::NONE;
  if (walk_port != nullptr) {
    const Result ret =
        translate_shared_via_io_generator(p_addr, v_addr, type, status);
    if (itlb_focus_vaddr(v_addr)) {
      const TranslateContext ctx_view = build_translate_context(type, status);
      const WalkRegs &walk = visible_walk_regs();
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=shared_core v=0x%08x p=0x%08x "
          "satp=0x%08x asid=0x%03x priv=%u type=%u walk_active=%d "
          "walk_req_sent=%d ret=%s\n",
          (long long)sim_time, v_addr, p_addr,
          static_cast<uint32_t>(ctx_view.satp),
          sv32_asid(static_cast<uint32_t>(ctx_view.satp)),
          static_cast<unsigned>(ctx_view.eff_priv), type,
          static_cast<int>(walk.active), static_cast<int>(walk.req_sent),
          mmu_result_name(ret));
    }
    return ret;
  }

  const TranslateContext ctx_view = build_translate_context(type, status);
  const uint32_t satp = ctx_view.satp;
  const int eff_priv = ctx_view.eff_priv;

  if (eff_priv == 3 || ((satp & 0x80000000u) == 0)) {
    p_addr = v_addr;
    record_trace(TraceSource::IDENTITY, Result::OK, v_addr, p_addr, satp, 0,
                 type, 0xff, 0);
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=identity v=0x%08x p=0x%08x satp=0x%08x "
          "priv=%d type=%u ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, eff_priv, type,
          mmu_result_name(Result::OK));
    }
    return Result::OK;
  }

  const uint16_t asid = sv32_asid(satp);

  TlbEntry hit = {};
  if (lookup(v_addr, asid, hit)) {
    const bool perm_ok =
        check_perm(hit.perm, type, eff_priv, ctx_view.sum, ctx_view.mxr);
    if (!perm_ok) {
      record_trace(TraceSource::TLB_PERM_FAULT, Result::FAULT, v_addr, 0, satp,
                   asid, type, hit.level, hit.perm);
      if (itlb_focus_vaddr(v_addr)) {
        std::printf(
            "[ITLB][TRACE] cyc=%lld src=tlb_hit_perm_fault v=0x%08x "
            "satp=0x%08x asid=0x%03x priv=%d type=%u perm=0x%02x ret=%s\n",
            (long long)sim_time, v_addr, satp, asid, eff_priv, type, hit.perm,
            mmu_result_name(Result::FAULT));
      }
      return Result::FAULT;
    }
    p_addr = compose_paddr(v_addr, hit);
    record_trace(TraceSource::TLB_HIT, Result::OK, v_addr, p_addr, satp, asid,
                 type, hit.level, hit.perm);
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=tlb_hit v=0x%08x p=0x%08x satp=0x%08x "
          "asid=0x%03x priv=%d type=%u level=%u perm=0x%02x ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
          static_cast<unsigned>(hit.level), hit.perm,
          mmu_result_name(Result::OK));
    }
    return Result::OK;
  }
  if (walk_port != nullptr) {
    const Result ret = walk_and_refill_shared(p_addr, v_addr, type, status);
    const WalkRegs &walk = visible_walk_regs();
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=walk_shared v=0x%08x p=0x%08x "
          "satp=0x%08x asid=0x%03x priv=%d type=%u walk_active=%d "
          "walk_req_sent=%d ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
          static_cast<int>(walk.active), static_cast<int>(walk.req_sent),
          mmu_result_name(ret));
    }
    return ret;
  }
  const Result ret = walk_and_refill(p_addr, v_addr, type, status);
  if (itlb_focus_vaddr(v_addr)) {
    std::printf(
        "[ITLB][TRACE] cyc=%lld src=walk_local v=0x%08x p=0x%08x "
        "satp=0x%08x asid=0x%03x priv=%d type=%u ret=%s\n",
        (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
        mmu_result_name(ret));
  }
  return ret;
}
