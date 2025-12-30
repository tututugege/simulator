CXXSRC = $(shell find ./back-end/ -name "*.cpp")
CXXSRC += $(shell find ./front-end/ -name "*.cpp")
CXXSRC += $(shell find ./diff/ -name "*.cpp")
CXXSRC += $(shell find ./mmu/ -name "*.cpp")
CXXSRC += $(shell find ./memory/ -name "*.cpp")
CXXSRC += ./main.cpp
CXXSRC += ./rv_simu_mmu_v2.cpp # cpp file with main function
CXXSRC += ./softfloat.a

CXXINCLUDE = -I./include/
CXXINCLUDE += -I./back-end/include/
CXXINCLUDE += -I./back-end/EXU/include/
CXXINCLUDE += -I./back-end/tools/include/
CXXINCLUDE += -I./diff/include/
CXXINCLUDE += -I./front-end/
CXXINCLUDE += -I./mmu/include/
CXXINCLUDE += -I./memory/include/

MEM_DIR=./baremetal
IMG=./baremetal/memory

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -O3 -march=native -funroll-loops -mtune=native -lz

cov: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -O0 --coverage 

run: 
	./a.out $(IMG)

clean:
	rm -f a.out
	rm -rf ./baremetal/memory
	rm -rf ./baremetal/test.code

gdb:
	g++ $(CXXINCLUDE) $(CXXSRC) -O0 -g -march=native -lz
	gdb --args ./a.out $(IMG)

gdb_linux:
	g++ $(CXXINCLUDE) $(CXXSRC) -g
	gdb --args ./a.out ./baremetal/linux.bin
.PHONY: all clean mem run linux gdb gdb_linux

