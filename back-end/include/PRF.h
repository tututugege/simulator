#include <config.h>
#include <cstdint>

class PRF {
public:
  void init();
  void read(int preg_idx[PRF_RD_NUM], int rdata[PRF_RD_NUM]);
  void write();

  int wr_idx[PRF_WR_NUM];
  uint32_t wdata[PRF_WR_NUM];
  bool wen[PRF_WR_NUM];

  uint32_t PRF[PRF_NUM];
};
