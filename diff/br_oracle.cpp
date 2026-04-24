#include "config.h"
#include "Csr.h"
#include "diff.h"
#include "PhysMemory.h"
#include "front_IO.h"
#include "frontend.h"
#include "util.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <queue>

static RefCpuContext *oracle_ctx = nullptr;
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

void ensure_oracle() {
  if (oracle_ctx == nullptr) {
    oracle_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
    Assert(oracle_ctx != nullptr && "oracle refcpu_init failed");
  }
}

RefCpuState current_oracle_state() {
  ensure_oracle();
  RefCpuState state{};
  refcpu_get_state(oracle_ctx, &state);
  return state;
}

RefCpuStepInfo current_oracle_step_info() {
  ensure_oracle();
  RefCpuStepInfo info{};
  refcpu_get_step_info(oracle_ctx, &info);
  return info;
}

RefCpuState dut_to_ref_state(uint8_t privilege) {
  RefCpuState state{};
  std::memcpy(state.gpr, dut_cpu.gpr, sizeof(state.gpr));
  std::memcpy(state.csr, dut_cpu.csr, sizeof(state.csr));
  state.pc = dut_cpu.pc;
  state.privilege = privilege;
  state.store = dut_cpu.store;
  state.store_addr = dut_cpu.store_addr;
  state.store_data = dut_cpu.store_data;
  state.store_strb = dut_cpu.store_strb;
  state.instruction = dut_cpu.instruction;
  state.page_fault_inst = dut_cpu.page_fault_inst;
  state.page_fault_load = dut_cpu.page_fault_load;
  state.page_fault_store = dut_cpu.page_fault_store;
  state.reserve_valid = dut_cpu.reserve_valid;
  state.reserve_addr = dut_cpu.reserve_addr;
  return state;
}

inline void seed_oracle_io_from_backing() {
  ensure_oracle();
  refcpu_clear_io_words(oracle_ctx);
  for (const auto &range : kCkptIoRanges) {
    for (uint32_t off = 0; off + 4u <= range.size; off += 4u) {
      const uint32_t addr = range.base + off;
      const uint32_t data = pmem_read(addr);
      refcpu_seed_ref_io_from_backing(oracle_ctx, addr, data);
    }
  }
}

inline void sync_oracle_control_state(const front_top_in &in) {
  RefCpuState state = current_oracle_state();
  state.pc = in.refetch_address;
  if (in.csr_status != nullptr) {
    state.csr[csr_mstatus] = static_cast<uint32_t>(in.csr_status->mstatus);
    state.csr[csr_sstatus] = static_cast<uint32_t>(in.csr_status->sstatus);
    state.csr[csr_satp] = static_cast<uint32_t>(in.csr_status->satp);
    state.privilege = static_cast<uint8_t>(in.csr_status->privilege);
  }
  refcpu_set_state(oracle_ctx, &state);
}

inline bool oracle_gpr_matches_dut() {
  const RefCpuState state = current_oracle_state();
  for (int idx = 0; idx < ARF_NUM; ++idx) {
    if (state.gpr[idx] != dut_cpu.gpr[idx]) {
      return false;
    }
  }
  return true;
}

inline void sync_oracle_arch_state_from_dut(const front_top_in &in) {
  const uint8_t privilege = in.csr_status == nullptr
                                ? RISCV_MODE_M
                                : static_cast<uint8_t>(in.csr_status->privilege);
  RefCpuState state = dut_to_ref_state(privilege);
  state.pc = in.refetch_address;
  if (in.csr_status != nullptr) {
    state.csr[csr_mstatus] = static_cast<uint32_t>(in.csr_status->mstatus);
    state.csr[csr_sstatus] = static_cast<uint32_t>(in.csr_status->sstatus);
    state.csr[csr_satp] = static_cast<uint32_t>(in.csr_status->satp);
  }
  refcpu_set_state(oracle_ctx, &state);
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
  if (oracle_ctx != nullptr) {
    refcpu_destroy(oracle_ctx);
  }
  oracle_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
  Assert(oracle_ctx != nullptr && "init_oracle: refcpu_init failed");
  refcpu_set_uart_print(oracle_ctx, false);
  refcpu_sync_ram_from_dut(oracle_ctx, pmem_ram_ptr(), img_size);
  seed_oracle_io_from_backing();
}

void init_oracle_ckpt(CPU_state ckpt_state, uint8_t privilege) {
  while (!oracle_timer_queue.empty()) {
    oracle_timer_queue.pop();
  }
  if (oracle_ctx != nullptr) {
    refcpu_destroy(oracle_ctx);
  }
  oracle_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
  Assert(oracle_ctx != nullptr && "init_oracle_ckpt: refcpu_init failed");
  RefCpuState state{};
  std::memcpy(state.gpr, ckpt_state.gpr, sizeof(state.gpr));
  std::memcpy(state.csr, ckpt_state.csr, sizeof(state.csr));
  state.pc = ckpt_state.pc;
  state.privilege = privilege;
  state.store = ckpt_state.store;
  state.store_addr = ckpt_state.store_addr;
  state.store_data = ckpt_state.store_data;
  state.store_strb = ckpt_state.store_strb;
  state.instruction = ckpt_state.instruction;
  state.page_fault_inst = ckpt_state.page_fault_inst;
  state.page_fault_load = ckpt_state.page_fault_load;
  state.page_fault_store = ckpt_state.page_fault_store;
  state.reserve_valid = ckpt_state.reserve_valid;
  state.reserve_addr = ckpt_state.reserve_addr;
  refcpu_set_state(oracle_ctx, &state);

  const uint32_t *ckpt_memory = pmem_ram_ptr();
  Assert(ckpt_memory != nullptr &&
         "init_oracle_ckpt: pmem RAM backend is not initialized");
  refcpu_sync_ram_from_dut(oracle_ctx, ckpt_memory,
                           static_cast<int>(RAM_SIZE));
  seed_oracle_io_from_backing();
}

void get_oracle(struct front_top_in &in, struct front_top_out &out) {
  int i;
  static bool stall = false;

  if (in.refetch) {
    ensure_oracle();
    refcpu_set_sim_end(oracle_ctx, false);
    stall = false;
  }

  if (current_oracle_step_info().sim_end) {
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
  for (i = 0; i < FETCH_WIDTH; i++) {
    RefCpuState state = current_oracle_state();
    out.inst_valid[i] = true;
    out.pc[i] = state.pc;
    out.page_fault_inst[i] = false;

    refcpu_step(oracle_ctx, 1);
    RefCpuStepInfo info = current_oracle_step_info();
    state = current_oracle_state();
    out.instructions[i] = info.instruction;


    if (info.is_exception || info.is_csr || info.is_io) {
      out.predict_dir[i] = false;
      if (info.is_exception || info.is_csr) {
        stall = true;
      }

      if (info.page_fault_inst) {
        out.page_fault_inst[i] = true;
      }
      break;
    }

    if (info.is_br) {
      if (stall) {
        out.predict_dir[i] = !info.br_taken;
        out.predict_next_fetch_address = 0;
      } else {
        out.predict_dir[i] = info.br_taken;
        out.predict_next_fetch_address = state.pc;
      }

      if (info.br_taken || stall)
        break;
    } else {
      out.predict_dir[i] = false;
    }

    // Cache line boundary check: truncate if next instruction is in a different line
    if ((state.pc ^ out.pc[i]) & ~(ICACHE_LINE_SIZE - 1)) {
      break;
    }
  }

  for (i++; i < FETCH_WIDTH; i++) {
    out.inst_valid[i] = false;
  }
}
