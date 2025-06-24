#pragma once

typedef struct hw_virtio_rng {
    hw_virtio vio;
} hw_virtio_rng;

void hw_virtio_rng_init(hw_virtio_rng *rng, mach *m);
void hw_virtio_rng_destroy(hw_virtio_rng *rng);