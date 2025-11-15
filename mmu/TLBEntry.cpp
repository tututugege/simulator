#include "include/TLBEntry.h"

void TLBEntry::set_valid_pte(pte_t pte, uint32_t asid_in, uint32_t vpn1_in, uint32_t vpn0_in, bool is_megapage_in) {
  vpn1 = vpn1_in;
  vpn0 = vpn0_in;
  ppn1 = pte.ppn1;
  ppn0 = pte.ppn0;
  asid = asid_in;
  megapage = is_megapage_in;
  dirty = pte.dirty;
  accessed = pte.accessed;
  global = pte.global;
  user = pte.user;
  execute = pte.execute;
  write = pte.write;
  read = pte.read;
  valid = pte.valid;
  pte_valid = true;
}

bool TLBEntry::is_page_fault(uint8_t op_type, uint32_t mstatus,
                            uint32_t sstatus, uint32_t privilege) const {
  // op_type: 
  // - 0: 执行 (fetch)
  // - 1: 读 (load)
  // - 2: 写 (store)
  if (!valid) return true; // 无效页
  if (!read && write) return true;
  
  bool mxr = (mstatus >> 19) & 0x1; // Make eXecutable Readable
  bool sum_m = (mstatus >> 18) & 0x1; // bits[18]
  bool sum_s = (sstatus >> 18) & 0x1; // bits[18]
  bool mprv = (mstatus >> 17) & 0x1; // bits[17]
  uint32_t mpp = (mstatus >> 11) & 0x3; // bits[12:11]

  if (read || execute) {
    if ((op_type == 0 && execute) || 
        (op_type == 1 && read) || 
        (op_type == 1 && mxr && execute) || 
        (op_type == 2 && write)) {
      ; // 允许的访问类型
    } else {
      return true; // 不允许的访问类型
    }

    // 检查 S 模式下的访问权限
    if (privilege == 1 && !sum_m && user && !sum_s) return true; // S 模式下不允许访问 U 模式可访问的页面

    if (privilege != 1 && mprv && mpp == 1 && !sum_m && user && !sum_s) return true; // 非 S 模式下，MPRV 启用且 MPP 为 S 模式时不允许访问 U 模式可访问的页面

    // 检查 Megapage
    if (megapage && ppn0 != 0) {
      return true; // Megapage 的 PPN0 必须非0
    }

    if (!accessed) return true; // 如果未被访问过，返回缺页异常
    if (op_type == 2 && !dirty) return true; // 如果是写操作，脏位必须为1
  }
  return false; // 没有发生缺页异常
}

// for debugging
void TLBEntry::log_entry() const {
  // 打印 TLB 条目的信息
  std::cout << "TLB Entry: "
            << "VPN1: " << std::hex << vpn1
            << ", VPN0: " << vpn0
            << ", PPN1: " << ppn1
            << ", PPN0: " << ppn0
            << ", ASID: " << asid
            << ", Megapage: " << megapage
            << ", Dirty: " << dirty
            << ", Accessed: " << accessed
            << ", Global: " << global
            << ", User Visible: " << user
            << ", Execute: " << execute
            << ", Write: " << write
            << ", Read: " << read
            << ", Valid: " << valid
            << ", PTE_Valid: " << pte_valid
            << std::endl;
}