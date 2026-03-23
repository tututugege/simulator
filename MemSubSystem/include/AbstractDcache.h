#pragma once

#include "IO.h"

class AbstractDcache {
public:
  virtual ~AbstractDcache() {}


  virtual void init() = 0;
  virtual void comb() = 0;
  virtual void seq() = 0;
};
