CXXSRC = $(shell find ./back-end/ -name "*.cpp")
CXXSRC += $(shell find ./front-end/ -name "*.cpp")
CXXSRC += $(shell find ./memory-system/ -name "*.cpp")
CXXSRC += $(shell find ./diff/ -name "*.cpp")
CXXSRC += ./rv_simu_mmu.cpp
CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./back-end/EXU/include/
CXXINCLUDE += -I./back-end/tools/include/
CXXINCLUDE += -I./diff/include/
CXXINCLUDE += -I./front-end/
CXXINCLUDE += -I./memory-system/include
CXXINCLUDE += -I./memory-system/arbiter/include
CXXINCLUDE += -I./memory-system/component/include

MEM_DIR=./baremetal
IMG=./baremetal/memory

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -g

run: 
	./a.out $(IMG)

clean:
	rm -f a.out
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

gdb:
	gdb --args ./a.out $(IMG)

.PHONY: all clean mem run

