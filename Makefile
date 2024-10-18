CXXSRC=./*.cpp
CXXSRC+=./back-end/*.cpp
CXXSRC+=./diff/*.cpp

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./diff/include/

MEM_DIR=./baremetal
IMG=./memory
TEST?=test

all: $(CXXSRC) mem
	g++ $(CXXINCLUDE) $(CXXSRC) -g
	rm -rf $(MEM_DIR)/test/$(TEST)

clean:
	rm -f a.out
	rm -rf ./baremetal/test/*.o
	rm -rf ./baremetal/test/*.dump
	rm -rf ./baremetal/test/*.bin
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

run: 
	./a.out $(MEM_DIR)/memory

gdb:
	gdb --args ./a.out $(IMG)

mem:
	make -C $(MEM_DIR)/test TARGET=$(TEST)
	cd $(MEM_DIR) ; python get_memory.py $(TEST)

.PHONY: all clean mem run
