#pragma once
#include <SRAM.h>
#include <config.h>

typedef struct ROB_entry {
  uint32_t PC;
  Inst_type type;
  int dest_preg_idx;
  int dest_areg_idx;
  int old_dest_preg_idx;
  bool dest_en;

  // 放入store queue
  /*int store_addr;*/
  /*int store_data;*/
} ROB_entry;

typedef struct ROB_in {

  // dispatch写入ROB
  bool valid[INST_WAY];
  uint32_t PC[INST_WAY];
  Inst_type type[INST_WAY];
  int dest_areg_idx[INST_WAY];
  int dest_preg_idx[INST_WAY];
  bool dest_en[INST_WAY];
  int old_dest_preg_idx[INST_WAY];

  // execute完成情况 store地址
  bool complete[ALU_NUM + AGU_NUM];
  bool br_taken[ALU_NUM];
  int idx[ALU_NUM + AGU_NUM];
  /*uint32_t store_addr[AGU_NUM];*/
  /*uint32_t store_data[AGU_NUM];*/
  /*uint32_t store_size[AGU_NUM];*/

} ROB_in;

typedef struct ROB_out {
  bool full;
  bool enq_bit;
  int enq_idx;
  int ld_commit_num;
  ROB_entry commit_entry[ISSUE_WAY];
  bool valid[ISSUE_WAY];
} ROB_out;

class ROB {
public:
  void init();
  void seq();
  void comb();

  ROB_in in;
  ROB_out out;

  bool branch_1[ROB_NUM];
  bool complete_1[ROB_NUM];

private:
  SRAM<ROB_entry> entry = SRAM<ROB_entry>(3, 2, ROB_NUM, sizeof(ROB_entry) * 8);
  bool pos_bit;
  int enq_ptr;
  int deq_ptr;
  int count;
  bool valid[ROB_NUM];
  bool branch[ROB_NUM];
  bool complete[ROB_NUM];
  bool trap[ROB_NUM];

  bool valid_1[ROB_NUM];
  bool pos_bit_1;
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;
  bool trap_1[ROB_NUM];
};

bool rob_cmp(int idx1, bool bit1, int idx2, bool bit2);
