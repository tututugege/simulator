#include "include/ICacheTop.h"
#define DEBUG_PRINT 0
#include "../frontend.h"
#include "RISCV.h"
// #include "TOP.h"
#include "config.h" // For SimContext
// #include "cvt.h"
#include "include/icache_module.h"
#include "mmu_io.h"
#include "ref.h"
#include <BackTop.h>
#include <Csr.h>
#include <MMU.h>
#include <SimCpu.h>
#include <cstdio>
#include <iostream>

// External dependencies
extern SimCpu cpu;
extern uint32_t *p_memory;
extern ICache icache; // Defined in icache.cpp

// Forward declaration if not available in headers
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
           CsrStatusIO *status, uint32_t *p_memory) {
  // === 1. 状态准备 (保持不变) ===
  uint32_t mstatus = status->mstatus;
  uint32_t satp = status->satp;
  uint32_t privilege = status->privilege;

  // 提取控制位
  bool mxr = (mstatus & MSTATUS_MXR) != 0;
  bool sum = (mstatus & MSTATUS_SUM) != 0;
  bool mprv = (mstatus & MSTATUS_MPRV) != 0;

  // 计算有效特权级
  int eff_priv = privilege;
  if (type != 0 && mprv) {
    eff_priv = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
  }

  // === 2. 开启页表漫游循环 ===
  // SV32 有两级：Level 1 (Superpage) -> Level 0 (4KB Page)
  uint32_t ppn = satp & 0x3FFFFF;

  for (int level = 1; level >= 0; level--) {
    // A. 计算当前层级的 VPN 和 PTE 地址
    // Level 1: shift=22, Level 0: shift=12
    int vpn_shift = 12 + level * 10;
    uint32_t vpn = (v_addr >> vpn_shift) & 0x3FF;

    uint32_t pte_addr = (ppn << 12) + (vpn * 4);
    uint32_t pte = p_memory[pte_addr >> 2]; // 直接读内存 (暂不考虑 Cache 模拟)

    // B. 有效性检查 (!V 或 !R && W)
    if (!(pte & PTE_V) || (!(pte & PTE_R) && (pte & PTE_W))) {
      return false;
    }

    // C. 判断是否为叶子节点 (R=1 或 X=1 表示找到了！)
    if ((pte & PTE_R) || (pte & PTE_X)) {
      // 1. 对齐检查 (Superpage 要求低位 PPN 为 0)
      // 如果是 Level 1，PPN 的低 10 位必须为 0
      if ((pte >> 10) & ((1 << (level * 10)) - 1)) {
        return false;
      }

      // 2. 权限检查 (Permission Check)
      if (type == 0 && !(pte & PTE_X))
        return false; // Fetch
      if (type == 1 && !(pte & PTE_R) && !(mxr && (pte & PTE_X)))
        return false; // Load
      if (type == 2 && !(pte & PTE_W))
        return false; // Store

      // 3. 用户/特权级检查
      bool is_user_page = (pte & PTE_U) != 0;
      if (eff_priv == 0 && !is_user_page)
        return false; // U 访 S
      if (eff_priv == 1 && is_user_page && !sum)
        return false; // S 访 U

      // 4. A/D 位检查
      if (!(pte & PTE_A))
        return false;
      if (type == 2 && !(pte & PTE_D))
        return false;

      // --- D. 计算物理地址 (通用公式) ---
      // Level 1: mask = 0x3FFFFF (22位), Level 0: mask = 0xFFF (12位)
      uint32_t mask = (1 << vpn_shift) - 1;

      // 物理地址 = (PTE.PPN 对齐后的高位) | (虚拟地址的低位偏移)
      // (pte >> 10) << 12 还原出物理基址，& ~mask 清掉低位，换成 v_addr 的低位
      p_addr = (((pte >> 10) << 12) & ~mask) | (v_addr & mask);

      return true; // ✅ 翻译成功！
    }

    // D. 如果不是叶子节点，继续向下走
    if (level == 0) {
      return false; // Level 0 必须是叶子，否则就是无效页表
    }

    // 更新 PPN 为下一级页表的基址
    ppn = (pte >> 10) & 0x3FFFFF;
  }

  return false; // 兜底
}

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
    icache_hw.set_refetch(in->fence_i);
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
  // [Fix] Only acknowledge MMU response when ICache is actually ready to
  // consume the PPN
  cpu.mmu.io.in.mmu_ifu_resp.ready = icache_hw.io.out.ppn_ready;
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
    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = in->fetch_address + (i * 4);
      uint32_t p_addr = 0;
      if (v_addr / ICACHE_LINE_SIZE != (in->fetch_address) / ICACHE_LINE_SIZE) {
        out->fetch_group[i] = INST_NOP;
        out->page_fault_inst[i] = false;
        out->inst_valid[i] = false;
        continue;
      }
      out->inst_valid[i] = true;
      if (in->run_comb_only)
        continue;

      if ((cpu.back.out.satp & 0x80000000) && cpu.back.out.privilege != 3) {
        out->page_fault_inst[i] =
            !va2pa(p_addr, v_addr, 0, cpu.back.csr->out.csr_status, p_memory);
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
        printf("[icache] vaddr: %08x -> paddr: %08x, inst: %08x, satp: %x, "
               "priv: %d\n",
               v_addr, p_addr, out->fetch_group[i], cpu.back.out.satp,
               cpu.back.out.privilege);
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
