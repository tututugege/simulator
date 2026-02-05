#pragma once
#include "IO.h"
#include "Dcache_Utils.h"
#include <config.h>

const int HIGH_WATERMARK = 6;
const int LOW_WATERMARK = 2;
const int WRITE_BUFFER_SIZE = 8;

typedef struct{
    bool valid;
    uint32_t addr;
    uint32_t data[DCACHE_OFFSET_NUM];
}WriteBuffer_entry;

class WriteBuffer_IN{
public:
    ExmemDataIO* arbiter2writebuffer_data;
    MshrWbIO* mshr2writebuffer;
};
class WriteBuffer_OUT{
public:
    ExmemControlIO* writebuffer2arbiter_control;
    WbArbiterIO* writebuffer2arbiter;
    WbMshrIO* writebuffer2mshr;
};
class WriteBuffer {
public:
    void init();
    void comb_ready();
    void comb_writemark();
    void comb();
    void seq();

    WriteBuffer_IN in;
    WriteBuffer_OUT out;
    uint32_t head;
    uint32_t tail;
    uint32_t count;

    uint32_t wdone;
    uint32_t wdone_count;

    void print();
};
bool writebuffer_find(uint32_t addr,uint32_t offset, uint32_t& data);
int find_in_writebuffer(uint32_t addr);
void fwd(uint32_t idx, uint32_t data[DCACHE_OFFSET_NUM]);
