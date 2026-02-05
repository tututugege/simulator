RISCV_ARCH := rv32ima
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

.PHONY: all clean

BUILD_DIR ?= build
ELF := $(BUILD_DIR)/$(TARGET)
BINARY := $(ELF).bin
MEMORY_BIN := $(COMMON_DIR)/memory

# Sources
ASM_SRCS += $(COMMON_DIR)/start.S
override C_SRCS += $(wildcard *.c) $(wildcard $(COMMON_DIR)/lib/*.c)

LINKER_SCRIPT := $(COMMON_DIR)/link.lds

INCLUDES += -I$(COMMON_DIR)
INCLUDES += -I$(COMMON_DIR)/include

LDFLAGS += -T $(LINKER_SCRIPT) -nostartfiles -Wl,--gc-sections -Wl,--check-sections

# Define objects (sort to remove duplicates)
ASM_OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SRCS:.S=.o))))
C_OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(notdir $(C_SRCS:.c=.o))))

# Handle source paths via VPATH
SRC_DIRS := $(sort $(dir $(ASM_SRCS) $(C_SRCS)))
vpath %.S $(SRC_DIRS)
vpath %.c $(SRC_DIRS)

LINK_OBJS += $(ASM_OBJS) $(C_OBJS)
LINK_DEPS += $(LINKER_SCRIPT)

# Objects to clean (everything in build dir)
CLEAN_DIRS += $(BUILD_DIR)

CFLAGS += -march=$(RISCV_ARCH)
CFLAGS += -mabi=$(RISCV_ABI)
CFLAGS += -mcmodel=$(RISCV_MCMODEL)  -O2 --specs=nosys.specs  -gdwarf

all: $(ELF)
	cp $(BINARY) $(MEMORY_BIN)
	make -C $(COMMON_DIR)/.. run

$(ELF): $(LINK_OBJS) $(LINK_DEPS) Makefile
	@mkdir -p $(dir $@)
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) $(LINK_OBJS) -o $@ $(LDFLAGS)
	$(RISCV_OBJCOPY) -O binary $@ $(BINARY)
	$(RISCV_OBJDUMP) -alDS -M no-aliases $@ > $(ELF).dump

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(RISCV_GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(CLEAN_DIRS)


