#include "SimpleMmu.h"
#include "AbstractLsu.h" // Added for coherent_read
#include "ref.h"         // For PTE_V, etc.
#include <cstdio>

// Helper extern from main.cpp (or wherever p_memory is defined globally)
extern uint32_t *p_memory;

SimpleMmu::SimpleMmu(SimContext *ctx, AbstractLsu *lsu) : ctx(ctx), lsu(lsu) {
  // p_memory is extern global, no need to cache it (it's not ready at ctor time
  // anyway)
}

extern long long sim_time;

AbstractMmu::Result SimpleMmu::translate(uint32_t &p_addr, uint32_t v_addr,
                                         uint32_t type, CsrStatusIO *status) {
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

  if ((eff_priv == 3) || ((satp & 0x80000000) == 0)) {
    p_addr = v_addr;
    return Result::OK;
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
    uint32_t pte = lsu ? lsu->coherent_read(pte_addr) : p_memory[pte_addr >> 2];

    // B. 有效性检查 (!V 或 !R && W)
    if (!(pte & PTE_V) || (!(pte & PTE_R) && (pte & PTE_W))) {
      return Result::FAULT;
    }

    // C. 判断是否为叶子节点 (R=1 或 X=1 表示找到了！)
    if ((pte & PTE_R) || (pte & PTE_X)) {
      // 1. 对齐检查 (Superpage 要求低位 PPN 为 0)
      // 如果是 Level 1，PPN 的低 10 位必须为 0
      if ((pte >> 10) & ((1 << (level * 10)) - 1)) {
        return Result::FAULT;
      }

      // 2. 权限检查 (Permission Check)
      if (type == 0 && !(pte & PTE_X))
        return Result::FAULT; // Fetch
      if (type == 1 && !(pte & PTE_R) && !(mxr && (pte & PTE_X)))
        return Result::FAULT; // Load
      if (type == 2 && !(pte & PTE_W))
        return Result::FAULT; // Store

      // 3. 用户/特权级检查
      bool is_user_page = (pte & PTE_U) != 0;
      if (eff_priv == 0 && !is_user_page)
        return Result::FAULT; // U 访 S
      if (eff_priv == 1 && is_user_page && !sum)
        return Result::FAULT; // S 访 U

      // 4. A/D 位检查
      if (!(pte & PTE_A))
        return Result::FAULT;
      if (type == 2 && !(pte & PTE_D))
        return Result::FAULT;

      // --- D. 计算物理地址 (通用公式) ---
      // Level 1: mask = 0x3FFFFF (22位), Level 0: mask = 0xFFF (12位)
      uint32_t mask = (1 << vpn_shift) - 1;

      // 物理地址 = (PTE.PPN 对齐后的高位) | (虚拟地址的低位偏移)
      // (pte >> 10) << 12 还原出物理基址，& ~mask 清掉低位，换成 v_addr 的低位
      p_addr = (((pte >> 10) << 12) & ~mask) | (v_addr & mask);

      return Result::OK; // ✅ 翻译成功！
    }

    // D. 如果不是叶子节点，继续向下走
    if (level == 0) {
      return Result::FAULT; // Level 0 必须是叶子，否则就是无效页表
    }

    // 更新 PPN 为下一级页表的基址
    ppn = (pte >> 10) & 0x3FFFFF;
  }

  return Result::FAULT; // 兜底
}
