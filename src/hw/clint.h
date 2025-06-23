#pragma once


#define RV_CLINT_SIZE  0x10000

struct rv_clint {
  mach* mach;
  rv *cpu;
  u32 mswi, mtimecmp, mtimecmph;
};

void rv_clint_init(rv_clint *clint, rv *cpu);

rv_res rv_clint_bus(rv_clint *clint, u32 addr, u8 *data, bool is_store, u32 width);

bool rv_clint_msi(rv_clint *clint, u32 context);

bool rv_clint_mti(rv_clint *clint, u32 context);

