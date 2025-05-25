#pragma once

#include "config.h"

class FU {
public:
  void exec(Inst_uop &);
  int latency = 0;
  int cycle = 0;
  bool complete = false;
};
