TARGET := fence_i_dcache_test
BUILD_DIR ?= build
RISCV32_ELF32_BIN ?= /nfs_global/S/daiyihao/opt/riscv-gnu-toolchain/riscv-elf32/bin
RISCV_AS ?= $(RISCV32_ELF32_BIN)/riscv32-unknown-elf-as
RISCV_LD ?= $(RISCV32_ELF32_BIN)/riscv32-unknown-elf-ld
OBJCOPY ?= $(RISCV32_ELF32_BIN)/riscv32-unknown-elf-objcopy
OBJDUMP ?= $(RISCV32_ELF32_BIN)/riscv32-unknown-elf-objdump

ELF := $(BUILD_DIR)/$(TARGET)
OBJ := $(BUILD_DIR)/$(TARGET).o
BINARY := $(ELF).bin
DUMP := $(ELF).dump

.PHONY: all clean

all: $(BINARY)

$(BUILD_DIR):
	mkdir -p $@

$(OBJ): fence_i_dcache_test.S | $(BUILD_DIR)
	$(RISCV_AS) -march=rv32i -mabi=ilp32 -o $@ $<

$(ELF): $(OBJ)
	$(RISCV_LD) -Ttext=0x80000000 -e _start --no-relax -o $@ $<
	$(OBJDUMP) -d --no-show-raw-insn $@ > $(DUMP)

$(BINARY): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)
