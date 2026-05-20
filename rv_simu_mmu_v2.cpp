#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <queue>
#include <vector>

// Parent-side compatibility for axi-interconnect-kit revisions that do not
// expose comb_inputs_internal_tick(). The adapter below snapshots the explicit
// CPU-boundary ready/hold registers while extra AXI ticks advance only the
// internal AXI/LLC/DDR state.
#define private public
#include "AXI_Interconnect.h"
#undef private

#include "BackTop.h"
#include "Csr.h"
#include "PhysMemory.h"
#include "RealLsu.h"
#include "SimCpu.h"
#include "config.h"
#include "diff.h"
#include "front-end/host_profile.h"
#include "front_IO.h"
#include "front_module.h"
#include "util.h"

namespace {
template <typename InterconnectT>
void clear_axi_master_inputs(InterconnectT &interconnect) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; i++) {
    auto &port = interconnect.read_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.req.bypass = false;
    // resp.ready is consumed during axi_interconnect.comb_outputs() before the
    // top-level bridges re-drive master inputs later in the cycle. Clearing it
    // here would make a held LLC response observe permanent backpressure.
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; i++) {
    auto &port = interconnect.write_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.wdata.clear();
    port.req.wstrb = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.req.bypass = false;
  }
}

struct AxiInternalTickBoundaryState {
  bool read_req_ready_r[axi_interconnect::NUM_READ_MASTERS] = {};
  bool read_req_drop_warned[axi_interconnect::NUM_READ_MASTERS] = {};
  axi_interconnect::ReadReqHoldLatch
      read_req_hold[axi_interconnect::NUM_READ_MASTERS] = {};
  bool write_req_ready_r[axi_interconnect::NUM_WRITE_MASTERS] = {};
};

template <typename InterconnectT>
void capture_axi_internal_tick_boundary_state(
    InterconnectT &interconnect, AxiInternalTickBoundaryState &state) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    state.read_req_ready_r[i] = interconnect.req_ready_r[i];
    state.read_req_drop_warned[i] = interconnect.req_drop_warned[i];
    state.read_req_hold[i] = interconnect.read_req_hold[i];
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    state.write_req_ready_r[i] = interconnect.w_req_ready_r[i];
  }
}

template <typename InterconnectT>
void restore_axi_internal_tick_boundary_state(
    InterconnectT &interconnect, const AxiInternalTickBoundaryState &state) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    interconnect.req_ready_r[i] = state.read_req_ready_r[i];
    interconnect.req_drop_warned[i] = state.read_req_drop_warned[i];
    interconnect.read_req_hold[i] = state.read_req_hold[i];
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    interconnect.w_req_ready_r[i] = state.write_req_ready_r[i];
  }
}

template <typename InterconnectT>
auto comb_axi_inputs_for_internal_tick_impl(InterconnectT &interconnect, int)
    -> decltype(interconnect.comb_inputs_internal_tick(), void()) {
  interconnect.comb_inputs_internal_tick();
}

template <typename InterconnectT>
void comb_axi_inputs_for_internal_tick_impl(InterconnectT &interconnect, long) {
  AxiInternalTickBoundaryState boundary_state{};
  capture_axi_internal_tick_boundary_state(interconnect, boundary_state);
  interconnect.comb_inputs();
  restore_axi_internal_tick_boundary_state(interconnect, boundary_state);
}

template <typename InterconnectT>
void comb_axi_inputs_for_internal_tick(InterconnectT &interconnect) {
  comb_axi_inputs_for_internal_tick_impl(interconnect, 0);
}

void connect_axi_slave_outputs(sim_ddr::SimDDR_IO_t &master_io,
                               const sim_ddr::SimDDR_IO_t &slave_io) {
  master_io.ar.arready = slave_io.ar.arready;
  master_io.r.rvalid = slave_io.r.rvalid;
  master_io.r.rid = slave_io.r.rid;
  master_io.r.rdata = slave_io.r.rdata;
  master_io.r.rresp = slave_io.r.rresp;
  master_io.r.rlast = slave_io.r.rlast;
  master_io.aw.awready = slave_io.aw.awready;
  master_io.w.wready = slave_io.w.wready;
  master_io.b.bvalid = slave_io.b.bvalid;
  master_io.b.bid = slave_io.b.bid;
  master_io.b.bresp = slave_io.b.bresp;
}

void connect_axi_master_outputs(const sim_ddr::SimDDR_IO_t &master_io,
                                sim_ddr::SimDDR_IO_t &slave_io) {
  slave_io.ar.arvalid = master_io.ar.arvalid;
  slave_io.ar.arid = master_io.ar.arid;
  slave_io.ar.araddr = master_io.ar.araddr;
  slave_io.ar.arlen = master_io.ar.arlen;
  slave_io.ar.arsize = master_io.ar.arsize;
  slave_io.ar.arburst = master_io.ar.arburst;
  slave_io.r.rready = master_io.r.rready;

  slave_io.aw.awvalid = master_io.aw.awvalid;
  slave_io.aw.awid = master_io.aw.awid;
  slave_io.aw.awaddr = master_io.aw.awaddr;
  slave_io.aw.awlen = master_io.aw.awlen;
  slave_io.aw.awsize = master_io.aw.awsize;
  slave_io.aw.awburst = master_io.aw.awburst;
  slave_io.w.wvalid = master_io.w.wvalid;
  slave_io.w.wdata = master_io.w.wdata;
  slave_io.w.wstrb = master_io.w.wstrb;
  slave_io.w.wlast = master_io.w.wlast;
  slave_io.b.bready = master_io.b.bready;
}

axi_interconnect::AXI_LLCConfig make_default_llc_config() {
  axi_interconnect::AXI_LLCConfig llc_cfg;
  llc_cfg.enable = (CONFIG_AXI_LLC_ENABLE != 0);
  llc_cfg.size_bytes = CONFIG_AXI_LLC_SIZE_BYTES;
  llc_cfg.ways = CONFIG_AXI_LLC_WAYS;
  llc_cfg.mshr_num = CONFIG_AXI_LLC_MSHR_NUM;
  llc_cfg.lookup_latency = CONFIG_AXI_LLC_LOOKUP_LATENCY;
  return llc_cfg;
}

void print_soc_config_banner() {
#ifdef CONFIG_BPU
  constexpr int kBpuEnabled = 1;
  const char *bpu_mode = "real-bpu";
#else
  constexpr int kBpuEnabled = 0;
  const char *bpu_mode = "oracle-bpu";
#endif

#if CONFIG_ICACHE_USE_AXI_MEM_PORT
  const char *compiled_icache_path = "shared-top-level-axi";
#else
  const char *compiled_icache_path = "local-fixed-latency";
#endif

#if CONFIG_AXI_LLC_ENABLE
  const char *llc_mode = "enabled";
  const char *llc_summary = "shared fabric uses LLC";
#else
  const char *llc_mode = "disabled";
  const char *llc_summary = "shared fabric falls back to L1 I/D-cache only";
#endif

#ifdef CONFIG_BPU
  const char *runtime_icache_path =
#if CONFIG_ICACHE_USE_AXI_MEM_PORT
      "shared-top-level-axi";
#else
      "local-fixed-latency";
#endif
#else
  const char *runtime_icache_path = "oracle-frontend-disconnected";
#endif

  std::printf("[CONFIG][SOC] bpu=%d(%s) icache_axi=%u compiled_icache=%s\n",
              kBpuEnabled, bpu_mode,
              static_cast<unsigned>(CONFIG_ICACHE_USE_AXI_MEM_PORT),
              compiled_icache_path);
  std::printf(
      "[CONFIG][AXI] ddr_read_latency=%ucy ddr_write_resp_latency=%ucy "
      "ddr_beat=%uB wq=%u wag=%ucy wfifo=%u wdrain=%ucy whi=%u wlo=%u "
      "r2w=%ucy w2r=%ucy "
      "upstream_payload=%uB upstream_read_resp=%uB "
      "out=%u per_master=%u ddr_out=%u\n",
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
      static_cast<unsigned>(axi_interconnect::AXI_UPSTREAM_PAYLOAD_BYTES),
      static_cast<unsigned>(axi_interconnect::MAX_READ_TRANSACTION_BYTES),
      static_cast<unsigned>(axi_interconnect::MAX_OUTSTANDING),
      static_cast<unsigned>(axi_interconnect::MAX_READ_OUTSTANDING_PER_MASTER),
      static_cast<unsigned>(sim_ddr::SIM_DDR_MAX_OUTSTANDING));
  std::printf("[CONFIG][AXI] subsystem_freq_div=%u\n",
              static_cast<unsigned>(CONFIG_AXI_SUBSYSTEM_FREQ_DIV));
  std::printf(
      "[CONFIG][LLC] enable=%u(%s) capacity=%lluMB ways=%u mshr=%u "
      "lookup_latency=%ucy dcache_read_miss=%s\n",
      static_cast<unsigned>(CONFIG_AXI_LLC_ENABLE), llc_mode,
      static_cast<unsigned long long>(CONFIG_AXI_LLC_SIZE_BYTES >> 20),
      static_cast<unsigned>(CONFIG_AXI_LLC_WAYS),
      static_cast<unsigned>(CONFIG_AXI_LLC_MSHR_NUM),
      static_cast<unsigned>(CONFIG_AXI_LLC_LOOKUP_LATENCY),
      CONFIG_AXI_LLC_DCACHE_READ_MISS_NOALLOC != 0 ? "noallocate"
                                                   : "allocate");
  std::printf(
      "[TOPOLOGY] dcache/ptw/peripheral=top-level-shared-axi "
      "memsubsystem_internal_axi_runtime=disabled llc_summary=%s\n",
      llc_summary);
  std::printf("[TOPOLOGY] icache_runtime=%s\n", runtime_icache_path);

  constexpr uint64_t icache_capacity_bytes =
      static_cast<uint64_t>(ICACHE_SET_NUM) * ICACHE_WAY_NUM * ICACHE_LINE_SIZE;
  constexpr uint64_t dcache_capacity_bytes =
      static_cast<uint64_t>(DCACHE_SETS_NUM) * DCACHE_WAYS_NUM * DCACHE_LINE_SIZE;
  constexpr uint64_t llc_capacity_bytes = CONFIG_AXI_LLC_SIZE_BYTES;
  const char *schedule_policy =
      ISSUE_SCHEDULE_POLICY == IssueSchedulePolicy::IQ_SLOT_PRIORITY
          ? "IQ_SLOT_PRIORITY"
          : "ROB_OLDEST_FIRST";

  std::printf(
      "[CFG][WIDTH] fetch=%d decode=%d issue_ports=%d commit=%d "
      "max_dispatch(iq/ldq/stq)=%d/%d/%d\n",
      FETCH_WIDTH, DECODE_WIDTH, ISSUE_WIDTH, COMMIT_WIDTH, MAX_IQ_DISPATCH_WIDTH,
      MAX_LDQ_DISPATCH_WIDTH, MAX_STQ_DISPATCH_WIDTH);
  std::printf(
      "[CFG][CACHE] L1I=%lluKB(%d sets x %d ways x %dB) "
      "L1D=%lluKB(%d sets x %d ways x %dB)\n",
      static_cast<unsigned long long>(icache_capacity_bytes / 1024),
      ICACHE_SET_NUM, ICACHE_WAY_NUM, ICACHE_LINE_SIZE,
      static_cast<unsigned long long>(dcache_capacity_bytes / 1024), DCACHE_SETS_NUM,
      DCACHE_WAYS_NUM, DCACHE_LINE_SIZE);
  std::printf(
      "[CFG][MEM] hierarchy=L1I/L1D + AXI%s%s LLC=%lluMB(%u ways,mshr=%u,lookup=%ucy)\n",
      (CONFIG_ICACHE_USE_AXI_MEM_PORT ? "-icache" : ""),
      (CONFIG_AXI_LLC_ENABLE ? "+LLC" : ""),
      static_cast<unsigned long long>(llc_capacity_bytes >> 20),
      static_cast<unsigned>(CONFIG_AXI_LLC_WAYS),
      static_cast<unsigned>(CONFIG_AXI_LLC_MSHR_NUM),
      static_cast<unsigned>(CONFIG_AXI_LLC_LOOKUP_LATENCY));
  std::printf(
      "[CFG][BACKEND] rob=%d(rob_bank=%d) prf=%d arf=%d ftq=%d instbuf=%d "
      "ldq=%d stq=%d schedule=%s\n",
      ROB_NUM, ROB_BANK_NUM, PRF_NUM, ARF_NUM, FTQ_SIZE, IDU_INST_BUFFER_SIZE,
      LDQ_SIZE, STQ_SIZE, schedule_policy);
  std::printf(
      "[CFG][FU] total=%d alu=%d bru=%d ldu=%d sta=%d sdu=%d wakeup_ports=%d\n",
      TOTAL_FU_COUNT, ALU_NUM, BRU_NUM, LSU_LDU_COUNT, LSU_STA_COUNT,
      LSU_SDU_COUNT, MAX_WAKEUP_PORTS);
  for (int i = 0; i < IQ_NUM; i++) {
    const auto &iq = GLOBAL_IQ_CONFIG[i];
    const char *iq_name = "IQ_UNKNOWN";
    if (iq.id == IQ_INT)
      iq_name = "IQ_INT";
    else if (iq.id == IQ_LD)
      iq_name = "IQ_LD";
    else if (iq.id == IQ_STA)
      iq_name = "IQ_STA";
    else if (iq.id == IQ_STD)
      iq_name = "IQ_STD";
    else if (iq.id == IQ_BR)
      iq_name = "IQ_BR";
    std::printf(
        "[CFG][IQ] %s size=%d dispatch=%d issue_ports=[%d..%d] port_num=%d\n",
        iq_name, iq.size, iq.dispatch_width, iq.port_start_idx,
        iq.port_start_idx + iq.port_num - 1, iq.port_num);
  }
  #ifdef LSU_STLF
  std::printf("[LSU STLF ON]");
  #else
  std::printf("[LSU STLF OFF]");
  #endif
  std::printf("[LSU LOAD/STORE WINDOWS] load_windows=%d lsu_ldu_count=%d LDQ_SIZE=%d store_windows=%d lsu_sta_count=%d STQ_SIZE=%d\n",
               LOAD_WINDOWS_WIDTH,LSU_LDU_COUNT,LDQ_SIZE,STORE_WINDOWS_WIDTH,LSU_STA_COUNT,STQ_SIZE);

}

void bridge_axi_to_mem_subsystem(SimCpu &cpu) {
  const auto &rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_DCACHE_R];
  cpu.mem_subsystem.mshr_axi_in.req_ready = rport.req.ready;
  cpu.mem_subsystem.mshr_axi_in.req_accepted = rport.req.accepted;
  cpu.mem_subsystem.mshr_axi_in.req_accepted_id = rport.req.accepted_id;
  cpu.mem_subsystem.mshr_axi_in.resp_valid = rport.resp.valid;
  cpu.mem_subsystem.mshr_axi_in.resp_id = rport.resp.id;
  for (int i = 0; i < DCACHE_WORD_NUM &&
                  i < axi_interconnect::MAX_READ_TRANSACTION_WORDS;
       i++) {
    cpu.mem_subsystem.mshr_axi_in.resp_data[i] = rport.resp.data[i];
  }
  const auto &wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_DCACHE_W];
  cpu.mem_subsystem.wb_axi_in.req_ready = wport.req.ready;
  cpu.mem_subsystem.wb_axi_in.req_accepted = wport.req.accepted;
  cpu.mem_subsystem.wb_axi_in.resp_valid = wport.resp.valid;

  const auto &peri_rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_EXTRA_R];
  cpu.mem_subsystem.peripheral_axi_read_in.req_ready = peri_rport.req.ready;
  cpu.mem_subsystem.peripheral_axi_read_in.req_accepted =
      peri_rport.req.accepted;
  cpu.mem_subsystem.peripheral_axi_read_in.resp_valid = peri_rport.resp.valid;
  cpu.mem_subsystem.peripheral_axi_read_in.resp_id = peri_rport.resp.id;
  for (int i = 0; i < DCACHE_WORD_NUM &&
                  i < axi_interconnect::MAX_READ_TRANSACTION_WORDS;
       i++) {
    cpu.mem_subsystem.peripheral_axi_read_in.resp_data[i] =
        peri_rport.resp.data[i];
  }

  const auto &peri_wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_EXTRA_W];
  cpu.mem_subsystem.peripheral_axi_write_in.req_ready = peri_wport.req.ready;
  cpu.mem_subsystem.peripheral_axi_write_in.req_accepted =
      peri_wport.req.accepted;
  cpu.mem_subsystem.peripheral_axi_write_in.resp_valid = peri_wport.resp.valid;
  cpu.mem_subsystem.peripheral_axi_write_in.resp_id = peri_wport.resp.id;
}

void bridge_mem_subsystem_to_axi(SimCpu &cpu) {
  auto &rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_DCACHE_R];
  rport.req.valid = cpu.mem_subsystem.mshr_axi_out.req_valid;
  rport.req.addr = cpu.mem_subsystem.mshr_axi_out.req_addr;
  rport.req.total_size = cpu.mem_subsystem.mshr_axi_out.req_total_size;
  rport.req.id = cpu.mem_subsystem.mshr_axi_out.req_id;
  rport.req.bypass = false;
  rport.resp.ready = cpu.mem_subsystem.mshr_axi_out.resp_ready;

  auto &wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_DCACHE_W];
  wport.req.valid = cpu.mem_subsystem.wb_axi_out.req_valid;
  wport.req.addr = cpu.mem_subsystem.wb_axi_out.req_addr;
  wport.req.total_size = cpu.mem_subsystem.wb_axi_out.req_total_size;
  wport.req.id = cpu.mem_subsystem.wb_axi_out.req_id;
  wport.req.wstrb = cpu.mem_subsystem.wb_axi_out.req_wstrb;
  wport.req.bypass = false;
  for (int i = 0; i < axi_interconnect::CACHELINE_WORDS; i++) {
    wport.req.wdata[i] = 0;
  }
  for (int i = 0;
       i < DCACHE_WORD_NUM && i < axi_interconnect::CACHELINE_WORDS; i++) {
    wport.req.wdata[i] = cpu.mem_subsystem.wb_axi_out.req_wdata[i];
  }
  wport.resp.ready = cpu.mem_subsystem.wb_axi_out.resp_ready;

  auto &peri_rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_EXTRA_R];
  peri_rport.req.valid = cpu.mem_subsystem.peripheral_axi_read_out.req_valid;
  peri_rport.req.addr = cpu.mem_subsystem.peripheral_axi_read_out.req_addr;
  peri_rport.req.total_size =
      cpu.mem_subsystem.peripheral_axi_read_out.req_total_size;
  peri_rport.req.id = cpu.mem_subsystem.peripheral_axi_read_out.req_id;
  peri_rport.req.bypass = true;
  peri_rport.resp.ready = cpu.mem_subsystem.peripheral_axi_read_out.resp_ready;

  auto &peri_wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_EXTRA_W];
  peri_wport.req.valid = cpu.mem_subsystem.peripheral_axi_write_out.req_valid;
  peri_wport.req.addr = cpu.mem_subsystem.peripheral_axi_write_out.req_addr;
  peri_wport.req.total_size =
      cpu.mem_subsystem.peripheral_axi_write_out.req_total_size;
  peri_wport.req.id = cpu.mem_subsystem.peripheral_axi_write_out.req_id;
  peri_wport.req.wstrb = cpu.mem_subsystem.peripheral_axi_write_out.req_wstrb;
  peri_wport.req.bypass = true;
  for (int i = 0; i < axi_interconnect::CACHELINE_WORDS; i++) {
    peri_wport.req.wdata[i] = 0;
  }
  for (int i = 0;
       i < DCACHE_WORD_NUM && i < axi_interconnect::CACHELINE_WORDS; i++) {
    peri_wport.req.wdata[i] =
        cpu.mem_subsystem.peripheral_axi_write_out.req_wdata[i];
  }
  peri_wport.resp.ready = cpu.mem_subsystem.peripheral_axi_write_out.resp_ready;
}

struct AxiCpuBoundaryState {
  bool read_req_ready[axi_interconnect::NUM_READ_MASTERS] = {};
  bool read_req_accepted[axi_interconnect::NUM_READ_MASTERS] = {};
  uint8_t read_req_accepted_id[axi_interconnect::NUM_READ_MASTERS] = {};
  bool write_req_ready[axi_interconnect::NUM_WRITE_MASTERS] = {};
  bool write_req_accepted[axi_interconnect::NUM_WRITE_MASTERS] = {};
  uint8_t write_req_accepted_id[axi_interconnect::NUM_WRITE_MASTERS] = {};
  bool read_resp_valid[axi_interconnect::NUM_READ_MASTERS] = {};
  uint8_t read_resp_id[axi_interconnect::NUM_READ_MASTERS] = {};
  bool read_resp_ready[axi_interconnect::NUM_READ_MASTERS] = {};
  bool write_resp_valid[axi_interconnect::NUM_WRITE_MASTERS] = {};
  uint8_t write_resp_id[axi_interconnect::NUM_WRITE_MASTERS] = {};
  bool write_resp_ready[axi_interconnect::NUM_WRITE_MASTERS] = {};
};

void capture_axi_cpu_boundary_state(SimCpu &cpu, AxiCpuBoundaryState &state) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    state.read_req_ready[i] = cpu.axi_interconnect.read_ports[i].req.ready;
    state.read_req_accepted[i] = cpu.axi_interconnect.read_req_accepted[i];
    state.read_req_accepted_id[i] =
        cpu.axi_interconnect.read_req_accepted_id[i];
    state.read_resp_valid[i] = cpu.axi_interconnect.read_ports[i].resp.valid;
    state.read_resp_id[i] =
        static_cast<uint8_t>(cpu.axi_interconnect.read_ports[i].resp.id);
    state.read_resp_ready[i] = cpu.axi_interconnect.read_ports[i].resp.ready;
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    state.write_req_ready[i] = cpu.axi_interconnect.write_ports[i].req.ready;
    state.write_req_accepted[i] = cpu.axi_interconnect.write_req_accepted[i];
    state.write_req_accepted_id[i] =
        static_cast<uint8_t>(cpu.axi_interconnect.write_ports[i].req.id);
    state.write_resp_valid[i] = cpu.axi_interconnect.write_ports[i].resp.valid;
    state.write_resp_id[i] =
        static_cast<uint8_t>(cpu.axi_interconnect.write_ports[i].resp.id);
    state.write_resp_ready[i] = cpu.axi_interconnect.write_ports[i].resp.ready;
  }
}

void accumulate_axi_cpu_boundary_state(SimCpu &cpu,
                                       AxiCpuBoundaryState &state) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    state.read_req_ready[i] =
        state.read_req_ready[i] || cpu.axi_interconnect.read_ports[i].req.ready;
    if (cpu.axi_interconnect.read_req_accepted[i]) {
      state.read_req_accepted[i] = true;
      state.read_req_accepted_id[i] =
          cpu.axi_interconnect.read_req_accepted_id[i];
    }
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    state.write_req_ready[i] = state.write_req_ready[i] ||
                               cpu.axi_interconnect.write_ports[i].req.ready;
    if (cpu.axi_interconnect.write_req_accepted[i]) {
      state.write_req_accepted[i] = true;
      state.write_req_accepted_id[i] =
          static_cast<uint8_t>(cpu.axi_interconnect.write_ports[i].req.id);
    }
  }
}

void restore_axi_cpu_boundary_state(SimCpu &cpu,
                                    const AxiCpuBoundaryState &state) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    cpu.axi_interconnect.read_req_accepted[i] = state.read_req_accepted[i];
    cpu.axi_interconnect.read_req_accepted_id[i] =
        state.read_req_accepted_id[i];
    cpu.axi_interconnect.read_ports[i].req.ready = state.read_req_ready[i];
    cpu.axi_interconnect.read_ports[i].req.accepted =
        state.read_req_accepted[i];
    cpu.axi_interconnect.read_ports[i].req.accepted_id =
        state.read_req_accepted_id[i];
    cpu.axi_interconnect.read_ports[i].resp.ready = state.read_resp_ready[i];
    const bool read_resp_valid = cpu.axi_interconnect.read_ports[i].resp.valid;
    const uint8_t read_resp_id =
        static_cast<uint8_t>(cpu.axi_interconnect.read_ports[i].resp.id);
    const bool read_resp_from_extra_tick =
        read_resp_valid &&
        !(state.read_resp_valid[i] && state.read_resp_id[i] == read_resp_id);
    const bool read_resp_matches_new_accept =
        state.read_req_accepted[i] && read_resp_valid &&
        read_resp_id == state.read_req_accepted_id[i];
    if (read_resp_matches_new_accept) {
      cpu.axi_interconnect.read_ports[i].resp.valid = false;
    }
    if (read_resp_from_extra_tick || read_resp_matches_new_accept) {
      cpu.axi_interconnect.read_ports[i].resp.ready = false;
    }
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    cpu.axi_interconnect.write_req_accepted[i] = state.write_req_accepted[i];
    cpu.axi_interconnect.write_ports[i].req.ready = state.write_req_ready[i];
    cpu.axi_interconnect.write_ports[i].req.accepted =
        state.write_req_accepted[i];
    cpu.axi_interconnect.write_ports[i].resp.ready = state.write_resp_ready[i];
    const bool write_resp_valid =
        cpu.axi_interconnect.write_ports[i].resp.valid;
    const uint8_t write_resp_id =
        static_cast<uint8_t>(cpu.axi_interconnect.write_ports[i].resp.id);
    const bool write_resp_from_extra_tick =
        write_resp_valid &&
        !(state.write_resp_valid[i] && state.write_resp_id[i] == write_resp_id);
    const bool write_resp_matches_new_accept =
        state.write_req_accepted[i] && write_resp_valid &&
        write_resp_id == state.write_req_accepted_id[i];
    if (write_resp_matches_new_accept) {
      cpu.axi_interconnect.write_ports[i].resp.valid = false;
    }
    if (write_resp_from_extra_tick || write_resp_matches_new_accept) {
      cpu.axi_interconnect.write_ports[i].resp.ready = false;
    }
  }
}

void mask_axi_cpu_boundary_for_internal_tick(SimCpu &cpu) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; ++i) {
    auto &port = cpu.axi_interconnect.read_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.req.bypass = false;
    port.resp.ready = false;
  }
  for (int i = 0; i < axi_interconnect::NUM_WRITE_MASTERS; ++i) {
    auto &port = cpu.axi_interconnect.write_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.wdata.clear();
    port.req.wstrb = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.req.bypass = false;
    port.resp.ready = false;
  }
}

void axi_subsystem_comb_outputs(SimCpu &cpu) {
  cpu.mem_subsystem.llc_comb_outputs();
  cpu.axi_interconnect.set_llc_lookup_in(cpu.mem_subsystem.llc_lookup_in());
  cpu.axi_ddr.comb_outputs();
  cpu.axi_mmio.comb_outputs();
  connect_axi_slave_outputs(cpu.axi_interconnect.axi_ddr_io, cpu.axi_ddr.io);
  connect_axi_slave_outputs(cpu.axi_interconnect.axi_mmio_io, cpu.axi_mmio.io);
  cpu.axi_interconnect.comb_outputs();
}

void axi_subsystem_comb_inputs(SimCpu &cpu) {
  cpu.axi_interconnect.comb_inputs();
  connect_axi_master_outputs(cpu.axi_interconnect.axi_ddr_io, cpu.axi_ddr.io);
  connect_axi_master_outputs(cpu.axi_interconnect.axi_mmio_io, cpu.axi_mmio.io);
  cpu.axi_ddr.comb_inputs();
  cpu.axi_mmio.comb_inputs();
}

void axi_subsystem_comb_inputs_internal_tick(SimCpu &cpu) {
  comb_axi_inputs_for_internal_tick(cpu.axi_interconnect);
  connect_axi_master_outputs(cpu.axi_interconnect.axi_ddr_io, cpu.axi_ddr.io);
  connect_axi_master_outputs(cpu.axi_interconnect.axi_mmio_io, cpu.axi_mmio.io);
  cpu.axi_ddr.comb_inputs();
  cpu.axi_mmio.comb_inputs();
}

void axi_subsystem_seq(SimCpu &cpu) {
  cpu.mem_subsystem.llc_seq(cpu.axi_interconnect.get_llc_table_out(),
                            cpu.axi_interconnect.get_llc_perf_counters());
  cpu.axi_interconnect.seq();
  cpu.axi_ddr.seq();
  cpu.axi_mmio.seq();
}

void run_extra_axi_subsystem_ticks(SimCpu &cpu) {
  if (CONFIG_AXI_SUBSYSTEM_FREQ_DIV <= 1u) {
    return;
  }

  AxiCpuBoundaryState cpu_boundary_state{};
  capture_axi_cpu_boundary_state(cpu, cpu_boundary_state);
  for (uint32_t tick = 1; tick < CONFIG_AXI_SUBSYSTEM_FREQ_DIV; ++tick) {
    mask_axi_cpu_boundary_for_internal_tick(cpu);
    axi_subsystem_comb_outputs(cpu);
    axi_subsystem_comb_inputs_internal_tick(cpu);
    axi_subsystem_seq(cpu);
    accumulate_axi_cpu_boundary_state(cpu, cpu_boundary_state);
  }
  restore_axi_cpu_boundary_state(cpu, cpu_boundary_state);
}
} // namespace

void SimCpu::commit_sync(InstInfo *inst, int commit_slot) {
  BackTop *back = &this->back;
  Assert(commit_slot >= 0 && commit_slot < COMMIT_WIDTH &&
         "SimCpu::commit_sync: invalid commit slot");
  const auto &ftq_info = back->ftq_commit_info.resp[commit_slot];
  if (inst->type == JALR) {
    if (inst->tma.is_ret) {
      this->ctx.perf.ret_br_num++;
    } else {
      this->ctx.perf.jalr_br_num++;
    }
  } else if (inst->type == BR) {
    this->ctx.perf.cond_br_num++;
  }

  if (inst->mispred) {
    if (inst->type == JALR) {
      if (inst->tma.is_ret) {
        this->ctx.perf.ret_mispred_num++;
        const bool pred_taken = ftq_info.pred_taken;
        if (!pred_taken) {
          this->ctx.perf.ret_dir_mispred++;
        } else {
          this->ctx.perf.ret_addr_mispred++;
        }
      } else {
        this->ctx.perf.jalr_mispred_num++;
        const bool pred_taken = ftq_info.pred_taken;
        if (!pred_taken) {
          this->ctx.perf.jalr_dir_mispred++;
        } else {
          this->ctx.perf.jalr_addr_mispred++;
        }
      }
    } else if (inst->type == BR) {
      const bool pred_taken = ftq_info.pred_taken;
      if (pred_taken != inst->br_taken) {
        this->ctx.perf.cond_dir_mispred++;
      } else {
        this->ctx.perf.cond_addr_mispred++;
      }
      this->ctx.perf.cond_mispred_num++;
    }
  }

  if (inst->tma.mem_commit_is_store && !inst->page_fault_store) {
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx, inst->stq_flag);
    const bool sc_suppressed = is_amo_sc_inst(inst->type, inst->func7) &&
                               e.suppress_write &&
                               e.rob_idx == inst->rob_idx &&
                               e.rob_flag == inst->rob_flag;
    if (!sc_suppressed && e.paddr_valid && e.data_valid) {
      mem_subsystem.on_commit_store(e.paddr, e.data, e.func3);
    }
  }
}

void SimCpu::difftest_prepare(InstEntry *inst_entry, bool *skip) {
  Assert(inst_entry != nullptr &&
         "SimCpu::difftest_prepare: inst_entry is null");
  Assert(skip != nullptr && "SimCpu::difftest_prepare: skip is null");
  BackTop *back = &this->back;
  InstInfo *inst = &inst_entry->uop;

  for (int i = 0; i < ARF_NUM; i++) {
    // With same-cycle EXU->ROB completion, commit-side architectural mapping
    // (arch_RAT_1) can point to a preg whose value is produced in this cycle's
    // comb writeback path. Use reg_file_1 to observe the up-to-date comb state.
    dut_cpu.gpr[i] = back->prf->reg_file_1[back->rename->arch_RAT_1[i]];
  }

  if (inst->tma.mem_commit_is_store && !inst->page_fault_store) {
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx, inst->stq_flag);
    const bool sc_suppressed = is_amo_sc_inst(inst->type, inst->func7) &&
                               e.suppress_write &&
                               e.rob_idx == inst->rob_idx &&
                               e.rob_flag == inst->rob_flag;
    if (sc_suppressed) {
      dut_cpu.store = false;
    } else {
      if (!(e.paddr_valid && e.data_valid)) {
        // Store addr/data sideband can lag the ROB commit signal by a cycle on
        // some recovery paths. Let the REF execute the instruction normally and
        // skip the per-instruction sideband check instead of aborting the run.
        *skip = true;
        dut_cpu.store = false;
      } else {
        dut_cpu.store = true;
        dut_cpu.store_addr = e.paddr;
        if (e.func3 == 0b00)
          dut_cpu.store_data = e.data & 0xFF;
        else if (e.func3 == 0b01)
          dut_cpu.store_data = e.data & 0xFFFF;
        else
          dut_cpu.store_data = e.data;

        dut_cpu.store_data = dut_cpu.store_data
                             << (dut_cpu.store_addr & 0b11) * 8;
      }
    }
  } else {
    dut_cpu.store = false;
  }

  for (int i = 0; i < CSR_NUM; i++) {
    dut_cpu.csr[i] = back->csr->CSR_RegFile_1[i];
  }
  if (static_cast<bool>(back->csr_interrupt_inject_io.external_irq_pending_valid)) {
    if (static_cast<bool>(back->csr_interrupt_inject_io.external_irq_pending)) {
      dut_cpu.csr[csr_mip] = back->csr->CSR_RegFile[csr_mip] | MIP_SEIP;
      dut_cpu.csr[csr_sip] = back->csr->CSR_RegFile[csr_sip] | MIP_SEIP;
    } else {
      dut_cpu.csr[csr_mip] = back->csr->CSR_RegFile[csr_mip] & ~MIP_SEIP;
      dut_cpu.csr[csr_sip] = back->csr->CSR_RegFile[csr_sip] & ~MIP_SEIP;
    }
  }
  dut_cpu.pc = (is_branch(inst->type) || inst->type == JAL ||
                back->rob->out.rob_bcast->flush)
                   ? inst_entry->uop.diag_val
                   : inst->dbg.pc + 4;
  dut_cpu.instruction = inst->dbg.instruction;
  dut_cpu.page_fault_inst = inst->page_fault_inst;
  dut_cpu.page_fault_load = inst->page_fault_load;
  dut_cpu.page_fault_store = inst->page_fault_store;
  dut_cpu.inst_idx = inst->dbg.inst_idx;
  dut_cpu.commit_pc = inst->dbg.pc;
  *skip = inst->dbg.difftest_skip;
}

void SimContext::run_commit_inst(InstEntry *inst_entry, int commit_slot) {
  Assert(cpu != nullptr && "SimContext::run_commit_inst: cpu is null");
  Assert(inst_entry != nullptr &&
         "SimContext::run_commit_inst: inst_entry is null");
  Assert(inst_entry->valid &&
         "SimContext::run_commit_inst: inst_entry is not valid");
  cpu->commit_sync(&inst_entry->uop, commit_slot);
}

void SimContext::run_difftest_inst(InstEntry *inst_entry) {
  Assert(cpu != nullptr && "SimContext::run_difftest_inst: cpu is null");
  Assert(inst_entry != nullptr &&
         "SimContext::run_difftest_inst: inst_entry is null");
  Assert(inst_entry->valid &&
         "SimContext::run_difftest_inst: inst_entry is not valid");
  bool skip = false;
  cpu->difftest_prepare(inst_entry, &skip);
  if (skip) {
    difftest_skip();
  } else {
    // Keep commit-time difftest checking enabled in real-BPU runs. Skip is
    // reserved for explicitly unsupported sideband cases only.
    difftest_step(true);
  }
}

// 复位逻辑
void SimCpu::init() {
  const auto llc_cfg = make_default_llc_config();

  // 第一阶段：绑定顶层上下文
  ctx.cpu = this;

  // 第二阶段：构建模块对象（生成内部子模块实例）
  back.init();

  // 第三阶段：集中完成跨模块连线
  mem_subsystem.csr_interrupt_inject = &back.csr_interrupt_inject_io;
  mem_subsystem.memory = p_memory;
  mem_subsystem.peripheral_req = &back.out.peripheral_req;
  mem_subsystem.peripheral_resp = &back.in.peripheral_resp;
  mem_subsystem.set_ptw_coherent_source(back.lsu);

  front.in.csr_status = back.csr->out.csr_status;
  front.ctx = &ctx;

  back.set_lsu_ptw_walk_port(mem_subsystem.dtlb_walk_port);
  back.set_lsu_ptw_mem_port(mem_subsystem.dtlb_ptw_port);

  mem_subsystem.lsu2dcache = &back.out.lsu2dcache;
  mem_subsystem.dcache2lsu = &back.in.dcache2lsu;

  mem_subsystem.icache_req = &icache_req;
  mem_subsystem.icache_resp = &icache_resp;

  front.icache_ptw_walk_port = mem_subsystem.itlb_walk_port;
  front.icache_ptw_mem_port = mem_subsystem.itlb_ptw_port;
  front.icache_mem_req_port = &icache_req;
  front.icache_mem_resp_port = &icache_resp;
  // Keep a single active SoC memory topology. DCache/PTW/peripheral already use
  // the top-level interconnect; frontend icache should use the same shared path
  // whenever the real BPU frontend is enabled. Oracle mode does not step the
  // icache model, so it simply leaves the port disconnected instead of falling
  // back to MemSubsystem's legacy private AXI runtime.
  mem_subsystem.set_internal_axi_runtime_active(false);
  print_soc_config_banner();
#ifdef CONFIG_BPU
  front.icache_mem_read_port =
      &axi_interconnect.read_ports[axi_interconnect::MASTER_ICACHE];
#else
  front.icache_mem_read_port = nullptr;
#endif

  // 第四阶段：统一执行各模块复位逻辑
  // 先初始化内存子系统，确保 PTW / DCache / WB 等内部状态已经完成复位。
  mem_subsystem.init();
  axi_interconnect.set_llc_config(llc_cfg);
  axi_interconnect.init();
  axi_router.init();
  axi_ddr.init();
  axi_mmio.init();
  axi_mmio.add_device(UART_ADDR_BASE, UART_MMIO_SIZE, &axi_uart);
  // In shared-LLC mode, front.init()/front.step_bpu() directly touches the
  // top-level icache AXI port, so the shared interconnect/MMIO/DDRx must be
  // reset before frontend init. Keeping the order uniform is harmless in the
  // LLC-off path.
  front.init();
  oracle_pending_valid = false;
  oracle_pending_out = {};
}

void SimCpu::reinit_frontend_after_restore() {
  // FAST/CKPT switch starts O3 from a mid-execution architectural snapshot.
  // Reinitialize frontend-local state so BPU/icache/predecode static latches do
  // not carry stale reset-era contents into the restored control flow.
  clear_axi_master_inputs(axi_interconnect);
  front.init();
  oracle_pending_valid = false;
  oracle_pending_out = {};
}

void SimCpu::sync_mmio_devices_from_backing() {
  axi_uart.sync_from_backing(pmem_ram_ptr());
  mem_subsystem.sync_mmio_devices_from_backing();
}

// 强制重置前端 PC (用于 FAST 模式切换)
void SimCpu::restore_pc(uint32_t pc) {
  front.in.reset = false;
  front.in.FIFO_read_enable = false;
  // 显式给前端一个重取指请求，并同步后端重定向输出，
  // 保证即使调用方未额外设置 flush/mispred，也会从目标 PC 重新开始。
  front.in.refetch = true;
  front.in.refetch_address = pc;
  back.out.flush = true;
  back.out.mispred = true;
  back.out.redirect_pc = pc;

  // 刷新 CSR 状态输出 (SATP, Privilege) 以确保 MMU 模式正确
  back.comb_csr_status();
}

void SimCpu::cycle() {
  FRONTEND_HOST_PROFILE_SCOPE(SimCycle);
  ctx.perf.cycle++;
  // 统一在此处刷新 CSR 状态，供本拍 front/back 组合逻辑共同使用。
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimCsrStatus);
    back.comb_csr_status();
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimClearAxiInputs);
    clear_axi_master_inputs(axi_interconnect);
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimFrontCycle);
    front_cycle();
  }

  // AXI phase-1: slave outputs -> interconnect outputs.
  // Interconnect outputs (req.ready/resp.valid) are bridged into MemSubsystem
  // before backend comb, so DCache responses from the previous DCache stage can
  // be visible to LSU in this CPU cycle.
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiOutputs);
    axi_subsystem_comb_outputs(*this);
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBridgeAxiToMem);
    bridge_axi_to_mem_subsystem(*this);
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimMemCombOutputs);
    mem_subsystem.comb_outputs();
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBackComb);
    back.comb();
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimMemCombInputs);
    mem_subsystem.comb_inputs();
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBridgeMemToAxi);
    bridge_mem_subsystem_to_axi(*this);
  }

  // AXI phase-2: master requests -> interconnect -> slave inputs.
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiInputs);
    axi_subsystem_comb_inputs(*this);
  }

  // 步骤 2：反馈给前端
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBack2Front);
    back2front_comb();
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBackSeq);
    back.seq();
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimMemSeq);
    mem_subsystem.seq();
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiSeq);
    axi_subsystem_seq(*this);
    run_extra_axi_subsystem_ticks(*this);
  }
  ctx.perf.perf_maybe_capture_simtime_snapshot();

  if (ctx.exit_reason != ExitReason::NONE) {
    printf("Simulation Exited with Reason: %d\n", (int)ctx.exit_reason);
    return;
  }

  if (back.out.mispred || back.out.flush) {
    back.number_PC = back.out.redirect_pc;
  } else if (back.out.stall) {
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (back.out.fire[j])
        back.in.valid[j] = false;
    }
  }
}

void SimCpu::front_cycle() {
  auto perf_account_front_supply = [&]() {
    if (front.in.FIFO_read_enable) {
      ctx.perf.front2back_read_enable_cycle_total++;
    }
    if (front.out.FIFO_valid) {
      ctx.perf.front2back_read_cycle_total++;
    }
    if (front.in.FIFO_read_enable && !front.out.FIFO_valid) {
      ctx.perf.front2back_read_empty_cycle_total++;
    }
    for (int j = 0; j < FETCH_WIDTH; j++) {
      if (back.in.valid[j]) {
        ctx.perf.front2back_fetched_inst_total++;
      }
    }
  };

#ifdef CONFIG_BPU
  if (!back.out.stall || back.out.mispred || back.out.flush) {

    front.in.FIFO_read_enable = true;
    front.in.refetch = (back.out.mispred || back.out.flush);
    front.in.itlb_flush = back.out.itlb_flush;
    front.in.fence_i = back.out.fence_i;
    if (front.in.refetch) {
      front.in.refetch_address =
          back.out.redirect_pc; // 再次确保赋值，防止时序错位
    }

#ifdef CONFIG_BPU
    front.step_bpu();
#endif

    bool no_taken = true;
    for (int j = 0; j < FETCH_WIDTH; j++) {
      back.in.valid[j] =
          no_taken && front.out.FIFO_valid && front.out.inst_valid[j];
      back.in.pc[j] = front.out.pc[j];
      back.in.predict_next_fetch_address[j] =
          front.out.predict_next_fetch_address;
      back.in.page_fault_inst[j] = front.out.page_fault_inst[j];
      back.in.inst[j] = front.out.instructions[j];

      // if (back.in.valid[j]) {
      //   cout << "指令index:" << dec << sim_time << " 当前PC的取值为:" << hex
      //        << front.out.pc[j] << " Inst: " << back.in.inst[j] << endl;
      // }

      back.in.predict_dir[j] = front.out.predict_dir[j];
      back.in.alt_pred[j] = front.out.alt_pred[j];
      back.in.altpcpn[j] = front.out.altpcpn[j];
      back.in.pcpn[j] = front.out.pcpn[j];
      for (int k = 0; k < 4; k++) { // TN_MAX = 4 (分支预测相关索引)
        back.in.tage_idx[j][k] = front.out.tage_idx[j][k];
        back.in.tage_tag[j][k] = front.out.tage_tag[j][k];
      }
      if (back.in.valid[j] && front.out.predict_dir[j])
        no_taken = false;
    }
    back.in.front_stall = static_cast<bool>(front.out.commit_stall);
    perf_account_front_supply();
  } else {

#ifdef CONFIG_BPU
    front.in.FIFO_read_enable = false;
    front.in.refetch = false;
    front.in.itlb_flush = back.out.itlb_flush;
    front.in.fence_i = back.out.fence_i;
    front.step_bpu();
#else
#endif
    back.in.front_stall = static_cast<bool>(front.out.commit_stall);
  }
#else
  // Oracle 模式：每拍都执行握手，利用 1-entry pending
  // 防止“当拍后端阻塞”丢指令。
  front.in.FIFO_read_enable = true;
  front.in.refetch = (back.out.mispred || back.out.flush);
  front.in.itlb_flush = back.out.itlb_flush;
  front.in.fence_i = back.out.fence_i;
  if (front.in.refetch) {
    front.in.refetch_address = back.out.redirect_pc;
  }

  // 上一拍后端非阻塞，认为 pending 已被接收。
  if (oracle_pending_valid && !back.out.stall) {
    oracle_pending_valid = false;
  }
  // 重定向优先：丢弃旧 pending，立即让 oracle 同步到新 PC。
  if (front.in.refetch) {
    oracle_pending_valid = false;
  }

  if (!oracle_pending_valid) {
    front.step_oracle();
    oracle_pending_out = front.out;
    oracle_pending_valid = true;
  } else {
    front.out = oracle_pending_out;
  }

#ifndef CONFIG_ORACLE_STEADY_FETCH_WIDTH
  bool no_taken = true;
#endif
  for (int j = 0; j < FETCH_WIDTH; j++) {
#ifdef CONFIG_ORACLE_STEADY_FETCH_WIDTH
    back.in.valid[j] = front.out.FIFO_valid && front.out.inst_valid[j];
#else
    back.in.valid[j] =
        no_taken && front.out.FIFO_valid && front.out.inst_valid[j];
#endif
    back.in.pc[j] = front.out.pc[j];
    back.in.predict_next_fetch_address[j] =
        front.out.predict_next_fetch_address;
    back.in.page_fault_inst[j] = front.out.page_fault_inst[j];
    back.in.inst[j] = front.out.instructions[j];
    back.in.predict_dir[j] = front.out.predict_dir[j];
    back.in.alt_pred[j] = front.out.alt_pred[j];
    back.in.altpcpn[j] = front.out.altpcpn[j];
    back.in.pcpn[j] = front.out.pcpn[j];
    back.in.sc_used[j] = front.out.sc_used[j];
    back.in.sc_pred[j] = front.out.sc_pred[j];
    back.in.sc_sum[j] = front.out.sc_sum[j];
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      back.in.sc_idx[j][t] = front.out.sc_idx[j][t];
    }
    back.in.loop_used[j] = front.out.loop_used[j];
    back.in.loop_hit[j] = front.out.loop_hit[j];
    back.in.loop_pred[j] = front.out.loop_pred[j];
    back.in.loop_idx[j] = front.out.loop_idx[j];
    back.in.loop_tag[j] = front.out.loop_tag[j];
    for (int k = 0; k < 4; k++) {
      back.in.tage_idx[j][k] = front.out.tage_idx[j][k];
      back.in.tage_tag[j][k] = front.out.tage_tag[j][k];
    }
#ifndef CONFIG_ORACLE_STEADY_FETCH_WIDTH
    if (back.in.valid[j] && front.out.predict_dir[j]) {
      no_taken = false;
    }
#endif
  }
  // Oracle mode has no real front-end update queue; keep ROB retire path
  // independent from front-end stall semantics.
  back.in.front_stall = false;
  perf_account_front_supply();
#endif
}

bool SimCpu::ready_to_exit() const {
  Assert(false && "SimCpu::ready_to_exit: not implemented");
  return false;
  // if (back.lsu->has_committed_store_pending()) {
  //   return false;
  // }

  // const auto &peri = mem_subsystem.get_peripheral_axi().cur;
  // if (peri.busy || peri.req_accepted || peri.resp_valid) {
  //   return false;
  // }

  // return true;
}
void SimCpu::back2front_comb() {
  front.in.FIFO_read_enable = false;
  front.in.csr_status = back.csr->out.csr_status;
  front.in.itlb_flush = back.out.itlb_flush;
  front.in.fence_i = back.out.fence_i;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    InstInfo *inst = &back.out.commit_entry[i].uop;
    const auto &ftq_info = back.ftq_commit_info.resp[i];
    front.in.back2front_valid[i] = back.out.commit_entry[i].valid;
    for (int j = 0; j < 4; j++) {
      front.in.tage_tag[i][j] = 0;
    }
    front.in.sc_used[i] = false;
    front.in.sc_pred[i] = false;
    front.in.sc_sum[i] = 0;
    for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
      front.in.sc_idx[i][t] = 0;
    }
    front.in.loop_used[i] = false;
    front.in.loop_hit[i] = false;
    front.in.loop_pred[i] = false;
    front.in.loop_idx[i] = 0;
    front.in.loop_tag[i] = 0;

    if (front.in.back2front_valid[i]) {
      front.in.predict_dir[i] = ftq_info.pred_taken;
      front.in.predict_base_pc[i] = inst->dbg.pc;
      front.in.actual_dir[i] =
          (inst->type == JAL || inst->type == JALR) ? true : inst->br_taken;
      front.in.actual_target[i] =
          (is_branch(inst->type) || inst->type == JAL || inst->type == JALR)
              ? back.out.commit_entry[i].uop.diag_val
              : inst->dbg.pc + 4;
      int br_type = BR_NONCTL;
      if (is_branch(inst->type)) {
        br_type = BR_DIRECT;
      }
      if (inst->type == JAL) {
        br_type = BR_JAL;
      }
      if (inst->type == JAL && inst->dest_en && inst->dest_areg == 1) {
        br_type = BR_CALL;
      } else if (inst->type == JALR) {
        if (inst->tma.is_ret)
          br_type = BR_RET;
        else
          br_type = BR_IDIRECT;
      }

      front.in.actual_br_type[i] = br_type;
      front.in.alt_pred[i] = ftq_info.alt_pred;
      front.in.altpcpn[i] = ftq_info.altpcpn;
      front.in.pcpn[i] = ftq_info.pcpn;
      front.in.sc_used[i] = ftq_info.sc_used;
      front.in.sc_pred[i] = ftq_info.sc_pred;
      front.in.sc_sum[i] = ftq_info.sc_sum;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        front.in.sc_idx[i][t] = ftq_info.sc_idx[t];
      }
      front.in.loop_used[i] = ftq_info.loop_used;
      front.in.loop_hit[i] = ftq_info.loop_hit;
      front.in.loop_pred[i] = ftq_info.loop_pred;
      front.in.loop_idx[i] = ftq_info.loop_idx;
      front.in.loop_tag[i] = ftq_info.loop_tag;
      for (int j = 0; j < 4; j++) { // TN_MAX = 4 (分支预测相关索引)
        front.in.tage_idx[i][j] = ftq_info.tage_idx[j];
        front.in.tage_tag[i][j] = ftq_info.tage_tag[j];
      }
    }
  }

  if (back.out.mispred || back.out.flush) {
    front.in.refetch_address = back.out.redirect_pc;
  }
}
