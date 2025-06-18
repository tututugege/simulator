#include "IO.h"
struct rs_trans_req_slave;
struct bcast_res_bus_master;
struct stq_alloc_slave;
class FU;

class Adaptor {
    public:
    Inst_entry *inst_r;
    struct rs_trans_req_slave *rs_req;

    Exe_Prf *exe2prf;
    FU *fu;
    struct bcast_res_bus_master *res_bus;

    Rob_Commit *rob_commit;
    int *retire_num_in;

    Ren_Stq *ren2stq;
    Ren_Ldq *ren2ldq;
    struct stq_alloc_slave *stq_alloc;
    struct ldq_alloc_slave *ldq_alloc;

    Stq_Ren *stq2ren;
    Ldq_Ren *ldq2ren;

    Adaptor();
    void mem_fire_adpt();
    void mem_return_adpt();
    void stq_commit_adpt();
    void lsq_alloc_forepart();
    void lsq_alloc_backpart();
};