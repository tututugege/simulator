#include <cstdint>

struct mmu_req_master {
    bool     valid_out;
    uint32_t vtag_out;
    uint8_t  op_out;
};

struct mmu_req_slave {
    bool     valid_in;
    uint32_t vtag_in;
    uint8_t  op_in;
};

union mmu_req_t {
    struct mmu_req_master m;
    struct mmu_req_slave  s;
};

struct mmu_resp_master {
    bool     okay_out;
    bool     excp_out;
    bool     miss_out;
    uint32_t ptag_out;
};

struct mmu_resp_slave {
    bool     okay_in;
    bool     excp_in;
    bool     miss_in;
    uint32_t ptag_in;
};

union mmu_resp_t {
    struct mmu_resp_master m;
    struct mmu_resp_slave  s;
};

class MMU {
    public:
    union mmu_req_t        *mmu_req;
    union mmu_resp_t       *mmu_resp;

    uint32_t vtag;
    uint32_t vtag_next;
    bool valid;
    bool valid_next;

    public:
    void req();
    void resp();
    void seq();
};