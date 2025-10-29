#include "IO.h"
#include "config.h"
class DIS_IO {
public:
  Ren_Dis *ren2dis;
  Dis_Ren *dis2ren;

  Rob_Dis *rob2dis;
  Dis_Rob *dis2rob;

  Dis_Iss *dis2iss;
  Iss_Dis *iss2dis;

  Stq_Dis *stq2dis;
  Dis_Stq *dis2stq;

  Prf_Awake *prf_awake;
  Iss_Awake *iss_awake;

  Rob_Broadcast *rob_bcast;
  Dec_Broadcast *dec_bcast;
};

class Dispatch {

public:
  DIS_IO io;

  // 对每个IQ选择最多2个
  wire1_t uop_sel[IQ_NUM][FETCH_WIDTH] = {0};
  wire1_t to_iq[IQ_NUM][FETCH_WIDTH] = {0};
  // 实际硬件可以写成4bit独热码用于选择，这里为了方便使用idx
  wire4_t port_idx[IQ_NUM][2];
  Inst_entry inst_alloc[FETCH_WIDTH];

  void comb_alloc();
  void comb_dispatch();
  void comb_wake();
  void comb_fire();
  void comb_pipeline();
  void seq();
  Inst_entry inst_r[FETCH_WIDTH];
  Inst_entry inst_r_1[FETCH_WIDTH];
};
