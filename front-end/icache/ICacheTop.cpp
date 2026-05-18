// ICacheTop is glue logic only:
// - bind MMU/PTW/AXI runtime objects
// - translate external table responses into icache_module generalized IO
// - do not add request/timing/register state here

#include "include/ICacheTop.h"

#include "../front_module.h"
#include "../frontend.h"
#include "Csr.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include "RISCV.h"
#include "TlbMmu.h"
#include "PhysMemory.h"
#include "config.h"
#include "include/icache_module.h"

#if __has_include("AXI_Interconnect_IO.h")
#include "AXI_Interconnect_IO.h"
#else
namespace axi_interconnect {
constexpr uint32_t MAX_READ_TRANSACTION_BYTES = 256;
struct WideData512_t {
  uint32_t words[MAX_READ_TRANSACTION_BYTES / 4] = {0};
  uint32_t &operator[](int idx) { return words[idx]; }
  const uint32_t &operator[](int idx) const { return words[idx]; }
};
struct ReadMasterReq_t {
  bool valid = false;
  bool ready = false;
  bool accepted = false;
  uint32_t addr = 0;
  uint8_t total_size = 0;
  uint8_t id = 0;
  bool bypass = false;
};
struct ReadMasterResp_t {
  bool valid = false;
  bool ready = false;
  WideData512_t data{};
  uint8_t id = 0;
};
struct ReadMasterPort_t {
  ReadMasterReq_t req{};
  ReadMasterResp_t resp{};
};
} // namespace axi_interconnect
#endif

#include <cstdlib>
#include <iostream>
#include <memory>

extern icache_module_n::ICache icache;
void icache_fill_lookup_meta_input(icache_module_n::ICache_lookup_in_t &dst);
void icache_fill_lookup_data_input(icache_module_n::ICache_lookup_in_t &dst,
                                   bool req_valid, uint32_t req_index,
                                   uint32_t req_way);

namespace {

#ifndef CONFIG_ICACHE_FOCUS_VADDR_BEGIN
#define CONFIG_ICACHE_FOCUS_VADDR_BEGIN 0u
#endif

#ifndef CONFIG_ICACHE_FOCUS_VADDR_END
#define CONFIG_ICACHE_FOCUS_VADDR_END 0u
#endif

inline bool icache_focus_vaddr(uint32_t v_addr) {
  const uint32_t begin = static_cast<uint32_t>(CONFIG_ICACHE_FOCUS_VADDR_BEGIN);
  const uint32_t end = static_cast<uint32_t>(CONFIG_ICACHE_FOCUS_VADDR_END);
  return end > begin && (v_addr - begin) < (end - begin);
}

inline void dump_icache_focus_line(const char *tag, uint32_t fetch_pc,
                                   const uint32_t *line_words) {
  std::printf("[ICACHE][TRACE][%s] cyc=%lld fetch_pc=0x%08x line=[", tag,
              (long long)sim_time, fetch_pc);
  for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
    std::printf("%s%08x", (i == 0) ? "" : " ", line_words[i]);
  }
  std::printf("]\n");
}

static_assert(ICACHE_LINE_SIZE <= axi_interconnect::MAX_READ_TRANSACTION_BYTES,
              "ICACHE_LINE_SIZE exceeds AXI upstream read transaction limit");

uint32_t icache_coherent_read(uint32_t p_addr) { return pmem_read(p_addr); }

void clear_secondary_outputs(struct icache_out *out) {
  out->icache_read_ready_2 = false;
  out->icache_read_complete_2 = false;
  out->fetch_pc_2 = 0;
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    out->fetch_group_2[i] = INST_NOP;
    out->page_fault_inst_2[i] = false;
    out->inst_valid_2[i] = false;
  }
}

void clear_primary_outputs(struct icache_out *out) {
  out->icache_read_complete = false;
  out->fetch_pc = 0;
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    out->fetch_group[i] = INST_NOP;
    out->page_fault_inst[i] = false;
    out->inst_valid[i] = false;
  }
}

void clear_perf_outputs(struct icache_out *out) {
  out->perf_req_fire = false;
  out->perf_req_blocked = false;
  out->perf_resp_fire = false;
  out->perf_miss_event = false;
  out->perf_miss_busy = false;
  out->perf_outstanding_req = false;
  out->perf_itlb_hit = false;
  out->perf_itlb_miss = false;
  out->perf_itlb_fault = false;
  out->perf_itlb_retry = false;
  out->perf_itlb_retry_other_walk = false;
  out->perf_itlb_retry_walk_req_blocked = false;
  out->perf_itlb_retry_wait_walk_resp = false;
  out->perf_itlb_retry_local_walker_busy = false;
}

void fill_fetch_group(uint32_t fetch_pc, const uint32_t *line_words,
                      bool page_fault, uint32_t *fetch_group,
                      bool *page_fault_inst, bool *inst_valid) {
  uint32_t mask = ICACHE_LINE_SIZE - 1u;
  int base_idx = static_cast<int>((fetch_pc & mask) / 4u);
  for (int i = 0; i < FETCH_WIDTH; ++i) {
    if (base_idx + i >= ICACHE_LINE_SIZE / 4) {
      fetch_group[i] = INST_NOP;
      page_fault_inst[i] = false;
      inst_valid[i] = false;
      continue;
    }
    fetch_group[i] = page_fault ? INST_NOP : line_words[base_idx + i];
    page_fault_inst[i] = page_fault;
    inst_valid[i] = true;
  }
}

class IcacheBlockingPtwPort : public PtwMemPort {
public:
  bool send_read_req(uint32_t paddr) override {
    resp_data_reg = icache_coherent_read(paddr);
    resp_valid_reg = true;
    return true;
  }
  bool resp_valid() const override { return resp_valid_reg; }
  uint32_t resp_data() const override { return resp_data_reg; }
  void consume_resp() override { resp_valid_reg = false; }

private:
  bool resp_valid_reg = false;
  uint32_t resp_data_reg = 0;
};

struct MemReadView {
  bool req_ready = true;
  bool req_accepted = false;
  uint8_t req_accepted_id = 0;
  bool resp_valid = false;
  uint8_t resp_id = 0;
  uint32_t resp_data[ICACHE_LINE_SIZE / 4] = {0};
};

struct ICacheMmuReqView {
  bool context_flush = false;
  bool cancel_pending = false;
  bool translate_valid = false;
  uint32_t vaddr = 0;
  CsrStatusIO *csr_status = nullptr;
};

struct ICacheMmuRespView {
  bool translate_invoked = false;
  bool ppn_valid = false;
  uint32_t ppn = 0;
  uint32_t paddr = 0;
  bool page_fault = false;
  TlbMmu::Result translate_result = TlbMmu::Result::OK;
  TlbMmu::RetryReason retry_reason = TlbMmu::RetryReason::NONE;
};

class ExternalReadPortAdapter {
public:
  void bind(axi_interconnect::ReadMasterPort_t *port) { port_ = port; }

  void reset() {
    if (port_ != nullptr) {
      port_->req.valid = false;
      port_->req.addr = 0;
      port_->req.total_size = 0;
      port_->req.id = 0;
      port_->req.bypass = false;
      port_->req.accepted = false;
      port_->req.accepted_id = 0;
      port_->resp.ready = false;
    }
  }

  MemReadView comb_view() const {
    MemReadView view;
    if (port_ == nullptr) {
      view.req_ready = false;
      return view;
    }
    view.req_ready = port_->req.ready;
    view.req_accepted = port_->req.accepted;
    view.req_accepted_id = static_cast<uint8_t>(port_->req.accepted_id & 0xF);
    view.resp_valid = port_->resp.valid;
    view.resp_id = static_cast<uint8_t>(port_->resp.id & 0xF);
    for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
      view.resp_data[i] = port_->resp.data[i];
    }
    if (SIM_DEBUG_PRINT_ACTIVE &&
        (port_->req.valid || port_->req.ready || port_->req.accepted ||
         port_->resp.valid || port_->resp.ready)) {
      std::printf(
          "[ICACHE][ADAPTER][VIEW] cyc=%lld req_v=%u req_addr=0x%08x "
          "req_id=%u req_ready=%u req_acc=%u acc_id=%u resp_v=%u resp_id=%u "
          "resp_ready=%u resp_w0=0x%08x resp_w7=0x%08x\n",
          (long long)sim_time, static_cast<unsigned>(port_->req.valid),
          static_cast<unsigned>(port_->req.addr),
          static_cast<unsigned>(port_->req.id & 0xF),
          static_cast<unsigned>(port_->req.ready),
          static_cast<unsigned>(port_->req.accepted),
          static_cast<unsigned>(port_->req.accepted_id & 0xF),
          static_cast<unsigned>(port_->resp.valid),
          static_cast<unsigned>(port_->resp.id & 0xF),
          static_cast<unsigned>(port_->resp.ready),
          static_cast<unsigned>(port_->resp.data[0]),
          static_cast<unsigned>(port_->resp.data[7]));
    }
    return view;
  }

  void comb_accept(bool req_valid, uint32_t req_addr, uint8_t req_id,
                   bool resp_ready) {
    if (port_ == nullptr) {
      return;
    }
    port_->req.valid = req_valid;
    port_->req.addr = req_addr;
    port_->req.total_size = static_cast<uint8_t>(ICACHE_LINE_SIZE - 1u);
    port_->req.id = static_cast<uint8_t>(req_id & 0xF);
    port_->req.bypass = false;
    port_->resp.ready = resp_ready;
    if (SIM_DEBUG_PRINT_ACTIVE &&
        (req_valid || resp_ready || port_->req.ready || port_->req.accepted ||
         port_->resp.valid)) {
      std::printf(
          "[ICACHE][ADAPTER][ACCEPT] cyc=%lld req_v=%u req_addr=0x%08x "
          "req_id=%u resp_ready=%u port_req_ready=%u port_req_acc=%u acc_id=%u "
          "port_resp_v=%u port_resp_id=%u port_resp_w0=0x%08x "
          "port_resp_w7=0x%08x\n",
          (long long)sim_time, static_cast<unsigned>(req_valid),
          static_cast<unsigned>(req_addr),
          static_cast<unsigned>(req_id & 0xF),
          static_cast<unsigned>(resp_ready),
          static_cast<unsigned>(port_->req.ready),
          static_cast<unsigned>(port_->req.accepted),
          static_cast<unsigned>(port_->req.accepted_id & 0xF),
          static_cast<unsigned>(port_->resp.valid),
          static_cast<unsigned>(port_->resp.id & 0xF),
          static_cast<unsigned>(port_->resp.data[0]),
          static_cast<unsigned>(port_->resp.data[7]));
    }
  }

  void seq(bool, uint32_t, uint8_t, bool, uint8_t, bool) {}

private:
  axi_interconnect::ReadMasterPort_t *port_ = nullptr;
};

template <typename ReadPort> ReadPort &read_port_runtime() {
  static ReadPort read_port;
  return read_port;
}

template <typename HW, typename ReadPort> struct TrueIcacheRuntime {
  TlbMmu *mmu_model = nullptr;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;
  axi_interconnect::ReadMasterPort_t *mem_read_port = nullptr;
};

template <typename HW, typename ReadPort>
TrueIcacheRuntime<HW, ReadPort> &true_icache_runtime() {
  static TrueIcacheRuntime<HW, ReadPort> runtime;
  return runtime;
}

struct SimpleIcacheRuntime {
  TlbMmu *mmu_model = nullptr;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;
  axi_interconnect::ReadMasterPort_t *mem_read_port = nullptr;
  uint32_t last_satp = 0;
  bool satp_seen = false;
  bool pending_req_valid = false;
  uint32_t pending_fetch_addr = 0;
  bool pend_on_retry_comb = false;
  bool resp_fire_comb = false;
};

SimpleIcacheRuntime &simple_icache_runtime() {
  static SimpleIcacheRuntime runtime;
  return runtime;
}

template <typename Runtime>
void ensure_mmu_model(Runtime &runtime, SimContext *ctx) {
  static IcacheBlockingPtwPort ptw_port;
  if (runtime.mmu_model != nullptr || ctx == nullptr) {
    return;
  }
  runtime.mmu_model =
      new TlbMmu(ctx, runtime.ptw_mem_port ? runtime.ptw_mem_port : &ptw_port,
                 ITLB_ENTRIES);
  if (runtime.ptw_walk_port != nullptr) {
    runtime.mmu_model->set_ptw_walk_port(runtime.ptw_walk_port);
  }
}

template <typename Runtime>
void bind_ptw_mem_port(Runtime &runtime, PtwMemPort *port) {
  runtime.ptw_mem_port = port;
  if (runtime.mmu_model != nullptr) {
    runtime.mmu_model->set_ptw_mem_port(port);
  }
}

template <typename Runtime>
void bind_ptw_walk_port(Runtime &runtime, PtwWalkPort *port) {
  runtime.ptw_walk_port = port;
  if (runtime.mmu_model != nullptr) {
    runtime.mmu_model->set_ptw_walk_port(port);
  }
}

template <typename Runtime>
void bind_mem_read_port(Runtime &runtime,
                        axi_interconnect::ReadMasterPort_t *port) {
  runtime.mem_read_port = port;
}

template <typename Runtime>
ICacheMmuRespView comb_mmu_view(Runtime &runtime, SimContext *ctx,
                                const ICacheMmuReqView &req) {
  ensure_mmu_model(runtime, ctx);
  ICacheMmuRespView resp;
  if (runtime.mmu_model == nullptr) {
    return resp;
  }

  if (req.context_flush) {
    runtime.mmu_model->flush();
  }
  if (req.cancel_pending) {
    runtime.mmu_model->cancel_pending_walk();
  }
  if (!req.translate_valid) {
    return resp;
  }

  resp.translate_invoked = true;
  uint32_t p_addr = 0;
  resp.translate_result =
      runtime.mmu_model->translate(p_addr, req.vaddr, 0, req.csr_status);
  if (resp.translate_result == TlbMmu::Result::OK) {
    resp.paddr = p_addr;
    resp.ppn_valid = true;
    resp.ppn = p_addr >> 12;
  } else if (resp.translate_result == TlbMmu::Result::FAULT) {
    resp.ppn_valid = true;
    resp.page_fault = true;
  }
  resp.retry_reason = runtime.mmu_model->last_retry_reason();
  return resp;
}

template <typename HW>
void apply_mmu_resp_view(HW &icache_hw, const ICacheMmuRespView &resp) {
  icache_hw.io.in.ppn = resp.ppn;
  icache_hw.io.in.ppn_valid = resp.ppn_valid;
  icache_hw.io.in.page_fault = resp.page_fault;
}

template <typename HW>
void refresh_lookup_meta_input(HW &icache_hw) {
  icache_fill_lookup_meta_input(icache_hw.io.lookup_in);
}

template <typename HW>
void refresh_lookup_data_input(HW &icache_hw) {
  icache_fill_lookup_data_input(
      icache_hw.io.lookup_in, icache_hw.io.out.lookup_data_req_valid,
      static_cast<uint32_t>(icache_hw.io.out.lookup_data_req_index),
      static_cast<uint32_t>(icache_hw.io.out.lookup_data_req_way));
}

template <typename HW, typename ReadPort>
class TrueICacheTopT : public ICacheTop {
public:
  explicit TrueICacheTopT(HW &hw) : icache_hw(hw) {}

  void set_ptw_mem_port(PtwMemPort *port) override {
    bind_ptw_mem_port(true_icache_runtime<HW, ReadPort>(), port);
  }

  void set_ptw_walk_port(PtwWalkPort *port) override {
    bind_ptw_walk_port(true_icache_runtime<HW, ReadPort>(), port);
  }

  void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) override {
    bind_mem_read_port(true_icache_runtime<HW, ReadPort>(), port);
  }

  void peek_ready() override {
    clear_primary_outputs(out);
    clear_secondary_outputs(out);
    clear_perf_outputs(out);
    bool hold_for_recovery = in->itlb_flush || in->fence_i || in->invalidate_req;
    out->icache_read_ready = in->reset ? true : comb_visible_ready(hold_for_recovery);
  }

  void dump_debug_state() const override {
    const auto &runtime = true_icache_runtime<HW, ReadPort>();
    auto &read_port = read_port_runtime<ReadPort>();
    read_port.bind(runtime.mem_read_port);
    const MemReadView mem = read_port.comb_view();
    bool hold_for_recovery =
        in && (in->itlb_flush || in->fence_i || in->invalidate_req);
    uint32_t inflight_mask = 0;
    uint32_t canceled_mask = 0;
    for (int i = 0; i < 16; ++i) {
      if (icache_hw.io.regs.txid_inflight_r[i]) {
        inflight_mask |= (1u << i);
      }
      if (icache_hw.io.regs.txid_canceled_r[i]) {
        canceled_mask |= (1u << i);
      }
    }
    std::printf(
        "[DEADLOCK][FRONT][ICACHE_HW] type=true req_valid=%d fetch_addr=0x%08x "
        "refetch=%d inv=%d fence_i=%d itlb_flush=%d hold_recovery=%d "
        "last_mem{ready=%d acc=%d acc_id=%u resp_v=%d resp_id=%u w0=0x%08x w7=0x%08x}\n",
        static_cast<int>(in && in->icache_read_valid),
        static_cast<unsigned>(in ? in->fetch_address : 0u),
        static_cast<int>(in && in->refetch),
        static_cast<int>(in && in->invalidate_req),
        static_cast<int>(in && in->fence_i),
        static_cast<int>(in && in->itlb_flush),
        static_cast<int>(hold_for_recovery),
        static_cast<int>(mem.req_ready),
        static_cast<int>(mem.req_accepted),
        static_cast<unsigned>(mem.req_accepted_id & 0xF),
        static_cast<int>(mem.resp_valid),
        static_cast<unsigned>(mem.resp_id & 0xF),
        static_cast<unsigned>(mem.resp_data[0]),
        static_cast<unsigned>(mem.resp_data[7]));
    std::printf(
        "[DEADLOCK][FRONT][ICACHE_HW] regs state=%u mem_axi=%u ifu_ready_r=%d "
        "req_valid_r=%d req_pc_r=0x%08x req_idx_r=%u lookup_pending=%d "
        "lookup_pc_r=0x%08x lookup_idx_r=%u ppn_r=0x%05x "
        "miss_txid_valid=%d miss_txid=%u miss_ready_seen=%d "
        "inflight=0x%04x canceled=0x%04x\n",
        static_cast<unsigned>(icache_hw.io.regs.state),
        static_cast<unsigned>(icache_hw.io.regs.mem_axi_state),
        static_cast<int>(icache_hw.io.regs.ifu_req_ready_r),
        static_cast<int>(icache_hw.io.regs.req_valid_r),
        static_cast<unsigned>(icache_hw.io.regs.req_pc_r),
        static_cast<unsigned>(icache_hw.io.regs.req_index_r),
        static_cast<int>(icache_hw.io.regs.lookup_pending_r),
        static_cast<unsigned>(icache_hw.io.regs.lookup_pc_r),
        static_cast<unsigned>(icache_hw.io.regs.lookup_index_r),
        static_cast<unsigned>(icache_hw.io.regs.ppn_r),
        static_cast<int>(icache_hw.io.regs.miss_txid_valid_r),
        static_cast<unsigned>(icache_hw.io.regs.miss_txid_r & 0xF),
        static_cast<int>(icache_hw.io.regs.miss_ready_seen_r),
        static_cast<unsigned>(inflight_mask),
        static_cast<unsigned>(canceled_mask));
    std::printf(
        "[DEADLOCK][FRONT][ICACHE_HW] out ifu_req_ready=%d ifu_resp_valid=%d "
        "ifu_resp_pc=0x%08x miss=%d page_fault=%d mmu_req_v=%d mmu_vtag=0x%05x "
        "mem_req_v=%d mem_req_addr=0x%08x mem_req_id=%u mem_resp_ready=%d\n",
        static_cast<int>(icache_hw.io.out.ifu_req_ready),
        static_cast<int>(icache_hw.io.out.ifu_resp_valid),
        static_cast<unsigned>(icache_hw.io.out.ifu_resp_pc),
        static_cast<int>(icache_hw.io.out.miss),
        static_cast<int>(icache_hw.io.out.ifu_page_fault),
        static_cast<int>(icache_hw.io.out.mmu_req_valid),
        static_cast<unsigned>(icache_hw.io.out.mmu_req_vtag),
        static_cast<int>(icache_hw.io.out.mem_req_valid),
        static_cast<unsigned>(icache_hw.io.out.mem_req_addr),
        static_cast<unsigned>(icache_hw.io.out.mem_req_id & 0xF),
        static_cast<int>(icache_hw.io.out.mem_resp_ready));
  }

  void comb() override {
    clear_primary_outputs(out);
    clear_secondary_outputs(out);
    clear_perf_outputs(out);

    auto &runtime = true_icache_runtime<HW, ReadPort>();
    auto &read_port = read_port_runtime<ReadPort>();
    read_port.bind(runtime.mem_read_port);

    if (in->reset) {
      DEBUG_LOG("[icache] reset\n");
      icache_hw.reset();
      read_port.reset();
      ICacheMmuReqView reset_mmu_req;
      reset_mmu_req.context_flush = true;
      (void)comb_mmu_view(runtime, ctx, reset_mmu_req);
      out->icache_read_ready = true;
      return;
    }

    // satp write itself must not implicitly flush TrueICache translation state.
    // The architectural shootdown path is explicit SFENCE.VMA -> itlb_flush.
    bool translation_context_flush = in->itlb_flush;
    bool cache_invalidate = in->fence_i;
    bool cancel_pending_req = in->refetch || in->invalidate_req || in->fence_i;
    bool hold_for_recovery = in->itlb_flush || in->fence_i || in->invalidate_req;

    MemReadView mem = read_port.comb_view();
    const bool req_valid = in->icache_read_valid;
    const uint32_t req_pc = in->fetch_address;
    bool itlb_translate_invoked = false;
    TlbMmu::Result itlb_translate_ret = TlbMmu::Result::OK;

    icache_hw.io.in.refetch = cancel_pending_req;
    icache_hw.io.in.flush = cache_invalidate;
    icache_hw.io.in.pc = req_pc;
    icache_hw.io.in.ifu_req_valid = req_valid;
    icache_hw.io.in.ifu_resp_ready = true;
    icache_hw.io.in.ppn = 0;
    icache_hw.io.in.ppn_valid = false;
    icache_hw.io.in.page_fault = false;
    icache_hw.io.in.mem_req_ready = mem.req_ready;
    icache_hw.io.in.mem_req_accepted = mem.req_accepted;
    icache_hw.io.in.mem_req_accepted_id = mem.req_accepted_id;
    icache_hw.io.in.mem_resp_valid = mem.resp_valid;
    icache_hw.io.in.mem_resp_id = mem.resp_id;
    for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
      icache_hw.io.in.mem_resp_data[i] = mem.resp_data[i];
    }

    refresh_lookup_meta_input(icache_hw);
    icache_hw.comb_lookup_meta();

    ICacheMmuReqView mmu_req;
    mmu_req.context_flush = translation_context_flush;
    mmu_req.cancel_pending = in->refetch;
    mmu_req.translate_valid = !in->refetch && icache_hw.io.out.mmu_req_valid;
    mmu_req.vaddr = static_cast<uint32_t>(icache_hw.io.out.mmu_req_vtag) << 12;
    mmu_req.csr_status = in->csr_status;
    const ICacheMmuRespView mmu_resp = comb_mmu_view(runtime, ctx, mmu_req);
    itlb_translate_invoked = mmu_resp.translate_invoked;
    itlb_translate_ret = mmu_resp.translate_result;
    apply_mmu_resp_view(icache_hw, mmu_resp);

    refresh_lookup_meta_input(icache_hw);
    icache_hw.comb_lookup_meta();
    refresh_lookup_data_input(icache_hw);
    icache_hw.comb_lookup_data();

    read_port.comb_accept(
        icache_hw.io.out.mem_req_valid, icache_hw.io.out.mem_req_addr,
        static_cast<uint8_t>(icache_hw.io.out.mem_req_id & 0xF),
        icache_hw.io.out.mem_resp_ready);

    out->icache_read_ready = comb_visible_ready(hold_for_recovery);
    out->perf_req_fire = req_valid && out->icache_read_ready;
    out->perf_req_blocked = req_valid && !out->icache_read_ready;

    bool resp_fire =
        icache_hw.io.out.ifu_resp_valid && icache_hw.io.in.ifu_resp_ready;
    out->perf_resp_fire = resp_fire;
    if (resp_fire) {
      if (icache_hw.io.out.miss) {
        std::cout << "[icache_top] WARNING: miss is true when ifu_resp is valid"
                  << std::endl;
        std::exit(1);
      }
      out->icache_read_complete = true;
      out->fetch_pc = icache_hw.io.out.ifu_resp_pc;
      if (icache_focus_vaddr(out->fetch_pc)) {
        dump_icache_focus_line("RESP", out->fetch_pc, icache_hw.io.out.rd_data);
      }
      fill_fetch_group(icache_hw.io.out.ifu_resp_pc, icache_hw.io.out.rd_data,
                       icache_hw.io.out.ifu_page_fault, out->fetch_group,
                       out->page_fault_inst, out->inst_valid);
    }

    bool mem_req_fire =
        icache_hw.io.out.mem_req_valid && icache_hw.io.in.mem_req_accepted;
    bool mem_req_issue = icache_hw.perf.miss_issue_valid;
    if ((mem_req_fire || mem_req_issue) &&
        icache_focus_vaddr(icache_hw.io.regs.lookup_pc_r)) {
      std::printf(
          "[ICACHE][TRACE][MEM_REQ] cyc=%lld issue=%d fire=%d req_pc_r=0x%08x "
          "lookup_pc_r=0x%08x mem_req_addr=0x%08x req_id=%u state=%u\n",
          (long long)sim_time, static_cast<int>(mem_req_issue),
          static_cast<int>(mem_req_fire), icache_hw.io.regs.req_pc_r,
          icache_hw.io.regs.lookup_pc_r, icache_hw.io.out.mem_req_addr,
          static_cast<unsigned>(icache_hw.io.out.mem_req_id & 0xF),
          static_cast<unsigned>(icache_hw.io.regs.state));
    }
    out->perf_miss_event = mem_req_issue;
    const auto icache_state =
        static_cast<icache_module_n::ICacheState>(icache_hw.io.regs.state);
    out->perf_miss_busy =
        (icache_state == icache_module_n::SWAP_IN ||
         icache_state == icache_module_n::DRAIN);
    out->perf_outstanding_req =
        out->perf_miss_busy || icache_hw.io.regs.req_valid_r ||
        icache_hw.io.regs.lookup_pending_r;

    if (itlb_translate_invoked) {
      if (icache_focus_vaddr(icache_hw.io.out.mmu_req_vtag << 12)) {
        std::printf(
            "[ICACHE][TRACE][MMU] cyc=%lld req_vaddr=0x%08x ppn_valid=%d "
            "ppn=0x%05x page_fault=%d ret=%d satp=0x%08x refetch=%d\n",
            (long long)sim_time, icache_hw.io.out.mmu_req_vtag << 12,
            static_cast<int>(icache_hw.io.in.ppn_valid),
            static_cast<unsigned>(icache_hw.io.in.ppn),
            static_cast<int>(icache_hw.io.in.page_fault),
            static_cast<int>(itlb_translate_ret),
            in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0u,
            static_cast<int>(in->refetch));
      }
      if (itlb_translate_ret == TlbMmu::Result::OK) {
        out->perf_itlb_hit = true;
      } else if (itlb_translate_ret == TlbMmu::Result::FAULT) {
        out->perf_itlb_fault = true;
      } else {
        out->perf_itlb_miss = true;
        out->perf_itlb_retry = true;
        TlbMmu::RetryReason reason = mmu_resp.retry_reason;
        switch (reason) {
        case TlbMmu::RetryReason::OTHER_WALK_ACTIVE:
          out->perf_itlb_retry_other_walk = true;
          break;
        case TlbMmu::RetryReason::WALK_REQ_BLOCKED:
          out->perf_itlb_retry_walk_req_blocked = true;
          break;
        case TlbMmu::RetryReason::WAIT_WALK_RESP:
          out->perf_itlb_retry_wait_walk_resp = true;
          break;
        case TlbMmu::RetryReason::LOCAL_WALKER_BUSY:
          out->perf_itlb_retry_local_walker_busy = true;
          break;
        case TlbMmu::RetryReason::NONE:
        default:
          out->perf_itlb_retry_local_walker_busy = true;
          break;
        }
      }
    }

  }

  void seq() override {
    auto &runtime = true_icache_runtime<HW, ReadPort>();
    if (in->reset) {
      if (runtime.mmu_model != nullptr) {
        runtime.mmu_model->seq();
      }
      return;
    }

    bool mem_req_issue =
        icache_hw.io.out.mem_req_valid && icache_hw.io.in.mem_req_accepted;
    bool mem_resp_fire =
        icache_hw.io.in.mem_resp_valid && icache_hw.io.out.mem_resp_ready;

    icache_hw.seq();
    read_port_runtime<ReadPort>().seq(
        mem_req_issue, icache_hw.io.out.mem_req_addr,
        static_cast<uint8_t>(icache_hw.io.out.mem_req_id & 0xF), mem_resp_fire,
        static_cast<uint8_t>(icache_hw.io.in.mem_resp_id & 0xF), in->refetch);
    if (runtime.mmu_model != nullptr) {
      runtime.mmu_model->seq();
    }
  }

private:
  bool comb_visible_ready(bool hold_for_recovery) const {
    const auto state =
        static_cast<icache_module_n::ICacheState>(icache_hw.io.regs.state);
    const auto mem_axi_state =
        static_cast<icache_module_n::AXIState>(icache_hw.io.regs.mem_axi_state);
    const bool local_busy =
        icache_hw.io.regs.lookup_pending_r || icache_hw.io.regs.req_valid_r ||
        icache_hw.io.regs.miss_txid_valid_r ||
        state != icache_module_n::IDLE ||
        mem_axi_state != icache_module_n::AXI_IDLE;
    return hold_for_recovery ? false
                             : (icache_hw.io.regs.ifu_req_ready_r &&
                                !local_busy &&
                                icache_hw.io.out.ifu_req_ready);
  }

  HW &icache_hw;
};

class SimpleICacheTop : public ICacheTop {
public:
  void dump_debug_state() const override {
    const auto &runtime = simple_icache_runtime();
    std::printf(
        "[DEADLOCK][FRONT][ICACHE_HW] type=simple pending_req_valid=%d "
        "pending_fetch_addr=0x%08x pend_on_retry=%d resp_fire=%d satp_seen=%d "
        "last_satp=0x%08x in_req_valid=%d in_fetch_addr=0x%08x\n",
        static_cast<int>(runtime.pending_req_valid),
        static_cast<unsigned>(runtime.pending_fetch_addr),
        static_cast<int>(runtime.pend_on_retry_comb),
        static_cast<int>(runtime.resp_fire_comb),
        static_cast<int>(runtime.satp_seen),
        static_cast<unsigned>(runtime.last_satp),
        static_cast<int>(in && in->icache_read_valid),
        static_cast<unsigned>(in ? in->fetch_address : 0u));
  }

  void peek_ready() override {
    clear_perf_outputs(out);
    out->icache_read_ready_2 = false;
    out->icache_read_complete_2 = false;
    const auto &runtime = simple_icache_runtime();
    out->icache_read_ready =
        in->reset || in->refetch || in->invalidate_req || in->fence_i ||
        !runtime.pending_req_valid;
    out->icache_read_complete = false;
  }

  void comb() override {
    auto &runtime = simple_icache_runtime();
    clear_perf_outputs(out);
    out->icache_read_ready_2 = false;
    out->icache_read_complete_2 = false;
    out->fetch_pc_2 = 0;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group_2[i] = INST_NOP;
      out->page_fault_inst_2[i] = false;
      out->inst_valid_2[i] = false;
    }

    ensure_mmu_model(runtime, ctx);
    runtime.pend_on_retry_comb = false;
    runtime.resp_fire_comb = false;

    if (in->reset) {
      DEBUG_LOG("[icache] reset\n");
      ICacheMmuReqView mmu_req;
      mmu_req.context_flush = true;
      (void)comb_mmu_view(runtime, ctx, mmu_req);
      runtime.satp_seen = false;
      runtime.last_satp = 0;
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
      runtime.pend_on_retry_comb = false;
      runtime.resp_fire_comb = false;
      out->icache_read_ready = true;
      out->icache_read_complete = false;
      out->perf_req_blocked = in->icache_read_valid;
      return;
    }

    if (in->refetch || in->invalidate_req || in->fence_i) {
      ICacheMmuReqView mmu_req;
      mmu_req.cancel_pending = true;
      (void)comb_mmu_view(runtime, ctx, mmu_req);
    }

    out->icache_read_complete = false;
    out->icache_read_ready =
        !(runtime.pending_req_valid || in->invalidate_req || in->fence_i);
    out->perf_req_fire = in->icache_read_valid && out->icache_read_ready;
    out->perf_req_blocked = in->icache_read_valid && !out->icache_read_ready;
    out->perf_outstanding_req = runtime.pending_req_valid;
    out->fetch_pc =
        runtime.pending_req_valid ? runtime.pending_fetch_addr : in->fetch_address;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }

    uint32_t cur_satp =
        in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
    if (!runtime.satp_seen || cur_satp != runtime.last_satp || in->itlb_flush) {
      ICacheMmuReqView mmu_req;
      mmu_req.context_flush = true;
      (void)comb_mmu_view(runtime, ctx, mmu_req);
      runtime.satp_seen = true;
      runtime.last_satp = cur_satp;
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
      out->icache_read_ready = true;
      out->perf_req_fire = in->icache_read_valid;
      out->perf_req_blocked = false;
      out->perf_outstanding_req = false;
    }

    if (in->invalidate_req || in->fence_i) {
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
      out->icache_read_ready = true;
      out->perf_req_fire = false;
      out->perf_req_blocked = false;
      out->perf_outstanding_req = false;
    }

    if (!runtime.pending_req_valid && !in->icache_read_valid) {
      return;
    }

    const uint32_t fetch_addr =
        runtime.pending_req_valid ? runtime.pending_fetch_addr : in->fetch_address;
    out->fetch_pc = fetch_addr;
    out->icache_read_complete = true;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = fetch_addr + (i * 4);
      uint32_t p_addr = 0;

      if (v_addr / ICACHE_LINE_SIZE != (fetch_addr) / ICACHE_LINE_SIZE) {
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = false;
        out->inst_valid[i] = false;
        continue;
      }
      out->inst_valid[i] = true;

      ICacheMmuReqView mmu_req;
      mmu_req.translate_valid = true;
      mmu_req.vaddr = v_addr;
      mmu_req.csr_status = in->csr_status;
      ICacheMmuRespView mmu_resp = comb_mmu_view(runtime, ctx, mmu_req);
      TlbMmu::Result ret = mmu_resp.translate_result;
      p_addr = mmu_resp.paddr;
      for (int spin = 0; spin < 8 && ret == TlbMmu::Result::RETRY;
           spin++) {
        mmu_resp = comb_mmu_view(runtime, ctx, mmu_req);
        ret = mmu_resp.translate_result;
        p_addr = mmu_resp.paddr;
      }

      if (ret == TlbMmu::Result::RETRY) {
        out->perf_itlb_miss = true;
        out->perf_itlb_retry = true;
        switch (mmu_resp.retry_reason) {
        case TlbMmu::RetryReason::OTHER_WALK_ACTIVE:
          out->perf_itlb_retry_other_walk = true;
          break;
        case TlbMmu::RetryReason::WALK_REQ_BLOCKED:
          out->perf_itlb_retry_walk_req_blocked = true;
          break;
        case TlbMmu::RetryReason::WAIT_WALK_RESP:
          out->perf_itlb_retry_wait_walk_resp = true;
          break;
        case TlbMmu::RetryReason::LOCAL_WALKER_BUSY:
        case TlbMmu::RetryReason::NONE:
        default:
          out->perf_itlb_retry_local_walker_busy = true;
          break;
        }
        out->icache_read_complete = false;
        out->icache_read_ready = false;
        if (!runtime.pending_req_valid) {
          runtime.pend_on_retry_comb = true;
        }
        for (int j = i; j < FETCH_WIDTH; j++) {
          out->fetch_group[j] = INST_NOP;
          out->page_fault_inst[j] = false;
          out->inst_valid[j] = false;
        }
        break;
      }

      if (ret == TlbMmu::Result::FAULT) {
        out->perf_itlb_fault = true;
        out->page_fault_inst[i] = true;
        out->fetch_group[i] = INST_NOP;
        for (int j = i + 1; j < FETCH_WIDTH; j++) {
          out->fetch_group[j] = INST_NOP;
          out->page_fault_inst[j] = false;
          out->inst_valid[j] = false;
        }
        break;
      }

      out->page_fault_inst[i] = false;
      out->perf_itlb_hit = true;
      out->fetch_group[i] = pmem_read(p_addr);

      if (DEBUG_PRINT) {
        uint32_t satp =
            in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
        uint32_t privilege = in->csr_status
                                 ? static_cast<uint32_t>(in->csr_status->privilege)
                                 : 0;
        printf("[icache] vaddr: %08x -> paddr: %08x, inst: %08x, satp: %x, "
               "priv: %d\n",
               v_addr, p_addr, out->fetch_group[i], satp, privilege);
      }
    }

    if (out->icache_read_complete) {
      runtime.resp_fire_comb = true;
    }
    out->perf_resp_fire = out->icache_read_complete;
    out->perf_miss_event = runtime.pend_on_retry_comb;
    out->perf_miss_busy = runtime.pending_req_valid;
    out->perf_outstanding_req =
        out->perf_outstanding_req || runtime.pending_req_valid ||
        runtime.pend_on_retry_comb;
  }

  void set_ptw_mem_port(PtwMemPort *port) override {
    bind_ptw_mem_port(simple_icache_runtime(), port);
  }

  void set_ptw_walk_port(PtwWalkPort *port) override {
    bind_ptw_walk_port(simple_icache_runtime(), port);
  }

  void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) override {
    (void)port;
  }

  void seq() override {
    auto &runtime = simple_icache_runtime();
    if (in->reset) {
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
      runtime.pend_on_retry_comb = false;
      runtime.resp_fire_comb = false;
      if (runtime.mmu_model != nullptr) {
        runtime.mmu_model->seq();
      }
      return;
    }

    if (in->refetch) {
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
      runtime.pend_on_retry_comb = false;
      runtime.resp_fire_comb = false;
      if (runtime.mmu_model != nullptr) {
        runtime.mmu_model->seq();
      }
      return;
    }

    if (runtime.pend_on_retry_comb) {
      runtime.pending_req_valid = true;
      runtime.pending_fetch_addr = in->fetch_address;
    }

    if (runtime.resp_fire_comb) {
      runtime.pending_req_valid = false;
      runtime.pending_fetch_addr = 0;
    }
    if (runtime.mmu_model != nullptr) {
      runtime.mmu_model->seq();
    }
  }
};

} // namespace

ICacheTop *get_icache_instance() {
  static std::unique_ptr<ICacheTop> instance = nullptr;
  if (!instance) {
#ifdef USE_IDEAL_ICACHE
    instance = std::make_unique<SimpleICacheTop>();
#else
    instance = std::make_unique<
        TrueICacheTopT<icache_module_n::ICache, ExternalReadPortAdapter>>(icache);
#endif
  }
  return instance.get();
}

void icache_dump_debug_state() {
  if (auto *instance = get_icache_instance(); instance != nullptr) {
    instance->dump_debug_state();
  } else {
    std::printf("[DEADLOCK][FRONT][ICACHE_HW] no icache instance\n");
  }
}
