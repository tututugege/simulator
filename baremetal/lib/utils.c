#include <stdint.h>

#include "../include/utils.h"

typedef unsigned int		u32;
typedef unsigned long		u64;
#define __io_rbr()		do {} while (0)
#define __io_rar()		do {} while (0)

static inline u32 __raw_readl(const volatile void *addr)
{
	u32 val;

	asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
	return val;
}
#define readl_relaxed(c)	({ u32 __v; __io_rbr(); __v = __raw_readl(c); __io_rar(); __v; })
// static float clint_time_rd32(volatile u64 *addr)
// {
// 	u32 lo, hi;

// 	do {
// 		hi = readl_relaxed((u32 *)addr + 1);
// 		lo = readl_relaxed((u32 *)addr);
// 	} while (hi != readl_relaxed((u32 *)addr + 1));
//     // xprintf("lo:%lu\r\n", lo);
//     // xprintf("hi:%lu\r\n", hi);
//     float current_time = (float)lo / (float)100000000.0;
//     current_time += hi * 42.94967295;
//     return current_time;
// 	// return ((u64)hi << 32) | (u64)lo;
// }

// float get_cycle_value()
// {
//     // uint64_t cycle;

//     // cycle = read_csr(cycle);
//     // cycle += (uint64_t)(read_csr(cycleh)) << 32;

//     // return cycle;
//     float cycle;
//     u64 * addr = 0x1fd0e000;
//     cycle = clint_time_rd32(addr);
//     // xprintf("cycle:%d.%04d\r\n", (uint32_t)cycle, (uint32_t)(10000*cycle)%10000);
//     return cycle;
// }

// void busy_wait(uint32_t us)
// {
//     uint64_t tmp;
//     uint32_t count;

//     count = us * CPU_FREQ_MHZ;
//     tmp = get_cycle_value();

//     while (get_cycle_value() < (tmp + count));
// }
