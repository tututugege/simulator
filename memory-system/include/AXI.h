#pragma once
#include <cstdint>
struct axi_ar_master {
    uint8_t  arid_out;
    uint32_t araddr_out;
    uint8_t  arlen_out;
    uint8_t  arsize_out;
    bool     arvalid_out;
    bool     arready_in;
};

struct axi_ar_slave {
    uint8_t  arid_in;
    uint32_t araddr_in;
    uint8_t  arlen_in;
    uint8_t  arsize_in;
    bool     arvalid_in;
    bool     arready_out;
};

union axi_ar_channel {
    struct axi_ar_master m;
    struct axi_ar_slave  s;
};

struct axi_r_master {
    uint8_t rid_out;
    uint8_t rdata_out[4];
    bool    rlast_out;
    bool    rvalid_out;
    bool    rready_in;
};

struct axi_r_slave {
    uint8_t rid_in;
    uint8_t rdata_in[4];
    bool    rlast_in;
    bool    rvalid_in;
    bool    rready_out;
};

union axi_r_channel {
    struct axi_r_master m;
    struct axi_r_slave  s;
};

struct axi_aw_master {
    uint8_t  awid_out;
    uint32_t awaddr_out;
    uint8_t  wdata_out[16];
    bool     awvalid_out;
    bool     awready_in;
};

struct axi_aw_slave {
    uint8_t  awid_in;
    uint32_t awaddr_in;
    uint8_t  wdata_in[16];
    bool     awvalid_in;
    bool     awready_out;
};

union axi_aw_channel {
    struct axi_aw_master m;
    struct axi_aw_slave  s; 
};

struct axi_b_master {
    int  bid_out;
    bool bvalid_out;
    bool bready_in;
};

struct axi_b_slave {
    int  bid_in;
    bool bvalid_in;
    bool bready_out;
};

union axi_b_channel {
    struct axi_b_master m;
    struct axi_b_slave  s;
};