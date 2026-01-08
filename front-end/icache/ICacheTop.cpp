#include "include/ICacheTop.h"
#include "../front_module.h"
#include "../frontend.h"
#include "RISCV.h"
#include "TOP.h"
#include "config.h" // For SimContext
#include "cvt.h"
#include "include/icache_module.h"
#include "mmu_io.h"
#include <MMU.h>
#include <SimCpu.h>
#include <cstdio>
#include <iostream>

// External dependencies
extern SimCpu cpu;
extern uint32_t *p_memory;
extern ICache icache; // Defined in icache.cpp

// Forward declaration if not available in headers
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

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

TrueICacheTop::TrueICacheTop(ICache &hw) : icache_hw(hw) {}

void TrueICacheTop::comb() {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    icache_hw.reset();
    out->icache_read_ready = true;
    return;
  }

  // deal with "refetch" signal (Async Reset behavior)
  if (in->refetch) {
    icache_hw.set_refetch();
    valid_reg = false;
    mem_busy = false;
    mem_latency_cnt = 0;
  }

  // set input for 1st pipeline stage (IFU)
  icache_hw.io.in.pc = in->fetch_address;
  icache_hw.io.in.ifu_req_valid = in->icache_read_valid;

  // set input for 2nd pipeline stage (IFU)
  icache_hw.io.in.ifu_resp_ready = true;

  // get ifu_resp from mmu (calculate last cycle)
  mmu_resp_master_t mmu_resp = cpu.mmu.io.out.mmu_ifu_resp;

  // set input for 2nd pipeline stage (MMU)
  icache_hw.io.in.ppn = mmu_resp.ptag;
  icache_hw.io.in.ppn_valid = mmu_resp.valid && !in->refetch && !mmu_resp.miss;
  icache_hw.io.in.page_fault = mmu_resp.excp;

  // set input for 2nd pipeline stage (Memory)
  if (mem_busy) {
    if (mem_latency_cnt >= ICACHE_MISS_LATENCY) {
      icache_hw.io.in.mem_resp_valid = true;
    } else {
      icache_hw.io.in.mem_resp_valid = false;
    }
    bool mem_resp_valid = icache_hw.io.in.mem_resp_valid;
    bool mem_resp_ready = icache_hw.io.out.mem_resp_ready;
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

  icache_hw.comb();

  // set input for request to mmu
  cpu.mmu.io.in.mmu_ifu_req.op_type = mmu_n::OP_FETCH;
  cpu.mmu.io.in.mmu_ifu_resp.ready = true;
  if (icache_hw.io.out.ifu_req_ready && icache_hw.io.in.ifu_req_valid) {
    cpu.mmu.io.in.mmu_ifu_req.valid =
        icache_hw.io.out.ifu_req_ready && in->icache_read_valid;
    cpu.mmu.io.in.mmu_ifu_req.vtag = in->fetch_address >> 12;
  } else if (!icache_hw.io.out.ifu_req_ready) {
    cpu.mmu.io.in.mmu_ifu_req.valid = true; // replay request
    if (!valid_reg) {
      std::cout
          << "[icache_top] ERROR: valid_reg is false when replaying mmu_ifu_req"
          << std::endl;
      std::cout << "[icache_top] sim_time: " << std::dec << sim_time
                << std::endl;
      exit(1);
    }
    cpu.mmu.io.in.mmu_ifu_req.vtag = current_vaddr_reg >> 12;
  } else {
    cpu.mmu.io.in.mmu_ifu_req.valid = false;
  }

  if (in->run_comb_only) {
    out->icache_read_ready = icache_hw.io.out.ifu_req_ready;
    return;
  }

  bool ifu_resp_valid = icache_hw.io.out.ifu_resp_valid;
  bool ifu_resp_ready = icache_hw.io.in.ifu_resp_ready;
  bool miss = icache_hw.io.out.miss;
  if (ifu_resp_valid && ifu_resp_ready) {
    out->icache_read_complete = true;
    if (miss) {
      std::cout << "[icache_top] WARNING: miss is true when ifu_resp is valid"
                << std::endl;
      std::cout << "[icache_top] sim_time: " << std::dec << sim_time
                << std::endl;
      exit(1);
    }
    out->fetch_pc = current_vaddr_reg;
    uint32_t mask = ICACHE_LINE_SIZE - 1;
    int base_idx = (current_vaddr_reg & mask) / 4;
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
    out->icache_read_complete = false;
    for (int i = 0; i < FETCH_WIDTH; i++) {
      out->fetch_group[i] = INST_NOP;
      out->page_fault_inst[i] = false;
      out->inst_valid[i] = false;
    }
  }
}

void TrueICacheTop::seq() {
  if (in->reset)
    return;

  icache_hw.seq();

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

  bool ifu_resp_valid = icache_hw.io.out.ifu_resp_valid;
  bool ifu_resp_ready = icache_hw.io.in.ifu_resp_ready;

  if (icache_hw.io.in.ifu_req_valid && icache_hw.io.out.ifu_req_ready) {
    current_vaddr_reg = in->fetch_address;
    valid_reg = true;
    access_delta++; // Use local delta
  } else if (ifu_resp_valid && ifu_resp_ready) {
    valid_reg = false;
  }
}

// --- SimpleICacheTop Implementation ---

void SimpleICacheTop::comb() {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    out->icache_read_ready = true;
    return;
  }

  out->icache_read_complete = true;
  out->icache_read_ready = true;
  out->fetch_pc = in->fetch_address;

  if (in->icache_read_valid) {
    bool mstatus[32], sstatus[32];

    cvt_number_to_bit_unsigned(mstatus, cpu.back.out.mstatus, 32);
    cvt_number_to_bit_unsigned(sstatus, cpu.back.out.sstatus, 32);

    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = in->fetch_address + (i * 4);
      uint32_t p_addr;

      if (v_addr / ICACHE_LINE_SIZE != (in->fetch_address) / ICACHE_LINE_SIZE) {
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = false;
        out->inst_valid[i] = false;
        continue;
      }
      out->inst_valid[i] = true;

      if ((cpu.back.out.satp & 0x80000000) && cpu.back.out.privilege != 3) {
        out->page_fault_inst[i] =
            !va2pa(p_addr, v_addr, cpu.back.out.satp, 0, mstatus, sstatus,
                   cpu.back.out.privilege, p_memory);
        if (out->page_fault_inst[i]) {
          out->fetch_group[i] = INST_NOP;
        } else {
          out->fetch_group[i] = p_memory[p_addr / 4];
        }
      } else {
        out->page_fault_inst[i] = false;
        out->fetch_group[i] = p_memory[v_addr / 4];
      }

      if (DEBUG_PRINT) {
        printf("[icache] pmem_address: %x\n", p_addr);
        printf("[icache] instruction : %x\n", out->fetch_group[i]);
      }
    }
  } else {
    out->fetch_pc = 0;
  }
}

void SimpleICacheTop::seq() {
  // No sequential logic
}

// --- Factory ---

ICacheTop *get_icache_instance() {
  static std::unique_ptr<ICacheTop> instance = nullptr;
  if (!instance) {
#ifdef USE_TRUE_ICACHE
    instance = std::make_unique<TrueICacheTop>(icache);
#else
    instance = std::make_unique<SimpleICacheTop>();
#endif
  }
  return instance.get();
}