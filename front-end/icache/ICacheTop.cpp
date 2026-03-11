#include "include/ICacheTop.h"

#include "../front_module.h"
#include "../frontend.h"
#include "Csr.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include "RISCV.h"
#include "SimpleMmu.h"
#include "TlbMmu.h"
#include "config.h"
#include "include/icache_module.h"

constexpr int kFrontendIcacheMissLatency = ICACHE_MISS_LATENCY;

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
  uint32_t addr = 0;
  uint8_t total_size = 0;
  uint8_t id = 0;
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

#ifdef ICACHE_MISS_LATENCY
#undef ICACHE_MISS_LATENCY
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

extern uint32_t *p_memory;
extern icache_module_n::ICache icache;

namespace {

#if CONFIG_ICACHE_USE_AXI_MEM_PORT
static_assert(ICACHE_LINE_SIZE <= axi_interconnect::MAX_READ_TRANSACTION_BYTES,
              "ICACHE_LINE_SIZE exceeds AXI upstream read transaction limit");
#endif

uint32_t icache_coherent_read(uint32_t p_addr) { return p_memory[p_addr >> 2]; }

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
  bool resp_valid = false;
  uint8_t resp_id = 0;
  uint32_t resp_data[ICACHE_LINE_SIZE / 4] = {0};
};

class FixedLatencyReadPort {
public:
  static constexpr int kMaxTxId = 16;

  void bind(axi_interconnect::ReadMasterPort_t *port) { (void)port; }

  void reset() {
    valid_.fill(false);
    addr_.fill(0);
    age_.fill(0);
  }

  MemReadView comb_view() const {
    MemReadView view;
    view.req_ready = true;
    int matured_id = pick_matured_resp_id();
    if (matured_id >= 0) {
      view.resp_valid = true;
      view.resp_id = static_cast<uint8_t>(matured_id & 0xF);
      uint32_t base = addr_[matured_id] & ~(ICACHE_LINE_SIZE - 1u);
      for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
        view.resp_data[i] = p_memory[(base >> 2) + static_cast<uint32_t>(i)];
      }
    }
    return view;
  }

  void comb_accept(bool, uint32_t, uint8_t, bool) {}

  void seq(bool req_fire, uint32_t req_addr, uint8_t req_id, bool resp_fire,
           uint8_t resp_id, bool) {
    for (int id = 0; id < kMaxTxId; ++id) {
      if (valid_[id]) {
        age_[id]++;
      }
    }

    if (req_fire) {
      uint8_t id = static_cast<uint8_t>(req_id & 0xF);
      if (valid_[id]) {
        std::cout << "[icache_top] ERROR: duplicate mem txid " << std::dec
                  << static_cast<int>(id) << std::endl;
        std::exit(1);
      }
      valid_[id] = true;
      addr_[id] = req_addr & ~(ICACHE_LINE_SIZE - 1u);
      age_[id] = 0;
    }

    if (resp_fire) {
      uint8_t id = static_cast<uint8_t>(resp_id & 0xF);
      valid_[id] = false;
      addr_[id] = 0;
      age_[id] = 0;
    }
  }

private:
  std::array<bool, kMaxTxId> valid_{};
  std::array<uint32_t, kMaxTxId> addr_{};
  std::array<uint32_t, kMaxTxId> age_{};

  int pick_matured_resp_id() const {
    for (int id = 0; id < kMaxTxId; ++id) {
      if (valid_[id] &&
          age_[id] >= static_cast<uint32_t>(kFrontendIcacheMissLatency)) {
        return id;
      }
    }
    return -1;
  }
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
    view.resp_valid = port_->resp.valid;
    view.resp_id = static_cast<uint8_t>(port_->resp.id & 0xF);
    for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
      view.resp_data[i] = port_->resp.data[i];
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
    port_->resp.ready = resp_ready;
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
  AbstractMmu *mmu_model = nullptr;
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
  AbstractMmu *mmu_model = nullptr;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;
  axi_interconnect::ReadMasterPort_t *mem_read_port = nullptr;
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
#ifdef CONFIG_TLB_MMU
  runtime.mmu_model =
      new TlbMmu(ctx, runtime.ptw_mem_port ? runtime.ptw_mem_port : &ptw_port,
                 ITLB_ENTRIES);
  if (runtime.ptw_walk_port != nullptr) {
    runtime.mmu_model->set_ptw_walk_port(runtime.ptw_walk_port);
  }
#else
  runtime.mmu_model = new SimpleMmu(ctx, nullptr);
#endif
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

  void comb() override {
    clear_primary_outputs(out);
    clear_secondary_outputs(out);

    auto &runtime = true_icache_runtime<HW, ReadPort>();
    auto &read_port = read_port_runtime<ReadPort>();
    ensure_mmu_model(runtime, ctx);
    read_port.bind(runtime.mem_read_port);

    if (in->reset) {
      DEBUG_LOG("[icache] reset\n");
      icache_hw.reset();
      read_port.reset();
      if (runtime.mmu_model != nullptr) {
        runtime.mmu_model->flush();
      }
      out->icache_read_ready = true;
      return;
    }

    if (in->refetch && runtime.mmu_model != nullptr) {
      runtime.mmu_model->flush();
    }

    MemReadView mem = read_port.comb_view();
    const bool probe_only = in->run_comb_only;
    const bool req_valid = probe_only ? false : in->icache_read_valid;
    const uint32_t req_pc = probe_only ? 0u : in->fetch_address;

    icache_hw.io.in.refetch = in->refetch;
    icache_hw.io.in.flush = false;
    icache_hw.io.in.pc = req_pc;
    icache_hw.io.in.ifu_req_valid = req_valid;
    icache_hw.io.in.ifu_resp_ready = true;
    icache_hw.io.in.ppn = 0;
    icache_hw.io.in.ppn_valid = false;
    icache_hw.io.in.page_fault = false;
    icache_hw.io.in.mem_req_ready = mem.req_ready;
    icache_hw.io.in.mem_resp_valid = mem.resp_valid;
    icache_hw.io.in.mem_resp_id = mem.resp_id;
    for (int i = 0; i < ICACHE_LINE_SIZE / 4; ++i) {
      icache_hw.io.in.mem_resp_data[i] = mem.resp_data[i];
    }

    icache_hw.comb();

    if (!probe_only && !in->refetch && runtime.mmu_model != nullptr &&
        icache_hw.io.out.mmu_req_valid) {
      uint32_t p_addr = 0;
      uint32_t v_addr = icache_hw.io.out.mmu_req_vtag << 12;
      AbstractMmu::Result ret =
          runtime.mmu_model->translate(p_addr, v_addr, 0, in->csr_status);
      if (ret == AbstractMmu::Result::OK) {
        icache_hw.io.in.ppn = p_addr >> 12;
        icache_hw.io.in.ppn_valid = true;
      } else if (ret == AbstractMmu::Result::FAULT) {
        icache_hw.io.in.ppn_valid = true;
        icache_hw.io.in.page_fault = true;
      }
    }

    icache_hw.comb();

    read_port.comb_accept(
        icache_hw.io.out.mem_req_valid, icache_hw.io.out.mem_req_addr,
        static_cast<uint8_t>(icache_hw.io.out.mem_req_id & 0xF),
        icache_hw.io.out.mem_resp_ready);

    out->icache_read_ready = icache_hw.io.out.ifu_req_ready;

    bool resp_fire =
        icache_hw.io.out.ifu_resp_valid && icache_hw.io.in.ifu_resp_ready;
    if (resp_fire) {
      if (icache_hw.io.out.miss) {
        std::cout << "[icache_top] WARNING: miss is true when ifu_resp is valid"
                  << std::endl;
        std::exit(1);
      }
      out->icache_read_complete = true;
      out->fetch_pc = icache_hw.io.out.ifu_resp_pc;
      fill_fetch_group(icache_hw.io.out.ifu_resp_pc, icache_hw.io.out.rd_data,
                       icache_hw.io.out.ifu_page_fault, out->fetch_group,
                       out->page_fault_inst, out->inst_valid);
    }

    if (probe_only) {
      out->icache_read_complete = false;
    }
  }

  void seq() override {
    if (in->reset) {
      return;
    }

    bool req_fire = icache_hw.io.in.ifu_req_valid && icache_hw.io.out.ifu_req_ready;
    bool mem_req_fire =
        icache_hw.io.out.mem_req_valid && icache_hw.io.in.mem_req_ready;
    bool mem_resp_fire =
        icache_hw.io.in.mem_resp_valid && icache_hw.io.out.mem_resp_ready;

    icache_hw.seq();
    read_port_runtime<ReadPort>().seq(
        mem_req_fire, icache_hw.io.out.mem_req_addr,
        static_cast<uint8_t>(icache_hw.io.out.mem_req_id & 0xF), mem_resp_fire,
        static_cast<uint8_t>(icache_hw.io.in.mem_resp_id & 0xF), in->refetch);

    if (!in->refetch && req_fire) {
      access_delta++;
    }
    if (mem_req_fire) {
      miss_delta++;
    }
  }

private:
  HW &icache_hw;
};

class SimpleICacheTop : public ICacheTop {
public:
  void comb() override {
    out->icache_read_ready_2 = false;
    out->icache_read_complete_2 = false;
    out->fetch_pc_2 = 0;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group_2[i] = INST_NOP;
      out->page_fault_inst_2[i] = false;
      out->inst_valid_2[i] = false;
    }

    static IcacheBlockingPtwPort ptw_port;
    if (mmu_model == nullptr && ctx != nullptr) {
#ifdef CONFIG_TLB_MMU
      mmu_model =
          new TlbMmu(ctx, ptw_mem_port ? ptw_mem_port : &ptw_port, ITLB_ENTRIES);
      if (ptw_walk_port != nullptr) {
        mmu_model->set_ptw_walk_port(ptw_walk_port);
      }
#else
      mmu_model = new SimpleMmu(ctx, nullptr);
#endif
    }

    pend_on_retry_comb = false;
    resp_fire_comb = false;

    if (in->reset) {
      DEBUG_LOG("[icache] reset\n");
      if (mmu_model != nullptr) {
        mmu_model->flush();
      }
      satp_seen = false;
      pending_req_valid = false;
      pending_fetch_addr = 0;
      out->icache_read_ready = true;
      out->icache_read_complete = false;
      return;
    }

    out->icache_read_complete = false;
    out->icache_read_ready = !pending_req_valid;
    out->fetch_pc =
        pending_req_valid ? pending_fetch_addr : in->fetch_address;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }

    uint32_t cur_satp =
        in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
    if (!satp_seen || cur_satp != last_satp || in->refetch) {
      if (mmu_model != nullptr) {
        mmu_model->flush();
      }
      satp_seen = true;
      last_satp = cur_satp;
      pending_req_valid = false;
      pending_fetch_addr = 0;
      out->icache_read_ready = true;
    }

    if (in->run_comb_only) {
      return;
    }

    if (!pending_req_valid && !in->icache_read_valid) {
      return;
    }

    const uint32_t fetch_addr =
        pending_req_valid ? pending_fetch_addr : in->fetch_address;
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

      AbstractMmu::Result ret = AbstractMmu::Result::FAULT;
      if (mmu_model != nullptr) {
        ret = mmu_model->translate(p_addr, v_addr, 0, in->csr_status);
        for (int spin = 0; spin < 8 && ret == AbstractMmu::Result::RETRY;
             spin++) {
          ret = mmu_model->translate(p_addr, v_addr, 0, in->csr_status);
        }
      }

      if (ret == AbstractMmu::Result::RETRY) {
        out->icache_read_complete = false;
        out->icache_read_ready = false;
        if (!pending_req_valid) {
          pend_on_retry_comb = true;
        }
        for (int j = i; j < FETCH_WIDTH; j++) {
          out->fetch_group[j] = INST_NOP;
          out->page_fault_inst[j] = false;
          out->inst_valid[j] = false;
        }
        break;
      }

      if (ret == AbstractMmu::Result::FAULT) {
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
      out->fetch_group[i] = p_memory[p_addr / 4];

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
      resp_fire_comb = true;
    }
  }

  void set_ptw_mem_port(PtwMemPort *port) override {
    ptw_mem_port = port;
    if (mmu_model != nullptr) {
      mmu_model->set_ptw_mem_port(port);
    }
  }

  void set_ptw_walk_port(PtwWalkPort *port) override {
    ptw_walk_port = port;
    if (mmu_model != nullptr) {
      mmu_model->set_ptw_walk_port(port);
    }
  }

  void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) override {
    (void)port;
  }

  void seq() override {
    if (in->reset) {
      pending_req_valid = false;
      pending_fetch_addr = 0;
      pend_on_retry_comb = false;
      resp_fire_comb = false;
      return;
    }

    if (in->refetch) {
      pending_req_valid = false;
      pending_fetch_addr = 0;
      pend_on_retry_comb = false;
      resp_fire_comb = false;
      return;
    }

    if (pend_on_retry_comb) {
      pending_req_valid = true;
      pending_fetch_addr = in->fetch_address;
      access_delta++;
    }

    if (resp_fire_comb) {
      pending_req_valid = false;
      pending_fetch_addr = 0;
    }
  }

private:
  AbstractMmu *mmu_model = nullptr;
  uint32_t last_satp = 0;
  bool satp_seen = false;
  bool pending_req_valid = false;
  uint32_t pending_fetch_addr = 0;
  bool pend_on_retry_comb = false;
  bool resp_fire_comb = false;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;
};

} // namespace

void ICacheTop::syncPerf() {
  if (ctx) {
    ctx->perf.icache_access_num += access_delta;
    ctx->perf.icache_miss_num += miss_delta;
  }
  access_delta = 0;
  miss_delta = 0;
}

ICacheTop *get_icache_instance() {
  static std::unique_ptr<ICacheTop> instance = nullptr;
  if (!instance) {
#ifdef USE_IDEAL_ICACHE
    instance = std::make_unique<SimpleICacheTop>();
#else
#if CONFIG_ICACHE_USE_AXI_MEM_PORT
    instance = std::make_unique<
        TrueICacheTopT<icache_module_n::ICache, ExternalReadPortAdapter>>(icache);
#else
    instance = std::make_unique<
        TrueICacheTopT<icache_module_n::ICache, FixedLatencyReadPort>>(icache);
#endif
#endif
  }
  return instance.get();
}
