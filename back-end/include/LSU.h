#include <FIFO.h>
#include <config.h>
typedef struct {
  // 入队信息
  uint32_t addr;
  uint32_t size;
  uint32_t data;
  bool valid;
} STQ_entry;

typedef struct {
  // 违例检查

  // 分配
  STQ_entry alloc[INST_WAY];

  // 实际写入
  STQ_entry write;
} STQ_in;

typedef struct {

  // 入队信息
  uint32_t addr;
  uint32_t size;
  uint32_t dest_preg_idx;
  int rob_idx;
  bool valid;

  // 违例检查
  uint32_t store_addr;
  bool store_valid;
} LDQ_entry;

typedef struct {
  // 违例检查

  // 分配
  LDQ_entry alloc[INST_WAY];

  // 实际写入
  LDQ_entry write;

  // 提交数目
  bool commit[ISSUE_WAY];
} LDQ_in;

typedef struct {
  bool refetch;
} LDQ_out;

// ROB子集 已经dispatch但未提交的load指令
class LDQ {
public:
  LDQ_in in;
  LDQ_out out;
  void comb();
  void seq();
  void init();

private:
  LDQ_entry entry[LDQ_NUM];
  int enq_ptr;
  int deq_ptr;
  int count;

  LDQ_entry entry_1[LDQ_NUM];
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;
};

class STQ {
public:
  STQ_in in;
  void comb();
  void seq();
  void init();

private:
  STQ_entry entry[LDQ_NUM];
  int enq_ptr;
  int deq_ptr;
  int count;

  STQ_entry entry_1[LDQ_NUM];
  int enq_ptr_1;
  int deq_ptr_1;
  int count_1;
};
