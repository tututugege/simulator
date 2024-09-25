#include <FIFO.h>
#include <config.h>
#include <cstdint>

typedef struct BRU_in {
  uint32_t pc;
  uint32_t off;
  uint32_t src1;
  uint32_t src2;
  Inst_op op;
} BRU_in;

typedef struct BRU_out {
  uint32_t pc_next;
  bool br_taken;
  bool idle;
} BRU_out;

class BRU {
public:
  void cycle();
  BRU_in in;
  BRU_out out;
};

typedef struct Br_Tag_in {
  bool alloc[INST_WAY];

  bool free[ISSUE_WAY];
  uint32_t free_tag[ISSUE_WAY];

} Br_Tag_in;

typedef struct Br_Tag_out {
  uint32_t alloc_tag[INST_WAY];
  uint32_t now_tag;
} Br_Tag_out;

class Br_Tag {
public:
  Br_Tag_in in;
  Br_Tag_out out;
  void init();
  void comb();
  void seq();

  FIFO<uint32_t> free_tag_list = FIFO<uint32_t>(1, 1, 4, 32);
  FIFO<uint32_t> tag_list = FIFO<uint32_t>(1, 1, 4, 32);
};
