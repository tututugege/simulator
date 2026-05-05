#include "MemSubsystem.h"
#include "config.h"
#include "icache/GenericTable.h"
#include <assert.h>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

#if __has_include("UART16550_Device.h") &&                                        \
                  __has_include("AXI_Interconnect.h") &&                          \
                                __has_include("AXI_Router_AXI4.h") &&             \
                                              __has_include("MMIO_Bus_AXI4.h") && \
                                                            __has_include("SimDDR.h")
#define AXI_KIT_HEADERS_AVAILABLE 1
#include "AXI_Interconnect.h"
#include "AXI_Router_AXI4.h"
#include "MMIO_Bus_AXI4.h"
#include "SimDDR.h"
#include "UART16550_Device.h"
namespace {
using InterconnectImpl = axi_interconnect::AXI_Interconnect;
using DdrImpl = sim_ddr::SimDDR;
using RouterImpl = axi_interconnect::AXI_Router_AXI4;
using MmioImpl = mmio::MMIO_Bus_AXI4;
} // namespace
#else
#define AXI_KIT_HEADERS_AVAILABLE 0
#if CONFIG_ICACHE_USE_AXI_MEM_PORT
#error "CONFIG_ICACHE_USE_AXI_MEM_PORT requires axi-interconnect-kit"
#endif
#endif

#if AXI_KIT_HEADERS_AVAILABLE && CONFIG_ICACHE_USE_AXI_MEM_PORT
#define AXI_KIT_RUNTIME_ENABLED 1
#else
#define AXI_KIT_RUNTIME_ENABLED 0
#endif

#if AXI_KIT_RUNTIME_ENABLED
namespace {

struct AxiLlcTableRuntime {
  DynamicGenericTable<SramTablePolicy> data;
  DynamicGenericTable<SramTablePolicy> meta;
  DynamicGenericTable<SramTablePolicy> repl;
  axi_interconnect::AXI_LLC_LookupIn_t lookup_in{};
  axi_interconnect::AXI_LLCConfig config{};
  bool enabled = false;
  bool lookup_pending_valid = false;
  uint32_t lookup_pending_index = 0;
  uint32_t lookup_delay_left = 0;
  bool lookup_queued_valid = false;
  uint32_t lookup_queued_index = 0;

  static DynamicTableConfig make_table_config(uint32_t rows, uint32_t row_bytes,
                                              uint32_t latency) {
    DynamicTableConfig cfg;
    cfg.rows = rows;
    cfg.chunks = row_bytes;
    cfg.chunk_bits = 8;
    cfg.timing.fixed_latency = latency == 0 ? 1 : latency;
    cfg.timing.random_delay = false;
    return cfg;
  }

  static DynamicTableReadReq make_read_req(
      const axi_interconnect::AXI_LLC_TableReq_t &req) {
    DynamicTableReadReq read_req;
    read_req.enable = req.enable && !req.write;
    read_req.address = req.index;
    return read_req;
  }

  static DynamicTableWriteReq make_write_req(
      const axi_interconnect::AXI_LLC_TableReq_t &req, uint32_t row_bytes,
      uint32_t unit_bytes) {
    DynamicTableWriteReq write_req;
    write_req.enable = req.enable && req.write;
    write_req.address = req.index;
    write_req.payload.reset(row_bytes);
    write_req.chunk_enable.assign(row_bytes, 0);
    if (!write_req.enable) {
      return write_req;
    }
    const size_t base =
        unit_bytes == 0 ? 0 : static_cast<size_t>(req.way) * unit_bytes;
    const size_t copy_bytes =
        std::min(req.payload.size(), row_bytes > base ? row_bytes - base : 0u);
    if (copy_bytes == 0) {
      return write_req;
    }
    std::memcpy(write_req.payload.data() + base, req.payload.data(), copy_bytes);
    const size_t en_bytes = std::min(req.byte_enable.size(), copy_bytes);
    for (size_t i = 0; i < en_bytes; ++i) {
      write_req.chunk_enable[base + i] = req.byte_enable[i];
    }
    return write_req;
  }

  void configure(const axi_interconnect::AXI_LLCConfig &cfg) {
    config = cfg;
    enabled = cfg.enable && cfg.valid();
    lookup_in = {};
    lookup_pending_valid = false;
    lookup_pending_index = 0;
    lookup_delay_left = 0;
    lookup_queued_valid = false;
    lookup_queued_index = 0;
    if (!enabled) {
      return;
    }
    const uint32_t sets = cfg.set_count();
    data.configure(make_table_config(sets, cfg.ways * cfg.line_bytes,
                                     cfg.lookup_latency));
    meta.configure(make_table_config(
        sets, cfg.ways * axi_interconnect::AXI_LLC_META_ENTRY_BYTES,
        cfg.lookup_latency));
    repl.configure(make_table_config(sets, axi_interconnect::AXI_LLC_REPL_BYTES,
                                     cfg.lookup_latency));
    data.reset();
    meta.reset();
    repl.reset();
  }

  void comb_outputs() {
    lookup_in = {};
    if (!enabled) {
      return;
    }
    if (!lookup_pending_valid || lookup_delay_left != 0) {
      return;
    }

    DynamicTablePayload data_payload;
    DynamicTablePayload meta_payload;
    DynamicTablePayload repl_payload;
    lookup_in.data_valid =
        data.debug_read_row(lookup_pending_index, data_payload);
    lookup_in.meta_valid =
        meta.debug_read_row(lookup_pending_index, meta_payload);
    lookup_in.repl_valid =
        repl.debug_read_row(lookup_pending_index, repl_payload);
    lookup_in.data.bytes = data_payload.bytes;
    lookup_in.meta.bytes = meta_payload.bytes;
    lookup_in.repl.bytes = repl_payload.bytes;
  }

  void seq(const axi_interconnect::AXI_LLC_TableOut_t &table_out) {
    if (!enabled) {
      return;
    }
    if (table_out.invalidate_all) {
      data.reset();
      meta.reset();
      repl.reset();
      lookup_pending_valid = false;
      lookup_pending_index = 0;
      lookup_delay_left = 0;
      lookup_queued_valid = false;
      lookup_queued_index = 0;
      return;
    }
    const auto data_write =
        make_write_req(table_out.data, config.ways * config.line_bytes,
                       config.line_bytes);
    const auto meta_write = make_write_req(
        table_out.meta,
        config.ways * axi_interconnect::AXI_LLC_META_ENTRY_BYTES,
        axi_interconnect::AXI_LLC_META_ENTRY_BYTES);
    const auto repl_write =
        make_write_req(table_out.repl, axi_interconnect::AXI_LLC_REPL_BYTES, 0);

    data.seq({}, data_write);
    meta.seq({}, meta_write);
    repl.seq({}, repl_write);

    const bool data_read_en = table_out.data.enable && !table_out.data.write;
    const bool meta_read_en = table_out.meta.enable && !table_out.meta.write;
    const bool repl_read_en = table_out.repl.enable && !table_out.repl.write;
    const bool any_read_en = data_read_en || meta_read_en || repl_read_en;
    if (any_read_en) {
      assert(data_read_en && meta_read_en && repl_read_en);
      assert(table_out.data.index == table_out.meta.index);
      assert(table_out.data.index == table_out.repl.index);
    }

    if (lookup_pending_valid) {
      if (lookup_delay_left > 0) {
        lookup_delay_left--;
      } else {
        lookup_pending_valid = false;
        lookup_pending_index = 0;
      }
    }

    if (any_read_en) {
      const uint32_t req_index = table_out.data.index;
      if (!lookup_pending_valid && !lookup_queued_valid) {
        lookup_pending_valid = true;
        lookup_pending_index = req_index;
        lookup_delay_left = config.lookup_latency == 0 ? 0 : (config.lookup_latency - 1);
      } else if (!lookup_pending_valid) {
        lookup_pending_valid = true;
        lookup_pending_index = lookup_queued_index;
        lookup_delay_left = config.lookup_latency == 0 ? 0 : (config.lookup_latency - 1);
        lookup_queued_valid = false;
        lookup_queued_index = 0;
        lookup_queued_valid = true;
        lookup_queued_index = req_index;
      } else if (lookup_pending_index != req_index) {
        lookup_queued_valid = true;
        lookup_queued_index = req_index;
      }
    } else if (!lookup_pending_valid && lookup_queued_valid) {
      lookup_pending_valid = true;
      lookup_pending_index = lookup_queued_index;
      lookup_delay_left = config.lookup_latency == 0 ? 0 : (config.lookup_latency - 1);
      lookup_queued_valid = false;
      lookup_queued_index = 0;
    }
  }
};

} // namespace

struct AxiKitRuntime {
  InterconnectImpl interconnect;
  DdrImpl ddr;
  RouterImpl router;
  MmioImpl mmio;
  mmio::UART16550_Device uart0{0x10000000u};
  AxiLlcTableRuntime llc_tables;
  axi_interconnect::AXI_LLCPerfCounters_t llc_perf_snapshot{};
};
#else
struct AxiKitRuntime {};
#endif

class MemSubsystemPtwMemPortAdapter : public PtwMemPort {
public:
  bool send_read_req(uint32_t paddr) override {
    if (!comb_out_.req_ready || comb_in_.req_valid) {
      return false;
    }
    comb_in_.req_valid = true;
    comb_in_.req_addr = paddr;
    comb_out_.req_ready = false;
    comb_out_.resp_valid = false;
    comb_out_.resp_data = 0;
    return true;
  }

  bool resp_valid() const override { return comb_out_.resp_valid; }

  uint32_t resp_data() const override { return comb_out_.resp_data; }

  void consume_resp() override {
    comb_in_.resp_consumed = true;
    comb_out_.resp_valid = false;
    comb_out_.resp_data = 0;
  }

  const PtwMemPortCombIn &comb_input() const { return comb_in_; }

  void set_comb_output(const PtwMemPortCombOut &out) { comb_out_ = out; }

  void reset_cycle_input() { comb_in_ = {}; }

private:
  PtwMemPortCombIn comb_in_{};
  PtwMemPortCombOut comb_out_{};
};

class MemSubsystemPtwWalkPortAdapter : public PtwWalkPort {
public:
  bool send_walk_req(const PtwWalkReq &req) override {
    if (!comb_out_.req_ready || comb_in_.req_valid) {
      return false;
    }
    comb_in_.req_valid = true;
    comb_in_.req = req;
    comb_out_.req_ready = false;
    comb_out_.resp_valid = false;
    comb_out_.resp = {};
    return true;
  }

  bool resp_valid() const override { return comb_out_.resp_valid; }

  PtwWalkResp resp() const override { return comb_out_.resp; }

  void consume_resp() override {
    comb_in_.resp_consumed = true;
    comb_out_.resp_valid = false;
    comb_out_.resp = {};
  }

  void flush_client() override {
    seq_in_.flush = true;
    comb_out_.req_ready = true;
    comb_out_.resp_valid = false;
    comb_out_.resp = {};
  }

  const PtwWalkPortCombIn &comb_input() const { return comb_in_; }

  const PtwWalkPortSeqIn &seq_input() const { return seq_in_; }

  void set_comb_output(const PtwWalkPortCombOut &out) { comb_out_ = out; }

  void reset_cycle_input() { comb_in_ = {}; }

  void reset_seq_input() { seq_in_ = {}; }

private:
  PtwWalkPortCombIn comb_in_{};
  PtwWalkPortSeqIn seq_in_{};
  PtwWalkPortCombOut comb_out_{};
};

void MemSubsystem::sync_ptw_port_outputs() {
  const auto &ptw_out = ptw_block.comb_outputs();
  if (dtlb_ptw_port_inst != nullptr) {
    PtwMemPortCombOut out{};
    const auto &src = ptw_out.mem_clients[static_cast<size_t>(PtwClient::DTLB)];
    out.req_ready = src.req_ready;
    out.resp_valid = src.resp_valid;
    out.resp_data = src.resp_data;
    dtlb_ptw_port_inst->set_comb_output(out);
  }
  if (itlb_ptw_port_inst != nullptr) {
    PtwMemPortCombOut out{};
    const auto &src = ptw_out.mem_clients[static_cast<size_t>(PtwClient::ITLB)];
    out.req_ready = src.req_ready;
    out.resp_valid = src.resp_valid;
    out.resp_data = src.resp_data;
    itlb_ptw_port_inst->set_comb_output(out);
  }
  if (dtlb_walk_port_inst != nullptr) {
    PtwWalkPortCombOut out{};
    const auto &src =
        ptw_out.walk_clients[static_cast<size_t>(PtwClient::DTLB)];
    out.req_ready = src.req_ready;
    out.resp_valid = src.resp_valid;
    out.resp = src.resp;
    dtlb_walk_port_inst->set_comb_output(out);
  }
  if (itlb_walk_port_inst != nullptr) {
    PtwWalkPortCombOut out{};
    const auto &src =
        ptw_out.walk_clients[static_cast<size_t>(PtwClient::ITLB)];
    out.req_ready = src.req_ready;
    out.resp_valid = src.resp_valid;
    out.resp = src.resp;
    itlb_walk_port_inst->set_comb_output(out);
  }
}

void MemSubsystem::sync_llc_perf() {
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr) {
    return;
  }
  const auto &perf = axi_kit_runtime->llc_perf_snapshot;
  LlcPerfShadow current{};
  current.read_access = perf.read_access;
  current.read_hit = perf.read_hit;
  current.read_miss = perf.read_miss;
  current.read_access_icache = perf.read_access_by_master[axi_interconnect::MASTER_ICACHE];
  current.read_hit_icache = perf.read_hit_by_master[axi_interconnect::MASTER_ICACHE];
  current.read_miss_icache = perf.read_miss_by_master[axi_interconnect::MASTER_ICACHE];
  current.read_access_dcache = perf.read_access_by_master[axi_interconnect::MASTER_DCACHE_R];
  current.read_hit_dcache = perf.read_hit_by_master[axi_interconnect::MASTER_DCACHE_R];
  current.read_miss_dcache = perf.read_miss_by_master[axi_interconnect::MASTER_DCACHE_R];
  current.bypass_read = perf.bypass_read;
  current.write_passthrough = perf.write_passthrough;
  current.refill = perf.refill;
  current.mshr_alloc = perf.mshr_alloc;
  current.mshr_merge = perf.mshr_merge;
  current.prefetch_issue = perf.prefetch_issue;
  current.prefetch_hit = perf.prefetch_hit;
  current.prefetch_drop_inflight = perf.prefetch_drop_inflight;
  current.prefetch_drop_mshr_full = perf.prefetch_drop_mshr_full;
  current.prefetch_drop_queue_full = perf.prefetch_drop_queue_full;
  current.prefetch_drop_table_hit = perf.prefetch_drop_table_hit;
  current.ddr_read_total_cycles = perf.ddr_read_total_cycles;
  current.ddr_read_samples = perf.ddr_read_samples;
  current.ddr_write_total_cycles = perf.ddr_write_total_cycles;
  current.ddr_write_samples = perf.ddr_write_samples;

  if (!llc_perf_shadow_valid_) {
    llc_perf_shadow_ = current;
    llc_perf_shadow_valid_ = true;
    return;
  }

  auto sync_counter = [](uint64_t current_value, uint64_t &shadow_value,
                         uint64_t &perf_value) {
    perf_value +=
        current_value >= shadow_value ? current_value - shadow_value
                                      : current_value;
    shadow_value = current_value;
  };

  if (ctx != nullptr) {
    sync_counter(current.read_access, llc_perf_shadow_.read_access,
                 ctx->perf.llc_read_access);
    sync_counter(current.read_hit, llc_perf_shadow_.read_hit,
                 ctx->perf.llc_read_hit);
    sync_counter(current.read_miss, llc_perf_shadow_.read_miss,
                 ctx->perf.llc_read_miss);
    sync_counter(current.read_access_icache, llc_perf_shadow_.read_access_icache,
                 ctx->perf.llc_icache_read_access);
    sync_counter(current.read_hit_icache, llc_perf_shadow_.read_hit_icache,
                 ctx->perf.llc_icache_read_hit);
    sync_counter(current.read_miss_icache, llc_perf_shadow_.read_miss_icache,
                 ctx->perf.llc_icache_read_miss);
    sync_counter(current.read_access_dcache, llc_perf_shadow_.read_access_dcache,
                 ctx->perf.llc_dcache_read_access);
    sync_counter(current.read_hit_dcache, llc_perf_shadow_.read_hit_dcache,
                 ctx->perf.llc_dcache_read_hit);
    sync_counter(current.read_miss_dcache, llc_perf_shadow_.read_miss_dcache,
                 ctx->perf.llc_dcache_read_miss);
    sync_counter(current.bypass_read, llc_perf_shadow_.bypass_read,
                 ctx->perf.llc_bypass_read);
    sync_counter(current.write_passthrough, llc_perf_shadow_.write_passthrough,
                 ctx->perf.llc_write_passthrough);
    sync_counter(current.refill, llc_perf_shadow_.refill, ctx->perf.llc_refill);
    sync_counter(current.mshr_alloc, llc_perf_shadow_.mshr_alloc,
                 ctx->perf.llc_mshr_alloc);
    sync_counter(current.mshr_merge, llc_perf_shadow_.mshr_merge,
                 ctx->perf.llc_mshr_merge);
    sync_counter(current.prefetch_issue, llc_perf_shadow_.prefetch_issue,
                 ctx->perf.llc_prefetch_issue);
    sync_counter(current.prefetch_hit, llc_perf_shadow_.prefetch_hit,
                 ctx->perf.llc_prefetch_hit);
    sync_counter(current.prefetch_drop_inflight,
                 llc_perf_shadow_.prefetch_drop_inflight,
                 ctx->perf.llc_prefetch_drop_inflight);
    sync_counter(current.prefetch_drop_mshr_full,
                 llc_perf_shadow_.prefetch_drop_mshr_full,
                 ctx->perf.llc_prefetch_drop_mshr_full);
    sync_counter(current.prefetch_drop_queue_full,
                 llc_perf_shadow_.prefetch_drop_queue_full,
                 ctx->perf.llc_prefetch_drop_queue_full);
    sync_counter(current.prefetch_drop_table_hit,
                 llc_perf_shadow_.prefetch_drop_table_hit,
                 ctx->perf.llc_prefetch_drop_table_hit);
    sync_counter(current.ddr_read_total_cycles,
                 llc_perf_shadow_.ddr_read_total_cycles,
                 ctx->perf.llc_ddr_read_total_cycles);
    sync_counter(current.ddr_read_samples, llc_perf_shadow_.ddr_read_samples,
                 ctx->perf.llc_ddr_read_samples);
    sync_counter(current.ddr_write_total_cycles,
                 llc_perf_shadow_.ddr_write_total_cycles,
                 ctx->perf.llc_ddr_write_total_cycles);
    sync_counter(current.ddr_write_samples, llc_perf_shadow_.ddr_write_samples,
                 ctx->perf.llc_ddr_write_samples);
  }
#else
  return;
#endif
}

void MemSubsystem::set_internal_axi_runtime_active(bool active) {
#if AXI_KIT_RUNTIME_ENABLED
  internal_axi_runtime_active_ = active;
#else
  (void)active;
  internal_axi_runtime_active_ = false;
#endif
}

void MemSubsystem::set_llc_config(const axi_interconnect::AXI_LLCConfig &cfg) {
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr) {
    return;
  }
  axi_kit_runtime->interconnect.set_llc_config(cfg);
  axi_kit_runtime->llc_tables.configure(cfg);
  axi_kit_runtime->llc_perf_snapshot = {};
  llc_perf_shadow_ = {};
  llc_perf_shadow_valid_ = false;
#else
  (void)cfg;
#endif
}

void MemSubsystem::llc_comb_outputs() {
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr) {
    return;
  }
  axi_kit_runtime->llc_tables.comb_outputs();
#endif
}

const axi_interconnect::AXI_LLC_LookupIn_t &MemSubsystem::llc_lookup_in() const {
  static const axi_interconnect::AXI_LLC_LookupIn_t kEmpty{};
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr) {
    return kEmpty;
  }
  return axi_kit_runtime->llc_tables.lookup_in;
#else
  return kEmpty;
#endif
}

void MemSubsystem::llc_seq(
    const axi_interconnect::AXI_LLC_TableOut_t &table_out,
    const axi_interconnect::AXI_LLCPerfCounters_t &perf) {
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr) {
    return;
  }
  axi_kit_runtime->llc_tables.seq(table_out);
  axi_kit_runtime->llc_perf_snapshot = perf;
  sync_llc_perf();
#else
  (void)table_out;
  (void)perf;
#endif
}

axi_interconnect::ReadMasterPort_t *MemSubsystem::icache_read_port() {
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime == nullptr || !internal_axi_runtime_active_) {
    return nullptr;
  }
  return &axi_kit_runtime->interconnect.read_ports[axi_interconnect::MASTER_ICACHE];
#else
  return nullptr;
#endif
}

MemSubsystem::MemSubsystem(SimContext *ctx) : ctx(ctx) {
#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime = std::make_unique<AxiKitRuntime>();
  internal_axi_runtime_active_ = true;
#else
  internal_axi_runtime_active_ = false;
#endif
  ptw_block.bind_context(ctx);
  dtlb_ptw_port_inst = std::make_unique<MemSubsystemPtwMemPortAdapter>();
  itlb_ptw_port_inst = std::make_unique<MemSubsystemPtwMemPortAdapter>();
  dtlb_walk_port_inst = std::make_unique<MemSubsystemPtwWalkPortAdapter>();
  itlb_walk_port_inst = std::make_unique<MemSubsystemPtwWalkPortAdapter>();
  dtlb_ptw_port = dtlb_ptw_port_inst.get();
  itlb_ptw_port = itlb_ptw_port_inst.get();
  dtlb_walk_port = dtlb_walk_port_inst.get();
  itlb_walk_port = itlb_walk_port_inst.get();
}

MemSubsystem::~MemSubsystem() = default;

void MemSubsystem::init() {
  Assert(peripheral_req != nullptr && "MemSubsystem: peripheral_req is not connected");
  Assert(peripheral_resp != nullptr && "MemSubsystem: peripheral_resp is not connected");
  Assert(csr != nullptr && "MemSubsystem: csr is not connected");
  Assert(memory != nullptr && "MemSubsystem: memory is not connected");

  // Route LSU/PTW/ICache requests through MemRouteBlock and capture raw
  // DCache responses before routing them back to their owners.
  dcache_.in.lsu2dcache = &dcache_req_mux_;
  dcache_.out.dcache2lsu = &dcache_resp_raw_;

  // Internal MSHR ↔ DCache wires: RealDcache reads/writes MSHR IO structs
  // directly via pointers, keeping the connection zero-copy.
  dcache_.in.mshr2dcache = &mshr_dcache_io_;  // MSHR output → DCache input
  dcache_.out.dcache2mshr = &dcache_mshr_io_; // DCache output → MSHR input
  mshr_.in.dcache2mshr = &dcache_mshr_io_;    // DCache output → MSHR comb input
  mshr_.out.mshr2dcache = &mshr_dcache_io_;   // MSHR output → DCache input

  mshr_.in.axi_in = &mshr_axi_in;    // MSHR comb input ← AXI read/write ports
  mshr_.out.axi_out = &mshr_axi_out; // MSHR output → AXI read/write ports
  wb_.in.axi_in = &wb_axi_in;        // WB comb input ← AXI read/write ports (for write response handling)
  wb_.out.axi_out = &wb_axi_out;     // WB output → AXI read/write ports (for write issuance)

  // Internal WriteBuffer ↔ DCache wires.
  dcache_.in.wb2dcache = &wb_dcache_io_;  // WB output → DCache input
  dcache_.out.dcache2wb = &dcache_wb_io_; // DCache output → WB input
  wb_.in.dcache2wb = &dcache_wb_io_;      // DCache output → WB comb input
  wb_.out.wb2dcache = &wb_dcache_io_;     // WB output → DCache inputs

  for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
    dcache_.in.dcachelinereadresp[i] = &dcache_line_read_resp_[i]; // DCache line read response → LSU
    dcache_.out.dcachereadreq[i] = &dcache_line_read_req_[i];      // DCache line read request ← LSU
    dcache_.out.lru_updates[i] = &lru_updates_[i];                 // DCache line write request ← LSU
    dcache_.out.pendingwrite[i] = &pending_writes_[i];             // DCache pending write flag ← LSU
  }

  dcache_.out.fill_write = &fill_writes_; // DCache fill write-back flag ← LSU
  dcache_.in.fillin = &fill_in_;          // DCache fill input (from MSHR/WB)
  dcache_.out.fillout = &fill_out_;       // DCache fill output (to MSHR/WB)

  mem_route_block.out.dcache_req = &dcache_req_mux_;  // MemRouteBlock output → DCache request multiplexer
  mem_route_block.in.dcache_resp = &dcache_resp_raw_; // LSU request → MemRouteBlock input

  mem_route_block.in.lsu_req = &lsu2dcache->req_ports;
  mem_route_block.out.lsu_resp = dcache2lsu;

  mem_route_block.in.icache_req = icache_req;
  mem_route_block.out.icache_resp = icache_resp;

  mem_route_block.in.ptw_walk_req = &ptw_walk_req; // PTW walk request → MemRouteBlock input
  mem_route_block.in.ptw_dtlb_req = &ptw_dtlb_req; // MemRouteBlock output → PTW walk response
  mem_route_block.in.ptw_itlb_req = &ptw_itlb_req; // MemRouteBlock output → PTW walk response

  mem_route_block.out.ptw_events = &ptw_events; // MemRouteBlock output → PTW event signals
  mem_route_block.out.ptw_grant = &ptw_grant;   // MemRouteBlock output → PTW grant signals
  mem_route_block.out.wakeup = &wakeup;         // MemRouteBlock output → LSU wakeup signals

  wb_.bind_context(ctx);

  // ── Initialise sub-modules ─────────────────────────────────────────────────
  mshr_.init();
  wb_.init();
  dcache_.init();
  peripheral_model_.bind(csr, memory);
  peripheral_axi_.peripheral_req = peripheral_req;
  peripheral_axi_.peripheral_resp = peripheral_resp;
  peripheral_axi_.peripheral_model = &peripheral_model_;
  peripheral_axi_.init();

  ptw_block.init();
  mem_route_block.init();
  sync_ptw_port_outputs();
  dtlb_ptw_port_inst->reset_cycle_input();
  itlb_ptw_port_inst->reset_cycle_input();
  dtlb_walk_port_inst->reset_cycle_input();
  itlb_walk_port_inst->reset_cycle_input();
  dtlb_walk_port_inst->reset_seq_input();
  itlb_walk_port_inst->reset_seq_input();

  dcache_req_mux_ = {};
  dcache_resp_raw_ = {};

  mshr_dcache_io_ = {};
  dcache_mshr_io_ = {};
  wb_dcache_io_ = {};
  dcache_wb_io_ = {};

  memset(dcache_line_read_req_, 0, sizeof(dcache_line_read_req_));
  memset(dcache_line_read_resp_, 0, sizeof(dcache_line_read_resp_));
  memset(lru_updates_, 0, sizeof(lru_updates_));
  memset(pending_writes_, 0, sizeof(pending_writes_));
  fill_writes_ = {};
  fill_in_ = {};
  fill_out_ = {};

  mshr_axi_in = {};
  mshr_axi_out = {};
  wb_axi_in = {};
  wb_axi_out = {};
  peripheral_axi_read_in = {};
  peripheral_axi_read_out = {};
  peripheral_axi_write_in = {};
  peripheral_axi_write_out = {};

#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime->interconnect.init();
  {
    axi_interconnect::AXI_LLCConfig llc_cfg;
    llc_cfg.enable = (CONFIG_AXI_LLC_ENABLE != 0);
    llc_cfg.size_bytes = CONFIG_AXI_LLC_SIZE_BYTES;
    llc_cfg.ways = CONFIG_AXI_LLC_WAYS;
    llc_cfg.mshr_num = CONFIG_AXI_LLC_MSHR_NUM;
    llc_cfg.lookup_latency = CONFIG_AXI_LLC_LOOKUP_LATENCY;
    set_llc_config(llc_cfg);
  }
  axi_kit_runtime->router.init();
  axi_kit_runtime->mmio.init();
  axi_kit_runtime->mmio.add_device(UART_ADDR_BASE, UART_MMIO_SIZE,
                                   &axi_kit_runtime->uart0);
  axi_kit_runtime->ddr.init();
  static bool printed_axi_cfg = false;
  if (!printed_axi_cfg) {
    printed_axi_cfg = true;
    printf(
        "[CONFIG][AXI] ddr_read_latency=%ucy ddr_write_resp_latency=%ucy "
        "ddr_beat=%uB wq=%u wag=%ucy wfifo=%u wdrain=%ucy whi=%u wlo=%u "
        "r2w=%ucy w2r=%ucy "
        "out=%u per_master=%u ddr_out=%u "
        "dcache_line=%uB(%u words) "
        "upstream_write_payload=%uB upstream_read_resp=%uB\n",
        static_cast<unsigned>(sim_ddr::SIM_DDR_LATENCY),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_RESP_LATENCY),
        static_cast<unsigned>(sim_ddr::SIM_DDR_BEAT_BYTES),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_QUEUE_DEPTH),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_ACCEPT_GAP),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_DATA_FIFO_DEPTH),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_DRAIN_GAP),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_DRAIN_HIGH_WATERMARK),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_DRAIN_LOW_WATERMARK),
        static_cast<unsigned>(sim_ddr::SIM_DDR_READ_TO_WRITE_TURNAROUND),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_TO_READ_TURNAROUND),
        static_cast<unsigned>(axi_interconnect::MAX_OUTSTANDING),
        static_cast<unsigned>(axi_interconnect::MAX_READ_OUTSTANDING_PER_MASTER),
        static_cast<unsigned>(sim_ddr::SIM_DDR_MAX_OUTSTANDING),
        static_cast<unsigned>(DCACHE_LINE_SIZE),
        static_cast<unsigned>(DCACHE_WORD_NUM),
        static_cast<unsigned>(axi_interconnect::AXI_UPSTREAM_PAYLOAD_BYTES),
        static_cast<unsigned>(axi_interconnect::MAX_READ_TRANSACTION_BYTES));
    if (DCACHE_WORD_NUM > axi_interconnect::CACHELINE_WORDS) {
      printf(
          "[MEM][AXI CFG][WARN] dcache line (%u words) is wider than AXI upstream "
          "write payload (%u words). High words may be dropped in bridge path.\n",
          static_cast<unsigned>(DCACHE_WORD_NUM),
          static_cast<unsigned>(axi_interconnect::CACHELINE_WORDS));
    }
  }
#endif

#if AXI_KIT_RUNTIME_ENABLED
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; i++) {
    auto &port = axi_kit_runtime->interconnect.read_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.resp.ready = false;
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; i++) {
    auto &port = axi_kit_runtime->interconnect.write_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.wdata.clear();
    port.req.wstrb = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.resp.ready = false;
  }
#endif
}

void MemSubsystem::on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3) {
  peripheral_model_.on_commit_store(paddr, data, func3);
}

void MemSubsystem::sync_mmio_devices_from_backing() {
#if AXI_KIT_RUNTIME_ENABLED
  if (memory != nullptr && axi_kit_runtime != nullptr) {
    axi_kit_runtime->uart0.sync_from_backing(memory);
  }
#endif
}

void MemSubsystem::dump_debug_state(FILE *out) const {
  if (out == nullptr) {
    return;
  }
  ptw_block.dump_debug_state(out);
  mem_route_block.dump_debug_state(out);
  mshr_.dump_debug_state(out);
  wb_.dump_debug_state(out);
  dcache_.dump_debug_state(out);
}

void MemSubsystem::comb() {
#if AXI_KIT_RUNTIME_ENABLED
  if (internal_axi_runtime_active_) {
    auto &interconnect = axi_kit_runtime->interconnect;
    auto &ddr = axi_kit_runtime->ddr;
    auto &router = axi_kit_runtime->router;
    auto &mmio = axi_kit_runtime->mmio;

    // Internal icache-only runtime is optional. When shared LLC is driven by
    // the top-level interconnect, keep this path idle and let SimCpu feed the
    // LLC tables/perf shadow directly.
    llc_comb_outputs();
    interconnect.set_llc_lookup_in(llc_lookup_in());
    ddr.comb_outputs();
    mmio.comb_outputs();
    router.comb_outputs(interconnect.axi_io, ddr.io, mmio.io);
    interconnect.comb_outputs();
  }
#endif

  MemPtwBlock::PortIn ptw_port_in{};
  {
    const auto &dtlb_mem_in = dtlb_ptw_port_inst->comb_input();
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::DTLB)].req_valid =
        dtlb_mem_in.req_valid;
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::DTLB)].req_addr =
        dtlb_mem_in.req_addr;
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::DTLB)]
        .resp_consumed = dtlb_mem_in.resp_consumed;

    const auto &itlb_mem_in = itlb_ptw_port_inst->comb_input();
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::ITLB)].req_valid =
        itlb_mem_in.req_valid;
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::ITLB)].req_addr =
        itlb_mem_in.req_addr;
    ptw_port_in.mem_clients[static_cast<size_t>(PtwClient::ITLB)]
        .resp_consumed = itlb_mem_in.resp_consumed;

    const auto &dtlb_walk_in = dtlb_walk_port_inst->comb_input();
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::DTLB)].req_valid =
        dtlb_walk_in.req_valid;
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::DTLB)].req =
        dtlb_walk_in.req;
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::DTLB)]
        .resp_consumed = dtlb_walk_in.resp_consumed;

    const auto &itlb_walk_in = itlb_walk_port_inst->comb_input();
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::ITLB)].req_valid =
        itlb_walk_in.req_valid;
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::ITLB)].req =
        itlb_walk_in.req;
    ptw_port_in.walk_clients[static_cast<size_t>(PtwClient::ITLB)]
        .resp_consumed = itlb_walk_in.resp_consumed;
  }
  ptw_block.comb_begin(ptw_port_in);
  const auto &ptw_out = ptw_block.comb_outputs();

  ptw_walk_req.valid = ptw_out.issue_walk_read;
  ptw_walk_req.addr = ptw_out.walk_read_addr;

  ptw_dtlb_req.valid =
      ptw_out.mem_req_pending[static_cast<size_t>(PtwClient::DTLB)];
  ptw_dtlb_req.addr =
      ptw_out.mem_req_addr[static_cast<size_t>(PtwClient::DTLB)];

  ptw_itlb_req.valid =
      ptw_out.mem_req_pending[static_cast<size_t>(PtwClient::ITLB)];
  ptw_itlb_req.addr =
      ptw_out.mem_req_addr[static_cast<size_t>(PtwClient::ITLB)];

  wb_.comb_outputs_dcache();
  wb_.comb_outputs_axi();

  mshr_.comb_outputs_dcache();
  mshr_.comb_outputs_axi();
  mem_route_block.comb_request();

  Dcache_Read(dcache_line_read_req_,
              dcache_line_read_resp_,
              fill_out_,
              fill_in_);

  dcache_.stage2_comb();
  Dcache_Write(pending_writes_,
               lru_updates_,
               fill_writes_);

  mem_route_block.comb_response();
  dcache_.stage1_comb();

  MemPtwBlock::FeedbackIn ptw_feedback{};

  ptw_feedback.wakeup_dtlb = wakeup.dtlb;
  ptw_feedback.wakeup_itlb = wakeup.itlb;
  ptw_feedback.wakeup_walk = wakeup.walk;

  // grant 转换
  if (ptw_grant.valid) {
    ptw_feedback.grant_valid = true;
    ptw_feedback.grant_req_id = ptw_grant.req_id;

    switch (ptw_grant.owner) {
    case Owner::PTW_DTLB:
      ptw_feedback.grant_owner = MemPtwBlock::GrantOwner::MEM_DTLB;
      break;
    case Owner::PTW_ITLB:
      ptw_feedback.grant_owner = MemPtwBlock::GrantOwner::MEM_ITLB;
      break;
    case Owner::PTW_WALK:
      ptw_feedback.grant_owner = MemPtwBlock::GrantOwner::WALK;
      break;
    default:
      ptw_feedback.grant_valid = false;
      break;
    }
  }

  // 单个 ptw_event 转换
  if (ptw_events.valid) {
    const auto &evt = ptw_events;

    MemPtwBlock::RoutedEvent mapped{};
    mapped.valid = true;
    mapped.data = evt.data;
    mapped.replay = evt.replay;
    mapped.req_addr = evt.req_addr;
    mapped.req_id = evt.req_id;

    switch (evt.owner) {
    case Owner::PTW_DTLB:
      mapped.owner = MemPtwBlock::RoutedEventOwner::MEM_DTLB;
      break;
    case Owner::PTW_ITLB:
      mapped.owner = MemPtwBlock::RoutedEventOwner::MEM_ITLB;
      break;
    case Owner::PTW_WALK:
      mapped.owner = MemPtwBlock::RoutedEventOwner::WALK;
      break;
    default:
      mapped.valid = false;
      break;
    }

    if (mapped.valid) {
      ptw_feedback.events[0] = mapped;
      ptw_feedback.event_count = 1;
    }
  }

  ptw_block.comb_finish(ptw_feedback);

  sync_ptw_port_outputs();

  // Phase 3a: run MSHR comb_inputs (may accept AXI R, allocate entries, and
  // prepare next-cycle registered fill / eviction outputs).
  mshr_.comb_inputs_dcache();
  mshr_.comb_inputs_axi();

  wb_.comb_inputs_dcache();
  wb_.comb_inputs_axi();

  peripheral_axi_.in.read = peripheral_axi_read_in;
  peripheral_axi_.in.write = peripheral_axi_write_in;
  peripheral_axi_.comb_outputs();
  peripheral_axi_.comb_inputs();

  peripheral_axi_read_out = peripheral_axi_.out.read;
  peripheral_axi_write_out = peripheral_axi_.out.write;

  // Stage-1 AXI wiring: connect ICache read master, keep all others idle.
#if AXI_KIT_RUNTIME_ENABLED
  if (internal_axi_runtime_active_) {
    auto &interconnect = axi_kit_runtime->interconnect;
    auto &ddr = axi_kit_runtime->ddr;
    auto &router = axi_kit_runtime->router;
    auto &mmio = axi_kit_runtime->mmio;

    for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; i++) {
      if (i == axi_interconnect::MASTER_ICACHE) {
        continue;
      }
      auto &port = interconnect.read_ports[i];
      port.req.valid = false;
      port.req.addr = 0;
      port.req.total_size = 0;
      port.req.id = 0;
      port.resp.ready = false;
    }
    for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; i++) {
      auto &port = interconnect.write_ports[i];
      port.req.valid = false;
      port.req.addr = 0;
      port.req.wdata.clear();
      port.req.wstrb = 0;
      port.req.total_size = 0;
      port.req.id = 0;
      port.resp.ready = false;
    }

    // AXI-kit phase-2 combinational inputs.
    interconnect.comb_inputs();
    router.comb_inputs(interconnect.axi_io, ddr.io, mmio.io);
    ddr.comb_inputs();
    mmio.comb_inputs();
  }
#endif

  sync_ptw_port_outputs();
  dtlb_ptw_port_inst->reset_cycle_input();
  itlb_ptw_port_inst->reset_cycle_input();
  dtlb_walk_port_inst->reset_cycle_input();
  itlb_walk_port_inst->reset_cycle_input();
}

void MemSubsystem::seq() {
  MemPtwBlock::SeqIn ptw_seq_in{};
  ptw_seq_in.walk_client_flush[static_cast<size_t>(PtwClient::DTLB)] =
      dtlb_walk_port_inst->seq_input().flush;
  ptw_seq_in.walk_client_flush[static_cast<size_t>(PtwClient::ITLB)] =
      itlb_walk_port_inst->seq_input().flush;
  ptw_block.seq(ptw_seq_in);
  dcache_.seq();
  mshr_.seq();
  wb_.seq();
  peripheral_axi_.seq();
  mem_route_block.seq();
#if AXI_KIT_RUNTIME_ENABLED
  if (internal_axi_runtime_active_) {
    axi_kit_runtime->ddr.seq();
    axi_kit_runtime->mmio.seq();
    axi_kit_runtime->router.seq(axi_kit_runtime->interconnect.axi_io,
                                axi_kit_runtime->ddr.io,
                                axi_kit_runtime->mmio.io);
    llc_seq(axi_kit_runtime->interconnect.get_llc_table_out(),
            axi_kit_runtime->interconnect.get_llc_perf_counters());
    axi_kit_runtime->interconnect.seq();
  }
#endif
  dtlb_walk_port_inst->reset_seq_input();
  itlb_walk_port_inst->reset_seq_input();
  sync_ptw_port_outputs();
}
