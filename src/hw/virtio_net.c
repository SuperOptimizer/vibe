#include "virtio_net.h"
#include "plic.h"
#include "mach.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#define IFF_TAP 0x0002
#define IFF_NO_PI 0x1000
#define TUNSETIFF 0x400454ca
#endif

static void virtio_net_config_handler(void *device, u32 offset, u8 *data, u32 len, u32 write) {
  virtio_net *net = (virtio_net *)device;
  
  if (offset + len > sizeof(virtio_net_config)) {
    return;
  }
  
  if (write) {
    memcpy((u8 *)&net->config + offset, data, len);
  } else {
    memcpy(data, (u8 *)&net->config + offset, len);
  }
}

static void virtio_net_handle_rx(virtio_net *net) {
  virtqueue *vq = &net->mmio.queues[0];
  
  if (!vq->ready) return;
  
  int len = read(net->tap_fd, net->rx_buffer + VIRTIO_NET_HDR_SIZE, 
                 sizeof(net->rx_buffer) - VIRTIO_NET_HDR_SIZE);
  
  if (len <= 0) return;
  
  virtio_net_hdr *hdr = (virtio_net_hdr *)net->rx_buffer;
  memset(hdr, 0, sizeof(*hdr));
  
  u16 avail_idx = *(u16 *)((u8 *)net->mach + vq->avail_addr + 2);
  if (vq->last_avail_idx == avail_idx) return;
  
  u16 desc_idx = *(u16 *)((u8 *)net->mach + vq->avail_addr + 4 + 
                          (vq->last_avail_idx % vq->num) * 2);
  
  virtq_desc *desc = (virtq_desc *)((u8 *)net->mach + vq->desc_addr + 
                                     desc_idx * sizeof(virtq_desc));
  
  u32 copy_len = len + VIRTIO_NET_HDR_SIZE;
  if (copy_len > desc->len) copy_len = desc->len;
  
  memcpy((u8 *)net->mach + desc->addr, net->rx_buffer, copy_len);
  
  virtq_used *used = (virtq_used *)((u8 *)net->mach + vq->used_addr);
  u16 used_idx = used->idx;
  used->ring[used_idx % vq->num].id = desc_idx;
  used->ring[used_idx % vq->num].len = copy_len;
  used->idx = used_idx + 1;
  
  vq->last_avail_idx++;
  
  net->mmio.interrupt_status |= 1;
  mach *m = (mach *)net->mach;
  rv_plic_irq(&m->plic0, net->irq);
}

static void virtio_net_handle_tx(virtio_net *net) {
  virtqueue *vq = &net->mmio.queues[1];
  
  if (!vq->ready) return;
  
  u16 avail_idx = *(u16 *)((u8 *)net->mach + vq->avail_addr + 2);
  
  while (vq->last_avail_idx != avail_idx) {
    u16 desc_idx = *(u16 *)((u8 *)net->mach + vq->avail_addr + 4 + 
                            (vq->last_avail_idx % vq->num) * 2);
    
    virtq_desc *desc = (virtq_desc *)((u8 *)net->mach + vq->desc_addr + 
                                       desc_idx * sizeof(virtq_desc));
    
    u32 total_len = 0;
    u32 hdr_len = 0;
    
    while (1) {
      if (total_len + desc->len > sizeof(net->tx_buffer)) break;
      
      memcpy(net->tx_buffer + total_len, (u8 *)net->mach + desc->addr, desc->len);
      
      if (hdr_len == 0) {
        hdr_len = (desc->len >= VIRTIO_NET_HDR_SIZE) ? VIRTIO_NET_HDR_SIZE : desc->len;
      }
      
      total_len += desc->len;
      
      if (!(desc->flags & 1)) break;
      desc = (virtq_desc *)((u8 *)net->mach + vq->desc_addr + 
                            desc->next * sizeof(virtq_desc));
    }
    
    if (total_len > hdr_len) {
      write(net->tap_fd, net->tx_buffer + hdr_len, total_len - hdr_len);
    }
    
    virtq_used *used = (virtq_used *)((u8 *)net->mach + vq->used_addr);
    u16 used_idx = used->idx;
    used->ring[used_idx % vq->num].id = desc_idx;
    used->ring[used_idx % vq->num].len = total_len;
    used->idx = used_idx + 1;
    
    vq->last_avail_idx++;
  }
  
  net->mmio.interrupt_status |= 1;
  mach *m = (mach *)net->mach;
  rv_plic_irq(&m->plic0, net->irq);
}

static void virtio_net_queue_notify(void *device, u32 queue) {
  virtio_net *net = (virtio_net *)device;
  
  if (queue == 0) {
    virtio_net_handle_rx(net);
  } else if (queue == 1) {
    virtio_net_handle_tx(net);
  }
}

static int create_tap_device(const char *name) {
#ifdef __linux__
  struct ifreq {
    char ifr_name[16];
    short ifr_flags;
  } ifr;
  int fd;
  
  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    return -1;
  }
  
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  strncpy(ifr.ifr_name, name, 15);
  
  if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
    close(fd);
    return -1;
  }
  
  fcntl(fd, F_SETFL, O_NONBLOCK);
  
  return fd;
#else
  (void)name;
  return -1;
#endif
}

void virtio_net_init(virtio_net *net, void *mach, u32 irq) {
  memset(net, 0, sizeof(*net));
  
  net->mach = mach;
  net->irq = irq;
  
  net->mmio.magic = VIRTIO_MAGIC;
  net->mmio.version = VIRTIO_VERSION;
  net->mmio.device_id = VIRTIO_DEVICE_ID_NET;
  net->mmio.vendor_id = VIRTIO_VENDOR_ID;
  net->mmio.device_features = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
  net->mmio.queue_num_max = VIRTIO_QUEUE_SIZE;
  net->mmio.device = net;
  net->mmio.device_config = virtio_net_config_handler;
  net->mmio.queue_notify_handler = virtio_net_queue_notify;
  
  net->config.mac[0] = 0x52;
  net->config.mac[1] = 0x54;
  net->config.mac[2] = 0x00;
  net->config.mac[3] = 0x12;
  net->config.mac[4] = 0x34;
  net->config.mac[5] = 0x56;
  net->config.status = VIRTIO_NET_S_LINK_UP;
  net->config.max_virtqueue_pairs = 1;
  net->config.mtu = 1500;
  
  net->tap_fd = create_tap_device("tap0");
  if (net->tap_fd < 0) {
    printf("Warning: Could not create TAP device\n");
  }
}

void virtio_net_deinit(virtio_net *net) {
  if (net->tap_fd >= 0) {
    close(net->tap_fd);
  }
}

rv_res virtio_net_bus(virtio_net *net, u32 addr, u8 *data, u32 store, u32 width) {
  u32 offset = addr & 0xFFF;
  
  if (store) {
    u32 value = 0;
    memcpy(&value, data, width);
    
    switch (offset) {
      case 0x10: net->mmio.device_features_sel = value; break;
      case 0x14: net->mmio.driver_features = value; break;
      case 0x18: net->mmio.driver_features_sel = value; break;
      case 0x30: net->mmio.queue_sel = value; break;
      case 0x38: net->mmio.queue_num = value; break;
      case 0x44: net->mmio.queue_ready = value; 
                 if (net->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   net->mmio.queues[net->mmio.queue_sel].ready = value;
                   net->mmio.queues[net->mmio.queue_sel].num = net->mmio.queue_num;
                 }
                 break;
      case 0x50: if (value < VIRTIO_QUEUE_MAX) virtio_net_queue_notify(net, value); break;
      case 0x64: net->mmio.interrupt_ack = value; net->mmio.interrupt_status &= ~value; break;
      case 0x70: net->mmio.status = value; break;
      case 0x80: net->mmio.queue_desc_low = value;
                 if (net->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   net->mmio.queues[net->mmio.queue_sel].desc_addr = value;
                 }
                 break;
      case 0x90: net->mmio.queue_avail_low = value;
                 if (net->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   net->mmio.queues[net->mmio.queue_sel].avail_addr = value;
                 }
                 break;
      case 0xA0: net->mmio.queue_used_low = value;
                 if (net->mmio.queue_sel < VIRTIO_QUEUE_MAX) {
                   net->mmio.queues[net->mmio.queue_sel].used_addr = value;
                 }
                 break;
      default:
        if (offset >= 0x100) {
          virtio_net_config_handler(net, offset - 0x100, data, width, 1);
        }
        break;
    }
  } else {
    u32 value = 0;
    
    switch (offset) {
      case 0x00: value = net->mmio.magic; break;
      case 0x04: value = net->mmio.version; break;
      case 0x08: value = net->mmio.device_id; break;
      case 0x0C: value = net->mmio.vendor_id; break;
      case 0x10: value = net->mmio.device_features; break;
      case 0x34: value = net->mmio.queue_num_max; break;
      case 0x44: value = net->mmio.queue_ready; break;
      case 0x60: value = net->mmio.interrupt_status; break;
      case 0x70: value = net->mmio.status; break;
      case 0xFC: value = net->mmio.config_generation; break;
      default:
        if (offset >= 0x100) {
          virtio_net_config_handler(net, offset - 0x100, data, width, 0);
          return RV_OK;
        }
        break;
    }
    
    memcpy(data, &value, width);
  }
  
  return RV_OK;
}

void virtio_net_update(virtio_net *net) {
  if (net->tap_fd >= 0) {
    virtio_net_handle_rx(net);
  }
}