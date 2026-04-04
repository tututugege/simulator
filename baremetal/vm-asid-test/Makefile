COMMON_DIR = ..
TARGET = vm_asid_test
RISCV_ARCH := rv32imab
C_SRCS = $(shell find -name "*.c")
ASM_SRCS += ../vm-pagefault-test/vm_helpers.S
CFLAGS := -I./ -I../include/ -std=gnu99

include $(COMMON_DIR)/common.mk
