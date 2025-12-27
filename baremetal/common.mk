RISCV_ARCH := rv32ima_zfinx
RISCV_ABI := ilp32
RISCV_MCMODEL := medlow
CFLAGS += -O0 

RISCV_PATH := /opt/riscv32/bin/riscv32-unknown-elf-

RISCV_GCC     := $(abspath $(RISCV_PATH)gcc)
RISCV_AS      := $(abspath $(RISCV_PATH)as)
RISCV_GXX     := $(abspath $(RISCV_PATH)g++)
RISCV_OBJDUMP := $(abspath $(RISCV_PATH)objdump)
RISCV_GDB     := $(abspath $(RISCV_PATH)gdb)
RISCV_AR      := $(abspath $(RISCV_PATH)ar)
RISCV_OBJCOPY := $(abspath $(RISCV_PATH)objcopy)
RISCV_READELF := $(abspath $(RISCV_PATH)readelf)

.PHONY: all

BINARY = $(CURDIR)/$(TARGET).bin

all: $(TARGET)
	cp $(BINARY) $(COMMON_DIR)/memory
	make -C $(COMMON_DIR)/.. run

ASM_SRCS += $(COMMON_DIR)/start.S

C_SRCS += $(wildcard *.c, $(COMMON_DIR)/lib/*.c)

LINKER_SCRIPT := $(COMMON_DIR)/link.lds

INCLUDES += -I$(COMMON_DIR)
INCLUDES += -I$(COMMON_DIR)/include

LDFLAGS += -T $(LINKER_SCRIPT) -nostartfiles -Wl,--gc-sections -Wl,--check-sections

ASM_OBJS := $(ASM_SRCS:.S=.o)
C_OBJS := $(C_SRCS:.c=.o)

LINK_OBJS += $(ASM_OBJS) $(C_OBJS)
LINK_DEPS += $(LINKER_SCRIPT)

CLEAN_OBJS += $(TARGET) $(LINK_OBJS) $(TARGET).dump $(TARGET).bin

CFLAGS += -march=$(RISCV_ARCH)
CFLAGS += -mabi=$(RISCV_ABI)

CFLAGS += -mcmodel=$(RISCV_MCMODEL)  -O2 --specs=nosys.specs  -gdwarf

$(TARGET): $(LINK_OBJS) $(LINK_DEPS) Makefile
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) $(LINK_OBJS) -o $@ $(LDFLAGS)
	$(RISCV_OBJCOPY) -O binary $@ $@.bin
	$(RISCV_OBJDUMP) $@ -S > $@.dump

$(ASM_OBJS): %.o: %.S
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(C_OBJS): %.o: %.c
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(CLEAN_OBJS)
