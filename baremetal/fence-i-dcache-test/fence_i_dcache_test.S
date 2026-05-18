    .option norvc
    .section .text
    .globl _start
    .type _start, @function

_start:
    call selfmod_target
    li t0, 1
    bne a0, t0, fail

    la t1, selfmod_target
    li t2, 0x00200513     # addi a0, zero, 2
    sw t2, 0(t1)
    fence rw, rw
    .word 0x0000100f       # fence.i

    call selfmod_target
    li t0, 2
    bne a0, t0, fail

pass:
    li a0, 0
    ebreak

fail:
    li a0, 1
    ebreak

    .balign 64
    .globl selfmod_target
    .type selfmod_target, @function
selfmod_target:
    addi a0, zero, 1
    ret
    .balign 64
