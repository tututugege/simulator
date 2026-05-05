# ==========================================
# Optimized Simulator Makefile
# ==========================================

BUILD_DIR := build
SIM_EXE   := $(BUILD_DIR)/simulator
IMG     := ./baremetal/memory
AXI_KIT_DIR := ./axi-interconnect-kit
PROFILE ?= default
PROFILE_FRONT_SRC = ./front-end/config/frontend_feature_config.h.$(PROFILE)
PROFILE_INCLUDE_SRC = ./include/config.h.$(PROFILE)
PROFILE_FRONT_DST = ./front-end/config/frontend_feature_config.h
PROFILE_INCLUDE_DST = ./include/config.h

# Compiler & Flags
CXX      ?= g++
CXXFLAGS := -O3 -march=native -funroll-loops -mtune=native
CXXFLAGS += -MMD -MP 
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter
CXXFLAGS += --std=c++2a
CXXFLAGS += $(EXTRA_CXXFLAGS)
ZLIB_CFLAGS := $(shell pkg-config --silence-errors --cflags zlib)
ZLIB_LIBDIR := $(shell pkg-config --silence-errors --variable=libdir zlib)
CXXFLAGS += $(ZLIB_CFLAGS)

# Libraries
LIBS := ./libs/softfloat.a
LDFLAGS := -lz -lstdc++fs
ifneq ($(ZLIB_LIBDIR),)
LDFLAGS := -L$(ZLIB_LIBDIR) $(LDFLAGS)
endif

# Debug Flags (Use 'make DEBUG=1' to enable)
DEBUG_ENABLED := 0
ifneq ($(filter 1 true TRUE yes YES on ON,$(DEBUG)),)
DEBUG_ENABLED := 1
endif

ifeq ($(DEBUG_ENABLED),1)
    CXXFLAGS += -g -O0
else
    CXXFLAGS += -DNDEBUG
endif

# Front-end source root
FRONT_DIR := ./front-end

INCLUDES := -I./include/ \
            -I./libs/include/ \
            -I./back-end/include/ \
            -I./back-end/Exu/include/ \
            -I./back-end/Lsu/include/ \
            -I./back-end/tools/include/ \
            -I./MemSubSystem/include/ \
            -I./diff/include/ \
            -I./legacy/mmu/include/ \
            -I$(FRONT_DIR)/

AXI_KIT_IO_HDR := $(AXI_KIT_DIR)/axi_interconnect/include/AXI_Interconnect_IO.h
ifneq ($(wildcard $(AXI_KIT_IO_HDR)),)
INCLUDES += -I$(AXI_KIT_DIR)/include/ \
            -I$(AXI_KIT_DIR)/axi_interconnect/include/ \
            -I$(AXI_KIT_DIR)/sim_ddr/include/ \
            -I$(AXI_KIT_DIR)/mmio/include/
AXI_KIT_SRC := $(shell find $(AXI_KIT_DIR)/axi_interconnect -name "*.cpp" ! -name "*_test.cpp") \
               $(shell find $(AXI_KIT_DIR)/sim_ddr -name "*.cpp" ! -name "*_test.cpp") \
               $(shell find $(AXI_KIT_DIR)/mmio -name "*.cpp" ! -name "*_test.cpp")
endif

# Source Files
# (Using find to locate all cpp files)
CXXSRC := $(shell find ./back-end -name "*.cpp") \
          ./MemSubSystem/MemSubsystem.cpp \
          ./MemSubSystem/PtwWalker.cpp \
          ./MemSubSystem/PeripheralAxi.cpp \
          ./MemSubSystem/RealDcache.cpp \
          ./MemSubSystem/MSHR.cpp \
          ./MemSubSystem/WriteBuffer.cpp \
          ./MemSubSystem/DcacheConfig.cpp \
          ./MemSubSystem/MemRouteBlock.cpp\
          $(shell find $(FRONT_DIR) -name "*.cpp") \
          $(shell find ./diff -name "*.cpp") \
          $(AXI_KIT_SRC) \
          ./main.cpp \
          ./rv_simu_mmu_v2.cpp

# Object Files
OBJS := $(CXXSRC:%.cpp=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ==========================================
# Rules
# ==========================================

.PHONY: all clean run gdb coverage help gdb_linux linux profile-config default small medium large

all: $(SIM_EXE)

default: PROFILE := default
default: all

small: PROFILE := small
small: all

medium: PROFILE := medium
medium: all

large: PROFILE := large
large: all

# Link
$(SIM_EXE): profile-config $(OBJS) $(LIBS)
	@echo "Linking $@"
	@$(CXX) $(OBJS) $(LIBS) $(LDFLAGS) -o $@ $(CXXFLAGS)

# Compile
$(BUILD_DIR)/%.o: %.cpp $(PROFILE_FRONT_DST) $(PROFILE_INCLUDE_DST) | profile-config
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

profile-config:
	@if [ ! -f $(PROFILE_FRONT_SRC) ]; then echo "Missing profile file: $(PROFILE_FRONT_SRC)"; exit 1; fi
	@if [ ! -f $(PROFILE_INCLUDE_SRC) ]; then echo "Missing profile file: $(PROFILE_INCLUDE_SRC)"; exit 1; fi
	@if [ ! -f $(PROFILE_FRONT_DST) ] || ! cmp -s $(PROFILE_FRONT_SRC) $(PROFILE_FRONT_DST); then cp $(PROFILE_FRONT_SRC) $(PROFILE_FRONT_DST); fi
	@if [ ! -f $(PROFILE_INCLUDE_DST) ] || ! cmp -s $(PROFILE_INCLUDE_SRC) $(PROFILE_INCLUDE_DST); then cp $(PROFILE_INCLUDE_SRC) $(PROFILE_INCLUDE_DST); fi

# Run
run: $(SIM_EXE)
	./$(SIM_EXE) $(IMG)

linux: $(SIM_EXE)
	./$(SIM_EXE) ./baremetal/linux.bin  

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
	@echo "  make          - Build the simulator with default profile"
	@echo "  make default  - Build with default profile"
	@echo "  make large    - Build with large profile"
	@echo "  make medium   - Build with medium profile"
	@echo "  make small    - Build with small profile"
	@echo "  make run      - Build and run the simulator"
	@echo "  make DEBUG=1  - Build with debug symbols"
	@echo "  make clean    - Clean build files"
	@echo "  make gdb_linux - Debug linux"

# Include dependencies
-include $(DEPS)
