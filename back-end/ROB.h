#include "config.h"
#include <cstdint>

typedef struct ROB_entry {
  uint32_t PC;
  Inst_op op;
  int dest_preg_idx;
  int dest_areg_idx;
  int old_dest_preg_idx;
  bool pos_bit;
  bool complete;
  bool branch;

  /*bool trap;*/
  /*int store_addr;*/
  /*int store_data;*/
} ROB_entry;

typedef struct ROB_in {
  int PC[WAY];
  Inst_op op[WAY];
  int dest_preg_idx[WAY];
  int old_dest_preg_idx[WAY];
} ROB_in;

class ROB {
public:
  void init();
  void ROB_enq(bool pos_bit[], int pos_idx[]);
  void ROB_deq();
  ROB_entry commit();

  uint32_t get_pc(int idx);
  void complete(int idx);
  void branch(int idx);

  ROB_in in;

private:
  int enq_ptr;
  int deq_ptr;
  int count;
  ROB_entry entry[ROB_NUM];
  void pos_invert();
};
