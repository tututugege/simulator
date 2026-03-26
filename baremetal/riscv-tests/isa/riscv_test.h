#ifndef _ENV_PHYSICAL_SINGLE_CORE_H
#define _ENV_PHYSICAL_SINGLE_CORE_H

#define TESTNUM gp

#define RVTEST_RV32U
#define RVTEST_RV32S
#define RVTEST_RV32M
#define RVTEST_RV64U
#define RVTEST_RV64S
#define RVTEST_RV64M

#define RVTEST_CODE_BEGIN                                               \
  .section .text;                                                       \
  .globl main;                                                          \
main:

#define RVTEST_CODE_END

#define RVTEST_PASS                                                     \
        li a0, 0;                                                       \
        ebreak;

#define RVTEST_FAIL                                                     \
        li a0, 1;                                                       \
        ebreak;

#define TEST_PASSFAIL                                                   \
        j pass;                                                         \
fail:                                                                   \
        RVTEST_FAIL;                                                    \
pass:                                                                   \
        RVTEST_PASS;

#define RVTEST_DATA_BEGIN .section .data; .align 4;
#define RVTEST_DATA_END

#endif
