    .equ SATP_MODE_SV32, 0x80000000
    .equ TEST_U_EXEC_VA, 0x40000000
    .equ TEST_S_ENTRY_VA, 0x40001000

    .equ PTE_V, (1 << 0)
    .equ PTE_R, (1 << 1)
    .equ PTE_X, (1 << 3)
    .equ PTE_U, (1 << 4)
    .equ PTE_A, (1 << 6)

    .equ MSTATUS_MPP_MASK, 0x00001800
    .equ MSTATUS_MPP_S, 0x00000800
    .equ SSTATUS_SUM, 0x00040000

    .equ INSN_PAGE_FAULT_CAUSE, 12
    .equ END_FLAG_ADDR, 0x1c
    .equ UART_BASE, 0x10000000
    .equ UART_LSR, 5
    .equ UART_LSR_THRE, 0x20

    .section .text
    .align 2
    .globl main
main:
    la t0, trap_vector
    csrw mtvec, t0

    la t0, l1_pt
    li t1, 2048
1:
    sw zero, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -1
    bnez t1, 1b

    # L1 non-leaf for VPN1(TEST_U_EXEC_VA / TEST_S_ENTRY_VA).
    la t0, l1_pt
    li t1, (TEST_U_EXEC_VA >> 22)
    slli t1, t1, 2
    add t0, t0, t1
    la t1, l0_pt
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, PTE_V
    sw t1, 0(t0)

    # Leaf for S-mode trampoline page: executable supervisor page.
    la t0, l0_pt
    li t1, ((TEST_S_ENTRY_VA >> 12) & 0x3ff)
    slli t1, t1, 2
    add t0, t0, t1
    la t1, supervisor_exec_page
    srli t1, t1, 12
    slli t1, t1, 10
    li t2, (PTE_V | PTE_R | PTE_X | PTE_A)
    or t1, t1, t2
    sw t1, 0(t0)

    # Leaf for user execute page: executable user page.
    la t0, l0_pt
    li t1, ((TEST_U_EXEC_VA >> 12) & 0x3ff)
    slli t1, t1, 2
    add t0, t0, t1
    la t1, user_exec_page
    srli t1, t1, 12
    slli t1, t1, 10
    li t2, (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A)
    or t1, t1, t2
    sw t1, 0(t0)

    la t0, l1_pt
    srli t0, t0, 12
    li t1, SATP_MODE_SV32
    or t0, t0, t1
    csrw satp, t0
    sfence.vma x0, x0

    li t0, TEST_S_ENTRY_VA
    csrw mepc, t0
    csrr t1, mstatus
    li t2, MSTATUS_MPP_MASK
    not t2, t2
    and t1, t1, t2
    li t2, MSTATUS_MPP_S
    or t1, t1, t2
    csrw mstatus, t1
    mret

    # We should never return here.
    j fail

    .align 2
trap_vector:
    csrr t0, mcause
    csrr t1, mtval
    la t2, last_mcause
    sw t0, 0(t2)
    la t2, last_mtval
    sw t1, 0(t2)

    li t2, INSN_PAGE_FAULT_CAUSE
    bne t0, t2, fail
    li t2, TEST_U_EXEC_VA
    bne t1, t2, fail

pass:
    la a0, pass_msg
    jal ra, uart_puts
    li t0, END_FLAG_ADDR
    sw zero, 0(t0)
1:
    j 1b

fail:
    la a0, fail_msg
    jal ra, uart_puts
    li t0, END_FLAG_ADDR
    li t1, 1
    sw t1, 0(t0)
1:
    j 1b

uart_puts:
    lbu a1, 0(a0)
    beqz a1, 1f
    addi sp, sp, -16
    sw ra, 12(sp)
    jal ra, uart_putc
    lw ra, 12(sp)
    addi sp, sp, 16
    addi a0, a0, 1
    j uart_puts
1:
    ret

uart_putc:
    li t0, UART_BASE
1:
    lbu t1, UART_LSR(t0)
    andi t1, t1, UART_LSR_THRE
    beqz t1, 1b
    sb a1, 0(t0)
    ret

    .balign 4096
supervisor_exec_page:
    li t0, SSTATUS_SUM
    csrs sstatus, t0
    li t0, TEST_U_EXEC_VA
    jr t0

    .balign 4096
user_exec_page:
    ecall
1:
    j 1b

    .section .data
    .balign 4096
l1_pt:
    .space 4096

    .balign 4096
l0_pt:
    .space 4096

last_mcause:
    .word 0
last_mtval:
    .word 0

pass_msg:
    .asciz "[VM-SUM-FETCH] PASS\n"
fail_msg:
    .asciz "[VM-SUM-FETCH] FAIL\n"
