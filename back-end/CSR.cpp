#include <CSR.h>
#include <cstdint>

#define CSR_W 0b01
#define CSR_S 0b10
#define CSR_C 0b11

uint32_t CSR_Hash_table[1 << 11];

void CSRU::init() {
  CSR_RegFile[CSR_MSTATUS] = 0x1800;
  CSR_Hash_table[number_mstatus] = CSR_MSTATUS;
  CSR_Hash_table[number_mepc] = CSR_MEPC;
  CSR_Hash_table[number_mtvec] = CSR_MTVEC;
  CSR_Hash_table[number_mcause] = CSR_MCAUSE;
}

void CSRU::comb() {
  int csr_pos = CSR_Hash_table[in.idx];
  if (in.we) {
    if (in.wcmd == CSR_W) {
      CSR_RegFile_1[csr_pos] = in.wdata;
    } else if (in.wcmd == CSR_S) {
      CSR_RegFile_1[csr_pos] = in.wdata | (~in.wdata & CSR_RegFile[csr_pos]);
    } else if (in.wcmd == CSR_C) {
      CSR_RegFile_1[csr_pos] = (~in.wdata & CSR_RegFile[csr_pos]);
    }
  }

  out.rdata = CSR_RegFile[CSR_Hash_table[in.idx]];
}

void CSRU::seq() {
  for (int i = 0; i < CSR_NUM; i++) {
    CSR_RegFile[i] = CSR_RegFile_1[i];
  }
}
