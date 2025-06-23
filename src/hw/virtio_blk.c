#include "virtio_blk.h"
#include "plic.h"
#include "mach.h"
#include <string.h>
#include <unistd.h>

static void virtio_blk_config_handler(void *device, u32 offset, u8 *data, u32 len, u32 write) {
  virtio_blk *blk = (virtio_blk *)device;
  
  if (offset + len > sizeof(virtio_blk_config)) {
    return;
  }
  
  if (write) {
    memcpy((u8 *)&blk->config + offset, data, len);
  } else {
    memcpy(data, (u8 *)&blk->config + offset, len);
  }
}

static void virtio_blk_handle_request(virtio_blk *blk) {
  virtqueue *vq = &blk->mmio.queues[0];
  
  if (!vq->ready) return;
  
  u16 avail_idx = *(u16 *)((u8 *)blk->mach + vq->avail_addr + 2);
  
  while (vq->last_avail_idx != avail_idx) {
    u16 desc_idx = *(u16 *)((u8 *)blk->mach + vq->avail_addr + 4 + 
                            (vq->last_avail_idx % vq->num) * 2);
    
    virtq_desc *desc = (virtq_desc *)((u8 *)blk->mach + vq->desc_addr + 
                                       desc_idx * sizeof(virtq_desc));
    
    virtio_blk_req req;
    memcpy(&req, (u8 *)blk->mach + desc->addr, sizeof(req));
    
    if (!(desc->flags & 1)) {
      printf("virtio-blk: invalid request descriptor\n");
      vq->last_avail_idx++;
      continue;
    }
    
    desc = (virtq_desc *)((u8 *)blk->mach + vq->desc_addr + 
                          desc->next * sizeof(virtq_desc));
    
    u8 status = VIRTIO_BLK_S_OK;
    u32 data_len = 0;
    
    switch (req.type) {
      case VIRTIO_BLK_T_IN: {
        if (blk->disk_file) {
          fseek(blk->disk_file, req.sector * 512, SEEK_SET);
          
          while (desc->flags & 1) {
            u32 len = desc->len;
            if (len > sizeof(blk->io_buffer)) len = sizeof(blk->io_buffer);
            
            size_t read_len = fread(blk->io_buffer, 1, len, blk->disk_file);
            memcpy((u8 *)blk->mach + desc->addr, blk->io_buffer, read_len);
            data_len += read_len;
            
            if (read_len < len) break;
            
            desc = (virtq_desc *)((u8 *)blk->mach + vq->desc_addr + 
                                  desc->next * sizeof(virtq_desc));
          }
        } else {
          status = VIRTIO_BLK_S_IOERR;
        }
        break;
      }
      
      case VIRTIO_BLK_T_OUT: {
        if (blk->disk_file && !(blk->config.writeback & VIRTIO_BLK_F_RO)) {
          fseek(blk->disk_file, req.sector * 512, SEEK_SET);
          
          while (desc->flags & 1) {
            u32 len = desc->len;
            if (len > sizeof(blk->io_buffer)) len = sizeof(blk->io_buffer);
            
            memcpy(blk->io_buffer, (u8 *)blk->mach + desc->addr, len);
            size_t write_len = fwrite(blk->io_buffer, 1, len, blk->disk_file);
            data_len += write_len;
            
            if (write_len < len) {
              status = VIRTIO_BLK_S_IOERR;
              break;
            }
            
            desc = (virtq_desc *)((u8 *)blk->mach + vq->desc_addr + 
                                  desc->next * sizeof(virtq_desc));
          }
        } else {
          status = VIRTIO_BLK_S_IOERR;
        }
        break;
      }
      
      case VIRTIO_BLK_T_FLUSH: {
        if (blk->disk_file) {
          fflush(blk->disk_file);
        }
        break;
      }
      
      case VIRTIO_BLK_T_GET_ID: {
        char id_string[20] = "vibe-virtio-blk";
        u32 len = desc->len;
        if (len > sizeof(id_string)) len = sizeof(id_string);
        memcpy((u8 *)blk->mach + desc->addr, id_string, len);
        data_len = len;
        break;
      }
      
      default:
        status = VIRTIO_BLK_S_UNSUPP;
        break;
    }
    
    memcpy((u8 *)blk->mach + desc->addr, &status, 1);
    
    virtq_used *used = (virtq_used *)((u8 *)blk->mach + vq->used_addr);
    u16 used_idx = used->idx;
    used->ring[used_idx % vq->num].id = desc_idx;
    used->ring[used_idx % vq->num].len = data_len + 1;
    used->idx = used_idx + 1;
    
    vq->last_avail_idx++;
  }
  
  blk->mmio.interrupt_status |= 1;
  mach *m = (mach *)blk->mach;
  rv_plic_irq(&m->plic0, blk->irq);
}

static void virtio_blk_queue_notify(void *device, u32 queue) {
  virtio_blk *blk = (virtio_blk *)device;
  
  if (queue == 0) {
    virtio_blk_handle_request(blk);
  }
}

void virtio_blk_init(virtio_blk *blk, void *mach, u32 irq, const char *disk_path) {
  memset(blk, 0, sizeof(*blk));
  
  blk->mach = mach;
  blk->irq = irq;
  
  blk->mmio.magic = VIRTIO_MAGIC;
  blk->mmio.version = VIRTIO_VERSION;
  blk->mmio.device_id = VIRTIO_DEVICE_ID_BLOCK;
  blk->mmio.vendor_id = VIRTIO_VENDOR_ID;
  blk->mmio.device_features = VIRTIO_F_VERSION_1 | VIRTIO_BLK_F_SIZE_MAX | 
                               VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_BLK_SIZE;
  blk->mmio.queue_num_max = VIRTIO_QUEUE_SIZE;
  blk->mmio.device = blk;
  blk->mmio.device_config = virtio_blk_config_handler;
  blk->mmio.queue_notify_handler = virtio_blk_queue_notify;
  
  if (disk_path) {
    blk->disk_file = fopen(disk_path, "r+b");
    if (!blk->disk_file) {
      blk->disk_file = fopen(disk_path, "rb");
      if (blk->disk_file) {
        blk->mmio.device_features |= VIRTIO_BLK_F_RO;
      }
    }
    
    if (blk->disk_file) {
      fseek(blk->disk_file, 0, SEEK_END);
      blk->disk_size = ftell(blk->disk_file);
      fseek(blk->disk_file, 0, SEEK_SET);
    }
  }
  
  blk->config.capacity = blk->disk_size / 512;
  blk->config.size_max = 32768;
  blk->config.seg_max = 128;
  blk->config.geometry.cylinders = 0;
  blk->config.geometry.heads = 0;
  blk->config.geometry.sectors = 0;
  blk->config.blk_size = 512;
  blk->config.topology.physical_block_exp = 0;
  blk->config.topology.alignment_offset = 0;
  blk->config.topology.min_io_size = 512;
  blk->config.topology.opt_io_size = 0;
  blk->config.writeback = 1;
}

void virtio_blk_deinit(virtio_blk *blk) {
  if (blk->disk_file) {
    fclose(blk->disk_file);
  }
}

rv_res virtio_blk_bus(virtio_blk *blk, u32 addr, u8 *data, u32 store, u32 width) {
  u32 offset = addr & 0xFFF;
  
  if (store) {
    u32 value = 0;
    memcpy(&value, data, width);
    
    switch (offset) {
      case 0x10: blk->mmio.device_features_sel = value; break;
      case 0x14: blk->mmio.driver_features = value; break;
      case 0x18: blk->mmio.driver_features_sel = value; break;
      case 0x30: blk->mmio.queue_sel = value; break;
      case 0x38: blk->mmio.queue_num = value; break;
      case 0x44: blk->mmio.queue_ready = value;
                 if (blk->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   blk->mmio.queues[blk->mmio.queue_sel].ready = value;
                   blk->mmio.queues[blk->mmio.queue_sel].num = blk->mmio.queue_num;
                 }
                 break;
      case 0x50: if (value < VIRTIO_QUEUE_MAX) virtio_blk_queue_notify(blk, value); break;
      case 0x64: blk->mmio.interrupt_ack = value; blk->mmio.interrupt_status &= ~value; break;
      case 0x70: blk->mmio.status = value; break;
      case 0x80: blk->mmio.queue_desc_low = value;
                 if (blk->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   blk->mmio.queues[blk->mmio.queue_sel].desc_addr = value;
                 }
                 break;
      case 0x90: blk->mmio.queue_avail_low = value;
                 if (blk->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   blk->mmio.queues[blk->mmio.queue_sel].avail_addr = value;
                 }
                 break;
      case 0xA0: blk->mmio.queue_used_low = value;
                 if (blk->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   blk->mmio.queues[blk->mmio.queue_sel].used_addr = value;
                 }
                 break;
      default:
        if (offset >= 0x100) {
          virtio_blk_config_handler(blk, offset - 0x100, data, width, 1);
        }
        break;
    }
  } else {
    u32 value = 0;
    
    switch (offset) {
      case 0x00: value = blk->mmio.magic; break;
      case 0x04: value = blk->mmio.version; break;
      case 0x08: value = blk->mmio.device_id; break;
      case 0x0C: value = blk->mmio.vendor_id; break;
      case 0x10: value = blk->mmio.device_features; break;
      case 0x34: value = blk->mmio.queue_num_max; break;
      case 0x44: value = blk->mmio.queue_ready; break;
      case 0x60: value = blk->mmio.interrupt_status; break;
      case 0x70: value = blk->mmio.status; break;
      case 0xFC: value = blk->mmio.config_generation; break;
      default:
        if (offset >= 0x100) {
          virtio_blk_config_handler(blk, offset - 0x100, data, width, 0);
          return RV_OK;
        }
        break;
    }
    
    memcpy(data, &value, width);
  }
  
  return RV_OK;
}

void virtio_blk_update(virtio_blk *blk) {
  (void)blk;
}