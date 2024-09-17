#include <config.h>

typedef struct AGU_in {
  uint32_t base;
  uint32_t off;
} AGU_in;

typedef struct AGU_out {
  uint32_t addr;
  bool idle;
} AGU_out;

class AGU {
public:
  void cycle();
  AGU_in in;
  AGU_out out;
};

typedef struct ALU_in {
  uint32_t src1;
  uint32_t src2;
  Inst_op op;
} ALU_in;

typedef struct ALU_out {
  uint32_t res;
  bool idle;
} ALU_out;

class ALU {
public:
  void cycle();
  ALU_in in;
  ALU_out out;
};

typedef struct BRU_in {
  uint32_t pc;
  uint32_t off;
  uint32_t src1;
  uint32_t src2;
  Inst_op op;
} BRU_in;

typedef struct BRU_out {
  uint32_t pc_next;
  bool branch;
  bool idle;
} BRU_out;

class BRU {
public:
  void cycle();
  BRU_in in;
  BRU_out out;
};
