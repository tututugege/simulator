#ifndef FRONTEND_H
#define FRONTEND_H
#include "config.h"
#include "config/frontend_diag_config.h"
#include "config/frontend_feature_config.h"

// 是否开启2ahead
// #define ENABLE_2AHEAD

#ifndef RESET_PC
#define RESET_PC 0x00000000
#endif
#ifndef PMEM_OFFSET
#define PMEM_OFFSET RESET_PC
#endif

/* ICache model selection:
 * - default: True ICache
 * - define USE_IDEAL_ICACHE for ideal model (performance upper-bound)
 */
// #define USE_IDEAL_ICACHE

/*#define IO_version*/

// #define RAS_ENABLE  // if not defined, return address is predicted by BTB

// #define IO_GEN_MODE
/*#define MISS_MODE*/
extern int io_gen_cnt;
#endif
