#pragma once

#include "AbstractMmu.h"
#include "config.h"

class SimContext; // Forward declaration
class AbstractLsu;

class SimpleMmu : public AbstractMmu {
private:
    SimContext *ctx;
    AbstractLsu *lsu = nullptr;

public:
    SimpleMmu(SimContext *ctx, AbstractLsu *lsu = nullptr);

    Result translate(uint32_t &p_addr, uint32_t v_addr, uint32_t type,
                     CsrStatusIO *status) override;
};
