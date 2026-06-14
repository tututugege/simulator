COMMON_DIR = ..
TARGET = dma_test
RISCV_ARCH := rv32im_zicsr
C_SRCS = $(shell find -name "*.c")
ASM_SRCS += $(shell find -name "*.S")
CFLAGS := -I./ -I../include/ -std=gnu99

include $(COMMON_DIR)/common.mk
