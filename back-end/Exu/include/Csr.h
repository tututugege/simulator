#pragma once
#include "IO.h"
#include "config.h"

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
  int ret;
  switch (csr_idx) {
  case number_mtvec:
    ret = csr_mtvec;
    break;
  case number_mepc:
    ret = csr_mepc;
    break;
  case number_mcause:
    ret = csr_mcause;
    break;
  case number_mie:
    ret = csr_mie;
    break;
  case number_mip:
    ret = csr_mip;
    break;
  case number_mtval:
    ret = csr_mtval;
    break;
  case number_mscratch:
    ret = csr_mscratch;
    break;
  case number_mstatus:
    ret = csr_mstatus;
    break;
  case number_mideleg:
    ret = csr_mideleg;
    break;
  case number_medeleg:
    ret = csr_medeleg;
    break;
  case number_sepc:
    ret = csr_sepc;
    break;
  case number_stvec:
    ret = csr_stvec;
    break;
  case number_scause:
    ret = csr_scause;
    break;
  case number_sscratch:
    ret = csr_sscratch;
    break;
  case number_stval:
    ret = csr_stval;
    break;
  case number_sstatus:
    ret = csr_sstatus;
    break;
  case number_sie:
    ret = csr_sie;
    break;
  case number_sip:
    ret = csr_sip;
    break;
  case number_satp:
    ret = csr_satp;
    break;
  case number_mhartid:
    ret = csr_mhartid;
    break;
  case number_misa:
    ret = csr_misa;
    break;
  case number_time:
  case number_timeh:
    Assert(0 && "time/timeh are not implemented in Csr RegFile");
    break;
  default:
    Assert(0);
  }
  return ret;
}

typedef struct {
  ExeCsrIO *exe2csr;
  RobCsrIO *rob2csr;
  RobBroadcastIO *rob_bcast;
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

  void comb_csr_status();
  void comb_csr_read();
  void comb_csr_write();
  void comb_interrupt();
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
