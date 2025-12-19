#include "IO.h"
#include "Dcache_Util.h"
#include <config.h>

const int WRITE_BUFFER_SIZE = 8;

typedef struct{
    bool valid;
    uint32_t addr;
    uint32_t data[DCACHE_OFFSET_NUM];
}WriteBuffer_entry;

class WriteBuffer_IN{
public:
    EXMem_DATA* arbiter2writebuffer_data;
    CACHE_WB* dcache2writebuffer;
};
class WriteBuffer_OUT{
public:
    WB_CACHE* writebuffer2dcache;
    WB_Arbiter* writebuffer2arbiter;
    EXMem_CONTROL* writebuffer2arbiter_control;
};
class WriteBuffer {
public:
    void init();
    void comb_ready();
    void comb();
    void seq();

    WriteBuffer_IN* in;
    WriteBuffer_OUT* out;
    uint32_t head;
    uint32_t tail;
    uint32_t count;

    uint32_t wdone;
    uint32_t wdone_count;
    WRITE_BUFFER_STATE state;
};