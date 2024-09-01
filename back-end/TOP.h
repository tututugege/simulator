#pragma once
#include "IQ.h"
#include "ROB.h"
#include "Rename.h"
#include "config.h"

typedef struct Back_in {
  int PC[WAY];
  Inst_info inst[WAY];
} Back_in;

class Back_Top {
private:
  Rename rename;
  IQ iq;
  ROB rob;
  Back_in in;

public:
  void Back_cycle();
  void init();
};
