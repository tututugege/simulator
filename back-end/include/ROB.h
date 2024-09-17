#pragma once
#include "config.h"
#include <cstdint>

typedef struct ROB_entry {
  uint32_t PC;
  Inst_op op;
  int dest_preg_idx;
  int dest_areg_idx;
  int old_dest_preg_idx;
  bool branch;
  bool dest_en;
  bool complete;

  bool pos_bit;

  /*bool trap;*/
  int store_addr;
  int store_data;
} ROB_entry;

typedef struct ROB_in {

  // dispatch写入ROB
  int PC[INST_WAY];
  Inst_op op[INST_WAY];
  int dest_areg_idx[INST_WAY];
  int dest_preg_idx[INST_WAY];
  int dest_en[INST_WAY];
  int old_dest_preg_idx[INST_WAY];

  // execute完成情况 store地址
  bool complete[ALU_NUM + AGU_NUM];
  bool br_taken[ALU_NUM];
  int idx[ALU_NUM + AGU_NUM];
  uint32_t store_addr[AGU_NUM];
  uint32_t store_data[AGU_NUM];
  uint32_t store_size[AGU_NUM];

} ROB_in;

typedef struct ROB_out {
  bool full;
  bool enq_bit;
  int enq_idx;
  int ld_commit_num;
  ROB_entry commit_entry[ISSUE_WAY];
} ROB_out;

class ROB {
public:
  void init();
  /*void ROB_enq(bool pos_bit[], int pos_idx[]);*/
  void seq();
  void comb();
  /*ROB_entry commit();*/
  /*void store(int idx, uint32_t address, uint32_t data);*/
  /*bool check_raw(int idx);*/

  /*void complete(int idx);*/
  /*void branch(int idx);*/

  ROB_in in;
  ROB_out out;

private:
  int enq_ptr;
  int deq_ptr;
  int count;
  ROB_entry entry[ROB_NUM];
  void pos_invert();
};

bool rob_cmp(int idx1, bool bit1, int idx2, bool bit2);
