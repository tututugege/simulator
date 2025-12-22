#pragma once
#include "IO.h"
#include <config.h>

class ARBITER_IN
{
public:
    EXMem_CONTROL* writebuffer2arbiter_control;
    EXMem_CONTROL* mshr2arbiter_control;

    WB_Arbiter* writebuffer2arbiter;
    MSHR_Arbiter* mshr2arbiter;

    EXMem_DATA* mem_data;
};
class ARBITER_OUT
{
public:
    EXMem_DATA* arbiter2writebuffer_data;
    EXMem_DATA* arbiter2mshr_data;

    EXMem_CONTROL* mem_control;
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
