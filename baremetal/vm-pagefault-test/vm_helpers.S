    .section .text
    .align 2

    .globl install_trap_vector
install_trap_vector:
    la t0, trap_vector
    .word 0x30529073 # csrw mtvec, t0
    ret

    .align 2
    .globl write_satp
write_satp:
    .word 0x18051073 # csrw satp, a0
    ret

    .align 2
    .globl sfence_vma_all_asm
sfence_vma_all_asm:
    .word 0x12000073 # sfence.vma x0, x0
    ret

    .align 2
trap_vector:
    .word 0x342022f3 # csrr t0, mcause
    .word 0x34302373 # csrr t1, mtval
    .word 0x341023f3 # csrr t2, mepc

    la t3, last_mcause
    sw t0, 0(t3)
    la t3, last_mtval
    sw t1, 0(t3)
    la t3, last_mepc
    sw t2, 0(t3)

    la t3, trap_count
    lw t4, 0(t3)
    addi t4, t4, 1
    sw t4, 0(t3)

    addi t2, t2, 4
    .word 0x34139073 # csrw mepc, t2
    .word 0x30200073 # mret

    .align 2
    .globl mprv_load_u32
mprv_load_u32:
    .word 0x300022f3 # csrr t0, mstatus
    li t1, 0x1800
    not t1, t1
    and t2, t0, t1
    li t1, 0x20800
    or t2, t2, t1
    .word 0x30039073 # csrw mstatus, t2
    lw a0, 0(a0)
    .word 0x30029073 # csrw mstatus, t0
    ret

    .align 2
    .globl mprv_store_u32
mprv_store_u32:
    .word 0x300022f3 # csrr t0, mstatus
    li t1, 0x1800
    not t1, t1
    and t2, t0, t1
    li t1, 0x20800
    or t2, t2, t1
    .word 0x30039073 # csrw mstatus, t2
    sw a1, 0(a0)
    .word 0x30029073 # csrw mstatus, t0
    ret
