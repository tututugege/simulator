#include "config.h"
#include "Csr.h"
#include "diff.h"
#include "PhysMemory.h"
#include "front_IO.h"
#include "frontend.h"
#include "ref.h"
#include "util.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <queue>

static RefCpu oracle;
static std::queue<uint32_t> oracle_timer_queue;

namespace {
struct IoRange {
  uint32_t base;
  uint32_t size;
};

constexpr IoRange kCkptIoRanges[] = {
    {BOOT_IO_BASE, BOOT_IO_SIZE},
    {UART_ADDR_BASE, UART_MMIO_SIZE},
    {PLIC_ADDR_BASE, PLIC_MMIO_SIZE},
    {OPENSBI_TIMER_BASE, OPENSBI_TIMER_MMIO_SIZE},
};

inline void seed_oracle_io_from_backing() {
  oracle.io_words.clear();
  for (const auto &range : kCkptIoRanges) {
    for (uint32_t off = 0; off + 4u <= range.size; off += 4u) {
      const uint32_t addr = range.base + off;
      const uint32_t data = pmem_read(addr);
      if (data != 0u) {
        oracle.io_words[addr] = data;
      }
    }
  }
}

inline void sync_oracle_control_state(const front_top_in &in) {
  oracle.state.pc = in.refetch_address;
  if (in.csr_status != nullptr) {
    oracle.state.csr[csr_mstatus] =
        static_cast<uint32_t>(in.csr_status->mstatus);
    oracle.state.csr[csr_sstatus] =
        static_cast<uint32_t>(in.csr_status->sstatus);
    oracle.state.csr[csr_satp] = static_cast<uint32_t>(in.csr_status->satp);
    oracle.privilege = static_cast<uint8_t>(in.csr_status->privilege);
  }
}

inline bool oracle_gpr_matches_dut() {
  for (int idx = 0; idx < ARF_NUM; ++idx) {
    if (oracle.state.gpr[idx] != dut_cpu.gpr[idx]) {
      return false;
    }
  }
  return true;
}

inline void sync_oracle_arch_state_from_dut(const front_top_in &in) {
  std::memcpy(oracle.state.gpr, dut_cpu.gpr, sizeof(oracle.state.gpr));
  std::memcpy(oracle.state.csr, dut_cpu.csr, sizeof(oracle.state.csr));
  oracle.state.store = dut_cpu.store;
  oracle.state.store_addr = dut_cpu.store_addr;
  oracle.state.store_data = dut_cpu.store_data;
  oracle.state.instruction = dut_cpu.instruction;
  oracle.state.page_fault_inst = dut_cpu.page_fault_inst;
  oracle.state.page_fault_load = dut_cpu.page_fault_load;
  oracle.state.page_fault_store = dut_cpu.page_fault_store;
  oracle.state.inst_idx = dut_cpu.inst_idx;
  oracle.state.commit_pc = dut_cpu.commit_pc;
  sync_oracle_control_state(in);
}
} // namespace

void push_oracle_timer(uint32_t val) {
  oracle_timer_queue.push(val);
}

uint64_t get_oracle_timer() {
  Assert(!oracle_timer_queue.empty() && "Oracle Timer queue underflow!");
  uint32_t val = oracle_timer_queue.front();
  oracle_timer_queue.pop();
  return val;
}

void init_oracle(int img_size) {
  while (!oracle_timer_queue.empty()) {
    oracle_timer_queue.pop();
  }
  oracle.init(0);
  oracle.dut_pf_check_enable = false;
  std::memcpy(oracle.memory, pmem_ram_ptr(), img_size);
  oracle.store_word(0x10000004, pmem_read(0x10000004));
  oracle.store_word(0x0, pmem_read(0x0));
  oracle.store_word(0x4, pmem_read(0x4));
  oracle.store_word(0x8, pmem_read(0x8));
  oracle.store_word(0xc, pmem_read(0xc));
  seed_oracle_io_from_backing();
}

void init_oracle_ckpt(CPU_state ckpt_state, uint8_t privilege) {
  while (!oracle_timer_queue.empty()) {
    oracle_timer_queue.pop();
  }
  oracle.init(0);
  oracle.dut_pf_check_enable = false;
  oracle.state = ckpt_state;
  oracle.privilege = privilege;

  const uint32_t *ckpt_memory = pmem_ram_ptr();
  Assert(ckpt_memory != nullptr &&
         "init_oracle_ckpt: pmem RAM backend is not initialized");
  std::memcpy(oracle.memory, ckpt_memory,
              static_cast<size_t>(PHYSICAL_MEMORY_LENGTH) * sizeof(uint32_t));
  seed_oracle_io_from_backing();

  if ((oracle.state.csr[csr_satp] & 0x80000000u) != 0 &&
      oracle.privilege != RISCV_MODE_M) {
    uint32_t p_addr = 0;
    (void)oracle.va2pa(p_addr, oracle.state.pc, 0);
  }
}

void get_oracle(struct front_top_in &in, struct front_top_out &out) {
  int i;
  static bool stall = false;
#ifdef CONFIG_ORACLE_STEADY_FETCH_WIDTH
  constexpr bool kOracleSteadyFetchWidth = true;
#else
  constexpr bool kOracleSteadyFetchWidth = false;
#endif

  // Oracle front-end has no BPU update queue. Backend commit must never be
  // back-pressured by a stale/uninitialized commit_stall in this mode.
  out.commit_stall = false;

  if (in.refetch) {
    oracle.sim_end = false;
    stall = false;
  }

  if (oracle.sim_end) {
    stall = true;
  }

  if (in.refetch) {
    sync_oracle_control_state(in);
    if (!oracle_gpr_matches_dut()) {
      sync_oracle_arch_state_from_dut(in);
    }
    stall = false;
  }

  if (stall) {
    out.FIFO_valid = false;
    for (i = 0; i < FETCH_WIDTH; i++) {
      out.inst_valid[i] = false;
    }

    return;
  }

  out.FIFO_valid = true;
  out.predict_next_fetch_address = oracle.state.pc;
  for (i = 0; i < FETCH_WIDTH; i++) {
    out.inst_valid[i] = true;
    out.pc[i] = oracle.state.pc;
    out.page_fault_inst[i] = false;

    oracle.exec();
    out.instructions[i] = oracle.Instruction;
    out.predict_next_fetch_address = oracle.state.pc;


    if (oracle.is_exception || oracle.is_csr || oracle.is_mmio_load ||
        oracle.is_mmio_store) {
      out.predict_dir[i] = false;
      if (oracle.is_exception || oracle.is_csr) {
        stall = true;
      }

      if (oracle.page_fault_inst) {
        out.page_fault_inst[i] = true;
      }
      break;
    }

    if (oracle.is_br) {
      if (kOracleSteadyFetchWidth) {
        // Stress mode: keep branch direction metadata, but do not truncate
        // this oracle fetch group on taken branches.
        out.predict_dir[i] = oracle.br_taken;
        out.predict_next_fetch_address = oracle.state.pc;
      } else {
        if (stall) {
          out.predict_dir[i] = !oracle.br_taken;
          out.predict_next_fetch_address = 0;
        } else {
          out.predict_dir[i] = oracle.br_taken;
          out.predict_next_fetch_address = oracle.state.pc;
        }

        if (oracle.br_taken || stall)
          break;
      }
    } else {
      out.predict_dir[i] = false;
    }

    // Cache line boundary check: truncate if next instruction is in a different line
    if (!kOracleSteadyFetchWidth &&
        ((oracle.state.pc ^ out.pc[i]) & ~(ICACHE_LINE_SIZE - 1))) {
      break;
    }
  }

  for (i++; i < FETCH_WIDTH; i++) {
    out.inst_valid[i] = false;
  }
}
