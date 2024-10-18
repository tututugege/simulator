
RISCV_ARCH := rv32ia
RISCV_ABI := ilp32
RISCV_MCMODEL := medlow
TARGET ?= test

C_SRCS := $(TARGET).c
CFLAGS := -O2
CFLAGS += -DFLAGS_STR=\""$(CFLAGS)"\"

COMMON_DIR = ..

include ../common.mk

