#pragma once
#include "IO.h"
#include <config.h>
#include <cstdint>

#define M_MODE_ECALL 0xb

enum csr_reg { CSR_MTVEC, CSR_MEPC, CSR_MCAUSE, CSR_MSTATUS, CSR_NUM };

enum enum_number_csr_code {
  number_mtvec = 0x305,
  number_mepc = 0x341,
  number_mcause = 0x342,
  number_mie = 0x304,
  number_mip = 0x344,
  number_mtval = 0x343,
  number_mscratch = 0x340,
  number_mstatus = 0x300,
  number_mideleg = 0x303,
  number_medeleg = 0x302,
  number_sepc = 0x141,
  number_stvec = 0x105,
  number_scause = 0x142,
  number_sscratch = 0x140,
  number_stval = 0x143,
  number_sstatus = 0x100,
  number_sie = 0x104,
  number_sip = 0x144,
  number_satp = 0x180,
  number_mhartid = 0xf14,
  number_misa = 0x301,
  number_time = 0xc01,
  number_timeh = 0xc81,
};

typedef struct {
  bool we;
  bool re;
  uint32_t idx;
  uint32_t wdata;
  uint32_t wcmd;
} Exe_Csr;

typedef struct {
  uint32_t rdata;
  uint32_t mepc;
  uint32_t mtvec;
} Csr_Exe;

typedef struct {
  Exe_Csr *exe2csr;
  Csr_Exe *csr2exe;
  Rob_Broadcast *rob_bc;
} CSR_IO;

class CSRU {
public:
  CSR_IO io;
  void init();
  void comb();
  void seq();

private:
  uint32_t CSR_RegFile[CSR_NUM];
};
