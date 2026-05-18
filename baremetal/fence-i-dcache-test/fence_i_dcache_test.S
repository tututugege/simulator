    .option norvc

    # Self-modifying instruction test for the ICache <-> DCache coherence path.
    #
    # The simulator starts from the boot stub at 0x0 and jumps to this image at
    # 0x80000000.  This test modifies instructions through normal stores, uses
    # fence.i, then executes the modified code again.  It is intended to catch
    # cases where ICache refills from the old memory/LLC line instead of seeing
    # the newer DCache/WriteBuffer copy.
    #
    # The raw fence.i instruction is emitted with ".word 0x0000100f" instead of
    # the "fence.i" mnemonic so the file can still be assembled with
    # "-march=rv32i" and does not require enabling the zifencei ISA extension in
    # this standalone baremetal Makefile.
    .section .text
    .globl _start
    .type _start, @function

_start:
    # Case 1: basic same-line self modification.
    #
    # First execute selfmod_target and expect a0=1.  Then overwrite the first
    # instruction in that function with "addi a0, zero, 2", execute fence.i, and
    # call it again.  Passing this case proves the simple "modified first word"
    # path works.
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

    # Case 2: partial-line modification where word 0 is unchanged.
    #
    # This targets the 32-bit DCache probe interface used by the ICache miss
    # path.  A buggy implementation might probe only the first word of a line,
    # see that word unchanged, and incorrectly stop before checking the later
    # words.  To expose that class of bug, word 0 remains "addi a0, zero, 0",
    # while words 1..15 in the same 64B cacheline are changed from
    # "addi a0, a0, 1" to "addi a0, a0, 2".
    #
    # The function initially returns 15.  After patching words 1..15 and
    # executing fence.i, it must return 30.  The ret instruction is deliberately
    # placed in the next cacheline, so the checked 64B line contains exactly
    # word 0 plus the fifteen patched arithmetic instructions.
    call selfmod_later_words_target
    li t0, 15
    bne a0, t0, fail

    la t1, selfmod_later_words_target
    li t2, 0x00250513     # addi a0, a0, 2
    addi t3, t1, 4        # leave word 0 unchanged
    addi t4, t1, 64       # patch words 1..15 in the first 64B cacheline
patch_later_words:
    sw t2, 0(t3)
    addi t3, t3, 4
    bltu t3, t4, patch_later_words
    fence rw, rw
    .word 0x0000100f       # fence.i

    call selfmod_later_words_target
    li t0, 30
    bne a0, t0, fail

    # Case 3: partial-word instruction modification.
    #
    # Cases 1 and 2 use full-word stores.  This case uses a halfword store to
    # change only the high 16 bits of one 32-bit instruction:
    #   0x00300513 = addi a0, zero, 3
    #   0x00400513 = addi a0, zero, 4
    # The low halfword 0x0513 is unchanged; the store writes 0x0040 at
    # target+2.  Passing this case checks that DCache byte-lane merge/write
    # strobes are visible to the later ICache refill after fence.i.
    call selfmod_halfword_target
    li t0, 3
    bne a0, t0, fail

    la t1, selfmod_halfword_target
    li t2, 0x0040         # high halfword for addi a0, zero, 4
    sh t2, 2(t1)          # patch only bits [31:16] of the first instruction
    fence rw, rw
    .word 0x0000100f       # fence.i

    call selfmod_halfword_target
    li t0, 4
    bne a0, t0, fail

    # Case 4: single-byte instruction modification.
    #
    # This closes the remaining write-strobe gap left by Case 3.  The patch
    # changes only byte 2 of the first instruction:
    #   0x00500513 = addi a0, zero, 5   bytes: 13 05 50 00
    #   0x00600513 = addi a0, zero, 6   bytes: 13 05 60 00
    # Passing this case checks that a single dirty byte lane in DCache/WB is
    # preserved when ICache refills the instruction line after fence.i.
    call selfmod_byte_target
    li t0, 5
    bne a0, t0, fail

    la t1, selfmod_byte_target
    li t2, 0x60           # byte 2 for addi a0, zero, 6
    sb t2, 2(t1)          # patch only bits [23:16] of the first instruction
    fence rw, rw
    .word 0x0000100f       # fence.i

    call selfmod_byte_target
    li t0, 6
    bne a0, t0, fail

pass:
    li a0, 0
    ebreak

fail:
    li a0, 1
    ebreak

    # One-instruction target for Case 1.
    .balign 64
    .globl selfmod_target
    .type selfmod_target, @function
selfmod_target:
    addi a0, zero, 1
    ret
    .balign 64

    # 64B-aligned target for Case 2.
    #
    # Layout:
    #   offset 0x00: addi a0, zero, 0   (left unchanged by the patch loop)
    #   offset 0x04..0x3c: 15x addi a0, a0, 1   (patched to +2)
    #   offset 0x40: ret                (next cacheline)
    .globl selfmod_later_words_target
    .type selfmod_later_words_target, @function
selfmod_later_words_target:
    addi a0, zero, 0
    .rept 15
    addi a0, a0, 1
    .endr
    ret
    .balign 64

    # Target for Case 3.  The patch changes only the high halfword of the first
    # instruction, leaving the low halfword in place.
    .globl selfmod_halfword_target
    .type selfmod_halfword_target, @function
selfmod_halfword_target:
    addi a0, zero, 3
    ret
    .balign 64

    # Target for Case 4.  The patch changes only byte 2 of the first
    # instruction, leaving the other three instruction bytes in place.
    .globl selfmod_byte_target
    .type selfmod_byte_target, @function
selfmod_byte_target:
    addi a0, zero, 5
    ret
    .balign 64
