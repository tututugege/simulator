#pragma once
#include "IO.h"
#include "config.h"
#include <cstdlib>
#include <cstdio>

#define M_MODE_ECALL 0xb
#define CSR_W 0b01
#define CSR_S 0b10
#define CSR_C 0b11

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

enum enum_csr {
  csr_mtvec,
  csr_mepc,
  csr_mcause,
  csr_mie,
  csr_mip,
  csr_mtval,
  csr_mscratch,
  csr_mstatus,
  csr_mideleg,
  csr_medeleg,
  csr_sepc,
  csr_stvec,
  csr_scause,
  csr_sscratch,
  csr_stval,
  csr_sstatus,
  csr_sie,
  csr_sip,
  csr_satp,
  csr_mhartid,
  csr_misa,
};

inline int cvt_number_to_csr(int csr_idx) {
  switch (csr_idx) {
  case number_mtvec:
    return csr_mtvec;
  case number_mepc:
    return csr_mepc;
  case number_mcause:
    return csr_mcause;
  case number_mie:
    return csr_mie;
  case number_mip:
    return csr_mip;
  case number_mtval:
    return csr_mtval;
  case number_mscratch:
    return csr_mscratch;
  case number_mstatus:
    return csr_mstatus;
  case number_mideleg:
    return csr_mideleg;
  case number_medeleg:
    return csr_medeleg;
  case number_sepc:
    return csr_sepc;
  case number_stvec:
    return csr_stvec;
  case number_scause:
    return csr_scause;
  case number_sscratch:
    return csr_sscratch;
  case number_stval:
    return csr_stval;
  case number_sstatus:
    return csr_sstatus;
  case number_sie:
    return csr_sie;
  case number_sip:
    return csr_sip;
  case number_satp:
    return csr_satp;
  case number_mhartid:
    return csr_mhartid;
  case number_misa:
    return csr_misa;
  case number_time:
  case number_timeh:
    std::fprintf(stderr,
                 "Fatal: CSR 0x%x (time/timeh) is not implemented in Csr RegFile\n",
                 csr_idx);
    std::abort();
  default:
    std::fprintf(stderr, "Fatal: unknown CSR index 0x%x\n", csr_idx);
    std::abort();
  }
}

typedef struct {
  ExeCsrIO *exe2csr;
  RobCsrIO *rob2csr;
  RobBroadcastIO *rob_bcast;
  CsrInterruptInjectIO *interrupt_inject;
} CsrIn;

typedef struct {
  CsrExeIO *csr2exe;
  CsrRobIO *csr2rob;
  CsrFrontIO *csr2front;
  CsrStatusIO *csr_status;
} CsrOut;

class Csr {
public:
  CsrIn in;
  CsrOut out;

  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）

  void comb_csr_status();
  void comb_csr_read();
  void comb_csr_write();
  void comb_interrupt();
  void comb_interrupt_inject();
  void comb_exception();
  void seq();

  reg<32> CSR_RegFile[CSR_NUM];
  reg<2> privilege = 0b11;

  // 执行时存下需要写入的idx，cmd和data
  // 提交时才写入
  // 否则csr指令未提交，其更改就已经生效
  reg<12> csr_idx;
  reg<32> csr_wdata;
  reg<2> csr_wcmd;
  reg<1> csr_we;

  wire<32> CSR_RegFile_1[CSR_NUM];
  wire<2> privilege_1 = 0b11;

  wire<12> csr_idx_1;
  wire<32> csr_wdata_1;
  wire<2> csr_wcmd_1;
  wire<1> csr_we_1;
};
