#pragma once
#include "IO.h"
#include <config.h>

class ARBITER_IN
{
public:
    ExmemControlIO* writebuffer2arbiter_control;
    ExmemControlIO* mshr2arbiter_control;

    WbArbiterIO* writebuffer2arbiter;
    MshrArbiterIO* mshr2arbiter;

    ExmemDataIO* mem_data;
};
class ARBITER_OUT
{
public:
    ExmemDataIO* arbiter2writebuffer_data;
    ExmemDataIO* arbiter2mshr_data;

    ExmemControlIO* mem_control;
};
typedef enum {
    ARBITER_STATE_IDLE = 0,
    ARBITER_STATE_STORE_PRIORITIZED = 1,
    ARBITER_STATE_LOAD_PRIORITIZED = 2
} ARBITER_STATE;
class Arbiter
{
public:
  void comb_in();
  void comb_out();
  void seq();

  void init();

  uint8_t state;

  ARBITER_IN in;
  ARBITER_OUT out;
  void print();
};
