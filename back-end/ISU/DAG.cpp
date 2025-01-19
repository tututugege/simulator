#include "config.h"
#include <DAG.h>
#include <RISCV.h>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

Inst_node node[ROB_NUM];
Inst_node Empty_node = {.valid = false};
vector<int> optimal_int_idx;
vector<int> optimal_ld_idx;
vector<int> optimal_st_idx;

void init_dag() {
  for (int i = 0; i < ROB_NUM; i++) {
    node[i].src1 = nullptr;
    node[i].src2 = nullptr;
    node[i].dependency = nullptr;
    node[i].valid = false;
  }
}

void dag_del_node(int idx) { node[idx].valid = false; }

void dag_add_node(Inst_info *inst) {
  node[inst->rob_idx].valid = true;
  node[inst->rob_idx].inst = inst;
  int i;
  if (inst->src1_en) {
    for (i = 1; i < ROB_NUM; i++) {
      int idx = (inst->rob_idx + ROB_NUM - i) % ROB_NUM;
      if (node[idx].valid && node[idx].inst->dest_en &&
          node[idx].inst->dest_preg == inst->src1_preg) {
        node[inst->rob_idx].src1 = &node[idx];
        break;
      }
    }
    if (i == ROB_NUM) {
      node[inst->rob_idx].src1 = &Empty_node;
    }
  } else {
    node[inst->rob_idx].src1 = &Empty_node;
  }

  if (inst->src2_en) {
    for (i = 1; i < ROB_NUM; i++) {
      int idx = (inst->rob_idx + ROB_NUM - i) % ROB_NUM;
      if (node[idx].valid && node[idx].inst->dest_en &&
          node[idx].inst->dest_preg == inst->src2_preg) {
        node[inst->rob_idx].src2 = &node[idx];
        break;
      }
    }
    if (i == ROB_NUM) {
      node[inst->rob_idx].src2 = &Empty_node;
    }
  } else {
    node[inst->rob_idx].src2 = &Empty_node;
  }

  // LOAD和STORE依赖之前首个的STORE
  if (inst->op == LOAD) {
    for (i = 1; i < ROB_NUM; i++) {
      int idx = (inst->rob_idx + ROB_NUM - i) % ROB_NUM;
      if (node[i].valid && node[idx].inst->op == STORE)
        node[inst->rob_idx].dependency = &node[idx];
      break;
    }
    if (i == ROB_NUM) {
      node[inst->rob_idx].dependency = &Empty_node;
    }
  } else {
    node[inst->rob_idx].dependency = &Empty_node;
  }
}

void combineHelper(int m, int n, int start, std::vector<int> &combination,
                   std::vector<std::vector<int>> &result) {
  if (n == 0) {
    result.push_back(combination);
    return;
  }
  for (int i = start; i < m; i++) {
    combination.push_back(i);
    combineHelper(m, n - 1, i + 1, combination, result);
    combination.pop_back();
  }
}

std::vector<std::vector<int>> combine(int m, int n) {
  std::vector<std::vector<int>> result;
  std::vector<int> combination;
  combineHelper(m, n, 0, combination, result);
  return result;
}

std::vector<int> createVector(int n) {
  std::vector<int> v(n);
  std::iota(v.begin(), v.end(), 0);
  return v;
}

int BF_search(int int_num, int ld_num, int st_num, int cycle) {
  vector<int> int_leaf_idx;
  vector<int> ld_leaf_idx;
  vector<int> st_leaf_idx;

  // 寻找叶子节点
  for (int i = 0; i < ROB_NUM; i++) {
    if (node[i].valid && node[i].src1->valid == false &&
        node[i].src2->valid == false && node[i].dependency->valid == false) {
      if (node[i].inst->op == LOAD)
        ld_leaf_idx.push_back(i);
      else if (node[i].inst->op == STORE)
        st_leaf_idx.push_back(i);
      else
        int_leaf_idx.push_back(i);
    }
  }

  if (int_leaf_idx.empty() && ld_leaf_idx.empty() && st_leaf_idx.empty())
    return cycle;

  int min_T = 1000;

  std::vector<std::vector<int>> int_iss_all;
  std::vector<std::vector<int>> ld_iss_all;
  std::vector<std::vector<int>> st_iss_all;
  int optimal_idx_sum;

  if (int_leaf_idx.size() > int_num)
    int_iss_all = combine(int_leaf_idx.size(), int_num);
  else {
    int_iss_all.push_back(createVector(int_leaf_idx.size()));
  }

  if (ld_leaf_idx.size() > ld_num)
    ld_iss_all = combine(ld_leaf_idx.size(), ld_num);
  else {
    ld_iss_all.push_back(createVector(ld_leaf_idx.size()));
  }

  if (st_leaf_idx.size() > st_num)
    st_iss_all = combine(st_leaf_idx.size(), st_num);
  else {
    st_iss_all.push_back(createVector(st_leaf_idx.size()));
  }

  for (const auto &int_iss : int_iss_all) {
    // 删除对应叶子节点
    for (int i : int_iss)
      node[int_leaf_idx[i]].valid = false;

    for (const auto &ld_iss : ld_iss_all) {
      // 删除对应叶子节点
      for (int i : ld_iss)
        node[ld_leaf_idx[i]].valid = false;

      for (const auto &st_iss : st_iss_all) {
        // 删除对应叶子节点
        for (int i : st_iss)
          node[st_leaf_idx[i]].valid = false;

        int T = BF_search(2, 1, 1, cycle + 1);

        // 如果找到更优的发射序列
        if (T <= min_T && cycle == 0) {
          int inst_idx_sum = 0;
          for (int i : int_iss)
            inst_idx_sum += node[int_leaf_idx[i]].inst->inst_idx;
          for (int i : ld_iss)
            inst_idx_sum += node[ld_leaf_idx[i]].inst->inst_idx;
          for (int i : st_iss)
            inst_idx_sum += node[st_leaf_idx[i]].inst->inst_idx;

          // 耗时相同优先找更老的指令
          if (T == min_T && inst_idx_sum < optimal_idx_sum || T < min_T) {
            optimal_int_idx.clear();
            optimal_ld_idx.clear();
            optimal_st_idx.clear();
            for (int i : int_iss) {
              optimal_int_idx.push_back(int_leaf_idx[i]);
            }

            for (int i : ld_iss)
              optimal_ld_idx.push_back(ld_leaf_idx[i]);
            for (int i : st_iss)
              optimal_st_idx.push_back(st_leaf_idx[i]);

            optimal_idx_sum = inst_idx_sum;
          }
        }

        min_T = min(min_T, T);

        // 恢复
        for (int i : st_iss)
          node[st_leaf_idx[i]].valid = true;
      }

      // 恢复
      for (int i : ld_iss)
        node[ld_leaf_idx[i]].valid = true;
    }
    // 恢复
    for (int i : int_iss)
      node[int_leaf_idx[i]].valid = true;
  }

  return min_T;
}

int search_optimal() {
  optimal_int_idx.clear();
  optimal_ld_idx.clear();
  optimal_st_idx.clear();
  int min = BF_search(2, 1, 1, 0);
  /*for (int idx : optimal_int_idx) {*/
  /*  cout << idx << " ";*/
  /*}*/
  /*cout << endl;*/
  /**/
  /*for (int idx : optimal_ld_idx) {*/
  /*  cout << idx << " ";*/
  /*}*/
  /*cout << endl;*/
  /**/
  /*for (int idx : optimal_st_idx) {*/
  /*  cout << idx << " ";*/
  /*}*/
  /*cout << endl;*/
  return min;
}
