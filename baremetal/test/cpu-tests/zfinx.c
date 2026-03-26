#include "trap.h"
#include <stdint.h>

uint32_t fadd_zfinx(uint32_t a, uint32_t b) {
    uint32_t res;
    asm volatile (
        ".insn r 0x53, 0, 0, %0, %1, %2"
        : "=r"(res)
        : "r"(a), "r"(b)
    );
    return res;
}

uint32_t fmul_zfinx(uint32_t a, uint32_t b) {
    uint32_t res;
    asm volatile (
        ".insn r 0x53, 0, 8, %0, %1, %2"
        : "=r"(res)
        : "r"(a), "r"(b)
    );
    return res;
}

uint32_t fsub_zfinx(uint32_t a, uint32_t b) {
    uint32_t res;
    asm volatile (
        ".insn r 0x53, 0, 4, %0, %1, %2"
        : "=r"(res)
        : "r"(a), "r"(b)
    );
    return res;
}

int main() {
    uint32_t a = 0x3f800000; // 1.0
    uint32_t b = 0x40000000; // 2.0
    
    uint32_t sum = fadd_zfinx(a, b);
    uint32_t prod = fmul_zfinx(a, b);
    uint32_t diff = fsub_zfinx(sum, a);
    
    // 1.0 + 2.0 = 3.0 (0x40400000)
    // 1.0 * 2.0 = 2.0 (0x40000000)
    // 3.0 - 1.0 = 2.0 (0x40000000)
    
    check(sum == 0x40400000);
    check(prod == 0x40000000);
    check(diff == 0x40000000);
    
    return 0;
}
