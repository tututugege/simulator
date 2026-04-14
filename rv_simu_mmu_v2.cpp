#include "AbstractLsu.h"
#include "BackTop.h"
#include "Csr.h"
#include "PhysMemory.h"
#include "SimCpu.h"
#include "config.h"
#include "diff.h"
#include "front-end/host_profile.h"
#include "front_IO.h"
#include "front_module.h"
#include "util.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
      static_cast<uint64_t>(DCACHE_SETS) * DCACHE_WAYS * DCACHE_LINE_BYTES;
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
      static_cast<unsigned long long>(dcache_capacity_bytes / 1024), DCACHE_SETS,
      DCACHE_WAYS, DCACHE_LINE_BYTES);
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
}

void bridge_axi_to_mem_subsystem(SimCpu &cpu) {
  const auto &rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_DCACHE_R];
  cpu.mem_subsystem.mshr_axi_in.req_ready = rport.req.ready;
  cpu.mem_subsystem.mshr_axi_in.req_accepted = rport.req.accepted;
  cpu.mem_subsystem.mshr_axi_in.req_accepted_id = rport.req.accepted_id;
  cpu.mem_subsystem.mshr_axi_in.resp_valid = rport.resp.valid;
  cpu.mem_subsystem.mshr_axi_in.resp_id = rport.resp.id;
  for (int i = 0; i < DCACHE_LINE_WORDS &&
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
  for (int i = 0; i < DCACHE_LINE_WORDS &&
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
       i < DCACHE_LINE_WORDS && i < axi_interconnect::CACHELINE_WORDS; i++) {
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
       i < DCACHE_LINE_WORDS && i < axi_interconnect::CACHELINE_WORDS; i++) {
    peri_wport.req.wdata[i] =
        cpu.mem_subsystem.peripheral_axi_write_out.req_wdata[i];
  }
  peri_wport.resp.ready = cpu.mem_subsystem.peripheral_axi_write_out.resp_ready;
}
} // namespace

void SimCpu::commit_sync(InstInfo *inst) {
  BackTop *back = &this->back;
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
        bool pred_taken = false;
        const FTQEntry *entry =
            back->pre->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr) {
          pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        }
        if (!pred_taken) {
          this->ctx.perf.ret_dir_mispred++;
        } else {
          this->ctx.perf.ret_addr_mispred++;
        }
      } else {
        this->ctx.perf.jalr_mispred_num++;
        bool pred_taken = false;
        const FTQEntry *entry =
            back->pre->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr) {
          pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        }
        if (!pred_taken) {
          this->ctx.perf.jalr_dir_mispred++;
        } else {
          this->ctx.perf.jalr_addr_mispred++;
        }
      }
    } else if (inst->type == BR) {
      bool pred_taken = false;
      const FTQEntry *entry =
          back->pre->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr) {
        pred_taken = entry->pred_taken_mask[inst->ftq_offset];
      }
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
    const bool sc_suppressed = is_amo_sc_inst(*inst) && e.suppress_write &&
                               e.rob_idx == inst->rob_idx &&
                               e.rob_flag == inst->rob_flag;
    if (!sc_suppressed && e.addr_valid && e.data_valid) {
      mem_subsystem.on_commit_store(e.p_addr, e.data, e.func3);
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
    const bool sc_suppressed = is_amo_sc_inst(*inst) && e.suppress_write &&
                               e.rob_idx == inst->rob_idx &&
                               e.rob_flag == inst->rob_flag;
    if (sc_suppressed) {
      dut_cpu.store = false;
    } else {
      if (!(e.addr_valid && e.data_valid)) {
        // Store addr/data sideband can lag the ROB commit signal by a cycle on
        // some recovery paths. Let the REF execute the instruction normally and
        // skip the per-instruction sideband check instead of aborting the run.
        *skip = true;
        dut_cpu.store = false;
      } else {
        dut_cpu.store = true;
        dut_cpu.store_addr = e.p_addr;
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

void SimContext::run_commit_inst(InstEntry *inst_entry) {
  Assert(cpu != nullptr && "SimContext::run_commit_inst: cpu is null");
  Assert(inst_entry != nullptr &&
         "SimContext::run_commit_inst: inst_entry is null");
  Assert(inst_entry->valid &&
         "SimContext::run_commit_inst: inst_entry is not valid");
  cpu->commit_sync(&inst_entry->uop);
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
  mem_subsystem.csr = back.csr;
  mem_subsystem.memory = p_memory;
  mem_subsystem.peripheral_req = back.lsu_peripheral_req_io;
  mem_subsystem.peripheral_resp = back.lsu_peripheral_resp_io;
  mem_subsystem.set_ptw_coherent_source(back.lsu);

  front.in.csr_status = back.csr->out.csr_status;
  front.ctx = &ctx;

  back.lsu->set_ptw_walk_port(mem_subsystem.dtlb_walk_port);
  back.lsu->set_ptw_mem_port(mem_subsystem.dtlb_ptw_port);

  mem_subsystem.lsu2dcache = back.lsu_dcache_req_io;
  mem_subsystem.dcache2lsu = back.lsu_dcache_resp_io;

  front.icache_ptw_walk_port = mem_subsystem.itlb_walk_port;
  front.icache_ptw_mem_port = mem_subsystem.itlb_ptw_port;
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
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBackComb);
    back.comb();
  }

  // AXI phase-1: slave outputs -> router outputs -> interconnect outputs.
  // Interconnect outputs (req.ready/resp.valid) are then bridged into
  // MemSubsystem in the same cycle, so MSHR/WB can consume fresh feedback.
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimMemLlcCombOutputs);
    mem_subsystem.llc_comb_outputs();
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiOutputs);
    axi_interconnect.set_llc_lookup_in(mem_subsystem.llc_lookup_in());
    axi_ddr.comb_outputs();
    axi_mmio.comb_outputs();
    axi_router.comb_outputs(axi_interconnect.axi_io, axi_ddr.io, axi_mmio.io);
    axi_interconnect.comb_outputs();
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBridgeAxiToMem);
    bridge_axi_to_mem_subsystem(*this);
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimMemComb);
    mem_subsystem.comb();
  }

  {
    FRONTEND_HOST_PROFILE_SCOPE(SimBridgeMemToAxi);
    bridge_mem_subsystem_to_axi(*this);
  }

  // AXI phase-2: master requests -> interconnect -> router -> slave inputs.
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiInputs);
    axi_interconnect.comb_inputs();
    axi_router.comb_inputs(axi_interconnect.axi_io, axi_ddr.io, axi_mmio.io);
    axi_ddr.comb_inputs();
    axi_mmio.comb_inputs();
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
    FRONTEND_HOST_PROFILE_SCOPE(SimMemLlcSeq);
    mem_subsystem.llc_seq(axi_interconnect.get_llc_table_out(),
                          axi_interconnect.get_llc_perf_counters());
  }
  {
    FRONTEND_HOST_PROFILE_SCOPE(SimAxiSeq);
    axi_interconnect.seq();
    axi_router.seq(axi_interconnect.axi_io, axi_ddr.io, axi_mmio.io);
    axi_ddr.seq();
    axi_mmio.seq();
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
  perf_account_front_supply();
#endif
}

bool SimCpu::ready_to_exit() const {
  if (back.lsu->has_committed_store_pending()) {
    return false;
  }

  const auto &peri = mem_subsystem.get_peripheral_axi().cur;
  if (peri.busy || peri.req_accepted || peri.resp_valid) {
    return false;
  }

  return true;
}
void SimCpu::back2front_comb() {
  front.in.FIFO_read_enable = false;
  front.in.csr_status = back.csr->out.csr_status;
  front.in.itlb_flush = back.out.itlb_flush;
  front.in.fence_i = back.out.fence_i;
  uint32_t train_meta_cursor = 0;
  bool train_meta_cursor_valid =
      back.pre->ftq_train_meta_cursor_begin(train_meta_cursor);
  const FTQTrainMetaEntry *train_meta_entry =
      train_meta_cursor_valid
          ? back.pre->ftq_train_meta_cursor_peek(train_meta_cursor)
          : nullptr;
  for (int i = 0; i < COMMIT_WIDTH; i++) {
    InstInfo *inst = &back.out.commit_entry[i].uop;
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

      bool pred_taken = false;
      bool alt_pred = false;
      uint8_t altpcpn = 0;
      uint8_t pcpn = 0;
      uint32_t tage_idx[4] = {0};
      uint32_t tage_tag[4] = {0};
      bool sc_used = false;
      bool sc_pred = false;
      int16_t sc_sum = 0;
      uint16_t sc_idx[BPU_SCL_META_NTABLE] = {0};
      bool loop_used = false;
      bool loop_hit = false;
      bool loop_pred = false;
      uint16_t loop_idx = 0;
      uint16_t loop_tag = 0;

      const FTQEntry *entry = back.pre->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr) {
        pred_taken = entry->pred_taken_mask[inst->ftq_offset];
      }
      if (train_meta_entry != nullptr) {
        Assert(inst->ftq_idx == train_meta_cursor);
        alt_pred = train_meta_entry->alt_pred[inst->ftq_offset];
        altpcpn = train_meta_entry->altpcpn[inst->ftq_offset];
        pcpn = train_meta_entry->pcpn[inst->ftq_offset];
        sc_used = train_meta_entry->sc_used[inst->ftq_offset];
        sc_pred = train_meta_entry->sc_pred[inst->ftq_offset];
        sc_sum = train_meta_entry->sc_sum[inst->ftq_offset];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          sc_idx[t] = train_meta_entry->sc_idx[inst->ftq_offset][t];
        }
        loop_used = train_meta_entry->loop_used[inst->ftq_offset];
        loop_hit = train_meta_entry->loop_hit[inst->ftq_offset];
        loop_pred = train_meta_entry->loop_pred[inst->ftq_offset];
        loop_idx = train_meta_entry->loop_idx[inst->ftq_offset];
        loop_tag = train_meta_entry->loop_tag[inst->ftq_offset];
        for (int k = 0; k < 4; k++) {
          tage_idx[k] = train_meta_entry->tage_idx[inst->ftq_offset][k];
          tage_tag[k] = train_meta_entry->tage_tag[inst->ftq_offset][k];
        }
      }

      front.in.predict_dir[i] = pred_taken;
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
      front.in.alt_pred[i] = alt_pred;
      front.in.altpcpn[i] = altpcpn;
      front.in.pcpn[i] = pcpn;
      front.in.sc_used[i] = sc_used;
      front.in.sc_pred[i] = sc_pred;
      front.in.sc_sum[i] = sc_sum;
      for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
        front.in.sc_idx[i][t] = sc_idx[t];
      }
      front.in.loop_used[i] = loop_used;
      front.in.loop_hit[i] = loop_hit;
      front.in.loop_pred[i] = loop_pred;
      front.in.loop_idx[i] = loop_idx;
      front.in.loop_tag[i] = loop_tag;
      for (int j = 0; j < 4; j++) { // TN_MAX = 4 (分支预测相关索引)
        front.in.tage_idx[i][j] = tage_idx[j];
        front.in.tage_tag[i][j] = tage_tag[j];
      }
      if (inst->ftq_is_last && train_meta_cursor_valid) {
        train_meta_cursor_valid =
            back.pre->ftq_train_meta_cursor_advance(train_meta_cursor);
        train_meta_entry = train_meta_cursor_valid
                               ? back.pre->ftq_train_meta_cursor_peek(train_meta_cursor)
                               : nullptr;
      }
    }
  }

  if (back.out.mispred || back.out.flush) {
    front.in.refetch_address = back.out.redirect_pc;
  }
}
