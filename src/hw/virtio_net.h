#pragma once

#include "virtio.h"

#define VIRTIO_NET_F_MAC (1 << 5)
#define VIRTIO_NET_F_STATUS (1 << 16)

#define VIRTIO_NET_S_LINK_UP 1
#define VIRTIO_NET_S_ANNOUNCE 2

#define VIRTIO_NET_HDR_SIZE 12

typedef struct virtio_net_hdr {
  u8 flags;
  u8 gso_type;
  u16 hdr_len;
  u16 gso_size;
  u16 csum_start;
  u16 csum_offset;
  u16 num_buffers;
} virtio_net_hdr;

typedef struct virtio_net_config {
  u8 mac[6];
  u16 status;
  u16 max_virtqueue_pairs;
  u16 mtu;
} virtio_net_config;

typedef struct virtio_net {
  virtio_mmio mmio;
  virtio_net_config config;
  void *mach;
  u32 irq;
  int tap_fd;
  u8 rx_buffer[2048];
  u8 tx_buffer[2048];
} virtio_net;

void virtio_net_init(virtio_net *net, void *mach, u32 irq);
void virtio_net_deinit(virtio_net *net);
rv_res virtio_net_bus(virtio_net *net, u32 addr, u8 *data, u32 store, u32 width);
void virtio_net_update(virtio_net *net);