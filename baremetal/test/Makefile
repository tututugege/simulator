TARGET ?= test

COLOR_RED   = \033[1;31m
COLOR_GREEN = \033[1;32m
COLOR_NONE  = \033[0m

RESULT = .result
$(shell > $(RESULT))

# Find all test names based on .c files in cpu-tests/
ALL ?= $(sort $(basename $(notdir $(shell find cpu-tests -name "*.c"))))

.PHONY: all list run clean help $(ALL)

# Default target: list available tests
all: help

help:
	@echo "Usage:"
	@echo "  make list        - List all available tests"
	@echo "  make run         - Run all tests"
	@echo "  make <testname>  - Run a specific test (e.g., 'make add')"
	@echo "  make clean       - Clean build artifacts"

list:
	@echo "Available tests [$(words $(ALL))]:"
	@echo $(ALL) | fmt -w 80

# Run all tests
run: $(ALL)
	@cat $(RESULT)
	@if grep -q "\*\*\*FAIL\*\*\*" $(RESULT); then \
		echo "Some tests failed."; \
		rm -f $(RESULT); \
		exit 1; \
	fi
	@rm -f $(RESULT)

# Rule for individual tests
# Invokes common.mk directly without generating a temporary Makefile
$(ALL): %:
	@echo "Running test: $*"
	@if $(MAKE) -s -f ../common.mk \
		COMMON_DIR=.. \
		TARGET=$* \
		C_SRCS=cpu-tests/$*.c; then \
		printf "[%14s] $(COLOR_GREEN)PASS$(COLOR_NONE)\n" $* >> $(RESULT); \
	else \
		printf "[%14s] $(COLOR_RED)***FAIL***$(COLOR_NONE)\n" $* >> $(RESULT); \
	fi
	@ echo "Finished test: $*"

clean:
	rm -rf build $(RESULT)
