#include "MemSubsystem.h"
#include "SimpleCache.h"
#include "config.h"
#include "icache/GenericTable.h"
#include <memory>

#if __has_include("UART16550_Device.h") && \
    __has_include("AXI_Interconnect.h") && \
    __has_include("AXI_Router_AXI4.h") && \
    __has_include("MMIO_Bus_AXI4.h") && \
    __has_include("SimDDR.h")
#define AXI_KIT_HEADERS_AVAILABLE 1
#include "UART16550_Device.h"
#if CONFIG_AXI_PROTOCOL == 4
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
#error "Current axi-interconnect-kit integration supports AXI4 only"
#endif
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
    DynamicTableReadResp data_resp, meta_resp, repl_resp;
    data.comb({}, data_resp);
    meta.comb({}, meta_resp);
    repl.comb({}, repl_resp);
    lookup_in.data_valid = data_resp.valid;
    lookup_in.meta_valid = meta_resp.valid;
    lookup_in.repl_valid = repl_resp.valid;
    lookup_in.data.bytes = data_resp.payload.bytes;
    lookup_in.meta.bytes = meta_resp.payload.bytes;
    lookup_in.repl.bytes = repl_resp.payload.bytes;
  }

  void seq(const axi_interconnect::AXI_LLC_TableOut_t &table_out) {
    if (!enabled) {
      return;
    }
    if (table_out.invalidate_all) {
      data.reset();
      meta.reset();
      repl.reset();
    }
    const auto data_read = make_read_req(table_out.data);
    const auto meta_read = make_read_req(table_out.meta);
    const auto repl_read = make_read_req(table_out.repl);
    const auto data_write =
        make_write_req(table_out.data, config.ways * config.line_bytes,
                       config.line_bytes);
    const auto meta_write = make_write_req(
        table_out.meta,
        config.ways * axi_interconnect::AXI_LLC_META_ENTRY_BYTES,
        axi_interconnect::AXI_LLC_META_ENTRY_BYTES);
    const auto repl_write =
        make_write_req(table_out.repl, axi_interconnect::AXI_LLC_REPL_BYTES, 0);
    data.seq(data_read, data_write);
    meta.seq(meta_read, meta_write);
    repl.seq(repl_read, repl_write);
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
  bool fire = ptw_block.client_send_read_req(to_block_client(client), paddr);
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
  bool fire = ptw_block.walk_client_send_req(to_block_client(client), req);
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
  }
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
  if (axi_kit_runtime == nullptr) {
    return nullptr;
  }
  return &axi_kit_runtime->interconnect.read_ports[axi_interconnect::MASTER_ICACHE];
#else
  return nullptr;
#endif
}

MemSubsystem::MemSubsystem(SimContext *ctx) : ctx(ctx) {
  dcache = std::make_unique<SimpleCache>(ctx);
#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime = std::make_unique<AxiKitRuntime>();
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
  Assert(lsu_req_io != nullptr && "MemSubsystem: lsu_req_io is not connected");
  Assert(lsu_wreq_io != nullptr &&
         "MemSubsystem: lsu_wreq_io is not connected");
  Assert(lsu_resp_io != nullptr &&
         "MemSubsystem: lsu_resp_io is not connected");
  Assert(lsu_wready_io != nullptr &&
         "MemSubsystem: lsu_wready_io is not connected");
  Assert(csr != nullptr && "MemSubsystem: csr is not connected");
  Assert(memory != nullptr && "MemSubsystem: memory is not connected");

  peripheral.csr = csr;
  peripheral.memory = memory;
  peripheral.init();

#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime->interconnect.init();
  {
    axi_interconnect::AXI_LLCConfig llc_cfg;
    llc_cfg.enable = (CONFIG_AXI_LLC_ENABLE != 0);
    llc_cfg.size_bytes = CONFIG_AXI_LLC_SIZE_BYTES;
    llc_cfg.line_bytes = CONFIG_AXI_LLC_LINE_BYTES;
    llc_cfg.ways = CONFIG_AXI_LLC_WAYS;
    llc_cfg.mshr_num = CONFIG_AXI_LLC_MSHR_NUM;
    llc_cfg.lookup_latency = CONFIG_AXI_LLC_LOOKUP_LATENCY;
    llc_cfg.prefetch_enable = (CONFIG_AXI_LLC_PREFETCH_ENABLE != 0);
    llc_cfg.prefetch_degree = CONFIG_AXI_LLC_PREFETCH_DEGREE;
    llc_cfg.nine = (CONFIG_AXI_LLC_NINE != 0);
    llc_cfg.unified = (CONFIG_AXI_LLC_UNIFIED != 0);
    llc_cfg.pipt = (CONFIG_AXI_LLC_PIPT != 0);
    set_llc_config(llc_cfg);
  }
  axi_kit_runtime->router.init();
  axi_kit_runtime->mmio.init();
  axi_kit_runtime->mmio.add_device(0x10000000u, 0x1000u, &axi_kit_runtime->uart0);
  axi_kit_runtime->ddr.init();
#endif

  dcache->lsu_req_io = &dcache_req_mux;
  dcache->lsu_wreq_io = &dcache_wreq_mux;
  dcache->lsu_resp_io = &dcache_resp_raw;
  dcache->lsu_wready_io = &dcache_wready_raw;
  dcache->peripheral_model = &peripheral;

  ptw_block.init();
  resp_route_block.init();
  ptw_mem_resp_ios = {};
  ptw_walk_resp_ios = {};
  refresh_ptw_client_outputs();

  dcache_req_mux = {};
  dcache_wreq_mux = {};
  dcache_resp_raw = {};
  dcache_wready_raw = {};
  *lsu_resp_io = {};
  *lsu_wready_io = {};

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

  dcache->init();
}

void MemSubsystem::on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3) {
  peripheral.on_commit_store(paddr, data, func3);
}

void MemSubsystem::comb() {
#if AXI_KIT_RUNTIME_ENABLED
  auto &interconnect = axi_kit_runtime->interconnect;
  auto &ddr = axi_kit_runtime->ddr;
  auto &router = axi_kit_runtime->router;
  auto &mmio = axi_kit_runtime->mmio;

  // AXI-kit phase-1 combinational outputs.
  llc_comb_outputs();
  interconnect.set_llc_lookup_in(llc_lookup_in());
  ddr.comb_outputs();
  mmio.comb_outputs();
  router.comb_outputs(interconnect.axi_io, ddr.io, mmio.io);
  interconnect.comb_outputs();
#endif

  // 子模块按组合逻辑顺序推进。
  ptw_block.comb_select_walk_owner();

  // Default outputs every cycle.
  *lsu_resp_io = {};
  dcache_req_mux = {};
  dcache_wreq_mux = {};

  // Pass write channel (currently only LSU issues writes).
  dcache_wreq_mux = *lsu_wreq_io;

  uint32_t ptw_walk_read_addr = 0;
  bool issue_ptw_walk_read = ptw_block.walk_read_req(ptw_walk_read_addr);

  bool has_ptw_dtlb = ptw_block.has_pending_mem_req(MemPtwBlock::Client::DTLB);
  bool has_ptw_itlb = ptw_block.has_pending_mem_req(MemPtwBlock::Client::ITLB);
  uint32_t ptw_dtlb_addr = has_ptw_dtlb
                               ? ptw_block.pending_mem_addr(
                                     MemPtwBlock::Client::DTLB)
                               : 0;
  uint32_t ptw_itlb_addr = has_ptw_itlb
                               ? ptw_block.pending_mem_addr(
                                     MemPtwBlock::Client::ITLB)
                               : 0;

  auto arb_ret = read_arb_block.arbitrate(
      lsu_req_io, issue_ptw_walk_read, ptw_walk_read_addr, has_ptw_dtlb,
      ptw_dtlb_addr, has_ptw_itlb, ptw_itlb_addr);
  ptw_block.count_wait_cycles();

  if (arb_ret.granted) {
    dcache_req_mux = arb_ret.req;
  }

  if (arb_ret.owner == MemReadArbBlock::Owner::LSU) {
    resp_route_block.enqueue_owner(MemRespRouteBlock::Owner::LSU);
  } else if (arb_ret.owner == MemReadArbBlock::Owner::PTW_WALK) {
    resp_route_block.enqueue_owner(MemRespRouteBlock::Owner::PTW_WALK);
    ptw_block.on_walk_read_granted();
  } else if (arb_ret.owner == MemReadArbBlock::Owner::PTW_DTLB) {
    ptw_block.on_mem_read_granted(MemPtwBlock::Client::DTLB);
    resp_route_block.enqueue_owner(MemRespRouteBlock::Owner::PTW_DTLB);
  } else if (arb_ret.owner == MemReadArbBlock::Owner::PTW_ITLB) {
    ptw_block.on_mem_read_granted(MemPtwBlock::Client::ITLB);
    resp_route_block.enqueue_owner(MemRespRouteBlock::Owner::PTW_ITLB);
  }

  dcache->comb();

  // Write ready backpressure directly reflects DCache.
  *lsu_wready_io = dcache_wready_raw;

  (void)resp_route_block.route_resp(dcache_resp_raw, lsu_resp_io, &ptw_block);

  // Stage-1 AXI wiring: connect ICache read master, keep all others idle.
#if AXI_KIT_RUNTIME_ENABLED
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
#endif

  refresh_ptw_client_outputs();
}

void MemSubsystem::seq() {
  dcache->seq();
#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime->ddr.seq();
  axi_kit_runtime->mmio.seq();
  axi_kit_runtime->router.seq(axi_kit_runtime->interconnect.axi_io, axi_kit_runtime->ddr.io,
                          axi_kit_runtime->mmio.io);
  llc_seq(axi_kit_runtime->interconnect.get_llc_table_out(),
          axi_kit_runtime->interconnect.get_llc_perf_counters());
  axi_kit_runtime->interconnect.seq();
#endif
}
