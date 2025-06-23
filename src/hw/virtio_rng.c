#include "virtio_rng.h"
#include "plic.h"
#include "mach.h"
#include <string.h>
#include <time.h>

static void virtio_rng_fill_buffer(virtio_rng *rng) {
  static int seeded = 0;
  if (!seeded) {
    srand(time(NULL));
    seeded = 1;
  }
  
  for (int i = 0; i < sizeof(rng->random_buffer); i++) {
    rng->random_buffer[i] = rand() & 0xFF;
  }
}

static void virtio_rng_handle_request(virtio_rng *rng) {
  virtqueue *vq = &rng->mmio.queues[0];
  
  if (!vq->ready) return;
  
  u16 avail_idx = *(u16 *)((u8 *)rng->mach + vq->avail_addr + 2);
  
  while (vq->last_avail_idx != avail_idx) {
    u16 desc_idx = *(u16 *)((u8 *)rng->mach + vq->avail_addr + 4 + 
                            (vq->last_avail_idx % vq->num) * 2);
    
    virtq_desc *desc = (virtq_desc *)((u8 *)rng->mach + vq->desc_addr + 
                                       desc_idx * sizeof(virtq_desc));
    
    u32 total_len = 0;
    
    while (1) {
      u32 copy_len = desc->len;
      if (copy_len > sizeof(rng->random_buffer) - total_len) {
        copy_len = sizeof(rng->random_buffer) - total_len;
      }
      
      if (copy_len > 0) {
        virtio_rng_fill_buffer(rng);
        memcpy((u8 *)rng->mach + desc->addr, rng->random_buffer, copy_len);
        total_len += copy_len;
      }
      
      if (!(desc->flags & 1)) break;
      desc = (virtq_desc *)((u8 *)rng->mach + vq->desc_addr + 
                            desc->next * sizeof(virtq_desc));
    }
    
    virtq_used *used = (virtq_used *)((u8 *)rng->mach + vq->used_addr);
    u16 used_idx = used->idx;
    used->ring[used_idx % vq->num].id = desc_idx;
    used->ring[used_idx % vq->num].len = total_len;
    used->idx = used_idx + 1;
    
    vq->last_avail_idx++;
  }
  
  if (vq->last_avail_idx != avail_idx) {
    rng->mmio.interrupt_status |= 1;
    mach *m = (mach *)rng->mach;
    rv_plic_irq(&m->plic0, rng->irq);
  }
}

static void virtio_rng_queue_notify(void *device, u32 queue) {
  virtio_rng *rng = (virtio_rng *)device;
  
  if (queue == 0) {
    virtio_rng_handle_request(rng);
  }
}

void virtio_rng_init(virtio_rng *rng, void *mach, u32 irq) {
  memset(rng, 0, sizeof(*rng));
  
  rng->mach = mach;
  rng->irq = irq;
  
  rng->mmio.magic = VIRTIO_MAGIC;
  rng->mmio.version = VIRTIO_VERSION;
  rng->mmio.device_id = 4;
  rng->mmio.vendor_id = VIRTIO_VENDOR_ID;
  rng->mmio.device_features = VIRTIO_F_VERSION_1;
  rng->mmio.queue_num_max = VIRTIO_QUEUE_SIZE;
  rng->mmio.device = rng;
  rng->mmio.queue_notify_handler = virtio_rng_queue_notify;
}

void virtio_rng_deinit(virtio_rng *rng) {
  (void)rng;
}

rv_res virtio_rng_bus(virtio_rng *rng, u32 addr, u8 *data, u32 store, u32 width) {
  u32 offset = addr & 0xFFF;
  
  if (store) {
    u32 value = 0;
    memcpy(&value, data, width);
    
    switch (offset) {
      case 0x10: rng->mmio.device_features_sel = value; break;
      case 0x14: rng->mmio.driver_features = value; break;
      case 0x18: rng->mmio.driver_features_sel = value; break;
      case 0x30: rng->mmio.queue_sel = value; break;
      case 0x38: rng->mmio.queue_num = value; break;
      case 0x44: rng->mmio.queue_ready = value;
                 if (rng->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   rng->mmio.queues[rng->mmio.queue_sel].ready = value;
                   rng->mmio.queues[rng->mmio.queue_sel].num = rng->mmio.queue_num;
                 }
                 break;
      case 0x50: if (value < VIRTIO_QUEUE_MAX) virtio_rng_queue_notify(rng, value); break;
      case 0x64: rng->mmio.interrupt_ack = value; rng->mmio.interrupt_status &= ~value; break;
      case 0x70: rng->mmio.status = value; break;
      case 0x80: rng->mmio.queue_desc_low = value;
                 if (rng->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   rng->mmio.queues[rng->mmio.queue_sel].desc_addr = value;
                 }
                 break;
      case 0x90: rng->mmio.queue_avail_low = value;
                 if (rng->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   rng->mmio.queues[rng->mmio.queue_sel].avail_addr = value;
                 }
                 break;
      case 0xA0: rng->mmio.queue_used_low = value;
                 if (rng->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   rng->mmio.queues[rng->mmio.queue_sel].used_addr = value;
                 }
                 break;
      default: break;
    }
  } else {
    u32 value = 0;
    
    switch (offset) {
      case 0x00: value = rng->mmio.magic; break;
      case 0x04: value = rng->mmio.version; break;
      case 0x08: value = rng->mmio.device_id; break;
      case 0x0C: value = rng->mmio.vendor_id; break;
      case 0x10: value = rng->mmio.device_features; break;
      case 0x34: value = rng->mmio.queue_num_max; break;
      case 0x44: value = rng->mmio.queue_ready; break;
      case 0x60: value = rng->mmio.interrupt_status; break;
      case 0x70: value = rng->mmio.status; break;
      case 0xFC: value = rng->mmio.config_generation; break;
      default: break;
    }
    
    memcpy(data, &value, width);
  }
  
  return RV_OK;
}

void virtio_rng_update(virtio_rng *rng) {
  (void)rng;
}