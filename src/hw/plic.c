#include "vibe.h"

void rv_plic_init(rv_plic *plic) { memset(plic, 0, sizeof(*plic)); }

bus_error rv_plic_bus(rv_plic *plic, u32 addr, u8 *d, bool is_store,
                   u32 width) {
  u32 *reg = NULL, wmask = 0 - 1U, data;
  rv_endcvt(d, (u8 *)&data, 4, 0);
  if (addr >= RV_PLIC_SIZE || width != 4)
    return BUS_INVALID;
  else if (addr < RV_PLIC_NSRC * 4) /*R Interrupt Source Priority */
    reg = plic->priority + (addr >> 2), wmask *= !!addr;
  else if (addr >= 0x1000 &&
           addr < 0x1000 + RV_PLIC_NSRC / 8) /*R Interrupt Pending Bits */
    reg = plic->pending + ((addr - 0x1000) >> 2), wmask ^= addr == 0x1000;
  else if (addr >= 0x2000 &&
           addr < 0x2000 + RV_PLIC_NSRC / 8) /*R Interrupt Enable Bits */
    reg = plic->enable + ((addr - 0x2000) >> 2), wmask ^= addr == 0x2000;
  else if (addr >> 12 >= 0x200 && (addr >> 12) < 0x200 + RV_PLIC_NCTX &&
           !(addr & 0xFFF)) /*R Priority Threshold */
    reg = plic->thresh + ((addr >> 12) - 0x200);
  else if (addr >> 12 >= 0x200 && (addr >> 12) < 0x200 + RV_PLIC_NCTX &&
           (addr & 0xFFF) == 4) /*R Interrupt Claim Register */ {
    u32 context = (addr >> 12) - 0x200, en_off = context * RV_PLIC_NSRC / 32;
    reg = plic->claim + context;
    if (!is_store && *reg < RV_PLIC_NSRC) {
      if (plic->pending[*reg / 32] & (1U << *reg % 32))
        plic->claiming[*reg / 32 + en_off] |=
            1U << *reg % 32; /* set claiming bit */
    } else if (is_store && data < RV_PLIC_NSRC) {
      plic->claiming[data / 32 + en_off] &=
          ~(1U << data % 32); /* unset claiming bit */
    }
  }
  if (reg && !is_store)
    data = *reg;
  else if (reg)
    *reg = (*reg & ~wmask) | (data & wmask);
  rv_endcvt((u8 *)&data, d, 4, 0);
  return BUS_OK;
}

rv_res rv_plic_irq(rv_plic *plic, u32 source) {
  if (source > RV_PLIC_NSRC || !source ||
      ((plic->claiming[source / 32] >> (source % 32)) & 1U) ||
      ((plic->pending[source / 32] >> (source % 32)) & 1U))
    return RV_BAD;
  plic->pending[source / 32] |= 1U << source % 32;
  return RV_OK;
}

bool rv_plic_mei(rv_plic *plic, u32 context) {
  u32 i, j, o = 0, h = 0;
  for (i = 0; i < RV_PLIC_NSRC / 32; i++) {
    u32 en_off = i + context * RV_PLIC_NSRC / 32;
    if (!((plic->enable[en_off] & plic->pending[i]) | plic->claiming[i]))
      continue;
    for (j = 0; j < 32; j++) {
      if ((plic->claiming[en_off] >> j) & 1U)
        plic->pending[i] &= ~(1U << j);
      else if (((plic->enable[i] >> j) & 1U) &&
               ((plic->pending[i] >> j) & 1U) &&
               plic->priority[i * 32 + j] >= h &&
               plic->priority[i * 32 + j] >= plic->thresh[context])
        o = i * 32 + j, h = plic->priority[i * 32 + j];
    }
  }
  plic->claim[context] = o;
  return !!o;
}
