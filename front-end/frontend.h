#ifndef FRONTEND_H
#define FRONTEND_H
#include <config.h>

#define RESET_PC 0x00000000
#define PMEM_OFFSET RESET_PC

/* Whether to use true icache model */
#define USE_TRUE_ICACHE

/*#define IO_version*/

// #define RAS_ENABLE  // if not defined, return address is predicted by BTB

// #define IO_GEN_MODE
/*#define MISS_MODE*/
extern int io_gen_cnt;
#endif
