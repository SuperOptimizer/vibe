#pragma once

#include "virtio.h"
#include <stdlib.h>

#define VIRTIO_RNG_F_RATE_LIMIT (1 << 0)

typedef struct virtio_rng {
  virtio_mmio mmio;
  void *mach;
  u32 irq;
  u8 random_buffer[256];
} virtio_rng;

void virtio_rng_init(virtio_rng *rng, void *mach, u32 irq);
void virtio_rng_deinit(virtio_rng *rng);
rv_res virtio_rng_bus(virtio_rng *rng, u32 addr, u8 *data, u32 store, u32 width);
void virtio_rng_update(virtio_rng *rng);