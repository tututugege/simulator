CXXSRC=./*.cpp
CXXSRC+=./back-end/*.cpp
CXXSRC+=./diff/*.cpp

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./diff/include/

MEM_DIR=../file/baremetal

all: $(CXXSRC) mem
	g++ $(CXXINCLUDE) $(CXXSRC) -g

clean:
	rm a.out

run: 
	./a.out $(MEM_DIR)/memory

mem:
	make -C $(MEM_DIR)/test
	$(shell cd $(MEM_DIR) && python get_memory.py)


.PHONY: all clean mem run
