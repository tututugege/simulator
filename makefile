CXXSRC=./*.cpp
CXXSRC+=./back-end/*.cpp
CXXSRC+=./diff/*.cpp

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./diff/include/

MEM_DIR=../file/baremetal
IMG=./memory

all: $(CXXSRC) mem
	g++ $(CXXINCLUDE) $(CXXSRC) -g

clean:
	rm a.out

run: 
	./a.out $(MEM_DIR)/memory

gdb:
	gdb --args ./a.out $(IMG)

mem:
	make -C $(MEM_DIR)/test
	cd $(MEM_DIR) ; python get_memory.py
	cp $(MEM_DIR)/memory $(IMG)


.PHONY: all clean mem run
