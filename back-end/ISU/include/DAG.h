#include "config.h"
#include <vector>

int search_optimal();
void init_dag();
void dag_add_node(Inst_info *);
void dag_del_node(int);

extern vector<int> optimal_int_idx;
extern vector<int> optimal_ld_idx;
extern vector<int> optimal_st_idx;

class Inst_node {
public:
  Inst_info *inst;
  Inst_node *src1;
  Inst_node *src2;
  Inst_node *dependency;
  /*int time = 1;*/
  bool valid;
};
