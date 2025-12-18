#include "IO.h"
#include <config.h>
#include <cstdint>

const int MSHR_ENTRY_SIZE = 8;
const int MSHR_TABLE_SIZE = 16;

typedef struct{
    bool valid;
    bool issued;
    uint32_t addr;
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
    MSHR_WAIT_READ,
    MSHR_TRAN
};


class MSHR_IN {
public:
    Dcache_MSHR* dcache2mshr_ld;
    Dcache_MSHR* dcache2mshr_st;    
    Dcache_CONTROL* control;
};
class MSHR_OUT {
public:
    Mem_RESP* mshr2cpu_resp;
    EXMem_IO* mshr2mem;
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