/* RISC-V Core-Local Interruptor implementation
* see: https://github.com/riscv/riscv-aclint */

#ifndef RV_CLINT_H
#define RV_CLINT_H

#include "rv.h"

typedef struct rv_clint {
  rv *cpu;
  u32 mswi, mtimecmp, mtimecmph;
} rv_clint;

/* initialize the interruptor for a given cpu */
void rv_clint_init(rv_clint *clint, rv *cpu);

#define RV_CLINT_SIZE /* size of memory map */ 0x10000

/* perform a bus access on the interruptor */
rv_res rv_clint_bus(rv_clint *clint, u32 addr, u8 *data, u32 is_store,
                    u32 width);

/* returns 1 if a machine software interrupt is occurring */
u32 rv_clint_msi(rv_clint *clint, u32 context);

/* returns 1 if a machine timer interrupt is occurring */
u32 rv_clint_mti(rv_clint *clint, u32 context);

#endif /* RV_CLINT_H */
