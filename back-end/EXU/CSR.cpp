#include <CSR.h>

#define CSR_W 0b01
#define CSR_S 0b10
#define CSR_C 0b11

void CSRU::init() { CSR_RegFile[number_mstatus] = 0x1800; }

void CSRU::comb() {
  io.csr2exe->rdata = CSR_RegFile[io.exe2csr->idx];
  io.csr2exe->mepc = CSR_RegFile[number_mepc];
  io.csr2exe->mtvec = CSR_RegFile[number_mtvec];
}

void CSRU::seq() {
  int csr_idx = io.exe2csr->idx;
  if (io.exe2csr->we) {
    if (io.exe2csr->wcmd == CSR_W) {
      CSR_RegFile[csr_idx] = io.exe2csr->wdata;
    } else if (io.exe2csr->wcmd == CSR_S) {
      CSR_RegFile[csr_idx] =
          io.exe2csr->wdata | (~io.exe2csr->wdata & CSR_RegFile[csr_idx]);
    } else if (io.exe2csr->wcmd == CSR_C) {
      CSR_RegFile[csr_idx] = (~io.exe2csr->wdata & CSR_RegFile[csr_idx]);
    }
  }

  if (io.rob_bc->exception) {
    CSR_RegFile[number_mcause] = io.rob_bc->cause;
    CSR_RegFile[number_mepc] = io.rob_bc->pc;
  }
}
