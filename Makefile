CXXSRC=./*.cpp
CXXSRC+=./back-end/*.cpp
CXXSRC+=./diff/*.cpp

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./diff/include/

MEM_DIR=./baremetal
IMG=./baremetal/memory

RESULT = .result
$(shell > $(RESULT))

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -g

clean:
	rm -f a.out
	rm -rf ./baremetal/test/*.o
	rm -rf ./baremetal/test/*.dump
	rm -rf ./baremetal/test/*.bin
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

gdb:
	gdb --args ./a.out $(IMG)

COLOR_RED   = \033[1;31m
COLOR_GREEN = \033[1;32m
COLOR_NONE  = \033[0m

ALL ?= $(basename $(notdir $(shell find baremetal/test/ -name "*.c")))

all: $(addprefix Makefile., $(ALL))
	@echo "test list [$(words $(ALL)) item(s)]:" $(ALL)

$(ALL): %: Makefile.%

Makefile.%: baremetal/test/%.c default 
	@if make -C $(MEM_DIR)/test TARGET=$* ; \
	cd $(MEM_DIR) ; python get_memory.py $* ;\
	cd ../ ; ./a.out $(IMG); then \
		printf "[%14s] $(COLOR_GREEN)PASS$(COLOR_NONE)\n" $* >> $(RESULT); \
	else \
		printf "[%14s] $(COLOR_RED)***FAIL***$(COLOR_NONE)\n" $* >> $(RESULT); \
	fi
	-@rm -f Makefile.$*

run: all
	@cat $(RESULT)
	@rm $(RESULT)

.PHONY: all clean mem run

