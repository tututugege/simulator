#include "IO.h"
#include <config.h>
#include <cstdint>

const int MSHR_ENTRY_SIZE = 8;
const int MSHR_TABLE_SIZE = 16;

typedef struct{
    bool valid;
    bool issued;
    uint32_t index;
    uint32_t tag;
    uint32_t count;
}mshr_entry;

typedef struct{
    bool valid;
    uint32_t entry;
    uint32_t type;
    uint32_t offset;
    uint32_t reg;
    
    Inst_uop uop;
}table_entry;

enum MSHR_STATE{
    MSHR_IDLE,
    MSHR_DEAL,
    MSHR_TRAN,
    MSHR_WB
};


class MSHR_IN {
public:
    Dcache_MSHR* dcache2mshr_ld;
    Dcache_MSHR* dcache2mshr_st;    
    Dcache_CONTROL* control;
    EXMem_DATA* arbiter2mshr_data;
};
class MSHR_OUT {
public:
    Mem_RESP* mshr2cpu_resp;
    Mem_READY* mshr2dcache_ready;
    EXMem_CONTROL* mshr2arbiter_control;
};

class MSHR {
public:
    void init();
    void comb_out();
    void comb();
    void seq();

    uint32_t mshr_head;
    uint32_t mshr_tail;
    uint32_t table_head;
    uint32_t table_tail;
    uint32_t count_mshr;
    uint32_t count_table;

    MSHR_IN in;
    MSHR_OUT out;
};