#include "vibe.h"
#include <stdlib.h>
#include <time.h>

static void virtio_rng_reset(hw_virtio *vio) {
}

static u32 virtio_rng_get_config(hw_virtio *vio, u32 offset) {
    // No device-specific configuration
    return 0;
}

static void virtio_rng_set_config(hw_virtio *vio, u32 offset, u32 value) {
    // No device-specific configuration
}

static void virtio_rng_process_request(hw_virtio_rng *rng, struct virtqueue *q) {
    struct virtq_desc desc;
    struct virtq_avail avail_header;
    
    u64 avail_addr = q->avail_addr;
    if (mach_bus(rng->vio.mach, avail_addr, (u8*)&avail_header, false, 4) != BUS_OK)
        return;
    
    u16 avail_flags, avail_idx;
    rv_endcvt((u8*)&avail_header, (u8*)&avail_flags, 2, 0);
    rv_endcvt((u8*)&avail_header + 2, (u8*)&avail_idx, 2, 0);
    
    while (q->last_avail_idx != avail_idx) {
        u16 desc_idx;
        u64 idx_addr = avail_addr + 4 + (q->last_avail_idx % q->num) * 2;
        if (mach_bus(rng->vio.mach, idx_addr, (u8*)&desc_idx, false, 2) != BUS_OK)
            return;
        rv_endcvt((u8*)&desc_idx, (u8*)&desc_idx, 2, 0);
        
        u16 first_desc_idx = desc_idx;
        u32 total_written = 0;
        
        // Process descriptor chain
        while (1) {
            u64 desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
            if (mach_bus(rng->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
                break;
            
            u64 addr;
            u32 len;
            u16 flags;
            rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
            rv_endcvt((u8*)&desc.len, (u8*)&len, 4, 0);
            rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
            
            if (flags & VIRTQ_DESC_F_WRITE) {
                // Generate random data
                u8 *buffer = malloc(len);
                if (buffer) {
                    for (u32 i = 0; i < len; i++) {
                        buffer[i] = rand() & 0xFF;
                    }
                    mach_bus(rng->vio.mach, addr, buffer, true, len);
                    free(buffer);
                    total_written += len;
                }
            }
            
            if (!(flags & VIRTQ_DESC_F_NEXT))
                break;
            
            rv_endcvt((u8*)&desc.next, (u8*)&desc_idx, 2, 0);
        }
        
        // Add to used ring
        struct virtq_used_elem used_elem;
        used_elem.id = first_desc_idx;
        used_elem.len = total_written;
        
        u32 id_le, len_le;
        rv_endcvt((u8*)&used_elem.id, (u8*)&id_le, 4, 1);
        rv_endcvt((u8*)&used_elem.len, (u8*)&len_le, 4, 1);
        
        u64 used_elem_addr = q->used_addr + 4 + (q->used_idx % q->num) * sizeof(struct virtq_used_elem);
        mach_bus(rng->vio.mach, used_elem_addr, (u8*)&id_le, true, 4);
        mach_bus(rng->vio.mach, used_elem_addr + 4, (u8*)&len_le, true, 4);
        
        q->used_idx++;
        u16 used_idx_le;
        rv_endcvt((u8*)&q->used_idx, (u8*)&used_idx_le, 2, 1);
        mach_bus(rng->vio.mach, q->used_addr + 2, (u8*)&used_idx_le, true, 2);
        
        q->last_avail_idx++;
    }
    
    hw_virtio_raise_interrupt(&rng->vio, VIRTIO_ISR_QUEUE_INT);
}

static void virtio_rng_queue_notify(hw_virtio *vio, u32 queue) {
    hw_virtio_rng *rng = (hw_virtio_rng *)vio;
    
    if (queue == 0 && vio->queues[0].ready) {
        virtio_rng_process_request(rng, &vio->queues[0]);
    }
}

void hw_virtio_rng_init(hw_virtio_rng *rng, mach *m) {
    memset(rng, 0, sizeof(*rng));
    hw_virtio_init(&rng->vio, m, 4); // VIRTIO_RNG_ID = 4
    
    rng->vio.irq_num = 4; // IRQ 4 for virtio-rng
    
    rng->vio.reset = virtio_rng_reset;
    rng->vio.get_config = virtio_rng_get_config;
    rng->vio.set_config = virtio_rng_set_config;
    rng->vio.queue_notify = virtio_rng_queue_notify;
    
    // Initialize random seed
    srand(time(NULL));
    
    printf("virtio-rng: initialized\n");
}

void hw_virtio_rng_destroy(hw_virtio_rng *rng) {
}