#pragma once

#define hw_plic_SIZE  0x4000000
#define hw_plic_NSRC 32
#define hw_plic_NCTX 1

struct hw_plic {
  mach* mach;
  u32 priority[hw_plic_NSRC];
  u32 pending[hw_plic_NSRC / 32];
  u32 enable[hw_plic_NSRC / 32 * hw_plic_NCTX];
  u32 thresh[hw_plic_NCTX];
  u32 claim[hw_plic_NCTX];
  u32 claiming[hw_plic_NSRC / 32]; /* interrupts with claiming in progress */
};

void hw_plic_init(hw_plic *plic);
bus_error hw_plic_bus(hw_plic *plic, u32 addr, u8 *data, bool is_store, u32 width);
rv_res hw_plic_irq(hw_plic *plic, u32 source);
bool hw_plic_mei(hw_plic *plic, u32 context);
