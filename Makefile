CXXSRC = $(shell find ./back-end/ -name "*.cpp")
CXXSRC += $(shell find ./front-end/ -name "*.cpp")
CXXSRC += $(shell find ./diff/ -name "*.cpp")
CXXSRC += ./rv_simu_mmu.cpp
CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./back-end/EXU/include/
CXXINCLUDE += -I./back-end/tools/include/
CXXINCLUDE += -I./diff/include/
CXXINCLUDE += -I./front-end/

MEM_DIR=./baremetal
IMG=./baremetal/memory

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -g

run: default
	./a.out $(IMG)

clean:
	rm -f a.out
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

gdb:
	gdb --args ./a.out $(IMG)

.PHONY: all clean mem run

