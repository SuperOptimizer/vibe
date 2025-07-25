#pragma once

#define VIRTIO_NET_F_CSUM               0
#define VIRTIO_NET_F_GUEST_CSUM         1
#define VIRTIO_NET_F_MAC                5
#define VIRTIO_NET_F_GUEST_TSO4         7
#define VIRTIO_NET_F_GUEST_TSO6         8
#define VIRTIO_NET_F_GUEST_ECN          9
#define VIRTIO_NET_F_GUEST_UFO          10
#define VIRTIO_NET_F_HOST_TSO4          11
#define VIRTIO_NET_F_HOST_TSO6          12
#define VIRTIO_NET_F_HOST_ECN           13
#define VIRTIO_NET_F_HOST_UFO           14
#define VIRTIO_NET_F_MRG_RXBUF          15
#define VIRTIO_NET_F_STATUS             16
#define VIRTIO_NET_F_CTRL_VQ            17
#define VIRTIO_NET_F_CTRL_RX            18
#define VIRTIO_NET_F_CTRL_VLAN          19
#define VIRTIO_NET_F_GUEST_ANNOUNCE     21
#define VIRTIO_NET_F_MQ                 22
#define VIRTIO_NET_F_CTRL_MAC_ADDR      23

#define VIRTIO_NET_S_LINK_UP            1
#define VIRTIO_NET_S_ANNOUNCE           2

struct virtio_net_config {
    u8 mac[6];
    u16 status;
    u16 max_virtqueue_pairs;
    u16 mtu;
};

struct virtio_net_hdr {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
    u16 num_buffers;
};

#define VIRTIO_NET_HDR_F_NEEDS_CSUM     1
#define VIRTIO_NET_HDR_F_DATA_VALID     2
#define VIRTIO_NET_HDR_F_RSC_INFO       4

#define VIRTIO_NET_HDR_GSO_NONE         0
#define VIRTIO_NET_HDR_GSO_TCPV4       1
#define VIRTIO_NET_HDR_GSO_UDP          3
#define VIRTIO_NET_HDR_GSO_TCPV6       4
#define VIRTIO_NET_HDR_GSO_ECN          0x80

typedef struct hw_virtio_net {
    hw_virtio vio;
    u8 mac[6];
    u16 status;
    int tap_fd;
} hw_virtio_net;

void hw_virtio_net_init(hw_virtio_net *net, mach *m);
void hw_virtio_net_destroy(hw_virtio_net *net);
void hw_virtio_net_update(hw_virtio_net *net);