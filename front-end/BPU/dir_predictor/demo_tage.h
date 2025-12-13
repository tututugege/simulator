#ifndef DEMO_TAGE_H
#define DEMO_TAGE_H

#define BASE_ENTRY_NUM 2048
#define GHR_LENGTH 256
#define TN_MAX 4 // 0-tage_indexed, which means 0,1,2,3
#define TN_ENTRY_NUM 4096
#define FH_N_MAX 3              // how many different types of Folded history
#define USEFUL_RESET_VAL 262144 // 256K
#include <cstdint>

struct pred_out {
  bool pred;
  bool altpred;
  uint8_t pcpn;
  uint8_t altpcpn;
  uint32_t tage_idx[TN_MAX]; // TODO

};
pred_out TAGE_get_prediction(uint32_t PC);
void TAGE_do_update(uint32_t PC, bool real_dir, pred_out pred_out);
void do_GHR_update(bool real_dir);
void TAGE_update_FH(bool real_dir);

#endif // DEMO_TAGE_H