COMMON_DIR = ..
TARGET = vm_pagefault_test
RISCV_ARCH := rv32imab
C_SRCS = $(shell find -name "*.c")
ASM_SRCS += $(shell find -name "*.S")
CFLAGS := -I./ -I../include/ -std=gnu99

include $(COMMON_DIR)/common.mk
