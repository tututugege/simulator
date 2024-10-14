#include <config.h>

typedef struct BRU_in {
  uint32_t pc;
  uint32_t off;
  uint32_t src1;
  bool alu_out;
  Inst_op op;
} BRU_in;

typedef struct BRU_out {
  uint32_t pc_next;
  bool br_taken;
} BRU_out;

class BRU {
public:
  void cycle();
  BRU_in in;
  BRU_out out;
};

typedef struct AGU_in {
  uint32_t base;
  uint32_t off;
} AGU_in;

typedef struct AGU_out {
  uint32_t addr;
} AGU_out;

class AGU {
public:
  void cycle();
  AGU_in in;
  AGU_out out;
};
typedef struct ALU_op {
  Inst_op op;
  uint32_t func3;
  bool func7_5;
  bool src2_is_imm;
} ALU_op;

typedef struct ALU_in {
  uint32_t src1;
  uint32_t src2;
  ALU_op alu_op;
} ALU_in;

typedef struct ALU_out {
  uint32_t res;
} ALU_out;

class ALU {
public:
  void cycle();
  ALU_in in;
  ALU_out out;
};
