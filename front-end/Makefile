CC=g++
TARGET_TEST_ENV=test

HOME_DIR = $(CURDIR)

GDB_TARGET_TEST_ENV=test.gdb

SRC_DIR_PRED = BPU/dir_predictor
SRC_TARGET_PRED = BPU/target_predictor
SRC_ICACHE = icache
SRC_FIFO = fifo
SRC_SEQ_COMP = sequential_components

SRC_TEST_ENV = test_env.cpp front_top.cpp BPU/BPU_top.cpp \
               $(wildcard $(SRC_ICACHE)/*.cpp) \
               $(wildcard $(SRC_FIFO)/*.cpp) \
			   $(wildcard $(SRC_TARGET_PRED)/*.cpp) \
			   $(SRC_DIR_PRED)/tage_IO.cpp \
			   $(wildcard $(SRC_SEQ_COMP)/*.cpp) 

.PHONY: all clean build test_env test_env_gdb

build: $(TARGET_TEST_ENV)

all: $(TARGET_TEST_ENV)

$(TARGET_TEST_ENV): $(SRC_TEST_ENV)
	$(CC) -O3 -w -o $@ $(SRC_TEST_ENV)

$(GDB_TARGET_TEST_ENV): $(SRC_TEST_ENV)
	$(CC) -g -w -o $@ $(SRC_TEST_ENV)

testenv: $(TARGET_TEST_ENV)
	./$(TARGET_TEST_ENV) > $(HOME_DIR)/log/test_env_log

testenv_gdb: $(GDB_TARGET_TEST_ENV)
	gdb --args ./$(GDB_TARGET_TEST_ENV)

clean:
	rm -f $(TARGET_TEST_ENV) $(GDB_TARGET_TEST_ENV)
	rm -f $(HOME_DIR)/log/test_env_log

