#ifndef AM_H__
#define AM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Memory area for [@start, @end)
typedef struct {
  void *start, *end;
} Area;

#define NR_REGS 32

struct Context {
  uintptr_t gpr[NR_REGS], mcause, mstatus, mepc;
  void *pdir;
};

#define ECALL_U 11
#define GPR1 gpr[17] // a7

// Arch-dependent processor context
typedef struct Context Context;

// An event of type @event, caused by @cause of pointer @ref
enum event { EVENT_NULL = 0, EVENT_YIELD, EVENT_ERROR };

typedef struct {
  enum event e;
  uintptr_t cause, ref;
  const char *msg;
} Event;

// ----------------------- TRM: Turing Machine -----------------------
void halt(int code) __attribute__((__noreturn__));
void assert(int code);
void panic(char *info);

// ---------- Interrupt Handling and Context Switching ----------
bool cte_init(Context *(*handler)(Event ev, Context *ctx));
void yield(void);
Context *kcontext(Area kstack, void (*entry)(void *), void *arg);

#endif
