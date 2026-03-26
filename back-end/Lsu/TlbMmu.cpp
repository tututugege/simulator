#include "TlbMmu.h"
#include "AbstractLsu.h"
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

inline const char *mmu_result_name(AbstractMmu::Result ret) {
  switch (ret) {
  case AbstractMmu::Result::OK:
    return "OK";
  case AbstractMmu::Result::RETRY:
    return "RETRY";
  case AbstractMmu::Result::FAULT:
    return "FAULT";
  default:
    return "UNKNOWN";
  }
}
} // namespace

TlbMmu::TlbMmu(SimContext *ctx, PtwMemPort *port, AbstractLsu *coherent_lsu,
               int tlb_entries)
    : ctx(ctx), walker(port), dtlb(tlb_entries > 0 ? tlb_entries : 1),
      tlb_entries(tlb_entries > 0 ? tlb_entries : 1), repl_ptr(0),
      coherent_lsu_(coherent_lsu) {
  (void)this->ctx;
  flush();
}

void TlbMmu::set_ptw_mem_port(PtwMemPort *port) { walker.set_mem_port(port); }
void TlbMmu::set_ptw_walk_port(PtwWalkPort *port) { walk_port = port; }

void TlbMmu::cancel_pending_walk() {
  walker.flush();
  walk_active = false;
  walk_req_sent = false;
  last_retry_reason_ = RetryReason::NONE;
  if (walk_port != nullptr) {
    walk_port->flush_client();
  }
}

void TlbMmu::flush() {
  for (auto &e : dtlb) {
    e = {};
    e.valid = false;
  }
  repl_ptr = 0;
  cancel_pending_walk();
}

TlbMmu::DebugState TlbMmu::debug_state() const {
  DebugState d{};
  d.walk_active = walk_active;
  d.walk_req_sent = walk_req_sent;
  d.walk_v_addr = walk_v_addr;
  d.walk_type = walk_type;
  d.walk_satp = walk_satp;
  d.walk_eff_priv = walk_eff_priv;
  d.walk_mxr = walk_mxr;
  d.walk_sum = walk_sum;
  d.last_retry_reason = last_retry_reason_;
  return d;
}

bool TlbMmu::lookup(uint32_t v_addr, uint8_t asid, TlbEntry &hit) const {
  uint16_t vpn1 = (v_addr >> 22) & 0x3FF;
  uint16_t vpn0 = (v_addr >> 12) & 0x3FF;
  for (const auto &e : dtlb) {
    if (!e.valid) {
      continue;
    }
    if (!(e.global || e.asid == asid)) {
      continue;
    }
    if (e.vpn1 != vpn1) {
      continue;
    }
    if (e.level == 0 && e.vpn0 != vpn0) {
      continue;
    }
    hit = e;
    return true;
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
  if (eff_priv == 1 && u && !sum) {
    return false;
  }
  if (!a) {
    return false;
  }
  if (type == 2 && !d) {
    return false;
  }
  return true;
}

void TlbMmu::refill(uint32_t v_addr, uint8_t asid, uint8_t level,
                    uint32_t pte) {
  TlbEntry &e = dtlb[repl_ptr];
  e = {};
  e.valid = true;
  e.global = (pte & PTE_G) != 0;
  e.asid = asid;
  e.level = level;
  e.vpn1 = (v_addr >> 22) & 0x3FF;
  e.vpn0 = (v_addr >> 12) & 0x3FF;
  e.ppn = (pte >> 10) & 0x3FFFFF;
  e.perm = pte & 0xFF;
  repl_ptr = (repl_ptr + 1) % tlb_entries;
}

AbstractMmu::Result TlbMmu::walk_and_refill(uint32_t &p_addr, uint32_t v_addr,
                                            uint32_t type,
                                            CsrStatusIO *status) {
  uint32_t satp = status->satp;
  uint32_t mstatus = status->mstatus;
  int eff_priv = status->privilege;
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  if (!walk_active) {
    if (!walker.start(v_addr, satp)) {
      last_retry_reason_ = RetryReason::LOCAL_WALKER_BUSY;
      return Result::RETRY;
    }
    walk_active = true;
    walk_v_addr = v_addr;
    walk_type = type;
    walk_satp = satp;
    walk_eff_priv = eff_priv;
    walk_mxr = mxr;
    walk_sum = sum;
  } else {
    // Single walker only. Different request must wait.
    if (walk_v_addr != v_addr || walk_type != type || walk_satp != satp) {
      last_retry_reason_ = RetryReason::LOCAL_WALKER_BUSY;
      return Result::RETRY;
    }
  }

  auto st = walker.tick();
  if (st != PtwWalker::State::DONE) {
    if (st == PtwWalker::State::FAULT) {
      walker.flush();
      walk_active = false;
      last_retry_reason_ = RetryReason::NONE;
      return Result::FAULT;
    }
    last_retry_reason_ = RetryReason::WAIT_WALK_RESP;
    return Result::RETRY;
  }

  uint32_t leaf_pte = walker.leaf_pte();
  uint8_t leaf_level = walker.leaf_level();
  uint8_t perm = leaf_pte & 0xFF;
  if (!check_perm(perm, walk_type, walk_eff_priv, walk_sum, walk_mxr)) {
    walker.flush();
    walk_active = false;
    last_retry_reason_ = RetryReason::NONE;
    return Result::FAULT;
  }

  uint8_t asid = (walk_satp >> 22) & 0xFF;
  refill(walk_v_addr, asid, leaf_level, leaf_pte);
  const TlbEntry &e = dtlb[(repl_ptr + tlb_entries - 1) % tlb_entries];
  p_addr = compose_paddr(walk_v_addr, e);
  walker.flush();
  walk_active = false;
  last_retry_reason_ = RetryReason::NONE;
  return Result::OK;
}

AbstractMmu::Result
TlbMmu::walk_and_refill_coherent(uint32_t &p_addr, uint32_t v_addr,
                                 uint32_t type, CsrStatusIO *status) {
  if (coherent_lsu_ == nullptr) {
    return Result::FAULT;
  }

  uint32_t satp = status->satp;
  uint32_t mstatus = status->mstatus;
  int eff_priv = status->privilege;
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  if ((eff_priv == 3) || ((satp & 0x80000000u) == 0)) {
    p_addr = v_addr;
    return Result::OK;
  }

  uint32_t ppn = satp & 0x3FFFFFu;
  for (int level = 1; level >= 0; level--) {
    const int vpn_shift = 12 + level * 10;
    const uint32_t vpn = (v_addr >> vpn_shift) & 0x3FFu;
    const uint32_t pte_addr = (ppn << 12) + (vpn << 2);
    const uint32_t pte = coherent_lsu_->coherent_read(pte_addr);
    const bool v = (pte & PTE_V) != 0;
    const bool r = (pte & PTE_R) != 0;
    const bool w = (pte & PTE_W) != 0;
    const bool x = (pte & PTE_X) != 0;
    if (!v || (!r && w)) {
      return Result::FAULT;
    }
    if (r || x) {
      if (((pte >> 10) & ((1u << (level * 10)) - 1u)) != 0) {
        return Result::FAULT;
      }
      if (!check_perm(pte & 0xFFu, type, eff_priv, sum, mxr)) {
        return Result::FAULT;
      }
      const uint8_t asid = (satp >> 22) & 0xFF;
      refill(v_addr, asid, static_cast<uint8_t>(level), pte);
      const TlbEntry &e = dtlb[(repl_ptr + tlb_entries - 1) % tlb_entries];
      p_addr = compose_paddr(v_addr, e);
      return Result::OK;
    }
    if (level == 0) {
      return Result::FAULT;
    }
    ppn = (pte >> 10) & 0x3FFFFFu;
  }

  return Result::FAULT;
}

AbstractMmu::Result
TlbMmu::walk_and_refill_shared(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                               CsrStatusIO *status) {
  uint32_t satp = status->satp;
  uint32_t mstatus = status->mstatus;
  int eff_priv = status->privilege;
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  if (!walk_active) {
    walk_active = true;
    walk_req_sent = false;
    walk_v_addr = v_addr;
    walk_type = type;
    walk_satp = satp;
    walk_eff_priv = eff_priv;
    walk_mxr = mxr;
    walk_sum = sum;
  } else {
    const uint32_t walk_page = walk_v_addr >> 12;
    const uint32_t req_page = v_addr >> 12;
    if (walk_page != req_page || walk_satp != satp) {
      last_retry_reason_ = RetryReason::OTHER_WALK_ACTIVE;
      return Result::RETRY;
    }
  }

  if (!walk_req_sent) {
    PtwWalkReq req{};
    req.vaddr = walk_v_addr;
    req.satp = walk_satp;
    req.access_type = walk_type;
    if (!walk_port->send_walk_req(req)) {
      last_retry_reason_ = RetryReason::WALK_REQ_BLOCKED;
      return Result::RETRY;
    }
    walk_req_sent = true;
  }

  if (!walk_port->resp_valid()) {
    last_retry_reason_ = RetryReason::WAIT_WALK_RESP;
    return Result::RETRY;
  }

  PtwWalkResp wr = walk_port->resp();
  walk_port->consume_resp();
  walk_active = false;
  walk_req_sent = false;

  if (wr.fault) {
    if (coherent_lsu_ != nullptr && coherent_lsu_->has_committed_store_pending()) {
      const Result coherent_ret =
          walk_and_refill_coherent(p_addr, v_addr, type, status);
      if (coherent_ret == Result::OK) {
        last_retry_reason_ = RetryReason::NONE;
        return coherent_ret;
      }
    }
    last_retry_reason_ = RetryReason::NONE;
    return Result::FAULT;
  }

  uint8_t perm = wr.leaf_pte & 0xFF;
  if (!check_perm(perm, type, eff_priv, sum, mxr)) {
    if (coherent_lsu_ != nullptr && coherent_lsu_->has_committed_store_pending()) {
      const Result coherent_ret =
          walk_and_refill_coherent(p_addr, v_addr, type, status);
      if (coherent_ret == Result::OK) {
        last_retry_reason_ = RetryReason::NONE;
        return coherent_ret;
      }
    }
    last_retry_reason_ = RetryReason::NONE;
    return Result::FAULT;
  }

  uint8_t asid = (walk_satp >> 22) & 0xFF;
  refill(v_addr, asid, wr.leaf_level, wr.leaf_pte);
  const TlbEntry &e = dtlb[(repl_ptr + tlb_entries - 1) % tlb_entries];
  p_addr = compose_paddr(v_addr, e);
  last_retry_reason_ = RetryReason::NONE;
  return Result::OK;
}

AbstractMmu::Result TlbMmu::translate(uint32_t &p_addr, uint32_t v_addr,
                                      uint32_t type, CsrStatusIO *status) {
  last_retry_reason_ = RetryReason::NONE;
  uint32_t mstatus = status->mstatus;
  uint32_t satp = status->satp;
  int eff_priv = status->privilege;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  if (eff_priv == 3 || ((satp & 0x80000000u) == 0)) {
    p_addr = v_addr;
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=identity v=0x%08x p=0x%08x satp=0x%08x "
          "priv=%d type=%u ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, eff_priv, type,
          mmu_result_name(Result::OK));
    }
    return Result::OK;
  }

  uint8_t asid = (satp >> 22) & 0xFF;
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;

  TlbEntry hit = {};
  if (lookup(v_addr, asid, hit)) {
    const bool perm_ok = check_perm(hit.perm, type, eff_priv, sum, mxr);
    if (!perm_ok) {
      if (itlb_focus_vaddr(v_addr)) {
        std::printf(
            "[ITLB][TRACE] cyc=%lld src=tlb_hit_perm_fault v=0x%08x "
            "satp=0x%08x asid=0x%02x priv=%d type=%u perm=0x%02x ret=%s\n",
            (long long)sim_time, v_addr, satp, asid, eff_priv, type, hit.perm,
            mmu_result_name(Result::FAULT));
      }
      return Result::FAULT;
    }
    p_addr = compose_paddr(v_addr, hit);
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=tlb_hit v=0x%08x p=0x%08x satp=0x%08x "
          "asid=0x%02x priv=%d type=%u level=%u perm=0x%02x ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
          static_cast<unsigned>(hit.level), hit.perm, mmu_result_name(Result::OK));
    }
    return Result::OK;
  }
  if (walk_port != nullptr) {
    const Result ret = walk_and_refill_shared(p_addr, v_addr, type, status);
    if (itlb_focus_vaddr(v_addr)) {
      std::printf(
          "[ITLB][TRACE] cyc=%lld src=walk_shared v=0x%08x p=0x%08x "
          "satp=0x%08x asid=0x%02x priv=%d type=%u walk_active=%d "
          "walk_req_sent=%d ret=%s\n",
          (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
          static_cast<int>(walk_active), static_cast<int>(walk_req_sent),
          mmu_result_name(ret));
    }
    return ret;
  }
  const Result ret = walk_and_refill(p_addr, v_addr, type, status);
  if (itlb_focus_vaddr(v_addr)) {
    std::printf(
        "[ITLB][TRACE] cyc=%lld src=walk_local v=0x%08x p=0x%08x "
        "satp=0x%08x asid=0x%02x priv=%d type=%u ret=%s\n",
        (long long)sim_time, v_addr, p_addr, satp, asid, eff_priv, type,
        mmu_result_name(ret));
  }
  return ret;
}
