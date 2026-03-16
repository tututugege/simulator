#include "MemSubsystem.h"
#include "DeadlockDebug.h"
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

const char *ptw_client_name(MemPtwBlock::Client c) {
  switch (c) {
  case MemPtwBlock::Client::DTLB:
    return "DTLB";
  case MemPtwBlock::Client::ITLB:
    return "ITLB";
  default:
    return "UNK";
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
  DBG_PRINTF("[MEM][PTW][MEM REQ] cyc=%lld client=%s fire=%d paddr=0x%08x\n",
             (long long)sim_time, ptw_client_name(block_client),
             static_cast<int>(fire), paddr);
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
  DBG_PRINTF(
      "[MEM][PTW][WALK REQ] cyc=%lld client=%s fire=%d vaddr=0x%08x satp=0x%08x type=%u\n",
      (long long)sim_time, ptw_client_name(block_client), static_cast<int>(fire),
      req.vaddr, req.satp, req.access_type);
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
  
  // Route LSU requests through MemReadArbBlock so PTW reads can be injected,
  // and capture raw DCache responses before routing them back to LSU/PTW.
  dcache_.lsu2dcache  = &dcache_req_mux_;
  dcache_.dcache2lsu  = &dcache_resp_raw_;

#if !CONFIG_MEM_DCACHE_USE_SIMPLE
  // Internal MSHR ↔ DCache wires: RealDcache reads/writes MSHR IO structs
  // directly via pointers, keeping the connection zero-copy.
  dcache_.mshr2dcache = &mshr_.out.mshr2dcache;  // MSHR output → DCache input
  dcache_.dcache2mshr = &mshr_.in.dcachemshr;    // DCache output → MSHR input

  // Internal WriteBuffer ↔ DCache wires.
  dcache_.wb2dcache   = &wb_.out.wbdcache;       // WB output → DCache input
  dcache_.dcache2wb   = &wb_.in.dcachewb;        // DCache output → WB input
#endif
  dcache_.bind_context(ctx);
  mshr_.bind_context(ctx);
  wb_.bind_context(ctx);

  // ── Initialise sub-modules ─────────────────────────────────────────────────
  mshr_.init();
  wb_.init();
  dcache_.init();
  peripheral_axi_.peripheral_io = peripheral_io;
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
#if CONFIG_BACKEND_USE_SIMPLE_LSU
  if (memory == nullptr) {
    return;
  }

  const bool is_uart = ((paddr & UART_ADDR_MASK) == UART_ADDR_BASE);
  const bool is_plic = ((paddr & PLIC_ADDR_MASK) == PLIC_ADDR_BASE);
  if (!is_uart && !is_plic) {
    return;
  }

  // Keep MMIO backing memory coherent with the committed store width.
  const uint32_t byte_off = paddr & 0x3u;
  uint32_t wstrb = 0;
  uint32_t wdata = 0;
  switch (func3 & 0x3u) {
  case 0:
    wstrb = (1u << byte_off);
    wdata = (data & 0xFFu) << (byte_off * 8);
    break;
  case 1:
    wstrb = (0x3u << byte_off);
    wdata = (data & 0xFFFFu) << (byte_off * 8);
    break;
  default:
    wstrb = 0xFu;
    wdata = data;
    break;
  }

  uint32_t wmask = 0;
  for (int i = 0; i < 4; i++) {
    if ((wstrb >> i) & 1u) {
      wmask |= (0xFFu << (i * 8));
    }
  }
  const uint32_t word_idx = paddr >> 2;
  const uint32_t old_val = memory[word_idx];
  const uint32_t new_val = (old_val & ~wmask) | (wdata & wmask);
  memory[word_idx] = new_val;

  // UART THR (0x1000_0000 + 0): committed byte write prints one char.
  if (paddr == UART_ADDR_BASE) {
    const unsigned char ch = static_cast<unsigned char>(data & 0xFFu);
    std::putchar(static_cast<int>(ch));
    std::fflush(stdout);
    // Keep legacy behavior: TX register low byte reads back as 0 after write.
    memory[UART_ADDR_BASE / 4] &= 0xFFFFFF00u;
  }
#else
  (void)paddr;
  (void)data;
  (void)func3;
#endif
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


  wb_.comb_outputs();
  mshr_.in.axi_in = mshr_axi_in;
  mshr_.in.wbmshr = wb_.out.wbmshr;
  // Phase 2: compute MSHR outputs from cur state so DCache can read them.
  mshr_.comb_outputs();
  // Phase 2: DCache pipeline.
  //   RealDcache reads/writes MSHR/WB sideband pointers.
  //   SimpleCache only uses LSU<->DCache request/response ports.

  const auto replay_bcast = replay_resp::from_io(mshr_.out.replay_resp);
  const auto ptw_wakeup = resp_route_block.peek_wakeup(replay_bcast);
  if (ptw_wakeup.dtlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::DTLB);
  }
  if (ptw_wakeup.itlb) {
    ptw_block.retry_mem_req(MemPtwBlock::Client::ITLB);
  }
  if (ptw_wakeup.walk) {
    ptw_block.retry_active_walk();
  }
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

  {
    const auto dbg = ptw_block.debug_state();
    if (dbg.walk_active && !issue_ptw_walk_read) {
      ptw_walk_wait_cycles_++;
      if (ptw_walk_wait_cycles_ >= PTW_WALK_WAIT_RETRY_CYCLES) {
        ptw_block.retry_active_walk();
        ptw_walk_wait_cycles_ = 0;
      }
    } else {
      ptw_walk_wait_cycles_ = 0;
    }
  }

  read_arb_block.eval_comb(lsu2dcache, issue_ptw_walk_read, ptw_walk_read_addr,
                           has_ptw_dtlb, ptw_dtlb_addr, has_ptw_itlb,
                           ptw_itlb_addr);

  dcache_req_mux_ = read_arb_block.comb_result().dcache_req;
  dcache_resp_raw_ = {};

  dcache_.comb();

  resp_route_block.eval_comb(&dcache_resp_raw_,
                             read_arb_block.comb_result().issued_tags,
                             replay_bcast);
  resp_route_block.apply_ptw_event(&ptw_block);

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

  if (read_arb_block.comb_result().granted) {
    switch (read_arb_block.comb_result().granted_owner) {
    case MemReadArbBlock::Owner::PTW_DTLB:
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::DTLB);
      break;
    case MemReadArbBlock::Owner::PTW_ITLB:
      ptw_block.on_mem_read_granted(MemPtwBlock::Client::ITLB);
      break;
    case MemReadArbBlock::Owner::PTW_WALK:
      ptw_block.on_walk_read_granted();
      break;
    default:
      break;
    }
  }

  refresh_ptw_client_outputs();

  // NOTE:
  // mshr_axi_in / wb_axi_in / peripheral_axi_*_in are sourced from the top-level
  // bridge (rv_simu_mmu_v2.cpp). Do not overwrite them with internal AXI-kit
  // runtime feedback here, otherwise WB/MSHR may observe a different handshake
  // timeline than the one used by the external interconnect.
  // Phase 3b: inject AXI write-channel inputs and MSHR eviction, then run
  //           WriteBuffer comb_inputs (drains evictions onto AXI).
  wb_.in.axi_in   = wb_axi_in;
  wb_.in.mshrwb   = mshr_.out.mshrwb;  // eviction push (set in Phase 3a)
  wb_.comb_inputs();

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
  read_arb_block.update_seq();
  resp_route_block.update_seq();
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
        tag.req_addr, tag.uop.pc, tag.uop.instruction);
  }
  std::printf(
      "[DEADLOCK][MEM][RESP_ROUTE][TRACK] dtlb{blk=%d reason=%u addr=0x%08x} itlb{blk=%d reason=%u addr=0x%08x} walk{blk=%d reason=%u addr=0x%08x}\n",
      static_cast<int>(route_dbg.dtlb.blocked),
      static_cast<unsigned>(route_dbg.dtlb.reason), route_dbg.dtlb.req_addr,
      static_cast<int>(route_dbg.itlb.blocked),
      static_cast<unsigned>(route_dbg.itlb.reason), route_dbg.itlb.req_addr,
      static_cast<int>(route_dbg.walk.blocked),
      static_cast<unsigned>(route_dbg.walk.reason), route_dbg.walk.req_addr);
}
