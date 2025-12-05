#include "IO.h"
#include "config.h"
class DIS_OUT {
public:
  Dis_Ren *dis2ren;
  Dis_Rob *dis2rob;
  Dis_Iss *dis2iss;
  Dis_Stq *dis2stq;
};

class DIS_IN {
public:
  Ren_Dis *ren2dis;
  Rob_Dis *rob2dis;
  Iss_Dis *iss2dis;
  Stq_Dis *stq2dis;
  Prf_Awake *prf_awake;
  Iss_Awake *iss_awake;
  Rob_Broadcast *rob_bcast;
  Dec_Broadcast *dec_bcast;
};

class Dispatch {

public:
  DIS_IN in;
  DIS_OUT out;

  void comb_alloc();
  void comb_dispatch();
  void comb_wake();
  void comb_fire();
  void comb_pipeline();
  void seq();

  Inst_entry inst_r[FETCH_WIDTH];
  Inst_entry inst_r_1[FETCH_WIDTH];
};
