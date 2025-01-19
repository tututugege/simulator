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
  /*Br_info br;*/

} STQ_in;

typedef struct {
  bool ready[INST_WAY];
  uint32_t enq_idx[INST_WAY];

  // 内存写端口
  bool wen;
  uint32_t wdata;
  uint32_t waddr;
  uint32_t wstrb;

  // 唤醒
  uint32_t st_idx;

  // 当前有效信息
  bool entry_valid[STQ_NUM];
} STQ_out;

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
