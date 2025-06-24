#pragma once

#define hw_clint_SIZE  0x10000

struct hw_clint {
  mach* mach;
  rv *cpu;
  u32 mswi, mtimecmp, mtimecmph;
};

void hw_clint_init(hw_clint *clint, rv *cpu);
bus_error hw_clint_bus(hw_clint *clint, u32 addr, u8 *data, bool is_store, u32 width);
bool hw_clint_msi(hw_clint *clint, u32 context);
bool hw_clint_mti(hw_clint *clint, u32 context);
