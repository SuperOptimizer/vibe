#include "vibe.h"


void hw_clint_init(hw_clint *clint, rv *cpu) {
  memset(clint, 0, sizeof(*clint));
  clint->cpu = cpu;
}

bus_error hw_clint_bus(hw_clint *clint, u32 addr, u8 *d, bool is_store, u32 width) {
  u32 *reg, data;
  rv_endcvt(d, (u8 *)&data, 4, 0);
  if (width != 4)
    return BUS_INVALID;
  if (addr == 0x0) /*R mswi */
    reg = &clint->mswi;
  else if (addr == 0x4000 + 0x0000) /*R mtimecmp */
    reg = &clint->mtimecmp;
  else if (addr == 0x4000 + 0x0000 + 4) /*R mtimecmph */
    reg = &clint->mtimecmph;
  else if (addr == 0x4000 + 0x7FF8) /*R mtime */
    reg = &clint->cpu->csr.mtime;
  else if (addr == 0x4000 + 0x7FF8 + 4) /*R mtimeh */
    reg = &clint->cpu->csr.mtimeh;
  else
    return BUS_UNMAPPED;
  if (is_store)
    *reg = data;
  else
    data = *reg;
  rv_endcvt((u8 *)&data, d, 4, 1);
  return BUS_OK;
}

bool hw_clint_msi(hw_clint *clint, u32 context) {
  (void)context; /* unused for now, perhaps add multicore support later */
  return !!(clint->mswi & 1);
}

bool hw_clint_mti(hw_clint *clint, u32 context) {
  (void)context;
  return !!((clint->cpu->csr.mtimeh > clint->mtimecmph) ||
            ((clint->cpu->csr.mtimeh == clint->mtimecmph) &&
             (clint->cpu->csr.mtime >= clint->mtimecmp)));
}
