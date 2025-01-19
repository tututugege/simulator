#include "config.h"
#include <EXU.h>
#include <IO.h>

class PRF_IO {
public:
  Iss_Prf *iss2prf;
  Prf_Exe *prf2exe;
  Exe_Prf *exe2prf;
  Prf_Rob *prf2rob;
};

class PRF {
public:
  PRF_IO io;
  uint32_t reg_file[PRF_NUM];

  void comb();
  void init();
  void seq();

private:
  Inst_entry inst_r[EXU_NUM];
};
