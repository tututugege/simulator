#pragma once
/*
 * MMU: Implements virtual to physical address translation
 * - Supports Sv32 paging mode
 * - Consists of TLB and PTW modules
 * - Interfaces with IFU and LSU for address translation requests
 */

#include "PTW.h"
#include "TLB.h"
#include "mmu_io.h"
#include <cstdint>

class MMU {
public:
  MMU();

  void reset();
  void comb_frontend();
  void comb_backend();
  void comb_arbiter();
  void comb_ptw();
  void seq();

  // I/O Ports
  MMU_IO_t io;

private:
  /*
   * Internal Modules
   */
  PTW_to_TLB ptw2tlb; // PTW 到 TLB 的接口
  TLB_to_PTW tlb2ptw; // TLB 到 PTW 的接口
  TLB tlb;
  PTW ptw;
  enum mmu_n::Privilege privilege;

  // wire candidates for arbiter
  TLB_to_PTW tlb2ptw_frontend;
  TLB_to_PTW tlb2ptw_backend[MAX_LSU_REQ_NUM];

  /*
   * MMU Top-Level Registers
   */
  mmu_resp_master_t *resp_ifu_r; // hold IFU resp for next cycle
  mmu_resp_master_t *resp_lsu_r; // hold LSU resp for next cycle

  /*
   * MMU Top-Level Wires
   */
  mmu_resp_master_t resp_ifu_r_1;
  mmu_resp_master_t resp_lsu_r_1[MAX_LSU_REQ_NUM];

  /* Helper function to set mmu response fields */
  static inline void comb_set_resp(mmu_resp_master_t &resp, bool valid,
                                   bool miss, bool excp, uint32_t ptag) {
    resp.valid = valid;
    resp.excp = excp;
    resp.miss = miss;
    resp.ptag = ptag;
  }
};
