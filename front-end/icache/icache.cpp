#include "../front_IO.h"
#include "../front_module.h"
#include "../frontend.h"
#include "TOP.h"
#include "cvt.h"
#include <cstdint>
#include <cstdio>
// no actual icache, just a simple simulation

extern uint32_t *p_memory;
extern Back_Top back;
bool va2pa(uint32_t &p_addr, uint32_t v_addr, uint32_t satp, uint32_t type,
           bool *mstatus, bool *sstatus, int privilege, uint32_t *p_memory);

void icache_top(struct icache_in *in, struct icache_out *out) {
  if (in->reset) {
    DEBUG_LOG("[icache] reset\n");
    return;
  }
  out->icache_read_ready = true;
  // when BPU sends a valid read request
  if (in->icache_read_valid) {
    // read instructions from pmem
    bool mstatus[32], sstatus[32];

    cvt_number_to_bit_unsigned(mstatus, back.csr.CSR_RegFile[number_mstatus],
                               32);

    cvt_number_to_bit_unsigned(sstatus, back.csr.CSR_RegFile[number_sstatus],
                               32);

    for (int i = 0; i < FETCH_WIDTH; i++) {
      uint32_t v_addr = in->fetch_address + (i * 4);
      uint32_t p_addr;
      if ((back.csr.CSR_RegFile[number_satp] & 0x80000000) &&
          back.csr.privilege != 3) {

        out->page_fault_inst[i] =
            !va2pa(p_addr, v_addr, back.csr.CSR_RegFile[number_satp], 0,
                   mstatus, sstatus, back.csr.privilege, p_memory);
        if (out->page_fault_inst[i]) {
          out->fetch_group[i] = 0;
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
  }
}
