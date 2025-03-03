#ifndef TAGE_IO_H
#define TAGE_IO_H

#include <cstdint>
struct pred_out {
  bool pred;
  bool altpred;
  uint8_t pcpn;
  uint8_t altpcpn;
};
pred_out C_TAGE_do_pred(uint32_t pc);
void C_TAGE_do_update(uint32_t pc, bool real_dir, pred_out pred_out);
void C_TAGE_update_HR(bool new_history);

#endif // TAGE_IO_H