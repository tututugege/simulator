#include <cstdint>
#include <diff.h>

class Ref_cpu {
  CPU_state state;

  uint32_t *memory;
  bool asy;
  bool page_fault_inst;
  bool page_fault_load;
  bool page_fault_store;
  bool privilege[2];

  void init();
  void exec();
};
