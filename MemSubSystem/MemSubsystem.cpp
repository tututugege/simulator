#include "MemSubsystem.h"
#include "DeadlockDebug.h"
#include "DeadlockReplayTrace.h"
#include "DebugPtwTrace.h"
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

namespace {
MemSubsystem *g_deadlock_mem = nullptr;

const char *mem_owner_name(uint8_t owner) {
  switch (static_cast<MemReadArbBlock::Owner>(owner)) {
  case MemReadArbBlock::Owner::LSU:
    return "LSU";
  case MemReadArbBlock::Owner::PTW_DTLB:
    return "PTW_DTLB";
  case MemReadArbBlock::Owner::PTW_ITLB:
    return "PTW_ITLB";
  case MemReadArbBlock::Owner::PTW_WALK:
    return "PTW_WALK";
  default:
    return "NONE";
  }
}

const char *replay_reason_name(uint8_t reason) {
  switch (reason) {
  case 0:
    return "none";
  case 1:
    return "mshr_full";
  case 2:
    return "wait_fill";
  case 3:
    return "struct_replay";
  default:
    return "unknown";
  }
}

void deadlock_dump_mem_cb() {
  if (g_deadlock_mem != nullptr) {
    g_deadlock_mem->dump_debug_state();
  }
}

void print_line_words(const char *prefix, const uint32_t *data) {
  std::printf("%s[", prefix);
  for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
    std::printf("%s%08x", (w == 0) ? "" : " ", data[w]);
  }
  std::printf("]\n");
}
} // namespace

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
  record_debug_event(DebugEventKind::PTW_MEM_REQ,
                     static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_DTLB) +
                         static_cast<uint8_t>(ptw_client_idx(client)),
                     paddr, 0, static_cast<uint8_t>(!fire));
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
  record_debug_event(DebugEventKind::PTW_WALK_REQ,
                     static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_DTLB) +
                         static_cast<uint8_t>(ptw_client_idx(client)),
                     req.vaddr, req.satp, static_cast<uint8_t>(!fire),
                     req.access_type);
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
  g_deadlock_mem = this;
  deadlock_debug::register_mem_dump_cb(deadlock_dump_mem_cb);
}

MemSubsystem::~MemSubsystem() {
  if (g_deadlock_mem == this) {
    g_deadlock_mem = nullptr;
  }
}

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
  debug_events_ = {};
  debug_event_head_ = 0;
  debug_event_count_ = 0;
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
        "[MEM][AXI CFG] protocol=%d dcache_line_bytes=%u dcache_line_words=%u "
        "axi_up_write_words=%u axi_up_read_words=%u\n",
        CONFIG_AXI_PROTOCOL, static_cast<unsigned>(DCACHE_LINE_BYTES),
        static_cast<unsigned>(DCACHE_LINE_WORDS),
        static_cast<unsigned>(axi_interconnect::CACHELINE_WORDS),
        static_cast<unsigned>(axi_interconnect::MAX_READ_TRANSACTION_WORDS));
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

  bool ptw_walk_direct_hit = false;
  bool ptw_walk_hold_for_coherence = false;
  uint32_t ptw_walk_direct_data = 0;
  if (issue_ptw_walk_read) {
    const auto q =
        dcache_.query_coherent_word(ptw_walk_read_addr, ptw_walk_direct_data);
    ptw_walk_direct_hit = (q == MemDcacheImpl::CoherentQueryResult::Hit);
    ptw_walk_hold_for_coherence =
        (q == MemDcacheImpl::CoherentQueryResult::Retry) ||
        (mshr_.cur.fill_valid &&
         cache_line_match(mshr_.cur.fill_addr, ptw_walk_read_addr));
  }

  read_arb_block.eval_comb(lsu2dcache,
                           issue_ptw_walk_read && !ptw_walk_direct_hit &&
                               !ptw_walk_hold_for_coherence,
                           ptw_walk_read_addr,
                           has_ptw_dtlb, ptw_dtlb_addr, has_ptw_itlb,
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

  if (ptw_walk_direct_hit) {
    record_debug_event(DebugEventKind::PTW_WALK_GRANT,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_WALK),
                       ptw_walk_read_addr, ptw_walk_direct_data, 0, 1, 0, 0);
    ptw_block.on_walk_read_granted(0);
    record_debug_event(DebugEventKind::PTW_ROUTE_EVENT,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_WALK),
                       ptw_walk_read_addr, ptw_walk_direct_data, 0, 1, 0, 0);
    record_debug_event(DebugEventKind::PTW_WALK_RESP,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_WALK),
                       ptw_walk_read_addr, ptw_walk_direct_data, 0, 1, 0, 0);
    debug_ptw_trace::record_ptw_walk_resp_detail(memory, ptw_walk_read_addr,
                                                 ptw_walk_direct_data);
    (void)ptw_block.on_walk_mem_resp(0, ptw_walk_direct_data);
  }

  if (read_arb_block.comb_result().granted) {
    switch (read_arb_block.comb_result().granted_owner) {
    case MemReadArbBlock::Owner::PTW_DTLB:
      record_debug_event(DebugEventKind::PTW_MEM_GRANT,
                         static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_DTLB),
                         ptw_dtlb_addr);
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::DTLB);
      break;
    case MemReadArbBlock::Owner::PTW_ITLB:
      record_debug_event(DebugEventKind::PTW_MEM_GRANT,
                         static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_ITLB),
                         ptw_itlb_addr);
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::ITLB);
      break;
    case MemReadArbBlock::Owner::PTW_WALK:
      record_debug_event(DebugEventKind::PTW_WALK_GRANT,
                         static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_WALK),
                         ptw_walk_read_addr);
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
    record_debug_event(DebugEventKind::PTW_ROUTE_EVENT,
                       static_cast<uint8_t>(ptw_evt.owner), ptw_evt.req_addr,
                       ptw_evt.data, ptw_evt.replay, 0, ptw_evt.req_id);
    if (ptw_evt.owner == MemReadArbBlock::Owner::PTW_WALK) {
      record_debug_event(DebugEventKind::PTW_WALK_RESP,
                         static_cast<uint8_t>(ptw_evt.owner), ptw_evt.req_addr,
                         ptw_evt.data, ptw_evt.replay, 0, ptw_evt.req_id);
      if (ptw_evt.replay == 0) {
        debug_ptw_trace::record_ptw_walk_resp_detail(memory, ptw_evt.req_addr,
                                                     ptw_evt.data);
      }
    } else {
      record_debug_event(DebugEventKind::PTW_MEM_RESP,
                         static_cast<uint8_t>(ptw_evt.owner), ptw_evt.req_addr,
                         ptw_evt.data, ptw_evt.replay, 0, ptw_evt.req_id);
    }
  }
  resp_route_block.apply_ptw_events(&ptw_block);

  if (route_out.wakeup.dtlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::DTLB);
    record_debug_event(DebugEventKind::REPLAY_WAKEUP,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_DTLB),
                       0, 0, 0, 0, 0, 0);
  }
  if (route_out.wakeup.itlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::ITLB);
    record_debug_event(DebugEventKind::REPLAY_WAKEUP,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_ITLB),
                       0, 0, 0, 0, 0, 0);
  }
  if (route_out.wakeup.walk) {
    ptw_block.retry_active_walk();
    record_debug_event(DebugEventKind::REPLAY_WAKEUP,
                       static_cast<uint8_t>(MemReadArbBlock::Owner::PTW_WALK),
                       0, 0, 0, 0, 0, 0);
  }

  if (dcache2lsu != nullptr) {
    *dcache2lsu = resp_route_block.comb_outputs().lsu_resp;

    for (int i = 0; i < LSU_LDU_COUNT; i++) {
      const auto &resp = dcache2lsu->resp_ports.load_resps[i];
      if (!resp.valid) {
        continue;
      }
      record_debug_event(DebugEventKind::LSU_LOAD_RESP,
                         static_cast<uint8_t>(MemReadArbBlock::Owner::LSU),
                         resp.debug_addr, resp.data, resp.replay, resp.debug_src,
                         resp.req_id,
                         static_cast<uint8_t>(i));
    }

    if (read_arb_block.comb_result().lsu_port0_preempted) {
      record_debug_event(DebugEventKind::LSU_PORT0_PREEMPT,
                         static_cast<uint8_t>(MemReadArbBlock::Owner::LSU),
                         read_arb_block.comb_result().preempted_lsu_tag.req_addr,
                         0, 0, 0,
                         read_arb_block.comb_result().preempted_lsu_tag.req_id, 0);
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
    if (mshr_.out.replay_resp.replay) {
      deadlock_replay_trace::record(
          DeadlockReplayTraceKind::MemToLsu, 0,
          static_cast<uint8_t>(mshr_.out.replay_resp.replay), 0,
          static_cast<uint8_t>(dcache2lsu->resp_ports.load_resps[0].valid),
          static_cast<uint8_t>(dcache2lsu->resp_ports.load_resps[1].valid),
          0, 0, static_cast<uint32_t>(mshr_.out.replay_resp.replay_addr),
          static_cast<uint32_t>(mshr_.out.replay_resp.free_slots), 0);
    }
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

void MemSubsystem::dump_debug_state() const {
  std::printf(
      "[DEADLOCK][MEM] mshr_count=%u fill=%d fill_addr=0x%08x wb_count=%u wb_head=%u wb_tail=%u\n",
      mshr_.cur.mshr_count, static_cast<int>(mshr_.cur.fill), mshr_.cur.fill_addr,
      wb_.cur.count, wb_.cur.head, wb_.cur.tail);
  std::printf(
      "[DEADLOCK][MEM][MSHR_STATE] cur_fill_valid=%d cur_fill_way=%u cur_wb_valid=%d cur_wb_addr=0x%08x nxt_count=%u\n",
      static_cast<int>(mshr_.cur.fill_valid), mshr_.cur.fill_way,
      static_cast<int>(mshr_.cur.wb_valid), mshr_.cur.wb_addr,
      mshr_.nxt.mshr_count);

  for (int i = 0; i < MSHR_ENTRIES; i++) {
    const MSHREntry &cur_e = mshr_entries[i];
    const MSHREntry &nxt_e = mshr_entries_nxt[i];
    std::printf(
        "[DEADLOCK][MEM][MSHR][%d] cur:{v=%d issue=%d fill=%d set=%u tag=0x%x line=0x%08x} nxt:{v=%d issue=%d fill=%d set=%u tag=0x%x line=0x%08x}\n",
        i, static_cast<int>(cur_e.valid), static_cast<int>(cur_e.issued),
        static_cast<int>(cur_e.fill), cur_e.index, cur_e.tag,
        get_addr(cur_e.index, cur_e.tag, 0), static_cast<int>(nxt_e.valid),
        static_cast<int>(nxt_e.issued), static_cast<int>(nxt_e.fill), nxt_e.index,
        nxt_e.tag, get_addr(nxt_e.index, nxt_e.tag, 0));
  }

  if (mshr_.cur.fill_valid) {
    print_line_words("[DEADLOCK][MEM][MSHR][FILL_DATA] ",
                     mshr_.cur.fill_data);
  }
  if (mshr_.cur.wb_valid) {
    print_line_words("[DEADLOCK][MEM][MSHR][WB_DATA]   ", mshr_.cur.wb_data);
  }

  std::printf(
      "[DEADLOCK][MEM][WB_STATE] cur_count=%u cur_head=%u cur_tail=%u cur_send=%u cur_issue_pending=%u nxt_count=%u nxt_head=%u nxt_tail=%u nxt_send=%u nxt_issue_pending=%u\n",
      wb_.cur.count, wb_.cur.head, wb_.cur.tail, wb_.cur.send,
      wb_.cur.issue_pending, wb_.nxt.count, wb_.nxt.head, wb_.nxt.tail, wb_.nxt.send,
      wb_.nxt.issue_pending);
  for (int i = 0; i < WB_ENTRIES; i++) {
    const WriteBufferEntry &cur_e = write_buffer[i];
    const WriteBufferEntry &nxt_e = write_buffer_nxt[i];
    std::printf(
        "[DEADLOCK][MEM][WB][%d] cur:{v=%d send=%d addr=0x%08x} nxt:{v=%d send=%d addr=0x%08x}\n",
        i, static_cast<int>(cur_e.valid), static_cast<int>(cur_e.send),
        cur_e.addr, static_cast<int>(nxt_e.valid), static_cast<int>(nxt_e.send),
        nxt_e.addr);
    if (cur_e.valid) {
      print_line_words("[DEADLOCK][MEM][WB][CUR_DATA] ", cur_e.data);
    }
    if (nxt_e.valid) {
      print_line_words("[DEADLOCK][MEM][WB][NXT_DATA] ", nxt_e.data);
    }
  }

  const auto ptw_dbg = ptw_block.debug_state();
  std::printf(
      "[DEADLOCK][MEM][PTW] walk_active=%d walk_state=%u walk_owner=%u dtlb_mem{pending=%d inflight=%d} itlb_mem{pending=%d inflight=%d} dtlb_walk{pending=%d inflight=%d resp=%d} itlb_walk{pending=%d inflight=%d resp=%d}\n",
      static_cast<int>(ptw_dbg.walk_active), static_cast<unsigned>(ptw_dbg.walk_state),
      static_cast<unsigned>(ptw_dbg.walk_owner),
      static_cast<int>(ptw_dbg.mem_req_pending[0]),
      static_cast<int>(ptw_dbg.mem_req_inflight[0]),
      static_cast<int>(ptw_dbg.mem_req_pending[1]),
      static_cast<int>(ptw_dbg.mem_req_inflight[1]),
      static_cast<int>(ptw_dbg.walk_req_pending[0]),
      static_cast<int>(ptw_dbg.walk_req_inflight[0]),
      static_cast<int>(ptw_dbg.walk_resp_valid[0]),
      static_cast<int>(ptw_dbg.walk_req_pending[1]),
      static_cast<int>(ptw_dbg.walk_req_inflight[1]),
      static_cast<int>(ptw_dbg.walk_resp_valid[1]));

  const auto route_dbg = resp_route_block.debug_state();
  std::printf(
      "[DEADLOCK][MEM][RESP_ROUTE] wakeup{dtlb=%d itlb=%d walk=%d} ptw_evt{valid=%d owner=%u replay=%u req_addr=0x%08x data=0x%08x} ptw_port0=%d lsu_port0_replayed=%d\n",
      static_cast<int>(route_dbg.wakeup.dtlb),
      static_cast<int>(route_dbg.wakeup.itlb),
      static_cast<int>(route_dbg.wakeup.walk),
      static_cast<int>(route_dbg.ptw_event.valid),
      static_cast<unsigned>(route_dbg.ptw_event.owner),
      static_cast<unsigned>(route_dbg.ptw_event.replay),
      route_dbg.ptw_event.req_addr, route_dbg.ptw_event.data,
      static_cast<int>(route_dbg.ptw_occupies_port0),
      static_cast<int>(route_dbg.lsu_port0_replayed));
  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    const auto &tag = route_dbg.issued_tags[i];
    std::printf(
        "[DEADLOCK][MEM][RESP_ROUTE][TAG %d] valid=%d owner=%u req_id=%zu req_addr=0x%08x pc=0x%08x inst=0x%08x\n",
        i, static_cast<int>(tag.valid), static_cast<unsigned>(tag.owner), tag.req_id,
        tag.req_addr, tag.uop.dbg.pc, tag.uop.dbg.instruction);
  }
  for (size_t i = 0; i < MemRespRouteBlock::kPtwTrackCount; i++) {
    const auto &track = route_dbg.ptw_tracks[i];
    std::printf(
        "[DEADLOCK][MEM][RESP_ROUTE][PTW_TRACK %d] valid=%d owner=%u req_id=%zu req_addr=0x%08x\n",
        static_cast<int>(i), static_cast<int>(track.valid),
        static_cast<unsigned>(track.owner),
        track.req_id, track.req_addr);
  }
  std::printf(
      "[DEADLOCK][MEM][RESP_ROUTE][TRACK] dtlb{blk=%d reason=%u addr=0x%08x} itlb{blk=%d reason=%u addr=0x%08x} walk{blk=%d reason=%u addr=0x%08x}\n",
      static_cast<int>(route_dbg.dtlb.blocked),
      static_cast<unsigned>(route_dbg.dtlb.reason), route_dbg.dtlb.req_addr,
      static_cast<int>(route_dbg.itlb.blocked),
      static_cast<unsigned>(route_dbg.itlb.reason), route_dbg.itlb.req_addr,
      static_cast<int>(route_dbg.walk.blocked),
      static_cast<unsigned>(route_dbg.walk.reason), route_dbg.walk.req_addr);
  dump_key_cache_lines();
  dump_recent_debug_events();
  debug_ptw_trace::dump_recent_satp_writes();
  debug_ptw_trace::dump_recent_ptw_walk_resps();
  dump_failure_analysis();
#if AXI_KIT_RUNTIME_ENABLED
  if (axi_kit_runtime != nullptr) {
    std::printf("[DEADLOCK][MEM][AXI_KIT] internal icache/llc path state dump follows\n");
    axi_kit_runtime->interconnect.debug_print();
    axi_kit_runtime->ddr.print_state();
  }
#endif
}

void MemSubsystem::record_debug_event(DebugEventKind kind, uint8_t owner,
                                      uint32_t addr, uint32_t data,
                                      uint8_t replay, uint8_t extra,
                                      size_t req_id, uint8_t port) {
  DebugEvent &slot = debug_events_[debug_event_head_];
  slot.cycle = static_cast<uint64_t>(sim_time);
  slot.kind = kind;
  slot.owner = owner;
  slot.port = port;
  slot.replay = replay;
  slot.extra = extra;
  slot.addr = addr;
  slot.data = data;
  slot.req_id = req_id;
  debug_event_head_ = (debug_event_head_ + 1) % kDebugEventCapacity;
  if (debug_event_count_ < kDebugEventCapacity) {
    debug_event_count_++;
  }
}

void MemSubsystem::dump_recent_debug_events() const {
  const size_t recent_count =
      (debug_event_count_ < 20) ? debug_event_count_ : static_cast<size_t>(20);
  if (recent_count == 0) {
    std::printf("[DEADLOCK][MEM][EVENTS] no recent MemSubsystem events recorded.\n");
    return;
  }

  auto event_name = [](DebugEventKind kind) {
    switch (kind) {
    case DebugEventKind::PTW_MEM_REQ:
      return "ptw_mem_req";
    case DebugEventKind::PTW_MEM_GRANT:
      return "ptw_mem_grant";
    case DebugEventKind::PTW_MEM_RESP:
      return "ptw_mem_resp";
    case DebugEventKind::PTW_WALK_REQ:
      return "ptw_walk_req";
    case DebugEventKind::PTW_WALK_GRANT:
      return "ptw_walk_grant";
    case DebugEventKind::PTW_WALK_RESP:
      return "ptw_walk_resp";
    case DebugEventKind::REPLAY_WAKEUP:
      return "replay_wakeup";
    case DebugEventKind::LSU_PORT0_PREEMPT:
      return "lsu_port0_preempt";
    case DebugEventKind::LSU_LOAD_RESP:
      return "lsu_load_resp";
    case DebugEventKind::PTW_ROUTE_EVENT:
      return "ptw_route";
    default:
      return "unknown";
    }
  };
  auto load_resp_src_name = [](uint8_t src) {
    switch (src) {
    case 1:
      return "special";
    case 2:
      return "mshr_fill";
    case 3:
      return "wb_bypass";
    case 4:
      return "dcache_hit";
    case 11:
      return "replay_bank_conflict";
    case 12:
      return "replay_mshr_pending_guard";
    case 13:
      return "replay_mshr_hit";
    case 14:
      return "replay_mshr_full";
    case 15:
      return "replay_first_alloc";
    default:
      return "unknown";
    }
  };

  const size_t start =
      (debug_event_head_ + kDebugEventCapacity - recent_count) %
      kDebugEventCapacity;
  std::printf("[DEADLOCK][MEM][EVENTS] showing last %zu events\n", recent_count);
  for (size_t i = 0; i < recent_count; i++) {
    const DebugEvent &e = debug_events_[(start + i) % kDebugEventCapacity];
    if (e.kind == DebugEventKind::LSU_LOAD_RESP) {
      std::printf(
          "[DEADLOCK][MEM][EVENT %02zu] cyc=%" PRIu64
          " kind=%s owner=%s port=%u replay=%u src=%s(%u) addr=0x%08x data=0x%08x req_id=%zu\n",
          i, e.cycle, event_name(e.kind), mem_owner_name(e.owner),
          static_cast<unsigned>(e.port), static_cast<unsigned>(e.replay),
          load_resp_src_name(e.extra), static_cast<unsigned>(e.extra), e.addr,
          e.data, e.req_id);
    } else {
      std::printf(
          "[DEADLOCK][MEM][EVENT %02zu] cyc=%" PRIu64
          " kind=%s owner=%s port=%u replay=%u extra=%u addr=0x%08x data=0x%08x req_id=%zu\n",
          i, e.cycle, event_name(e.kind), mem_owner_name(e.owner),
          static_cast<unsigned>(e.port), static_cast<unsigned>(e.replay),
          static_cast<unsigned>(e.extra), e.addr, e.data, e.req_id);
    }
  }
}

void MemSubsystem::dump_failure_analysis() const {
  const auto ptw_dbg = ptw_block.debug_state();
  const auto route_dbg = resp_route_block.debug_state();

  if (route_dbg.dtlb.blocked || route_dbg.itlb.blocked || route_dbg.walk.blocked) {
    std::printf(
        "[DEADLOCK][MEM][ANALYSIS] blocked_trackers dtlb=%s itlb=%s walk=%s\n",
        replay_reason_name(route_dbg.dtlb.reason),
        replay_reason_name(route_dbg.itlb.reason),
        replay_reason_name(route_dbg.walk.reason));
  }

  if (ptw_dbg.walk_active || ptw_dbg.mem_req_pending[0] || ptw_dbg.mem_req_pending[1] ||
      ptw_dbg.mem_req_inflight[0] || ptw_dbg.mem_req_inflight[1] ||
      ptw_dbg.walk_req_pending[0] || ptw_dbg.walk_req_pending[1] ||
      ptw_dbg.walk_req_inflight[0] || ptw_dbg.walk_req_inflight[1]) {
    std::printf(
        "[DEADLOCK][MEM][ANALYSIS] outstanding_ptw walk_active=%d walk_state=%u dtlb_mem{pending=%d inflight=%d} itlb_mem{pending=%d inflight=%d} dtlb_walk{pending=%d inflight=%d} itlb_walk{pending=%d inflight=%d}\n",
        static_cast<int>(ptw_dbg.walk_active),
        static_cast<unsigned>(ptw_dbg.walk_state),
        static_cast<int>(ptw_dbg.mem_req_pending[0]),
        static_cast<int>(ptw_dbg.mem_req_inflight[0]),
        static_cast<int>(ptw_dbg.mem_req_pending[1]),
        static_cast<int>(ptw_dbg.mem_req_inflight[1]),
        static_cast<int>(ptw_dbg.walk_req_pending[0]),
        static_cast<int>(ptw_dbg.walk_req_inflight[0]),
        static_cast<int>(ptw_dbg.walk_req_pending[1]),
        static_cast<int>(ptw_dbg.walk_req_inflight[1]));
  }

  if (route_dbg.lsu_port0_replayed || route_dbg.ptw_occupies_port0) {
    std::printf(
        "[DEADLOCK][MEM][ANALYSIS] arbitration_notice ptw_occupies_port0=%d lsu_port0_replayed=%d; if a later crash shows a bogus kernel pointer (for example 0xcfxxxxxx in kstrdup_const), prioritize PTW/LSU arbitration and response routing over SLUB itself.\n",
        static_cast<int>(route_dbg.ptw_occupies_port0),
        static_cast<int>(route_dbg.lsu_port0_replayed));
  } else {
    std::printf(
        "[DEADLOCK][MEM][ANALYSIS] kernel_hint=If Linux later crashes in kstrdup_const/__kernfs_new_node with a bogus kernel pointer, the allocator is usually the victim. Check earlier PTW walk results, DCache fills, and replay handling first.\n");
  }
}

void MemSubsystem::dump_key_cache_lines() const {
  uint32_t line_bases[32] = {};
  const char *reasons[32] = {};
  size_t line_count = 0;

  auto add_line = [&](const char *reason, uint32_t addr) {
    if (addr == 0) {
      return;
    }
    const uint32_t line_base = addr & ~(static_cast<uint32_t>(DCACHE_LINE_BYTES) - 1u);
    for (size_t i = 0; i < line_count; i++) {
      if (line_bases[i] == line_base) {
        return;
      }
    }
    if (line_count < (sizeof(line_bases) / sizeof(line_bases[0]))) {
      line_bases[line_count] = line_base;
      reasons[line_count] = reason;
      line_count++;
    }
  };

  if (mshr_.cur.fill_addr != 0) {
    add_line("mshr_fill_addr", mshr_.cur.fill_addr);
  }
  if (mshr_.cur.wb_addr != 0) {
    add_line("mshr_wb_addr", mshr_.cur.wb_addr);
  }

  for (int i = 0; i < MSHR_ENTRIES; i++) {
    const MSHREntry &cur_e = mshr_entries[i];
    const MSHREntry &nxt_e = mshr_entries_nxt[i];
    if (cur_e.valid) {
      add_line("mshr_cur", get_addr(cur_e.index, cur_e.tag, 0));
    }
    if (nxt_e.valid) {
      add_line("mshr_nxt", get_addr(nxt_e.index, nxt_e.tag, 0));
    }
  }

  for (int i = 0; i < WB_ENTRIES; i++) {
    const WriteBufferEntry &cur_e = write_buffer[i];
    const WriteBufferEntry &nxt_e = write_buffer_nxt[i];
    if (cur_e.valid) {
      add_line("wb_cur", cur_e.addr);
    }
    if (nxt_e.valid) {
      add_line("wb_nxt", nxt_e.addr);
    }
  }

  const auto route_dbg = resp_route_block.debug_state();
  if (route_dbg.dtlb.req_addr != 0) {
    add_line("route_dtlb", route_dbg.dtlb.req_addr);
  }
  if (route_dbg.itlb.req_addr != 0) {
    add_line("route_itlb", route_dbg.itlb.req_addr);
  }
  if (route_dbg.walk.req_addr != 0) {
    add_line("route_walk", route_dbg.walk.req_addr);
  }
  if (route_dbg.ptw_event.req_addr != 0) {
    add_line("ptw_event", route_dbg.ptw_event.req_addr);
  }

  const size_t recent_count =
      (debug_event_count_ < 16) ? debug_event_count_ : static_cast<size_t>(16);
  const size_t start =
      (debug_event_head_ + kDebugEventCapacity - recent_count) %
      kDebugEventCapacity;
  for (size_t i = 0; i < recent_count; i++) {
    const DebugEvent &e = debug_events_[(start + i) % kDebugEventCapacity];
    if (e.addr != 0) {
      add_line("recent_event", e.addr);
    }
  }

  if (line_count == 0) {
    std::printf("[DEADLOCK][MEM][CACHE_LINE] no key cache lines selected.\n");
    return;
  }

  std::printf("[DEADLOCK][MEM][CACHE_LINE] dumping %zu key cache lines\n", line_count);
  for (size_t i = 0; i < line_count; i++) {
    dump_cache_line_for_addr(reasons[i], line_bases[i]);
  }
}

void MemSubsystem::dump_cache_line_for_addr(const char *reason, uint32_t addr) const {
  const AddrFields f = decode(addr);
  const uint32_t line_base =
      addr & ~(static_cast<uint32_t>(DCACHE_LINE_BYTES) - 1u);

  std::printf(
      "[DEADLOCK][MEM][CACHE_LINE][%s] line=0x%08x set=%u tag=0x%x bank=%u word_off=%u\n",
      reason, line_base, f.set_idx, f.tag, f.bank, f.word_off);

  for (int w = 0; w < DCACHE_WAYS; w++) {
    const bool match = valid_array[f.set_idx][w] && tag_array[f.set_idx][w] == f.tag;
    std::printf(
        "[DEADLOCK][MEM][CACHE_LINE][%s][WAY %d] match=%d valid=%d dirty=%d tag=0x%x words=[",
        reason, w, static_cast<int>(match), static_cast<int>(valid_array[f.set_idx][w]),
        static_cast<int>(dirty_array[f.set_idx][w]), tag_array[f.set_idx][w]);
    for (int word = 0; word < DCACHE_LINE_WORDS; word++) {
      std::printf("%s%08x", (word == 0) ? "" : " ",
                  data_array[f.set_idx][w][word]);
    }
    std::printf("]\n");
  }
}
