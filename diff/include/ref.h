#include <cstdint>
#include <diff.h>

class Ref_cpu {
public:
  uint32_t *memory;
  uint32_t Instruction;
  CPU_state state;
  uint8_t privilege;
  bool asy;
  bool page_fault_inst;
  bool page_fault_load;
  bool page_fault_store;
  bool illegal_exception;

  bool M_software_interrupt;
  bool M_timer_interrupt;
  bool M_external_interrupt;
  bool S_software_interrupt;
  bool S_timer_interrupt;
  bool S_external_interrupt;

  bool is_br;
  bool br_taken;

  bool is_exception;
  bool is_csr;

  void init(uint32_t reset_pc);
  void exec();
  void RISCV();
  void RV32IM();
  void RV32A();
  void RV32CSR();
  void exception(uint32_t trap_val);
  void store_data();

  uint32_t store_buffer_addr[STQ_NUM];
  uint32_t store_buffer_data[STQ_NUM];
  int store_buffer_ptr = 0;
};
