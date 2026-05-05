#include "TlbMmu.h"
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
} // namespace

TlbMmu::TlbMmu(SimContext *ctx, PtwMemPort *port, int tlb_entries)
    : ctx(ctx), walker(port),
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
  walk_regs_ = {};
  walk_comb_ = {};
  walk_comb_valid_ = false;
  last_retry_reason_ = RetryReason::NONE;
  if (walk_port != nullptr) {
    walk_port->flush_client();
  }
}

void TlbMmu::flush() {
  const WalkRegs &walk = visible_walk_regs();
  if (walk.active) {
    record_trace(TraceSource::FLUSH, Result::RETRY, walk.v_addr, 0, walk.satp,
                 sv32_asid(walk.satp), walk.type, 0xff, 0);
  }
  cancel_pending_walk();
  flush_pending_ = true;
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
  TranslateContext ctx{};
  ctx.satp = status->satp;
  ctx.eff_priv = status->privilege;
  const uint32_t mstatus = status->mstatus;
  ctx.mxr = (mstatus & MSTATUS_MXR) != 0;
  ctx.sum = (mstatus & MSTATUS_SUM) != 0;
  const bool mprv = (mstatus & MSTATUS_MPRV) != 0;
  if (type != 0 && mprv) {
    ctx.eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }
  return ctx;
}

bool TlbMmu::entry_matches(const TlbEntry &entry, uint32_t v_addr,
                           uint16_t asid) const {
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

bool TlbMmu::lookup(uint32_t v_addr, uint16_t asid, TlbEntry &hit) const {
  if (refill_comb_.valid && entry_matches(refill_comb_.entry, v_addr, asid)) {
    hit = refill_comb_.entry;
    return true;
  }
  for (const auto &entry : tlb_entries_) {
    if (entry_matches(entry, v_addr, asid)) {
      hit = entry;
      return true;
    }
  }
  return false;
}

uint32_t TlbMmu::compose_paddr(uint32_t v_addr, const TlbEntry &e) const {
  if (e.level == 1) {
    const uint32_t mask = (1u << 22) - 1u;
    return ((e.ppn << 12) & ~mask) | (v_addr & mask);
  }
  return (e.ppn << 12) | (v_addr & 0xFFFu);
}

bool TlbMmu::check_perm(uint8_t perm, uint32_t type, int eff_priv, bool sum,
                        bool mxr) const {
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
