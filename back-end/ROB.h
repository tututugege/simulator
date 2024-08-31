#include "config.h"
#include <cstdint>

typedef struct ROB_entry {
  uint32_t PC;
  Inst_type type;
  bool trap;
  int dest_preg_idx;
  int old_dest_preg_idx;
  int store_addr;
  int store_data;
} ROB_entry;

class ROB {
public:
  void ROB_enq();
  void ROB_deq();
  void init();

private:
  int enq_ptr;
  int deq_ptr;
  int count;
  ROB_entry entry[ROB_NUM];
};
