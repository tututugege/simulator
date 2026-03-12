#include "MemSubsystem.h"
#include "DeadlockDebug.h"
#include "RealDcache.h"
#include "config.h"
#include <memory>
#include <cstdio>
#include <assert.h>
#include <cstring>

#if __has_include("UART16550_Device.h") && \
    __has_include("AXI_Interconnect.h") && \
    __has_include("AXI_Interconnect_AXI3.h") && \
    __has_include("AXI_Router_AXI4.h") && \
    __has_include("AXI_Router_AXI3.h") && \
    __has_include("MMIO_Bus_AXI4.h") && \
    __has_include("MMIO_Bus_AXI3.h") && \
    __has_include("SimDDR.h") && \
    __has_include("SimDDR_AXI3.h")
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
#elif CONFIG_AXI_PROTOCOL == 3
#include "AXI_Interconnect_AXI3.h"
#include "AXI_Router_AXI3.h"
#include "MMIO_Bus_AXI3.h"
#include "SimDDR_AXI3.h"
namespace {
using InterconnectImpl = axi_interconnect::AXI_Interconnect_AXI3;
using DdrImpl = sim_ddr_axi3::SimDDR_AXI3;
using RouterImpl = axi_interconnect::AXI_Router_AXI3;
using MmioImpl = mmio::MMIO_Bus_AXI3;
} // namespace
#else
#error "Unsupported CONFIG_AXI_PROTOCOL value"
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
struct AxiKitRuntime {
  InterconnectImpl interconnect;
  DdrImpl ddr;
  RouterImpl router;
  MmioImpl mmio;
  mmio::UART16550_Device uart0{0x10000000u};
};
#else
struct AxiKitRuntime {};
#endif

namespace {
MemSubsystem *g_deadlock_mem = nullptr;

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
  
  dcache_.lsu2dcache  = lsu2dcache;
  dcache_.dcache2lsu  = dcache2lsu;

  // Internal MSHR ↔ DCache wires: RealDcache reads/writes MSHR IO structs
  // directly via pointers, keeping the connection zero-copy.
  dcache_.mshr2dcache = &mshr_.out.mshr2dcache;  // MSHR output → DCache input
  dcache_.dcache2mshr = &mshr_.in.dcachemshr;    // DCache output → MSHR input

  // Internal WriteBuffer ↔ DCache wires.
  dcache_.wb2dcache   = &wb_.out.wbdcache;       // WB output → DCache input
  dcache_.dcache2wb   = &wb_.in.dcachewb;        // DCache output → WB input

  // ── Initialise sub-modules ─────────────────────────────────────────────────
  mshr_.init();
  wb_.init();
  dcache_.init();
  peripheral_axi_.peripheral_io = peripheral_io;
  peripheral_axi_.init();

  ptw_block.init();
  ptw_mem_resp_ios  = {};
  ptw_walk_resp_ios = {};
  refresh_ptw_client_outputs();

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
  axi_kit_runtime->router.init();
  axi_kit_runtime->mmio.init();
  axi_kit_runtime->mmio.add_device(0x10000000u, 0x1000u, &axi_kit_runtime->uart0);
  axi_kit_runtime->ddr.init();
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
  // if (memory == nullptr) {
  //   return;
  // }
  // const uint32_t word_idx = paddr >> 2;
  // const uint32_t byte_off = paddr & 0x3u;
  // uint32_t cur = memory[word_idx];

  // if (func3 == 0b000) { // SB
  //   const uint32_t mask = 0xFFu << (byte_off * 8);
  //   const uint32_t val = (data & 0xFFu) << (byte_off * 8);
  //   memory[word_idx] = (cur & ~mask) | val;
  // } else if (func3 == 0b001) { // SH
  //   const uint32_t half_off = (byte_off & 0x2u) * 8;
  //   const uint32_t mask = 0xFFFFu << half_off;
  //   const uint32_t val = (data & 0xFFFFu) << half_off;
  //   memory[word_idx] = (cur & ~mask) | val;
  // } else { // SW / fallback
  //   memory[word_idx] = data;
  // }
}

void MemSubsystem::comb() {
#if AXI_KIT_RUNTIME_ENABLED
  auto &interconnect = axi_kit_runtime->interconnect;
  auto &ddr = axi_kit_runtime->ddr;
  auto &router = axi_kit_runtime->router;
  auto &mmio = axi_kit_runtime->mmio;

  // AXI-kit phase-1 combinational outputs.
  ddr.comb_outputs();
  mmio.comb_outputs();
  router.comb_outputs(interconnect.axi_io, ddr.io, mmio.io);
  interconnect.comb_outputs();
#endif

  ptw_block.comb_select_walk_owner();
  ptw_block.count_wait_cycles();

  // Resolve pending single-address PTW mem reads directly from memory[].
  // (PTW bypasses RealDcache to keep the interface simple; page-table data
  //  is always coherent in memory[] because stores go through on_commit_store.)
  for (int i = 0; i < static_cast<int>(MemPtwBlock::Client::NUM_CLIENTS); i++) {
    auto client = static_cast<MemPtwBlock::Client>(i);
    if (ptw_block.has_pending_mem_req(client)) {
      uint32_t paddr = ptw_block.pending_mem_addr(client);
      uint32_t data  = memory[paddr >> 2];
      ptw_block.on_mem_read_granted(client);
      ptw_block.on_mem_resp_client(client, data);
    }
  }

  // Resolve PTW walk reads (L1 → optionally L2) directly from memory[].
  // The loop completes at most two iterations (one per page-table level).
  {
    uint32_t walk_addr = 0;
    while (ptw_block.walk_read_req(walk_addr)) {
      uint32_t data = memory[walk_addr >> 2];
      ptw_block.on_walk_read_granted();
      ptw_block.on_walk_mem_resp(data);
    }
  }

  refresh_ptw_client_outputs();

  wb_.comb_outputs();
  mshr_.in.axi_in = mshr_axi_in;
  mshr_.in.wbmshr = wb_.out.wbmshr;
  // Phase 2: compute MSHR outputs from cur state so DCache can read them.
  mshr_.comb_outputs();
  // Phase 2: RealDcache pipeline (S1 + S2).
  //   Reads : mshr_.out.mshr2dcache, wb_.out.wbdcache  (via dcache_ pointers)
  //   Writes: mshr_.in.dcachemshr,   wb_.in.dcachewb   (via dcache_ pointers)
  dcache_.comb();
  // Phase 3b: inject AXI write-channel inputs and MSHR eviction, then run
  //           WriteBuffer comb_inputs (drains evictions onto AXI).
  wb_.in.axi_in   = wb_axi_in;
  wb_.in.mshrwb   = mshr_.out.mshrwb;  // eviction push (set in Phase 3a)
  wb_.comb_inputs();
  // Export MSHR replay wakeup to LSU. RealDcache::comb() clears resp ports
  // every cycle, so this must be written after dcache_.comb().
  dcache2lsu->resp_ports.replay_resp = mshr_.out.replay_resp;

  // Phase 3a: run MSHR comb_inputs (may accept AXI R, allocate entries, and
  // prepare next-cycle registered fill / eviction outputs).
  mshr_.comb_inputs();

  // Expose updated AXI outputs for the caller.
  mshr_axi_out = mshr_.out.axi_out;
  wb_axi_out = wb_.out.axi_out;
  peripheral_axi_.in.read = peripheral_axi_read_in;
  peripheral_axi_.in.write = peripheral_axi_write_in;
  peripheral_axi_.comb_outputs();
  peripheral_axi_.comb_inputs();
  peripheral_axi_read_out = peripheral_axi_.out.read;
  peripheral_axi_write_out = peripheral_axi_.out.write;

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
  dcache_.seq();
  mshr_.seq();
  wb_.seq();
  peripheral_axi_.seq();
#if AXI_KIT_RUNTIME_ENABLED
  axi_kit_runtime->ddr.seq();
  axi_kit_runtime->mmio.seq();
  axi_kit_runtime->router.seq(axi_kit_runtime->interconnect.axi_io, axi_kit_runtime->ddr.io,
                          axi_kit_runtime->mmio.io);
  axi_kit_runtime->interconnect.seq();
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
}
