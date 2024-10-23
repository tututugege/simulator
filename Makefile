CXXSRC=./*.cpp
CXXSRC+=./back-end/*.cpp
CXXSRC+=./diff/*.cpp

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./diff/include/

MEM_DIR=./baremetal
IMG=./baremetal/memory

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -g
	./a.out $(IMG)

clean:
	rm -f a.out
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

gdb:
	gdb --args ./a.out $(IMG)

.PHONY: all clean mem run

