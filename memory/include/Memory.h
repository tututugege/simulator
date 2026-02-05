#pragma once
#include "IO.h"
#include <config.h>
#include <memory_config.h>

const int Latency = 2; // 内存延迟10个周期
const int MemorySize = 1<<20; // 1MB内存

class MEMORY_IN
{
public:
    ExmemControlIO* control;
};
class MEMORY_OUT
{
public:
    ExmemDataIO* data;
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

    MEMORY_IN in;
    MEMORY_OUT out;
    reg<32> Latency_cnt;

    MEMORY_STATE state;

    reg<32> data_cnt;
    reg<32> rdata;

    reg<1> donereg;
    void init();
    
};