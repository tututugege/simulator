#pragma once

#include "AbstractMmu.h"
#include "config.h"
#include "AbstractMmu.h"
#include "config.h"

class SimContext; // Forward declaration

class SimpleMmu : public AbstractMmu {
private:
    SimContext *ctx;
    // Helper access to memory pointer -> Removed to use global one directly
    // uint32_t *p_memory; 

public:
    SimpleMmu(SimContext *ctx);

    bool translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                         CsrStatusIO *status) override;
};
