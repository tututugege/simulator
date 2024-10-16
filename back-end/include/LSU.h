#include <FIFO.h>
#include <config.h>
#include <cstdint>
typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t data;

  bool compelete;
  bool valid;
  uint32_t tag;
} STQ_entry;

typedef struct {
  // 分配
  STQ_entry alloc[INST_WAY];
  bool valid[INST_WAY];
  bool dis_fire[INST_WAY];

  // 实际写入
  STQ_entry write;
  bool wr_valid;
  uint32_t wr_idx;

  // 提交个数
  bool commit[ISSUE_WAY];

  // 分支信息
  Br_info br;

} STQ_in;

typedef struct {
  bool ready[INST_WAY];
  uint32_t enq_idx[INST_WAY];

  // 内存写端口
  bool wen;
  uint32_t wdata;
  uint32_t waddr;
  bool wstrb[4];

  // 唤醒
  uint32_t st_idx;

  // 当前有效信息
  bool entry_valid[STQ_NUM];
} STQ_out;

/*typedef struct {*/
/*  // 入队信息*/
/*  uint32_t addr;*/
/*  uint32_t size;*/
/*  int rob_idx;*/
/*  bool pre_store[STQ_NUM];*/
/*} LDQ_entry;*/
/**/
/*typedef struct {*/
/*  // 分配*/
/*  LDQ_entry alloc[INST_WAY];*/
/*  bool valid[INST_WAY];*/
/*  bool dis_fire[INST_WAY];*/
/*  // 实际写入*/
/*  LDQ_entry write;*/
/*  // 提交数目*/
/*  bool commit[ISSUE_WAY];*/
/*} LDQ_in;*/
/**/
/*typedef struct {*/
/*  bool ready[INST_WAY];*/
/*} LDQ_out;*/

// ROB子集 已经dispatch但未提交的load指令
/*class LDQ {*/
/*public:*/
/*  LDQ_in in;*/
/*  void comb();*/
/*  void seq();*/
/*  void init();*/
/*  void comb_alloc();*/
/**/
/*private:*/
/*  LDQ_entry entry[LDQ_NUM];*/
/*  int enq_ptr;*/
/*  int deq_ptr;*/
/*  int count;*/
/**/
/*  LDQ_entry entry_1[LDQ_NUM];*/
/*  int enq_ptr_1;*/
/*  int deq_ptr_1;*/
/*  int count_1;*/
/*};*/
/**/

class STQ {
public:
  STQ_in in;
  STQ_out out;
  void comb_alloc();
  void comb_fire();
  void comb_deq();
  void seq();
  void init();
  int enq_ptr;
  int deq_ptr;

private:
  STQ_entry entry[STQ_NUM];
  int commit_ptr;
  int count;

  STQ_entry entry_1[STQ_NUM];
  int enq_ptr_1;
  int deq_ptr_1;
  int commit_ptr_1;
  int count_1;
};
