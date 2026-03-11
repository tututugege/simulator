#include "include/ICacheTop.h"
#include "../front_module.h"
#include "../frontend.h"
#include "AXI_Interconnect_IO.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include "RISCV.h"
#include "SimpleMmu.h"
#include "TlbMmu.h"
#include "config.h" // For SimContext
#include "include/icache_module.h"
#include "Csr.h"
#include <cstdio>
#include <iostream>

// External dependencies
extern uint32_t *p_memory;
extern icache_module_n::ICache icache; // Defined in icache.cpp

namespace {
uint32_t icache_coherent_read(uint32_t p_addr) { return p_memory[p_addr >> 2]; }

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

constexpr uint32_t kAxiChunkBytes =
    axi_interconnect::CACHELINE_WORDS * sizeof(uint32_t);
constexpr uint32_t kIcacheLineWords = ICACHE_LINE_SIZE / 4;
constexpr uint32_t kAxiChunksPerLine =
    kIcacheLineWords / axi_interconnect::CACHELINE_WORDS;
static_assert(kIcacheLineWords > 0, "ICACHE_LINE_SIZE must be non-zero");
static_assert((kIcacheLineWords % axi_interconnect::CACHELINE_WORDS) == 0,
              "ICACHE_LINE_SIZE must be divisible by AXI chunk size");
static_assert(kAxiChunkBytes <= 32,
              "AXI ReadMaster total_size only supports up to 32 bytes");

inline uint32_t top_xorshift32(uint32_t x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

inline uint32_t top_clamp_latency(uint32_t v) { return (v < 1u) ? 1u : v; }

inline bool top_lookup_latency_enabled() { return (ICACHE_LOOKUP_LATENCY > 0); }

inline bool top_axi_backend_enabled() {
  return (CONFIG_ICACHE_USE_AXI_MEM_PORT != 0);
}

inline bool top_use_axi_backend(
    const axi_interconnect::ReadMasterPort_t *port) {
  return top_axi_backend_enabled() && (port != nullptr);
}

inline uint32_t top_lookup_index_from_pc(uint32_t pc) {
  constexpr uint32_t offset_bits = __builtin_ctz(ICACHE_LINE_SIZE);
  constexpr uint32_t index_bits = 12 - offset_bits;
  constexpr uint32_t mask = (1u << index_bits) - 1u;
  return (pc >> offset_bits) & mask;
}

inline uint32_t top_peek_lookup_latency(uint32_t seed) {
  if (!top_lookup_latency_enabled()) {
    return 1u;
  }
  uint32_t lat = top_clamp_latency(ICACHE_LOOKUP_LATENCY);
#if ICACHE_SRAM_RANDOM_DELAY
  uint32_t min_lat = top_clamp_latency(ICACHE_SRAM_RANDOM_MIN);
  uint32_t max_lat = ICACHE_SRAM_RANDOM_MAX;
  if (max_lat < min_lat) {
    max_lat = min_lat;
  }
  uint32_t next_seed = top_xorshift32(seed);
  uint32_t range = max_lat - min_lat + 1u;
  lat = min_lat + (next_seed % range);
#endif
  return top_clamp_latency(lat);
}

inline uint32_t top_advance_seed(uint32_t seed) {
#if ICACHE_SRAM_RANDOM_DELAY
  return top_xorshift32(seed);
#else
  return seed;
#endif
}

inline void fill_lookup_input(icache_module_n::ICache &hw, bool resp_valid,
                              uint32_t pc) {
  hw.io.lookup_in.lookup_resp_valid = resp_valid;
  if (!resp_valid) {
    for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
      hw.io.lookup_in.lookup_set_tag[way] = 0;
      hw.io.lookup_in.lookup_set_valid[way] = false;
      for (uint32_t w = 0; w < (ICACHE_LINE_SIZE / 4); ++w) {
        hw.io.lookup_in.lookup_set_data[way][w] = 0;
      }
    }
    return;
  }

  uint32_t set_data[ICACHE_V1_WAYS][ICACHE_LINE_SIZE / 4] = {{0}};
  uint32_t set_tag[ICACHE_V1_WAYS] = {0};
  bool set_valid[ICACHE_V1_WAYS] = {false};
  hw.export_lookup_set_for_pc(pc, set_data, set_tag, set_valid);
  for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
    hw.io.lookup_in.lookup_set_tag[way] = set_tag[way];
    hw.io.lookup_in.lookup_set_valid[way] = set_valid[way];
    for (uint32_t w = 0; w < (ICACHE_LINE_SIZE / 4); ++w) {
      hw.io.lookup_in.lookup_set_data[way][w] = set_data[way][w];
    }
  }
}

inline void setup_lookup_request_for_comb(icache_module_n::ICache &icache_hw,
                                          const icache_in &input,
                                          bool lookup_pending,
                                          uint32_t lookup_delay,
                                          uint32_t lookup_pc,
                                          uint32_t lookup_seed,
                                          uint32_t &req_pc, bool &req_valid) {
  req_pc = input.fetch_address;
  req_valid = input.icache_read_valid;
  bool resp_valid = req_valid;
  uint32_t resp_pc = req_pc;

  if (top_lookup_latency_enabled()) {
    resp_valid = false;
    bool allow_resp = !input.run_comb_only && !input.reset && !input.refetch;
    if (lookup_pending && lookup_delay == 0u && allow_resp) {
      resp_valid = true;
      resp_pc = lookup_pc;
    } else if (!lookup_pending && req_valid && allow_resp &&
               (top_peek_lookup_latency(lookup_seed) <= 1u)) {
      resp_valid = true;
      resp_pc = req_pc;
    }
  }

  fill_lookup_input(icache_hw, resp_valid, resp_pc);
}

inline void update_lookup_state_in_seq(const icache_in &input,
                                       const icache_module_n::ICache &icache_hw,
                                       bool &lookup_pending,
                                       uint32_t &lookup_delay,
                                       uint32_t &lookup_index,
                                       uint32_t &lookup_pc,
                                       uint32_t &lookup_seed) {
  if (!top_lookup_latency_enabled() || input.reset || input.refetch) {
    lookup_pending = false;
    lookup_delay = 0;
    lookup_index = 0;
    lookup_pc = 0;
    lookup_seed = 1;
    return;
  }

  bool hw_fire = icache_hw.io.in.ifu_req_valid && icache_hw.io.out.ifu_req_ready;
  bool resp_fire = lookup_pending && (lookup_delay == 0u) && !input.run_comb_only;

  if (lookup_pending) {
    if (lookup_delay > 0u) {
      lookup_delay--;
    } else if (resp_fire) {
      lookup_pending = false;
      lookup_delay = 0;
      lookup_index = 0;
      lookup_pc = 0;
    }
  }

  if (lookup_pending || !hw_fire) {
    return;
  }

  uint32_t latency = top_peek_lookup_latency(lookup_seed);
  lookup_seed = top_advance_seed(lookup_seed);
  if (latency <= 1u) {
    return;
  }

  lookup_pending = true;
  lookup_delay = latency - 1u;
  lookup_pc = icache_hw.io.in.pc;
  lookup_index = top_lookup_index_from_pc(icache_hw.io.in.pc);
}
} // namespace

// --- ICacheTop Implementation ---

void ICacheTop::syncPerf() {
  if (ctx) {
    ctx->perf.icache_access_num += access_delta;
    ctx->perf.icache_miss_num += miss_delta;
  }
  // Reset deltas
  access_delta = 0;
  miss_delta = 0;
}

// --- TrueICacheTop Implementation ---

TrueICacheTop::TrueICacheTop(icache_module_n::ICache &hw) : icache_hw(hw) {}

void TrueICacheTop::set_ptw_mem_port(PtwMemPort *port) {
  ptw_mem_port = port;
  if (mmu_model != nullptr) {
    mmu_model->set_ptw_mem_port(port);
  }
}
void TrueICacheTop::set_ptw_walk_port(PtwWalkPort *port) {
  ptw_walk_port = port;
  if (mmu_model != nullptr) {
    mmu_model->set_ptw_walk_port(port);
  }
}

void TrueICacheTop::set_mem_read_port(
    axi_interconnect::ReadMasterPort_t *port) {
  mem_read_port = port;
}

void TrueICacheTop::comb() {
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

  tlb_set_pending_comb = false;
  tlb_clear_pending_comb = false;

  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    icache_hw.reset();
    if (mmu_model != nullptr) {
      mmu_model->flush();
    }
    satp_seen = false;
    valid_reg = false;
    mem_busy = false;
    mem_latency_cnt = 0;
    axi_fill_state = AxiFillState::IDLE;
    axi_fill_stale = false;
    axi_fill_base_addr = 0;
    axi_fill_chunk_idx = 0;
    for (uint32_t &w : axi_fill_data) {
      w = 0;
    }
    tlb_pending = false;
    tlb_pending_vaddr = 0;
    lookup_pending = false;
    lookup_delay = 0;
    lookup_index = 0;
    lookup_pc = 0;
    lookup_seed = 1;
    if (mem_read_port != nullptr) {
      mem_read_port->req.valid = false;
      mem_read_port->req.addr = 0;
      mem_read_port->req.total_size = 0;
      mem_read_port->req.id = 0;
      mem_read_port->resp.ready = false;
    }
    fill_lookup_input(icache_hw, false, 0);
    out->icache_read_ready = true;
    return;
  }

  uint32_t cur_satp = in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
  if (!satp_seen || cur_satp != last_satp || in->refetch) {
    if (mmu_model != nullptr) {
      mmu_model->flush();
    }
    satp_seen = true;
    last_satp = cur_satp;
    tlb_pending = false;
    tlb_pending_vaddr = 0;
  }

  // deal with "refetch" signal (Async Reset behavior)
  if (in->refetch) {
    valid_reg = false;
    if (top_use_axi_backend(mem_read_port) || !mem_busy) {
      mem_busy = false;
      mem_latency_cnt = 0;
    }
    // Non-AXI fallback intentionally keeps an in-flight synthetic miss alive to
    // mirror the post-issue behavior of the real AXI path.
    tlb_pending = false;
    tlb_pending_vaddr = 0;
    lookup_pending = false;
    lookup_delay = 0;
    lookup_index = 0;
    lookup_pc = 0;
    lookup_seed = 1;
  }

  icache_hw.io.in.refetch = in->refetch;
  icache_hw.io.in.flush = false;

  uint32_t lookup_req_pc = in->fetch_address;
  bool lookup_req_valid = in->icache_read_valid;
  setup_lookup_request_for_comb(icache_hw, *in, lookup_pending, lookup_delay,
                                lookup_pc, lookup_seed, lookup_req_pc,
                                lookup_req_valid);

  // set input for 1st pipeline stage (IFU)
  icache_hw.io.in.pc = lookup_req_pc;
  icache_hw.io.in.ifu_req_valid = lookup_req_valid;

  // set input for 2nd pipeline stage (IFU)
  icache_hw.io.in.ifu_resp_ready = true;

  // set input for 2nd pipeline stage (MMU): convert local AbstractMmu result
  icache_hw.io.in.ppn = 0;
  icache_hw.io.in.ppn_valid = false;
  icache_hw.io.in.page_fault = false;
  bool mmu_req_valid = false;
  uint32_t mmu_vaddr = 0;
  bool mmu_req_from_new = false;
  if (tlb_pending) {
    mmu_req_valid = true;
    mmu_vaddr = tlb_pending_vaddr;
  } else if (icache_hw.io.out.ifu_req_ready && icache_hw.io.in.ifu_req_valid) {
    mmu_req_valid = true;
    mmu_vaddr = icache_hw.io.in.pc;
    mmu_req_from_new = true;
  } else if (!icache_hw.io.out.ifu_req_ready && valid_reg) {
    // replay translation while IFU is stalled
    mmu_req_valid = true;
    mmu_vaddr = current_vaddr_reg;
  }
  if (!in->run_comb_only && mmu_req_valid && mmu_model != nullptr &&
      !in->refetch) {
    uint32_t p_addr = 0;
    AbstractMmu::Result ret = mmu_model->translate(p_addr, mmu_vaddr, 0, in->csr_status);
    if (ret == AbstractMmu::Result::OK) {
      icache_hw.io.in.ppn = p_addr >> 12;
      icache_hw.io.in.ppn_valid = true;
      tlb_clear_pending_comb = true;
    } else if (ret == AbstractMmu::Result::FAULT) {
      // icache_module consumes page_fault only when ppn_valid is asserted.
      icache_hw.io.in.ppn_valid = true;
      icache_hw.io.in.page_fault = true;
      tlb_clear_pending_comb = true;
    } else if (ret == AbstractMmu::Result::RETRY && mmu_req_from_new) {
      tlb_set_pending_comb = true;
      tlb_pending_vaddr = mmu_vaddr;
    }
  }

  // set input for 2nd pipeline stage (Memory)
  icache_hw.io.in.mem_req_ready = false;
  icache_hw.io.in.mem_resp_valid = false;
  icache_hw.io.in.mem_resp_id = 0;
  for (int i = 0; i < ICACHE_LINE_SIZE / 4; i++) {
    icache_hw.io.in.mem_resp_data[i] = 0;
  }

  if (top_use_axi_backend(mem_read_port)) {
    bool issue_req = (axi_fill_state == AxiFillState::REQ);
    bool wait_resp = (axi_fill_state == AxiFillState::WAIT);

    mem_read_port->req.valid = issue_req;
    mem_read_port->req.addr = axi_fill_base_addr + (axi_fill_chunk_idx * kAxiChunkBytes);
    mem_read_port->req.total_size =
        static_cast<uint8_t>(kAxiChunkBytes - 1u);
    mem_read_port->req.id = static_cast<uint8_t>(axi_fill_chunk_idx & 0xFu);
    mem_read_port->resp.ready = wait_resp;

    icache_hw.io.in.mem_req_ready = (axi_fill_state == AxiFillState::IDLE);
    if (axi_fill_state == AxiFillState::RESP_READY) {
      icache_hw.io.in.mem_resp_valid = true;
      for (uint32_t i = 0; i < kIcacheLineWords; i++) {
        icache_hw.io.in.mem_resp_data[i] = axi_fill_data[i];
      }
    }
  } else {
    if (mem_read_port != nullptr) {
      mem_read_port->req.valid = false;
      mem_read_port->req.addr = 0;
      mem_read_port->req.total_size = 0;
      mem_read_port->req.id = 0;
      mem_read_port->resp.ready = false;
    }
    if (mem_busy) {
      if (mem_latency_cnt >= ICACHE_MISS_LATENCY) {
        icache_hw.io.in.mem_resp_valid = true;
      } else {
        icache_hw.io.in.mem_resp_valid = false;
      }
      bool mem_resp_valid = icache_hw.io.in.mem_resp_valid;
      if (mem_resp_valid) {
        uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
        uint32_t cacheline_base_addr = icache_hw.io.out.mem_req_addr & mask;
        for (int i = 0; i < ICACHE_LINE_SIZE / 4; i++) {
          icache_hw.io.in.mem_resp_data[i] =
              p_memory[cacheline_base_addr / 4 + i];
        }
      }
    } else {
      icache_hw.io.in.mem_req_ready = true;
    }
  }

  icache_hw.comb();

  if (in->run_comb_only) {
    out->icache_read_ready = icache_hw.io.out.ifu_req_ready;
    return;
  }

  bool ifu_resp_valid = icache_hw.io.out.ifu_resp_valid;
  bool ifu_resp_ready = icache_hw.io.in.ifu_resp_ready;
  bool miss = icache_hw.io.out.miss;
  if (ifu_resp_valid) {
    if (miss) {
      std::cout << "[icache_top] WARNING: miss is true when ifu_resp is valid"
                << std::endl;
      std::cout << "[icache_top] sim_time: " << std::dec << sim_time
                << std::endl;
      exit(1);
    }
    out->fetch_pc = icache_hw.io.out.ifu_resp_pc;
    uint32_t mask = ICACHE_LINE_SIZE - 1;
    int base_idx = (out->fetch_pc & mask) / 4;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      if (base_idx + i >= ICACHE_LINE_SIZE / 4) {
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = false;
        out->inst_valid[i] = false;
        continue;
      }
      out->fetch_group[i] = icache_hw.io.out.ifu_page_fault
                                ? INST_NOP
                                : icache_hw.io.out.rd_data[i + base_idx];
      out->page_fault_inst[i] = icache_hw.io.out.ifu_page_fault;
      out->inst_valid[i] = true;
    }
  } else {
    out->fetch_pc = 0;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }
  }
  out->icache_read_complete = ifu_resp_valid && ifu_resp_ready;
  out->icache_read_ready = icache_hw.io.out.ifu_req_ready;
}

void TrueICacheTop::seq() {
  if (in->reset) {
    lookup_pending = false;
    lookup_delay = 0;
    lookup_index = 0;
    lookup_pc = 0;
    lookup_seed = 1;
    return;
  }

  icache_hw.seq();

  if (top_use_axi_backend(mem_read_port)) {
    bool mem_req_fire = icache_hw.io.out.mem_req_valid && icache_hw.io.in.mem_req_ready;
    bool req_fire = mem_read_port->req.valid && mem_read_port->req.ready;
    bool resp_fire = mem_read_port->resp.valid && mem_read_port->resp.ready;
    bool mem_resp_fire = icache_hw.io.in.mem_resp_valid && icache_hw.io.out.mem_resp_ready;

    if (axi_fill_state == AxiFillState::IDLE && mem_req_fire) {
      axi_fill_base_addr = icache_hw.io.out.mem_req_addr & ~(ICACHE_LINE_SIZE - 1u);
      axi_fill_chunk_idx = 0;
      axi_fill_state = AxiFillState::REQ;
      axi_fill_stale = false;
      miss_delta++;
    }

    if (axi_fill_state == AxiFillState::REQ && req_fire) {
      axi_fill_state = AxiFillState::WAIT;
    } else if (axi_fill_state == AxiFillState::WAIT && resp_fire) {
      uint32_t word_base = axi_fill_chunk_idx * axi_interconnect::CACHELINE_WORDS;
      for (uint32_t i = 0; i < axi_interconnect::CACHELINE_WORDS; i++) {
        axi_fill_data[word_base + i] = mem_read_port->resp.data[i];
      }
      if ((axi_fill_chunk_idx + 1u) < kAxiChunksPerLine) {
        axi_fill_chunk_idx++;
        axi_fill_state = AxiFillState::REQ;
      } else {
        axi_fill_state = AxiFillState::RESP_READY;
      }
    } else if (axi_fill_state == AxiFillState::RESP_READY && mem_resp_fire) {
      axi_fill_state = AxiFillState::IDLE;
      axi_fill_chunk_idx = 0;
      axi_fill_stale = false;
    }
  } else {
    if (mem_busy) {
      mem_latency_cnt++;
    }
    bool mem_req_ready = !mem_busy;
    bool mem_req_valid = icache_hw.io.out.mem_req_valid;
    if (mem_req_ready && mem_req_valid) {
      mem_busy = true;
      mem_latency_cnt = 0;
      miss_delta++; // Use local delta
    }
    bool mem_resp_valid = icache_hw.io.in.mem_resp_valid;
    bool mem_resp_ready = icache_hw.io.out.mem_resp_ready;
    if (mem_resp_valid && mem_resp_ready) {
      mem_busy = false;
      icache_hw.io.in.mem_resp_valid = false;
    }
  }

  bool ifu_resp_valid = icache_hw.io.out.ifu_resp_valid;
  bool ifu_resp_ready = icache_hw.io.in.ifu_resp_ready;

  if (icache_hw.io.in.ifu_req_valid && icache_hw.io.out.ifu_req_ready) {
    current_vaddr_reg = icache_hw.io.in.pc;
    valid_reg = true;
    access_delta++; // Use local delta
  } else if (ifu_resp_valid && ifu_resp_ready) {
    valid_reg = false;
  }

  if (tlb_set_pending_comb) {
    tlb_pending = true;
  }
  if (tlb_clear_pending_comb) {
    tlb_pending = false;
  }

  update_lookup_state_in_seq(*in, icache_hw, lookup_pending, lookup_delay,
                             lookup_index, lookup_pc, lookup_seed);
}

// --- SimpleICacheTop Implementation ---

void SimpleICacheTop::comb() {
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
  out->fetch_pc = pending_req_valid ? pending_fetch_addr : in->fetch_address;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    out->fetch_group[i] = INST_NOP;
    out->page_fault_inst[i] = false;
    out->inst_valid[i] = false;
  }

  uint32_t cur_satp = in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
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

  const uint32_t fetch_addr = pending_req_valid ? pending_fetch_addr : in->fetch_address;
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
        for (int spin = 0; spin < 8 && ret == AbstractMmu::Result::RETRY; spin++) {
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
        uint32_t satp = in->csr_status ? static_cast<uint32_t>(in->csr_status->satp) : 0;
        uint32_t privilege =
            in->csr_status ? static_cast<uint32_t>(in->csr_status->privilege) : 0;
        printf("[icache] vaddr: %08x -> paddr: %08x, inst: %08x, satp: %x, "
               "priv: %d\n",
               v_addr, p_addr, out->fetch_group[i], satp, privilege);
      }
  }

  if (out->icache_read_complete) {
    resp_fire_comb = true;
  }
}

void SimpleICacheTop::set_ptw_mem_port(PtwMemPort *port) {
  ptw_mem_port = port;
  if (mmu_model != nullptr) {
    mmu_model->set_ptw_mem_port(port);
  }
}
void SimpleICacheTop::set_ptw_walk_port(PtwWalkPort *port) {
  ptw_walk_port = port;
  if (mmu_model != nullptr) {
    mmu_model->set_ptw_walk_port(port);
  }
}

void SimpleICacheTop::set_mem_read_port(
    axi_interconnect::ReadMasterPort_t *port) {
  (void)port;
}

void SimpleICacheTop::seq() {
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

// --- Factory ---

ICacheTop *get_icache_instance() {
  static std::unique_ptr<ICacheTop> instance = nullptr;
  if (!instance) {
#ifdef USE_IDEAL_ICACHE
    instance = std::make_unique<SimpleICacheTop>();
#else
    instance = std::make_unique<TrueICacheTop>(icache);
#endif
  }
  return instance.get();
}
