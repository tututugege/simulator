#include <config.h>
class STQ {};

typedef struct {
  uint32_t pc;
  uint32_t addr;
  uint32_t size;
  uint32_t dest_preg_idx;
  int rob_idx;
  bool rob_bit;
  bool valid;
} LDQ_entry;

typedef struct {
  // 违例检查

  // 分配
  LDQ_entry alloc[INST_WAY];

  // 实际写入
  LDQ_entry write;

  // 提交数目
  int commit_num;
} LDQ_in;

typedef struct {

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
};

/*typedef struct {*/
/*  uint32_t addr;*/
/*  uint32_t data;*/
/*  uint32_t size;*/
/*} STQ_entry;*/
