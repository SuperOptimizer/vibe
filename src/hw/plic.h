#pragma once

#define RV_PLIC_SIZE  0x4000000
#define RV_PLIC_NSRC 32
#define RV_PLIC_NCTX 1

struct rv_plic {
  mach* mach;
  u32 priority[RV_PLIC_NSRC];
  u32 pending[RV_PLIC_NSRC / 32];
  u32 enable[RV_PLIC_NSRC / 32 * RV_PLIC_NCTX];
  u32 thresh[RV_PLIC_NCTX];
  u32 claim[RV_PLIC_NCTX];
  u32 claiming[RV_PLIC_NSRC / 32]; /* interrupts with claiming in progress */
};

void rv_plic_init(rv_plic *plic);
rv_res rv_plic_bus(rv_plic *plic, u32 addr, u8 *data, bool is_store, u32 width);
rv_res rv_plic_irq(rv_plic *plic, u32 source);
bool rv_plic_mei(rv_plic *plic, u32 context);
