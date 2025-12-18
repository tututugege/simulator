#pragma once

#include "IO.h"
#include <config.h>
#include <memory_config.h>

const int Latency = 2; // 内存延迟10个周期
const int MemorySize = 1<<20; // 1MB内存

class MEMORY_IO
{
public:
    EXMem_IO* mem;
};

enum MEMORY_STATE{
    MEM_IDLE,
    LATENCY,
    TRANSFER
};

class MEMORY
{
public:
    void comb();
    void seq();

    MEMORY_IO io;
    reg32_t Latency_cnt;

    MEMORY_STATE state;

    reg32_t data_cnt;
    reg32_t rdata;

    reg1_t donereg;
    void init();
};