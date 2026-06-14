    .section .text
    .align 2

    .globl install_trap_vector
install_trap_vector:
    la t0, trap_vector
    .word 0x30529073 # csrw mtvec, t0
    ret

    .align 2
trap_vector:
    .word 0x342022f3 # csrr t0, mcause
    .word 0x34102373 # csrr t1, mepc
    .word 0x344023f3 # csrr t2, mip

    la t3, last_mcause
    sw t0, 0(t3)
    la t3, last_mepc
    sw t1, 0(t3)
    la t3, last_mip
    sw t2, 0(t3)

    la t3, trap_count
    lw t4, 0(t3)
    addi t4, t4, 1
    sw t4, 0(t3)

    # Emit a visible marker from the trap handler itself.
    li t5, 0x10000000
    li t6, '!'
    sb t6, 0(t5)

    # Clear timer compare so MTIP deasserts after return.
    li t5, 0x1fd0e008
    li t6, -1
    sw t6, 0(t5)
    sw t6, 4(t5)

    addi t1, t1, 4
    .word 0x34131073 # csrw mepc, t1
    .word 0x30200073 # mret

    .align 2
    .globl read_mstatus
read_mstatus:
    .word 0x30002573 # csrr a0, mstatus
    ret

    .align 2
    .globl read_mie
read_mie:
    .word 0x30402573 # csrr a0, mie
    ret

    .align 2
    .globl write_mie
write_mie:
    .word 0x30451073 # csrw mie, a0
    ret

    .align 2
    .globl set_mstatus_bits
set_mstatus_bits:
    .word 0x30052073 # csrs mstatus, a0
    ret

    .align 2
    .globl read_mip
read_mip:
    .word 0x34402573 # csrr a0, mip
    ret
