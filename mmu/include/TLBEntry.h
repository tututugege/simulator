#pragma once
#include <cstdint>
#include <iostream>
#include <tuple>

/*
 * PTE: Page Table Entry [31:0]
 *
 * RISC-V Sv32 页表项结构体，具体而言：
 *  - [0] V (Valid): 页表项是否有效
 *  - [1] R (Read): 页是否可读
 *  - [2] W (Write): 页是否可写
 *  - [3] X (eXecute): 页是否可执行
 *  - [4] U (User): 页是否为用户态可访问
 *  - [5] G (Global): 页是否为全局页
 *  - [6] A (Accessed): 页是否被访问过
 *  - [7] D (Dirty): 页是否被写过
 *  - [9:8] Reserved: 保留位，未使用
 *  - [19:10] PPN0: 物理页号的低10位
 *  - [31:20] PPN1: 物理页号的高12位
 */
struct pte_t {
  bool valid : 1;
  bool read : 1;
  bool write : 1;
  bool execute : 1;
  bool user : 1;
  bool global : 1;
  bool accessed : 1;
  bool dirty : 1;
  uint32_t reserved : 2;
  uint32_t ppn0 : 10;
  uint32_t ppn1 : 12;
};

/*
 * TLB Entry: Translation Lookaside Buffer Entry
 * Size of each entry: 58 bits(Design for RISC-V Sv32)
 */
class TLBEntry {
public:
  // 20 bits VPN
  uint32_t vpn1 = 0; // vpn[31:22] (Sv32 模式下的一级页表索引)
  uint32_t vpn0 = 0; // vpn[21:12] (Sv32 模式下的二级页表索引)
  // 22/20 bits PPN
  uint32_t ppn1 = 0; // ppn[31:22] (Sv32 模式下的一级页表物理页号)
  uint32_t ppn0 = 0; // ppn[21:12] (Sv32 模式下的二级页表物理页号)
  // 9 bits Address Space Identifier
  uint32_t asid = 0; // ASID (Address Space Identifier)
  // 9 bits Flags
  bool megapage = false;  // 是否为大页（Sv32 模式下的一级页表）
  bool dirty = false;     // 是否脏页(D)
  bool accessed = false;  // 是否被访问过(A)
  bool global = false;    // 是否全局页(G)
  bool user = false;      // 是否用户可见页(U)
  bool execute = false;   // 是否可执行(X)
  bool write = false;     // 是否可写(W)
  bool read = false;      // 是否可读(R)
  bool valid = false;     // 是否有效(V)，PTE 的 V 位
  bool pte_valid = false; // Entry 是否有效

  void invalidate() { pte_valid = false; }
  // 返回物理页号 (PPN)
  uint32_t get_ppn() const {
    return (ppn1 << 10) | ppn0; // 组合 PPN[1] 和 PPN[0]
  }
  // 返回虚拟页号 (VPN)
  uint32_t get_vpn() const {
    return (vpn1 << 10) | vpn0; // 组合 VPN[1] 和 VPN[0]
  }
  // 检查是否发生页错误
  bool is_page_fault(uint8_t op_type, uint32_t mstatus,
    uint32_t sstatus, uint32_t privilege) const;
  // 设置 TLB 条目
  void set_valid_pte(pte_t pte, uint32_t asid_in, uint32_t vpn1_in,
    uint32_t vpn0_in, bool is_megapage_in);

  // for debug
  void log_entry() const;

  friend bool operator==(const TLBEntry &a, const TLBEntry &b) {
    return std::tie(
      a.vpn1, a.vpn0, a.ppn1, a.ppn0, a.asid,
      a.megapage, a.dirty, a.accessed, a.global,
      a.user, a.execute, a.write, a.read, a.pte_valid
    ) == std::tie(
      b.vpn1, b.vpn0, b.ppn1, b.ppn0, b.asid,
      b.megapage, b.dirty, b.accessed, b.global,
      b.user, b.execute, b.write, b.read, b.pte_valid
    );
  }
};