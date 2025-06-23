#pragma once

#include "../rv.h"

typedef rv_u16 u16;
typedef unsigned long long u64;

#define VIRTIO_MAGIC 0x74726976

#define VIRTIO_VERSION 2
#define VIRTIO_VENDOR_ID 0x1AF4

#define VIRTIO_DEVICE_ID_NET 1
#define VIRTIO_DEVICE_ID_BLOCK 2

#define VIRTIO_F_VERSION_1 (1 << 0)

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED 128

#define VIRTIO_QUEUE_MAX 8
#define VIRTIO_QUEUE_SIZE 256

typedef struct virtq_desc {
  u64 addr;
  u32 len;
  u16 flags;
  u16 next;
} virtq_desc;

typedef struct virtq_avail {
  u16 flags;
  u16 idx;
  u16 ring[VIRTIO_QUEUE_SIZE];
  u16 used_event;
} virtq_avail;

typedef struct virtq_used_elem {
  u32 id;
  u32 len;
} virtq_used_elem;

typedef struct virtq_used {
  u16 flags;
  u16 idx;
  virtq_used_elem ring[VIRTIO_QUEUE_SIZE];
  u16 avail_event;
} virtq_used;

typedef struct virtqueue {
  u32 num;
  u32 desc_addr;
  u32 avail_addr;
  u32 used_addr;
  u16 last_avail_idx;
  u32 ready;
} virtqueue;

typedef struct virtio_mmio {
  u32 magic;
  u32 version;
  u32 device_id;
  u32 vendor_id;
  u32 device_features;
  u32 device_features_sel;
  u32 driver_features;
  u32 driver_features_sel;
  u32 queue_sel;
  u32 queue_num_max;
  u32 queue_num;
  u32 queue_ready;
  u32 queue_notify;
  u32 interrupt_status;
  u32 interrupt_ack;
  u32 status;
  u32 queue_desc_low;
  u32 queue_desc_high;
  u32 queue_avail_low;
  u32 queue_avail_high;
  u32 queue_used_low;
  u32 queue_used_high;
  u32 config_generation;
  virtqueue queues[VIRTIO_QUEUE_MAX];
  void *device;
  void (*device_config)(void *device, u32 offset, u8 *data, u32 len, u32 write);
  void (*queue_notify_handler)(void *device, u32 queue);
} virtio_mmio;