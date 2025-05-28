#pragma once
#include "AXI.h"

enum stat_t {
    INIT,
    WAIT
};

struct ar_buffer_t {
    uint32_t addr;
    uint64_t trans_cycle;
    int arlen;
    int len;
    int id;
};

struct aw_buffer_t {
    uint32_t addr;
    uint64_t bvld_cycle;
    uint8_t  data[16];
    int id;
};

class Memory {
    public:
    union axi_ar_channel   *axi_ar;
    union axi_r_channel    *axi_r;
    union axi_aw_channel   *axi_aw;
    union axi_b_channel    *axi_b;

    uint8_t  mem[100000];
    uint64_t cycle;

    stat_t ar_stat;
    uint64_t arready_cycle;
    struct ar_buffer_t ar_buf[5]; 
    int ar_head;
    int ar_tail;

    stat_t aw_stat;
    uint64_t awready_cycle;
    struct aw_buffer_t aw_buf[5];
    int aw_head;
    int aw_tail;

    public:
    void ar_func();
    void r_func();
    void aw_func();
    void b_func();
    void seq();
};