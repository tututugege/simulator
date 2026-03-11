# ==========================================
# Optimized Simulator Makefile
# ==========================================

BUILD_DIR := build
SIM_EXE   := $(BUILD_DIR)/simulator
IMG     := ./baremetal/memory

# Compiler & Flags
CXX      := g++
CXXFLAGS := -O3 -march=native -funroll-loops -mtune=native
CXXFLAGS += -MMD -MP 
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter
CXXFLAGS += --std=c++2a

# Libraries
LIBS := ./libs/softfloat.a
LDFLAGS := -lz -lstdc++fs

# Debug Flags (Use 'make DEBUG=1' to enable)
ifdef DEBUG
    CXXFLAGS += -g -O0
else
    CXXFLAGS += -DNDEBUG
endif

# Front-end source root
FRONT_DIR := ./front-end

INCLUDES := -I./include/ \
            -I./back-end/include/ \
            -I./back-end/Exu/include/ \
            -I./back-end/Lsu/include/ \
            -I./back-end/tools/include/ \
            -I./MemSubSystem/include/ \
            -I./diff/include/ \
            -I./legacy/mmu/include/ \
            -I$(FRONT_DIR)/ \
            -I./axi-interconnect-kit/include/ \
            -I./axi-interconnect-kit/axi_interconnect/include/ \
            -I./axi-interconnect-kit/sim_ddr/include/ \
            -I./axi-interconnect-kit/mmio/include/

# Source Files
# (Using find to locate all cpp files)
CXXSRC := $(shell find ./back-end -name "*.cpp") \
          ./MemSubSystem/MemSubsystem.cpp \
          ./MemSubSystem/SimpleCache.cpp \
          ./MemSubSystem/PtwWalker.cpp \
          $(shell find $(FRONT_DIR) -name "*.cpp") \
          $(shell find ./diff -name "*.cpp") \
          $(shell find ./axi-interconnect-kit/axi_interconnect -name "*.cpp" ! -name "*_test.cpp") \
          $(shell find ./axi-interconnect-kit/sim_ddr -name "*.cpp" ! -name "*_test.cpp") \
          $(shell find ./axi-interconnect-kit/mmio -name "*.cpp" ! -name "*_test.cpp") \
          ./main.cpp \
          ./rv_simu_mmu_v2.cpp

# Object Files
OBJS := $(CXXSRC:%.cpp=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ==========================================
# Rules
# ==========================================

.PHONY: all clean run gdb coverage help gdb_linux

all: $(SIM_EXE)

# Link
$(SIM_EXE): $(OBJS) $(LIBS)
	@echo "Linking $@"
	@$(CXX) $(OBJS) $(LIBS) $(LDFLAGS) -o $@ $(CXXFLAGS)

# Compile
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Run
run: $(SIM_EXE)
	./$(SIM_EXE) $(IMG)

# Debug with GDB
gdb: CXXFLAGS += -g -O0
gdb: $(SIM_EXE)
	gdb --args ./$(SIM_EXE) $(IMG)

gdb_linux: CXXFLAGS += -g
gdb_linux: $(SIM_EXE)
	gdb --args ./$(SIM_EXE) ./baremetal/linux.bin

# Coverage
coverage: CXXFLAGS += --coverage -O0
coverage: $(SIM_EXE)

# Clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf ./baremetal/memory ./baremetal/test.code
	@rm -f a.out

help:
	@echo "Usage:"
	@echo "  make          - Build the simulator"
	@echo "  make run      - Build and run the simulator"
	@echo "  make DEBUG=1  - Build with debug symbols"
	@echo "  make clean    - Clean build files"
	@echo "  make gdb_linux - Debug linux"

# Include dependencies
-include $(DEPS)
