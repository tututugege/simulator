TARGET ?= test

COLOR_RED   = \033[1;31m
COLOR_GREEN = \033[1;32m
COLOR_NONE  = \033[0m

RESULT = .result
$(shell > $(RESULT))

ALL ?= $(basename $(notdir $(shell find cpu-tests -name "*.c")))

all: $(addprefix Makefile., $(ALL))
	@echo "test list [$(words $(ALL)) item(s)]:" $(ALL)

$(ALL): %: Makefile.%

Makefile.%: cpu-tests/%.c
	/bin/echo -e "COMMON_DIR = ..\nTARGET = $*\nC_SRCS = $<\ninclude ../common.mk" > $@
	if make -s -f $@; then \
		printf "[%14s] $(COLOR_GREEN)PASS$(COLOR_NONE)\n" $* >> $(RESULT); \
	else \
		printf "[%14s] $(COLOR_RED)***FAIL***$(COLOR_NONE)\n" $* >> $(RESULT); \
	fi
	# rm -f $* $*.bin
	rm -f Makefile.$*

run: all
	@cat $(RESULT)
	@rm $(RESULT)

gdb:
	make -C ../.. gdb

clean:
	rm -rf *.dump
	rm -rf ./cpu-tests/*.o
