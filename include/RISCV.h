#pragma once
#include <TOP.h>
#define BIT_WIDTH 3500
extern const int bit_width;
extern int time_i;
void RISCV(bool input_data[BIT_WIDTH], bool *output_data);
void RISCV_32I();
void RISCV_CSR(bool input_data[BIT_WIDTH], bool *output_data);
void RISCV_32A(bool input_data[BIT_WIDTH], bool *output_data);
bool va2pa(bool *p_addr, bool *satp, bool *v_addr, uint32_t *p_memory,
           uint32_t type, bool *mstatus, uint32_t privilege, bool *sstatus);

/*#define POS_CSR_MTVEC (CSR_MTVEC * 3)*/
/*#define POS_CSR_MEPC (CSR_MEPC * 32)*/
/*#define POS_CSR_MCAUSE (CSR_MCAUSE * 32)*/
/*#define POS_CSR_MIE (CSR_MIE * 32)*/
/*#define POS_CSR_MIP (CSR_MIP * 32)*/
/*#define POS_CSR_MTVAL (CSR_MTVAL * 32)*/
/*#define POS_CSR_MSCRATCH (CSR_MSCRATCH * 32)*/
/*#define POS_CSR_MSTATUS (CSR_MSTATUS * 32)*/
/*#define POS_CSR_MIDELEG (CSR_MIDELEG * 32)*/
/*#define POS_CSR_MEDELEG (CSR_MEDELEG * 32)*/
/*#define POS_CSR_SEPC (CSR_SEPC * 32)*/
/*#define POS_CSR_STVEC (CSR_STVEC * 32)*/
/*#define POS_CSR_SCAUSE (CSR_SCAUSE * 32)*/
/*#define POS_CSR_SSCRATCH (CSR_SSCATCH * 32)*/
/*#define POS_CSR_STVAL (CSR_STVAL * 32)*/
/*#define POS_CSR_SSTATUS (CSR_SSTATUS * 32)*/
/*#define POS_CSR_SIE (CSR_SIE * 32)*/
/*#define POS_CSR_SIP (CSR_SIP * 32)*/
/*#define POS_CSR_SATP (CSR_SATP * 32)*/
/*#define POS_CSR_MHARTID (CSR_MHARTID * 32)*/
/*#define POS_CSR_MISA (CSR_MISA * 32)*/
/**/

#define VIRTUAL_MEMORY_LENGTH (1024 * 1024 * 1024)  // 4B
#define PHYSICAL_MEMORY_LENGTH (1024 * 1024 * 1024) // 4B

#define INST_EBREAK 0x00100073
#define INST_ECALL 0x00000073
#define INST_MRET 0x30200073

extern Back_Top back;
enum enum_number_opcode {
  number_0_opcode_lui = 0b0110111,   // lui
  number_1_opcode_auipc = 0b0010111, // auipc
  number_2_opcode_jal = 0b1101111,   // jal
  number_3_opcode_jalr = 0b1100111,  // jalr
  number_4_opcode_beq = 0b1100011,   // beq, bne, blt, bge, bltu, bgeu
  number_5_opcode_lb = 0b0000011,    // lb, lh, lw, lbu, lhu
  number_6_opcode_sb = 0b0100011,    // sb, sh, sw
  number_7_opcode_addi =
      0b0010011, // addi, slti, sltiu, xori, ori, andi, slli, srli, srai
  number_8_opcode_add =
      0b0110011, // add, sub, sll, slt, sltu, xor, srl, sra, or, and
  number_9_opcode_fence = 0b0001111, // fence, fence.i
  number_10_opcode_ecall =
      0b1110011, // ecall, ebreak, csrrw, csrrs, csrrc, csrrwi, csrrsi, csrrci
  number_11_opcode_lrw =
      0b0101111, // lr.w, sc.w, amoswap.w, amoadd.w, amoxor.w, amoand.w,
                 // amoor.w, amomin.w, amomax.w, amominu.w, amomaxu.w
};
