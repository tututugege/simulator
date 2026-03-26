#include "AbstractLsu.h"
#include "BackTop.h"
#include "Csr.h"
#include "SimCpu.h"
#include "config.h"
#include "diff.h"
#include "front_IO.h"
#include "front-end/host_profile.h"
#include "front_module.h"
#include "oracle.h"
#include "util.h"
#include "DebugPtwTrace.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "DeadlockDebug.h"
#include <iostream>

uint32_t *p_memory;
namespace {
SimCpu *g_deadlock_cpu = nullptr;

#ifndef CONFIG_DEBUG_PAGEFAULT_TRACE_MAX
#define CONFIG_DEBUG_PAGEFAULT_TRACE_MAX 0
#endif

#ifndef CONFIG_RECENT_COMMIT_TRACE_SIZE
#define CONFIG_RECENT_COMMIT_TRACE_SIZE 64
#endif

struct RecentCommitTraceEntry {
  long long cycle = -1;
  int64_t inst_idx = -1;
  uint32_t commit_pc = 0;
  uint32_t next_pc = 0;
  uint32_t inst = 0;
  uint32_t diag_val = 0;
  uint8_t type = 0;
  uint32_t rob_idx = 0;
  uint32_t rob_flag = 0;
  uint8_t src1_areg = 0;
  uint8_t src2_areg = 0;
  uint8_t dest_areg = 0;
  uint32_t src1_val = 0;
  uint32_t src2_val = 0;
  uint32_t dest_val = 0;
  uint32_t imm = 0;
  uint32_t eff_addr = 0;
  bool mispred = false;
  bool flush = false;
  bool pf_inst = false;
  bool pf_load = false;
  bool pf_store = false;
};

constexpr size_t kRecentCommitTraceSize =
    static_cast<size_t>(CONFIG_RECENT_COMMIT_TRACE_SIZE);
std::array<RecentCommitTraceEntry, kRecentCommitTraceSize> g_recent_commits{};
size_t g_recent_commit_next = 0;
bool g_recent_commit_wrapped = false;

inline uint32_t sign_extend_imm12(uint32_t inst) {
  return static_cast<uint32_t>(static_cast<int32_t>(inst) >> 20);
}

inline uint32_t sign_extend_store_imm12(uint32_t inst) {
  const uint32_t imm12 =
      (((inst >> 25) & 0x7fu) << 5) | ((inst >> 7) & 0x1fu);
  return static_cast<uint32_t>(static_cast<int32_t>(imm12 << 20) >> 20);
}

inline uint32_t decode_debug_imm(uint32_t inst) {
  const uint32_t opcode = inst & 0x7fu;
  if (opcode == 0x23u) {
    return sign_extend_store_imm12(inst);
  }
  return sign_extend_imm12(inst);
}

inline uint32_t decode_debug_eff_addr(uint32_t inst, uint32_t src1_val) {
  const uint32_t opcode = inst & 0x7fu;
  if (opcode == 0x03u || opcode == 0x23u) {
    return src1_val + decode_debug_imm(inst);
  }
  return src1_val + sign_extend_imm12(inst);
}

inline bool is_amo_sc_inst(const InstInfo &inst) {
  return inst.type == AMO && ((inst.func7 >> 2) == AmoOp::SC);
}

void deadlock_dump_soc_cb() {
  if (g_deadlock_cpu == nullptr) {
    return;
  }
  g_deadlock_cpu->axi_interconnect.debug_print();
  g_deadlock_cpu->axi_ddr.print_state();
}

void record_recent_commit(const SimCpu *cpu, const InstEntry *inst_entry) {
  if (inst_entry == nullptr) {
    return;
  }
  auto &slot = g_recent_commits[g_recent_commit_next];
  const auto &inst = inst_entry->uop;
  slot.cycle = sim_time;
  slot.inst_idx = inst.dbg.inst_idx;
  slot.commit_pc = dut_cpu.commit_pc;
  slot.next_pc = dut_cpu.pc;
  slot.inst = dut_cpu.instruction;
  slot.diag_val = inst.diag_val;
  slot.type = static_cast<uint8_t>(inst.type);
  slot.rob_idx = inst.rob_idx;
  slot.rob_flag = inst.rob_flag;
  slot.src1_areg = static_cast<uint8_t>((slot.inst >> 15) & 0x1f);
  slot.src2_areg = static_cast<uint8_t>((slot.inst >> 20) & 0x1f);
  slot.dest_areg = static_cast<uint8_t>((slot.inst >> 7) & 0x1f);
  slot.src1_val = 0;
  slot.src2_val = 0;
  if (cpu != nullptr && cpu->back.prf != nullptr) {
    if (inst.src1_en) {
      slot.src1_val = cpu->back.prf->reg_file_1[inst.src1_preg];
    }
    if (inst.src2_en) {
      slot.src2_val = cpu->back.prf->reg_file_1[inst.src2_preg];
    }
  } else {
    slot.src1_val = dut_cpu.gpr[slot.src1_areg];
    slot.src2_val = dut_cpu.gpr[slot.src2_areg];
  }
  if (inst.src1_is_pc) {
    slot.src1_val = inst.dbg.pc;
  }
  slot.dest_val = dut_cpu.gpr[slot.dest_areg];
  slot.imm = decode_debug_imm(slot.inst);
  slot.eff_addr = decode_debug_eff_addr(slot.inst, slot.src1_val);
  slot.mispred = static_cast<bool>(inst.mispred);
  slot.flush = static_cast<bool>(inst.page_fault_inst || inst.page_fault_load ||
                                 inst.page_fault_store);
  slot.pf_inst = static_cast<bool>(inst.page_fault_inst);
  slot.pf_load = static_cast<bool>(inst.page_fault_load);
  slot.pf_store = static_cast<bool>(inst.page_fault_store);
  g_recent_commit_next = (g_recent_commit_next + 1) % g_recent_commits.size();
  if (g_recent_commit_next == 0) {
    g_recent_commit_wrapped = true;
  }
}

void dump_recent_commits() {
  const size_t count =
      g_recent_commit_wrapped ? g_recent_commits.size() : g_recent_commit_next;
  if (count == 0) {
    std::printf("[RECENT-COMMIT] no committed instructions recorded\n");
    return;
  }
  const size_t start = g_recent_commit_wrapped ? g_recent_commit_next : 0;
  std::printf("[RECENT-COMMIT] dumping latest %zu committed instructions\n",
              count);
  for (size_t i = 0; i < count; ++i) {
    const auto &e = g_recent_commits[(start + i) % g_recent_commits.size()];
    std::printf(
        "[RECENT-COMMIT] cyc=%lld inst_idx=%lld rob=%u/%u pc=0x%08x next=0x%08x "
        "inst=0x%08x type=%u src1=x%u/0x%08x src2=x%u/0x%08x dest=x%u/0x%08x "
        "imm=0x%08x eff=0x%08x diag=0x%08x "
        "mispred=%d pf{i=%d l=%d s=%d}\n",
        e.cycle, (long long)e.inst_idx, e.rob_idx, e.rob_flag, e.commit_pc,
        e.next_pc, e.inst,
        static_cast<unsigned>(e.type), static_cast<unsigned>(e.src1_areg),
        e.src1_val, static_cast<unsigned>(e.src2_areg), e.src2_val,
        static_cast<unsigned>(e.dest_areg), e.dest_val, e.imm, e.eff_addr,
        e.diag_val,
        static_cast<int>(e.mispred),
        static_cast<int>(e.pf_inst), static_cast<int>(e.pf_load),
        static_cast<int>(e.pf_store));
  }
}

void dump_soc_debug_state_impl() {
  if (g_deadlock_cpu == nullptr) {
    std::printf("[DIFF][SOC] no active cpu is registered for debug dump\n");
    return;
  }
  std::printf("[DIFF][SOC] dumping LSU/MemSubsystem/AXI state at cycle %lld\n",
              (long long)sim_time);
  g_deadlock_cpu->back.lsu->dump_debug_state();
  g_deadlock_cpu->mem_subsystem.dump_debug_state();
  g_deadlock_cpu->axi_interconnect.debug_print();
  g_deadlock_cpu->axi_ddr.print_state();
}

void maybe_trace_page_fault_commit(SimCpu *cpu, const InstEntry *inst_entry) {
#if CONFIG_DEBUG_PAGEFAULT_TRACE_MAX > 0
  static int remaining = CONFIG_DEBUG_PAGEFAULT_TRACE_MAX;
  if (cpu == nullptr || inst_entry == nullptr || remaining <= 0) {
    return;
  }
  const auto &inst = inst_entry->uop;
  if (!inst.page_fault_inst && !inst.page_fault_load && !inst.page_fault_store) {
    return;
  }
  --remaining;
  const uint32_t inst_bits = static_cast<uint32_t>(dut_cpu.instruction);
  const uint8_t src1_areg = static_cast<uint8_t>((inst_bits >> 15) & 0x1f);
  const uint8_t src2_areg = static_cast<uint8_t>((inst_bits >> 20) & 0x1f);
  const uint8_t dest_areg = static_cast<uint8_t>((inst_bits >> 7) & 0x1f);
  const uint32_t src1_val = dut_cpu.gpr[src1_areg];
  const uint32_t src2_val = dut_cpu.gpr[src2_areg];
  const uint32_t imm = decode_debug_imm(inst_bits);
  const uint32_t eff_addr = decode_debug_eff_addr(inst_bits, src1_val);
  std::printf(
      "[PF-TRACE][COMMIT] cyc=%lld inst_idx=%lld rob=%u/%u commit_pc=0x%08x "
      "next_pc=0x%08x inst=0x%08x src1=x%u/0x%08x src2=x%u/0x%08x dest=x%u "
      "imm=0x%08x eff=0x%08x "
      "pf{i=%d l=%d s=%d} satp=0x%08x priv=%u\n",
      (long long)sim_time, (long long)inst.dbg.inst_idx,
      static_cast<unsigned>(inst.rob_idx), static_cast<unsigned>(inst.rob_flag),
      (uint32_t)dut_cpu.commit_pc, (uint32_t)dut_cpu.pc, inst_bits,
      static_cast<unsigned>(src1_areg), src1_val,
      static_cast<unsigned>(src2_areg), src2_val,
      static_cast<unsigned>(dest_areg), imm, eff_addr,
      static_cast<int>(inst.page_fault_inst),
      static_cast<int>(inst.page_fault_load),
      static_cast<int>(inst.page_fault_store),
      static_cast<uint32_t>(dut_cpu.csr[csr_satp]),
      static_cast<unsigned>(cpu->back.csr->privilege_1));
  dump_recent_commits();
  debug_ptw_trace::dump_recent_satp_writes();
  debug_ptw_trace::dump_recent_ptw_walk_resps();
  cpu->axi_interconnect.debug_print();
  cpu->axi_ddr.print_state();
  std::fflush(stdout);
#else
  (void)cpu;
  (void)inst_entry;
#endif
}

template <typename InterconnectT>
void clear_axi_master_inputs(InterconnectT &interconnect) {
  for (int i = 0; i < axi_interconnect::NUM_READ_MASTERS; i++) {
    auto &port = interconnect.read_ports[i];
    port.req.valid = false;
    port.req.addr = 0;
    port.req.total_size = 0;
    port.req.id = 0;
    port.req.bypass = false;
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
    port.req.bypass = false;
    port.resp.ready = false;
  }
}

axi_interconnect::AXI_LLCConfig make_default_llc_config() {
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
  return llc_cfg;
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
  if (LSU_MEM_LOG && rport.req.accepted) {
    LSU_MEM_DBG_PRINTF(
        "[AXI->MSHR AR ACCEPT] cyc=%lld slot=%u ready=%d accepted=%d\n",
        (long long)sim_time, static_cast<unsigned>(rport.req.accepted_id),
        static_cast<int>(rport.req.ready), static_cast<int>(rport.req.accepted));
  }
  if (LSU_MEM_LOG && rport.resp.valid) {
    LSU_MEM_DBG_PRINTF(
        "[AXI->MSHR RESP] cyc=%lld resp_id=%u ready=%d data=[%08x %08x %08x %08x %08x %08x %08x %08x]\n",
        (long long)sim_time, static_cast<unsigned>(rport.resp.id),
        static_cast<int>(rport.resp.ready), rport.resp.data[0], rport.resp.data[1],
        rport.resp.data[2], rport.resp.data[3], rport.resp.data[4],
        rport.resp.data[5], rport.resp.data[6], rport.resp.data[7]);
  }

  const auto &wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_DCACHE_W];
  cpu.mem_subsystem.wb_axi_in.req_ready = wport.req.ready;
  cpu.mem_subsystem.wb_axi_in.req_accepted = wport.req.accepted;
  cpu.mem_subsystem.wb_axi_in.resp_valid = wport.resp.valid;

  const auto &peri_rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_EXTRA_R];
  cpu.mem_subsystem.peripheral_axi_read_in.req_ready = peri_rport.req.ready;
  cpu.mem_subsystem.peripheral_axi_read_in.req_accepted = peri_rport.req.accepted;
  cpu.mem_subsystem.peripheral_axi_read_in.resp_valid = peri_rport.resp.valid;
  cpu.mem_subsystem.peripheral_axi_read_in.resp_id = peri_rport.resp.id;
  for (int i = 0; i < DCACHE_LINE_WORDS &&
                  i < axi_interconnect::MAX_READ_TRANSACTION_WORDS;
       i++) {
    cpu.mem_subsystem.peripheral_axi_read_in.resp_data[i] = peri_rport.resp.data[i];
  }

  const auto &peri_wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_EXTRA_W];
  cpu.mem_subsystem.peripheral_axi_write_in.req_ready = peri_wport.req.ready;
  cpu.mem_subsystem.peripheral_axi_write_in.req_accepted = peri_wport.req.accepted;
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
  for (int i = 0; i < DCACHE_LINE_WORDS && i < axi_interconnect::CACHELINE_WORDS;
       i++) {
    wport.req.wdata[i] = cpu.mem_subsystem.wb_axi_out.req_wdata[i];
  }
  wport.resp.ready = cpu.mem_subsystem.wb_axi_out.resp_ready;

  auto &peri_rport =
      cpu.axi_interconnect.read_ports[axi_interconnect::MASTER_EXTRA_R];
  peri_rport.req.valid = cpu.mem_subsystem.peripheral_axi_read_out.req_valid;
  peri_rport.req.addr = cpu.mem_subsystem.peripheral_axi_read_out.req_addr;
  peri_rport.req.total_size = cpu.mem_subsystem.peripheral_axi_read_out.req_total_size;
  peri_rport.req.id = cpu.mem_subsystem.peripheral_axi_read_out.req_id;
  peri_rport.req.bypass = true;
  peri_rport.resp.ready = cpu.mem_subsystem.peripheral_axi_read_out.resp_ready;

  auto &peri_wport =
      cpu.axi_interconnect.write_ports[axi_interconnect::MASTER_EXTRA_W];
  peri_wport.req.valid = cpu.mem_subsystem.peripheral_axi_write_out.req_valid;
  peri_wport.req.addr = cpu.mem_subsystem.peripheral_axi_write_out.req_addr;
  peri_wport.req.total_size = cpu.mem_subsystem.peripheral_axi_write_out.req_total_size;
  peri_wport.req.id = cpu.mem_subsystem.peripheral_axi_write_out.req_id;
  peri_wport.req.wstrb = cpu.mem_subsystem.peripheral_axi_write_out.req_wstrb;
  peri_wport.req.bypass = true;
  for (int i = 0; i < axi_interconnect::CACHELINE_WORDS; i++) {
    peri_wport.req.wdata[i] = 0;
  }
  for (int i = 0; i < DCACHE_LINE_WORDS && i < axi_interconnect::CACHELINE_WORDS;
       i++) {
    peri_wport.req.wdata[i] = cpu.mem_subsystem.peripheral_axi_write_out.req_wdata[i];
  }
  peri_wport.resp.ready = cpu.mem_subsystem.peripheral_axi_write_out.resp_ready;
}
} // namespace

void difftest_dump_recent_commits() { dump_recent_commits(); }
void difftest_dump_soc_debug_state() { dump_soc_debug_state_impl(); }

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
        const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr && entry->valid) {
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
        const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
        if (entry != nullptr && entry->valid) {
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
      const FTQEntry *entry = back->pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr && entry->valid) {
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
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx);
    const bool sc_suppressed =
        is_amo_sc_inst(*inst) && e.suppress_write &&
        e.rob_idx == inst->rob_idx && e.rob_flag == inst->rob_flag;
    if (!sc_suppressed && e.addr_valid && e.data_valid) {
      mem_subsystem.on_commit_store(e.p_addr, e.data, e.func3);
    }
  }
}

void SimCpu::difftest_prepare(InstEntry *inst_entry, bool *skip) {
  Assert(inst_entry != nullptr && "SimCpu::difftest_prepare: inst_entry is null");
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
    StqEntry e = back->lsu->get_stq_entry(inst->stq_idx);
    const bool sc_suppressed =
        is_amo_sc_inst(*inst) && e.suppress_write &&
        e.rob_idx == inst->rob_idx && e.rob_flag == inst->rob_flag;
    if (sc_suppressed) {
      // if (!(e.addr_valid && e.data_valid)) {
      //   std::fprintf(
      //       stderr,
      //       "[DIFFTEST][SC_SUPPRESSED] cyc=%lld inst_idx=%lld pc=0x%08x "
      //       "inst=0x%08x rob=%u flag=%u stq_idx=%u stq_flag=%u stq_valid=%d\n",
      //       (long long)sim_time, (long long)inst->inst_idx, (uint32_t)inst->pc,
      //       (uint32_t)inst->instruction, (unsigned)inst->rob_idx,
      //       (unsigned)inst->rob_flag, (unsigned)inst->stq_idx,
      //       (unsigned)inst->stq_flag, (int)e.valid);
      //   std::fflush(stderr);
      // }
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

        dut_cpu.store_data =
            dut_cpu.store_data << (dut_cpu.store_addr & 0b11) * 8;
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
  record_recent_commit(cpu, inst_entry);
  maybe_trace_page_fault_commit(cpu, inst_entry);
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
  g_deadlock_cpu = this;
  deadlock_debug::register_soc_dump_cb(deadlock_dump_soc_cb);

  // 第二阶段：构建模块对象（生成内部子模块实例）
  back.init();

  // 第三阶段：集中完成跨模块连线
  mem_subsystem.csr = back.csr;
  mem_subsystem.memory = p_memory;
  mem_subsystem.peripheral_io = &back.lsu->peripheral_io;

  front.in.csr_status = back.csr->out.csr_status;
  front.ctx = &ctx;

  back.lsu->ptw_walk_port = mem_subsystem.dtlb_walk_port;
  back.lsu->ptw_mem_port = mem_subsystem.dtlb_ptw_port;

  mem_subsystem.lsu2dcache = back.lsu_dcache_req_io;
  mem_subsystem.dcache2lsu  = back.lsu_dcache_resp_io;

  front.icache_ptw_walk_port = mem_subsystem.itlb_walk_port;
  front.icache_ptw_mem_port = mem_subsystem.itlb_ptw_port;
  // Keep a single active SoC memory topology. DCache/PTW/peripheral already use
  // the top-level interconnect; frontend icache should use the same shared path
  // whenever the real BPU frontend is enabled. Oracle mode does not step the
  // icache model, so it simply leaves the port disconnected instead of falling
  // back to MemSubsystem's legacy private AXI runtime.
  mem_subsystem.set_internal_axi_runtime_active(false);
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
  axi_mmio.add_device(MMIO_RANGE_BASE, MMIO_RANGE_SIZE, &axi_uart);
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
  axi_uart.sync_from_backing(p_memory);
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
  // Oracle 模式：每拍都执行握手，利用 1-entry pending 防止“当拍后端阻塞”丢指令。
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

  bool no_taken = true;
  for (int j = 0; j < FETCH_WIDTH; j++) {
    back.in.valid[j] = no_taken && front.out.FIFO_valid && front.out.inst_valid[j];
    back.in.pc[j] = front.out.pc[j];
    back.in.predict_next_fetch_address[j] = front.out.predict_next_fetch_address;
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
    if (back.in.valid[j] && front.out.predict_dir[j]) {
      no_taken = false;
    }
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

      const FTQEntry *entry = back.pre_idu_queue->lookup_ftq_entry(inst->ftq_idx);
      if (entry != nullptr && entry->valid) {
        pred_taken = entry->pred_taken_mask[inst->ftq_offset];
        alt_pred = entry->alt_pred[inst->ftq_offset];
        altpcpn = entry->altpcpn[inst->ftq_offset];
        pcpn = entry->pcpn[inst->ftq_offset];
        sc_used = entry->sc_used[inst->ftq_offset];
        sc_pred = entry->sc_pred[inst->ftq_offset];
        sc_sum = entry->sc_sum[inst->ftq_offset];
        for (int t = 0; t < BPU_SCL_META_NTABLE; ++t) {
          sc_idx[t] = entry->sc_idx[inst->ftq_offset][t];
        }
        loop_used = entry->loop_used[inst->ftq_offset];
        loop_hit = entry->loop_hit[inst->ftq_offset];
        loop_pred = entry->loop_pred[inst->ftq_offset];
        loop_idx = entry->loop_idx[inst->ftq_offset];
        loop_tag = entry->loop_tag[inst->ftq_offset];
        for (int k = 0; k < 4; k++) {
          tage_idx[k] = entry->tage_idx[inst->ftq_offset][k];
          tage_tag[k] = entry->tage_tag[inst->ftq_offset][k];
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
    }
  }

  if (back.out.mispred || back.out.flush) {
    front.in.refetch_address = back.out.redirect_pc;
  }
}
