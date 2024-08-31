#include "RISCV.h"
#include "cvt.h"

bool va2pa(bool *p_addr, bool *satp, bool *v_addr, uint32_t *p_memory,
           uint32_t type, bool *mstatus, uint32_t privilege, bool *sstatus) {
  uint32_t d = 24;
  uint32_t a = 25;
  uint32_t g = 26;
  uint32_t u = 27;
  uint32_t x = 28;
  uint32_t w = 29;
  uint32_t r = 30;
  uint32_t v = 31;
  bool mxr = mstatus[31 - 19];
  bool sum = mstatus[31 - 18];
  bool mprv = mstatus[31 - 17];
  uint32_t mpp = cvt_bit_to_number_unsigned(mstatus + 19 * sizeof(bool), 2);
  bool pte1[32];
  init_indice(pte1, 0, 32);
  copy_indice(pte1, 0, satp, 12, 20);
  copy_indice(pte1, 20, v_addr, 0, 10);
  uint32_t number_pte1 = cvt_bit_to_number_unsigned(pte1, 32);
  uint32_t number_pte1_stored = p_memory[uint32_t(number_pte1 / 4)];
  if (log)
    cout << "pte1:" << hex << number_pte1_stored << endl;
  bool bit_pte1_stored[32];
  cvt_number_to_bit_unsigned(bit_pte1_stored, number_pte1_stored, 32);
  if (bit_pte1_stored[v] == false ||
      (bit_pte1_stored[r] == false && bit_pte1_stored[w] == true))
    return false;
  if (bit_pte1_stored[r] == true || bit_pte1_stored[x] == true) {
    if ((type == 0 && bit_pte1_stored[x] == true) ||
        (type == 1 && bit_pte1_stored[r] == true) ||
        (type == 2 && bit_pte1_stored[w] == true) ||
        (type == 1 && mxr == true && bit_pte1_stored[x] == true)) {
      ;
    } else
      return false;
    if (privilege == 1 && sum == 0 && bit_pte1_stored[u] == true &&
        sstatus[31 - 18] == false)
      return false;
    if (privilege != 1 && mprv == 1 && mpp == 1 && sum == 0 &&
        bit_pte1_stored[u] == true && sstatus[31 - 18] == false)
      return false;
    if ((number_pte1_stored >> 10) % 1024 != 0)
      return false;
    if (bit_pte1_stored[a] == false ||
        (type == 2 && bit_pte1_stored[d] == false))
      return false;
    copy_indice(p_addr, 0, bit_pte1_stored, 2, 10);
    copy_indice(p_addr, 10, v_addr, 10, 22);
    return true;
  }
  bool pte2[32];
  copy_indice(pte2, 0, bit_pte1_stored, 2, 20);
  copy_indice(pte2, 20, v_addr, 10, 10);
  uint32_t number_pte2 = cvt_bit_to_number_unsigned(pte2, 32);
  uint32_t number_pte2_stored = p_memory[uint32_t(number_pte2 / 4)];
  if (log)
    cout << "pte2: " << hex << number_pte2_stored << endl;
  bool bit_pte2_stored[32];
  cvt_number_to_bit_unsigned(bit_pte2_stored, number_pte2_stored, 32);
  if (bit_pte2_stored[v] == false ||
      (bit_pte2_stored[r] == false && bit_pte2_stored[w] == true))
    return false;
  if (bit_pte2_stored[r] == true || bit_pte2_stored[x] == true) {
    if ((type == 0 && bit_pte2_stored[x] == true) ||
        (type == 1 && bit_pte2_stored[r] == true) ||
        (type == 2 && bit_pte2_stored[w] == true) ||
        (type == 1 && mxr == true && bit_pte2_stored[x] == true)) {
      ;
    } else
      return false;
    if (privilege == 1 && sum == 0 && bit_pte2_stored[u] == true &&
        sstatus[31 - 18] == false)
      return false;
    if (privilege != 1 && mprv == 1 && mpp == 1 && sum == 0 &&
        bit_pte2_stored[u] == true && sstatus[31 - 18] == false)
      return false;
    if (bit_pte2_stored[a] == false ||
        (type == 2 && bit_pte2_stored[d] == false))
      return false;
    copy_indice(p_addr, 0, bit_pte2_stored, 2, 20);
    copy_indice(p_addr, 20, v_addr, 20, 12);
    return true;
  }

  // ???
  return true;
}
