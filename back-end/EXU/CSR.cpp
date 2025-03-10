#include <CSR.h>

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
  io.csr2exe->rdata = CSR_RegFile[CSR_Hash_table[io.exe2csr->idx]];
  io.csr2exe->mepc = CSR_RegFile[CSR_MEPC];
  io.csr2exe->mtvec = CSR_RegFile[CSR_MTVEC];
}

void CSRU::seq() {
  int csr_pos = CSR_Hash_table[io.exe2csr->idx];
  if (io.exe2csr->we) {
    if (io.exe2csr->wcmd == CSR_W) {
      CSR_RegFile[csr_pos] = io.exe2csr->wdata;
    } else if (io.exe2csr->wcmd == CSR_S) {
      CSR_RegFile[csr_pos] =
          io.exe2csr->wdata | (~io.exe2csr->wdata & CSR_RegFile[csr_pos]);
    } else if (io.exe2csr->wcmd == CSR_C) {
      CSR_RegFile[csr_pos] = (~io.exe2csr->wdata & CSR_RegFile[csr_pos]);
    }
  }

  if (io.rob_bc->exception) {
    CSR_RegFile[CSR_MCAUSE] = io.rob_bc->cause;
    CSR_RegFile[CSR_MEPC] = io.rob_bc->pc;
  }
}
