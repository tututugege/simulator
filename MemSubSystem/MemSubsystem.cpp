#include "MemSubsystem.h"
#include "config.h"
#include "icache/GenericTable.h"
#include <cinttypes>
#include <memory>
#include <cstdio>
#include <assert.h>
#include <cstring>

#if __has_include("UART16550_Device.h") && \
    __has_include("AXI_Interconnect.h") && \
    __has_include("AXI_Router_AXI4.h") && \
    __has_include("MMIO_Bus_AXI4.h") && \
    __has_include("SimDDR.h")
#define AXI_KIT_HEADERS_AVAILABLE 1
#include "UART16550_Device.h"
#include "AXI_Interconnect.h"
#include "AXI_Router_AXI4.h"
#include "MMIO_Bus_AXI4.h"
#include "SimDDR.h"
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
  MemSubsystemPtwMemPortAdapter(MemSubsystem *owner, MemSubsystem::PtwClient c)
      : owner(owner), client(c) {}

  bool send_read_req(uint32_t paddr) override {
    return owner->ptw_mem_send_read_req(client, paddr);
  }
  bool resp_valid() const override { return owner->ptw_mem_resp_valid(client); }
  uint32_t resp_data() const override { return owner->ptw_mem_resp_data(client); }
  void consume_resp() override { owner->ptw_mem_consume_resp(client); }

private:
  MemSubsystem *owner = nullptr;
  MemSubsystem::PtwClient client = MemSubsystem::PtwClient::DTLB;
};

class MemSubsystemPtwWalkPortAdapter : public PtwWalkPort {
public:
  MemSubsystemPtwWalkPortAdapter(MemSubsystem *owner, MemSubsystem::PtwClient c)
      : owner(owner), client(c) {}

  bool send_walk_req(const PtwWalkReq &req) override {
    return owner->ptw_walk_send_req(client, req);
  }
  bool resp_valid() const override { return owner->ptw_walk_resp_valid(client); }
  PtwWalkResp resp() const override { return owner->ptw_walk_resp(client); }
  void consume_resp() override { owner->ptw_walk_consume_resp(client); }
  void flush_client() override { owner->ptw_walk_flush(client); }

private:
  MemSubsystem *owner = nullptr;
  MemSubsystem::PtwClient client = MemSubsystem::PtwClient::DTLB;
};

MemPtwBlock::Client MemSubsystem::to_block_client(PtwClient c) {
  return (c == MemSubsystem::PtwClient::DTLB) ? MemPtwBlock::Client::DTLB
                                               : MemPtwBlock::Client::ITLB;
}

void MemSubsystem::refresh_ptw_client_outputs() {
  for (size_t i = 0; i < kPtwClientCount; i++) {
    PtwClient client = static_cast<PtwClient>(i);
    ptw_mem_resp_ios[i].valid =
        ptw_block.client_resp_valid(to_block_client(client));
    ptw_mem_resp_ios[i].data =
        ptw_block.client_resp_data(to_block_client(client));
    ptw_walk_resp_ios[i].valid =
        ptw_block.walk_client_resp_valid(to_block_client(client));
    ptw_walk_resp_ios[i].resp = ptw_block.walk_client_resp(to_block_client(client));
  }
}

bool MemSubsystem::ptw_mem_send_read_req(PtwClient client, uint32_t paddr) {
  auto block_client = to_block_client(client);
  bool fire = ptw_block.client_send_read_req(block_client, paddr);
  refresh_ptw_client_outputs();
  return fire;
}

bool MemSubsystem::ptw_mem_resp_valid(PtwClient client) const {
  return ptw_mem_resp_ios[ptw_client_idx(client)].valid;
}

uint32_t MemSubsystem::ptw_mem_resp_data(PtwClient client) const {
  return ptw_mem_resp_ios[ptw_client_idx(client)].data;
}

void MemSubsystem::ptw_mem_consume_resp(PtwClient client) {
  ptw_block.client_consume_resp(to_block_client(client));
  refresh_ptw_client_outputs();
}

bool MemSubsystem::ptw_walk_send_req(PtwClient client, const PtwWalkReq &req) {
  auto block_client = to_block_client(client);
  bool fire = ptw_block.walk_client_send_req(block_client, req);
  refresh_ptw_client_outputs();
  return fire;
}

bool MemSubsystem::ptw_walk_resp_valid(PtwClient client) const {
  return ptw_walk_resp_ios[ptw_client_idx(client)].valid;
}

PtwWalkResp MemSubsystem::ptw_walk_resp(PtwClient client) const {
  return ptw_walk_resp_ios[ptw_client_idx(client)].resp;
}

void MemSubsystem::ptw_walk_consume_resp(PtwClient client) {
  ptw_block.walk_client_consume_resp(to_block_client(client));
  refresh_ptw_client_outputs();
}

void MemSubsystem::ptw_walk_flush(PtwClient client) {
  ptw_block.walk_client_flush(to_block_client(client));
  refresh_ptw_client_outputs();
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
  dtlb_ptw_port_inst =
      std::make_unique<MemSubsystemPtwMemPortAdapter>(this, PtwClient::DTLB);
  itlb_ptw_port_inst =
      std::make_unique<MemSubsystemPtwMemPortAdapter>(this, PtwClient::ITLB);
  dtlb_walk_port_inst =
      std::make_unique<MemSubsystemPtwWalkPortAdapter>(this, PtwClient::DTLB);
  itlb_walk_port_inst =
      std::make_unique<MemSubsystemPtwWalkPortAdapter>(this, PtwClient::ITLB);
  dtlb_ptw_port = dtlb_ptw_port_inst.get();
  itlb_ptw_port = itlb_ptw_port_inst.get();
  dtlb_walk_port = dtlb_walk_port_inst.get();
  itlb_walk_port = itlb_walk_port_inst.get();
}

MemSubsystem::~MemSubsystem() = default;

void MemSubsystem::init() {
  Assert(lsu2dcache != nullptr && "MemSubsystem: lsu2dcache is not connected");
  Assert(dcache2lsu  != nullptr && "MemSubsystem: dcache2lsu is not connected");
  Assert(peripheral_io != nullptr && "MemSubsystem: peripheral_io is not connected");
  Assert(csr    != nullptr && "MemSubsystem: csr is not connected");
  Assert(memory != nullptr && "MemSubsystem: memory is not connected");
  
  // Route LSU requests through MemReadArbBlock so PTW reads can be injected,
  // and capture raw DCache responses before routing them back to LSU/PTW.
  dcache_.lsu2dcache  = &dcache_req_mux_;
  dcache_.dcache2lsu  = &dcache_resp_raw_;

  // Internal MSHR ↔ DCache wires: RealDcache reads/writes MSHR IO structs
  // directly via pointers, keeping the connection zero-copy.
  dcache_.mshr2dcache = &mshr_.out.mshr2dcache;  // MSHR output → DCache input
  dcache_.dcache2mshr = &mshr_.in.dcachemshr;    // DCache output → MSHR input

  // Internal WriteBuffer ↔ DCache wires.
  dcache_.wb2dcache   = &wb_.out.wbdcache;       // WB output → DCache input
  dcache_.dcache2wb   = &wb_.in.dcachewb;        // DCache output → WB input
  dcache_.bind_context(ctx);
  mshr_.bind_context(ctx);
  wb_.bind_context(ctx);

  // ── Initialise sub-modules ─────────────────────────────────────────────────
  mshr_.init();
  wb_.init();
  dcache_.init();
  peripheral_model_.bind(csr, memory);
  peripheral_axi_.peripheral_io = peripheral_io;
  peripheral_axi_.peripheral_model = &peripheral_model_;
  peripheral_axi_.init();

  ptw_block.init();
  read_arb_block.init();
  resp_route_block.init();
  ptw_mem_resp_ios  = {};
  ptw_walk_resp_ios = {};
  refresh_ptw_client_outputs();

  dcache_req_mux_ = {};
  dcache_resp_raw_ = {};
  mshr_axi_in  = {};
  mshr_axi_out = {};
  wb_axi_in    = {};
  wb_axi_out   = {};
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
  axi_kit_runtime->mmio.add_device(0x10000000u, 0x1000u, &axi_kit_runtime->uart0);
  axi_kit_runtime->ddr.init();
  static bool printed_axi_cfg = false;
  if (!printed_axi_cfg) {
    printed_axi_cfg = true;
    LSU_MEM_DBG_PRINTF(
        "[CONFIG][AXI] ddr_read_latency=%ucy ddr_write_resp_latency=%ucy "
        "ddr_beat=%uB out=%u per_master=%u ddr_out=%u "
        "dcache_line=%uB(%u words) "
        "upstream_write_payload=%uB upstream_read_resp=%uB\n",
        static_cast<unsigned>(sim_ddr::SIM_DDR_LATENCY),
        static_cast<unsigned>(sim_ddr::SIM_DDR_WRITE_RESP_LATENCY),
        static_cast<unsigned>(sim_ddr::SIM_DDR_BEAT_BYTES),
        static_cast<unsigned>(axi_interconnect::MAX_OUTSTANDING),
        static_cast<unsigned>(axi_interconnect::MAX_READ_OUTSTANDING_PER_MASTER),
        static_cast<unsigned>(sim_ddr::SIM_DDR_MAX_OUTSTANDING),
        static_cast<unsigned>(DCACHE_LINE_BYTES),
        static_cast<unsigned>(DCACHE_LINE_WORDS),
        static_cast<unsigned>(axi_interconnect::AXI_UPSTREAM_PAYLOAD_BYTES),
        static_cast<unsigned>(axi_interconnect::MAX_READ_TRANSACTION_BYTES));
    if (DCACHE_LINE_WORDS > axi_interconnect::CACHELINE_WORDS) {
      LSU_MEM_DBG_PRINTF(
          "[MEM][AXI CFG][WARN] dcache line (%u words) is wider than AXI upstream "
          "write payload (%u words). High words may be dropped in bridge path.\n",
          static_cast<unsigned>(DCACHE_LINE_WORDS),
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
  const auto ptw = ptw_block.debug_state();
  std::fprintf(out,
               "[MEM DEBUG][PTW] walk_active=%d state=%u owner=%u "
               "req_id_valid=%d req_id=%zu dtlb(req_p=%d req_i=%d resp=%d "
               "mem_p=%d mem_i=%d) itlb(req_p=%d req_i=%d resp=%d mem_p=%d "
               "mem_i=%d)\n",
               static_cast<int>(ptw.walk_active),
               static_cast<unsigned>(ptw.walk_state),
               static_cast<unsigned>(ptw.walk_owner),
               static_cast<int>(ptw.walk_req_id_valid), ptw.walk_req_id,
               static_cast<int>(ptw.walk_req_pending[0]),
               static_cast<int>(ptw.walk_req_inflight[0]),
               static_cast<int>(ptw.walk_resp_valid[0]),
               static_cast<int>(ptw.mem_req_pending[0]),
               static_cast<int>(ptw.mem_req_inflight[0]),
               static_cast<int>(ptw.walk_req_pending[1]),
               static_cast<int>(ptw.walk_req_inflight[1]),
               static_cast<int>(ptw.walk_resp_valid[1]),
               static_cast<int>(ptw.mem_req_pending[1]),
               static_cast<int>(ptw.mem_req_inflight[1]));

  const auto route = resp_route_block.debug_state();
  std::fprintf(out,
               "[MEM DEBUG][ROUTE] ptw_event_count=%u wake(dtlb=%d itlb=%d "
               "walk=%d) ptw_port0=%d lsu_port0_replayed=%d\n",
               static_cast<unsigned>(route.ptw_event_count),
               static_cast<int>(route.wakeup.dtlb),
               static_cast<int>(route.wakeup.itlb),
               static_cast<int>(route.wakeup.walk),
               static_cast<int>(route.ptw_occupies_port0),
               static_cast<int>(route.lsu_port0_replayed));
  for (size_t i = 0; i < MemRespRouteBlock::kPtwTrackCount; i++) {
    const auto &track = route.ptw_tracks[i];
    if (!track.valid) {
      continue;
    }
    std::fprintf(out,
                 "[MEM DEBUG][ROUTE][PTWTRACK] idx=%zu owner=%u req_id=%zu "
                 "addr=0x%08x\n",
                 i, static_cast<unsigned>(track.owner), track.req_id,
                 track.req_addr);
  }
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


  ptw_block.comb_select_walk_owner();
  ptw_block.count_wait_cycles();

  uint32_t ptw_walk_read_addr = 0;
  const bool issue_ptw_walk_read = ptw_block.walk_read_req(ptw_walk_read_addr);
  const bool has_ptw_dtlb =
      ptw_block.has_pending_mem_req(MemPtwBlock::Client::DTLB);
  const bool has_ptw_itlb =
      ptw_block.has_pending_mem_req(MemPtwBlock::Client::ITLB);
  const uint32_t ptw_dtlb_addr =
      has_ptw_dtlb ? ptw_block.pending_mem_addr(MemPtwBlock::Client::DTLB) : 0;
  const uint32_t ptw_itlb_addr =
      has_ptw_itlb ? ptw_block.pending_mem_addr(MemPtwBlock::Client::ITLB) : 0;

  auto same_cycle_store_conflicts_ptw = [&](uint32_t paddr) {
    if (lsu2dcache == nullptr) {
      return false;
    }
    for (int i = 0; i < LSU_STA_COUNT; i++) {
      const auto &store_req = lsu2dcache->req_ports.store_ports[i];
      if (!store_req.valid) {
        continue;
      }
      if ((static_cast<uint32_t>(store_req.addr) >> 2) == (paddr >> 2)) {
        return true;
      }
    }
    return false;
  };

  const bool ptw_dtlb_store_conflict =
      has_ptw_dtlb && same_cycle_store_conflicts_ptw(ptw_dtlb_addr);
  const bool ptw_itlb_store_conflict =
      has_ptw_itlb && same_cycle_store_conflicts_ptw(ptw_itlb_addr);

  bool ptw_walk_hold_for_coherence = false;
  if (issue_ptw_walk_read && same_cycle_store_conflicts_ptw(ptw_walk_read_addr)) {
    // PTW reads must not observe the pre-state of a same-cycle committed PTE
    // store routed into DCache. Hold the walk for one cycle and let the store
    // update become architecturally visible through the normal DCache seq path
    // first. Shared PTW intentionally stays on the ordinary DCache request
    // pipeline instead of sampling a speculative same-cycle view here.
    ptw_walk_hold_for_coherence = true;
  }

  read_arb_block.eval_comb(lsu2dcache,
                           issue_ptw_walk_read && !ptw_walk_hold_for_coherence,
                           ptw_walk_read_addr,
                           has_ptw_dtlb && !ptw_dtlb_store_conflict,
                           ptw_dtlb_addr,
                           has_ptw_itlb && !ptw_itlb_store_conflict,
                           ptw_itlb_addr);

  dcache_req_mux_ = read_arb_block.comb_result().dcache_req;
  dcache_resp_raw_ = {};

  // Feed current-cycle AXI feedback before any comb phase that consumes it.
  mshr_.in.axi_in = mshr_axi_in;
  wb_.in.axi_in   = wb_axi_in;

  // RealDcache::stage2_comb() consumes current-cycle MSHR/WB comb outputs.
  // Order:
  // 1. WB comb_outputs exposes ready from the current WB view.
  // 2. MSHR comb_outputs uses that ready to decide whether an AXI read response
  //    may retire this cycle.
  // 3. DCache stage1 snapshots the new requests into s1s2_nxt.
  // 4. DCache emits WB bypass/merge queries for s1s2_cur, the requests that
  //    stage2 will actually evaluate in this cycle.
  // 5. WB comb_inputs consumes those queries immediately.
  // 6. WB comb_outputs is refreshed so DCache stage2 sees same-cycle bypass.
  wb_.comb_outputs();
  mshr_.in.wbmshr = wb_.out.wbmshr;
  mshr_.comb_outputs();
  dcache_.stage1_comb();
  dcache_.prepare_wb_queries_for_stage2();

  wb_.in.mshrwb   = mshr_.out.mshrwb;  // eviction push from MSHR current comb
  wb_.comb_inputs();
  wb_.comb_outputs();
  mshr_.in.wbmshr = wb_.out.wbmshr;

  dcache_.stage2_comb();
  if (read_arb_block.comb_result().granted) {
    switch (read_arb_block.comb_result().granted_owner) {
    case MemReadArbBlock::Owner::PTW_DTLB:
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::DTLB);
      break;
    case MemReadArbBlock::Owner::PTW_ITLB:
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::ITLB);
      break;
    case MemReadArbBlock::Owner::PTW_WALK:
      if (read_arb_block.comb_result().injected_port >= 0) {
        const auto &tag = read_arb_block.comb_result()
                              .issued_tags[read_arb_block.comb_result()
                                               .injected_port];
        ptw_block.on_walk_read_granted(tag.req_id);
      } else {
        ptw_block.on_walk_read_granted(0);
      }
      break;
    default:
      break;
    }
  }

  const replay_resp replay_bcast =
      replay_resp::from_io(mshr_.out.replay_resp);

  resp_route_block.eval_comb(&dcache_resp_raw_,
                             read_arb_block.comb_result().issued_tags,
                             replay_bcast);
  const auto &route_out = resp_route_block.comb_outputs();
  for (uint8_t i = 0; i < route_out.ptw_event_count; i++) {
    const auto &ptw_evt = route_out.ptw_events[i];
    if (!ptw_evt.valid) {
      continue;
    }
  }
  for (uint8_t i = 0; i < route_out.ptw_event_count; i++) {
    const auto &evt = route_out.ptw_events[i];
    if (!evt.valid) {
      continue;
    }

    uint32_t coherent_data = evt.data;
    MemDcacheImpl::CoherentQueryResult coherent_q =
        MemDcacheImpl::CoherentQueryResult::Miss;
    if (evt.replay == 0) {
      uint32_t observed = 0;
      coherent_q = dcache_.query_coherent_word(evt.req_addr, observed);
      if (coherent_q == MemDcacheImpl::CoherentQueryResult::Hit) {
        coherent_data = observed;
      }
    }

    switch (evt.owner) {
    case MemReadArbBlock::Owner::PTW_DTLB:
      if (evt.replay == 0) {
        if (coherent_q == MemDcacheImpl::CoherentQueryResult::Retry) {
          ptw_block.retry_mem_req(MemPtwBlock::Client::DTLB);
        } else {
          ptw_block.on_mem_resp_client(MemPtwBlock::Client::DTLB,
                                       coherent_data);
        }
      }
      break;
    case MemReadArbBlock::Owner::PTW_ITLB:
      if (evt.replay == 0) {
        if (coherent_q == MemDcacheImpl::CoherentQueryResult::Retry) {
          ptw_block.retry_mem_req(MemPtwBlock::Client::ITLB);
        } else {
          ptw_block.on_mem_resp_client(MemPtwBlock::Client::ITLB,
                                       coherent_data);
        }
      }
      break;
    case MemReadArbBlock::Owner::PTW_WALK:
      if (evt.replay == 0) {
        if (coherent_q == MemDcacheImpl::CoherentQueryResult::Retry) {
          (void)ptw_block.on_walk_mem_replay(evt.req_id, 2);
        } else {
          (void)ptw_block.on_walk_mem_resp(evt.req_id, evt.req_addr,
                                           coherent_data);
        }
      } else {
        (void)ptw_block.on_walk_mem_replay(evt.req_id, evt.replay);
      }
      break;
    default:
      break;
    }
  }

  if (route_out.wakeup.dtlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::DTLB);
  }
  if (route_out.wakeup.itlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::ITLB);
  }
  if (route_out.wakeup.walk) {
    ptw_block.retry_active_walk();
  }

  if (dcache2lsu != nullptr) {
    *dcache2lsu = resp_route_block.comb_outputs().lsu_resp;

    if (read_arb_block.comb_result().lsu_port0_preempted) {
      if (ctx != nullptr) {
        ctx->perf.ptw_port0_replay_count++;
      }
      int replay_port = -1;
      for (int i = 0; i < LSU_LDU_COUNT; i++) {
        if (!dcache2lsu->resp_ports.load_resps[i].valid) {
          replay_port = i;
          break;
        }
      }
      if (replay_port < 0) {
        // Do not silently drop this replay: LSU has already marked the preempted
        // request as sent/waiting and will deadlock without a retry signal.
        replay_port = 0;
      }
      auto &dst = dcache2lsu->resp_ports.load_resps[replay_port];
      const auto &tag0 = read_arb_block.comb_result().preempted_lsu_tag;
      dst = {};
      dst.valid = tag0.valid;
      dst.req_id = tag0.req_id;
      dst.uop = tag0.uop;
      // LSU consumes replay by LDQ token; keep token and req_id consistent.
      dst.uop.rob_idx = static_cast<uint32_t>(tag0.req_id);
      dst.replay = 3;
    }
  }

  refresh_ptw_client_outputs();

  // Export MSHR replay wakeup to LSU. RealDcache::comb() clears resp ports
  // every cycle, so this must be written after dcache_.comb().
  if (dcache2lsu != nullptr) {
    dcache2lsu->resp_ports.replay_resp = mshr_.out.replay_resp;
  }

  // Phase 3a: run MSHR comb_inputs (may accept AXI R, allocate entries, and
  // prepare next-cycle registered fill / eviction outputs).
  mshr_.comb_inputs();

  peripheral_axi_.in.read = peripheral_axi_read_in;
  peripheral_axi_.in.write = peripheral_axi_write_in;
  peripheral_axi_.comb_outputs();
  peripheral_axi_.comb_inputs();

  mshr_axi_out = mshr_.out.axi_out;
  wb_axi_out = wb_.out.axi_out;
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

  refresh_ptw_client_outputs();
}

void MemSubsystem::seq() {
  dcache_.seq();
  mshr_.seq();
  wb_.seq();
  peripheral_axi_.seq();
  read_arb_block.update_seq();
  resp_route_block.update_seq();
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
}
