#include <config.h>
typedef struct {
  uint32_t csr_mtvec;
  uint32_t csr_mepc;
  uint32_t csr_mcause;
  uint32_t csr_mstatus;
} CSR;
