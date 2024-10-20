#include <FIFO.h>
#include <config.h>
#include <cstdint>
typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t dest_preg;

  bool issue;
  bool fire;
  bool complete;
  bool valid;

  bool pre_store[STQ_NUM];
} LDQ_entry;

typedef struct {
  // ld_iq发射的ld指令将地址写入ldq
  bool from_ld_iq_valid;
  uint32_t ldq_idx;
  uint32_t addr;

  // stq中fire to mem的st指令写回ldq
  uint32_t stq_idx;
  bool st_valid;

  // dispatch的ld指令写入ldq
  bool from_dis_valid;
  uint32_t tag;

  bool commit[ISSUE_WAY];
} LDQ_in;

typedef struct {
  bool mem_fire_valid;
  uint32_t dest_preg;
  uint32_t data;

  bool to_dis_ready;
} LDQ_out;

typedef struct {
  uint32_t addr;
  uint32_t size;
  uint32_t data;

  bool issue;
  bool compelete;
  bool valid;
  uint32_t tag;
} STQ_entry;

typedef struct {
  // 分配
  uint32_t tag[INST_WAY];
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

  // ldq前递
  bool *ld_pre_store;
  uint32_t forward_addr;
  uint32_t forward_size;

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

  // ldq前递有效
  bool forward_valid;

  bool to_dis_pre_store[STQ_NUM];
} STQ_out;

class LDQ {
  public:

  private:
  LDQ_entry entry[LDQ_NUM];
  LDQ_entry entry_1[LDQ_NUM];

  int enq_ptr;
  int enq_ptr_1;

  int deq_ptr;
  int deq_ptr_1;

  int num;
  int num_1;
};

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
