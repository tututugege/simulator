# Selective Makefile for riscv-tests/isa

# Subdirectories to include
SUBDIRS = rv32ui rv32um rv32ua

# Colors for output
COLOR_RED   = \033[1;31m
COLOR_GREEN = \033[1;32m
COLOR_NONE  = \033[0m

RESULT = .result

# Find all .S files in active subdirectories
ALL_SRCS = $(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.S))
ALL_TESTS = $(basename $(ALL_SRCS))

.PHONY: all list run clean $(ALL_TESTS) $(SUBDIRS)

all: list

list:
	@echo "Available tests [$(words $(ALL_TESTS))]:"
	@echo $(ALL_TESTS) | fmt -w 80

$(SUBDIRS):
	@$(MAKE) $(filter $@/%, $(ALL_TESTS))

run: $(ALL_TESTS)
	@echo ""
	@echo "--- Summary ---"
	@cat $(RESULT)
	@rm -f $(RESULT)

# Rule to run each test
$(ALL_TESTS): %:
	@echo "Running test: $*"
	@mkdir -p $(dir build/$*)
	@if [ "$(findstring rv32uz, $*)" != "" ]; then \
		ARCH_FLAGS="RISCV_ARCH=rv32im_zba_zbb_zbc_zbs_zifencei"; \
	else \
		ARCH_FLAGS="RISCV_ARCH=rv32ima_zifencei"; \
	fi; \
	if $(MAKE) -f ../../common.mk \
		COMMON_DIR=../.. \
		TARGET=$(notdir $*) \
		ASM_SRCS="../../start.S $*.S" \
		BUILD_DIR=build/$* \
		INCLUDES="-I. -Imacros/scalar" \
		$$ARCH_FLAGS \
		build/$*/$(notdir $*) > build/$*.build.log 2>&1 && \
		../../../build/simulator build/$*/$(notdir $*).bin; \
	then \
		printf "[%20s] $(COLOR_GREEN)PASS$(COLOR_NONE)\n" $* >> $(RESULT); \
	else \
		printf "[%20s] $(COLOR_RED)***FAIL***$(COLOR_NONE)\n" $* >> $(RESULT); \
		cat build/$*.run.log; \
	fi

clean:
	rm -rf build $(RESULT)
