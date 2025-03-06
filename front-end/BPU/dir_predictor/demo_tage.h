#ifndef DEMO_TAGE_H
#define DEMO_TAGE_H

#include <cstdint>
struct pred_out {
  bool pred;
  bool altpred;
  uint8_t pcpn;
  uint8_t altpcpn;
};
pred_out TAGE_get_prediction(uint32_t PC);
void TAGE_do_update(uint32_t PC, bool real_dir, pred_out pred_out);
void do_GHR_update(bool real_dir);
void TAGE_update_FH(bool real_dir);

#endif // DEMO_TAGE_H