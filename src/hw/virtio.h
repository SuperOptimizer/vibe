#pragma once

#define VIRTIO_MAGIC 0x74726976
#define VIRTIO_VERSION 0x2
#define VIRTIO_VENDOR_ID 0x554D4551  

#define VIRTIO_BLK_ID 2

#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED       128

#define VIRTIO_F_VERSION_1  32
#define VIRTIO_F_RING_INDIRECT_DESC 28
#define VIRTIO_F_RING_EVENT_IDX 29

#define VIRTIO_MMIO_MAGIC_VALUE      0x000
#define VIRTIO_MMIO_VERSION          0x004
#define VIRTIO_MMIO_DEVICE_ID        0x008
#define VIRTIO_MMIO_VENDOR_ID        0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL        0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034
#define VIRTIO_MMIO_QUEUE_NUM        0x038
#define VIRTIO_MMIO_QUEUE_READY      0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064
#define VIRTIO_MMIO_STATUS           0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW  0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH  0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc
#define VIRTIO_MMIO_CONFIG           0x100

#define VIRTIO_ISR_QUEUE_INT 1
#define VIRTIO_ISR_CONFIG_INT 2

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 4

struct virtq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

struct virtq_avail {
    u16 flags;
    u16 idx;
    u16 ring[];
};

struct virtq_used_elem {
    u32 id;
    u32 len;
};

struct virtq_used {
    u16 flags;
    u16 idx;
    struct virtq_used_elem ring[];
};

struct virtqueue {
    u32 num;
    u32 num_max;
    bool ready;
    
    u64 desc_addr;
    u64 avail_addr;
    u64 used_addr;
    
    u16 last_avail_idx;
    u16 used_idx;
};

typedef struct hw_virtio {
    mach *mach;
    u32 device_id;
    u32 vendor_id;
    u64 device_features;
    u64 driver_features;
    u32 device_features_sel;
    u32 driver_features_sel;
    u32 queue_sel;
    u32 interrupt_status;
    u8 status;
    u32 config_generation;
    
    struct virtqueue queues[8];
    
    void (*queue_notify)(struct hw_virtio *vio, u32 queue);
    u32 (*get_config)(struct hw_virtio *vio, u32 offset);
    void (*set_config)(struct hw_virtio *vio, u32 offset, u32 value);
    void (*reset)(struct hw_virtio *vio);
    
    void *device_data;
} hw_virtio;

void hw_virtio_init(hw_virtio *vio, mach *m, u32 device_id);
bus_error hw_virtio_mmio_bus(hw_virtio *vio, u32 addr, u8 *data, bool is_store, u32 width);
void hw_virtio_raise_interrupt(hw_virtio *vio, u32 flags);
void hw_virtio_queue_notify_handler(hw_virtio *vio, u32 queue);