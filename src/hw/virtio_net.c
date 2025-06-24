#include "vibe.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#endif

static void virtio_net_reset(hw_virtio *vio) {
    hw_virtio_net *net = (hw_virtio_net *)vio;
    net->status = VIRTIO_NET_S_LINK_UP;
}

static u32 virtio_net_get_config(hw_virtio *vio, u32 offset) {
    hw_virtio_net *net = (hw_virtio_net *)vio;
    u32 value = 0;
    
    if (offset < 6) {
        // MAC address
        value = net->mac[offset];
    } else if (offset >= 6 && offset < 8) {
        // status (2 bytes)
        value = (net->status >> ((offset - 6) * 8)) & 0xFF;
    } else if (offset >= 8 && offset < 10) {
        // max_virtqueue_pairs
        u16 max_pairs = 1;
        value = (max_pairs >> ((offset - 8) * 8)) & 0xFF;
    } else if (offset >= 10 && offset < 12) {
        // mtu
        u16 mtu = 1500;
        value = (mtu >> ((offset - 10) * 8)) & 0xFF;
    }
    
    return value;
}

static void virtio_net_set_config(hw_virtio *vio, u32 offset, u32 value) {
    // Configuration is read-only for now
}

static void virtio_net_send_packet(hw_virtio_net *net, const u8 *data, size_t len) {
#ifdef __linux__
    if (net->tap_fd >= 0) {
        write(net->tap_fd, data, len);
    }
#endif
}

static void virtio_net_process_tx(hw_virtio_net *net, struct virtqueue *q) {
    struct virtq_desc desc;
    struct virtq_avail avail_header;
    
    u64 avail_addr = q->avail_addr;
    if (mach_bus(net->vio.mach, avail_addr, (u8*)&avail_header, false, 4) != BUS_OK)
        return;
    
    u16 avail_flags, avail_idx;
    rv_endcvt((u8*)&avail_header, (u8*)&avail_flags, 2, 0);
    rv_endcvt((u8*)&avail_header + 2, (u8*)&avail_idx, 2, 0);
    
    while (q->last_avail_idx != avail_idx) {
        u16 desc_idx;
        u64 idx_addr = avail_addr + 4 + (q->last_avail_idx % q->num) * 2;
        if (mach_bus(net->vio.mach, idx_addr, (u8*)&desc_idx, false, 2) != BUS_OK)
            return;
        rv_endcvt((u8*)&desc_idx, (u8*)&desc_idx, 2, 0);
        
        u16 first_desc_idx = desc_idx;
        size_t total_len = 0;
        u8 *packet = NULL;
        size_t packet_offset = 0;
        
        // First pass: calculate total length
        while (1) {
            u64 desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
            if (mach_bus(net->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
                break;
            
            u64 addr;
            u32 len;
            u16 flags;
            rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
            rv_endcvt((u8*)&desc.len, (u8*)&len, 4, 0);
            rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
            
            total_len += len;
            
            if (!(flags & VIRTQ_DESC_F_NEXT))
                break;
            
            rv_endcvt((u8*)&desc.next, (u8*)&desc_idx, 2, 0);
        }
        
        if (total_len > 0) {
            packet = malloc(total_len);
            if (packet) {
                // Second pass: collect data
                desc_idx = first_desc_idx;
                packet_offset = 0;
                
                while (1) {
                    u64 desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
                    if (mach_bus(net->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
                        break;
                    
                    u64 addr;
                    u32 len;
                    u16 flags;
                    rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
                    rv_endcvt((u8*)&desc.len, (u8*)&len, 4, 0);
                    rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
                    
                    if (mach_bus(net->vio.mach, addr, packet + packet_offset, false, len) != BUS_OK)
                        break;
                    
                    packet_offset += len;
                    
                    if (!(flags & VIRTQ_DESC_F_NEXT))
                        break;
                    
                    rv_endcvt((u8*)&desc.next, (u8*)&desc_idx, 2, 0);
                }
                
                // Skip virtio_net_hdr and send actual packet
                if (total_len > sizeof(struct virtio_net_hdr)) {
                    virtio_net_send_packet(net, packet + sizeof(struct virtio_net_hdr), 
                                         total_len - sizeof(struct virtio_net_hdr));
                }
                
                free(packet);
            }
        }
        
        // Add to used ring
        struct virtq_used_elem used_elem;
        used_elem.id = first_desc_idx;
        used_elem.len = 0;
        
        u32 id_le, len_le;
        rv_endcvt((u8*)&used_elem.id, (u8*)&id_le, 4, 1);
        rv_endcvt((u8*)&used_elem.len, (u8*)&len_le, 4, 1);
        
        u64 used_elem_addr = q->used_addr + 4 + (q->used_idx % q->num) * sizeof(struct virtq_used_elem);
        mach_bus(net->vio.mach, used_elem_addr, (u8*)&id_le, true, 4);
        mach_bus(net->vio.mach, used_elem_addr + 4, (u8*)&len_le, true, 4);
        
        q->used_idx++;
        u16 used_idx_le;
        rv_endcvt((u8*)&q->used_idx, (u8*)&used_idx_le, 2, 1);
        mach_bus(net->vio.mach, q->used_addr + 2, (u8*)&used_idx_le, true, 2);
        
        q->last_avail_idx++;
    }
    
    hw_virtio_raise_interrupt(&net->vio, VIRTIO_ISR_QUEUE_INT);
}

static void virtio_net_queue_notify(hw_virtio *vio, u32 queue) {
    hw_virtio_net *net = (hw_virtio_net *)vio;
    
    if (queue == 1 && vio->queues[1].ready) {
        // TX queue
        virtio_net_process_tx(net, &vio->queues[1]);
    }
}

#ifdef __linux__
static int tap_open(const char *name) {
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0)
        return -1;
    
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        close(fd);
        return -1;
    }
    
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#endif

void hw_virtio_net_init(hw_virtio_net *net, mach *m) {
    memset(net, 0, sizeof(*net));
    hw_virtio_init(&net->vio, m, 1); // VIRTIO_NET_ID = 1
    
    net->vio.irq_num = 2; // IRQ 2 for virtio-net
    
    // Set device features
    net->vio.device_features |= (1ULL << VIRTIO_NET_F_MAC);
    net->vio.device_features |= (1ULL << VIRTIO_NET_F_STATUS);
    
    net->vio.reset = virtio_net_reset;
    net->vio.get_config = virtio_net_get_config;
    net->vio.set_config = virtio_net_set_config;
    net->vio.queue_notify = virtio_net_queue_notify;
    
    // Generate MAC address (52:54:00:xx:xx:xx - QEMU OUI)
    net->mac[0] = 0x52;
    net->mac[1] = 0x54;
    net->mac[2] = 0x00;
    net->mac[3] = 0x12;
    net->mac[4] = 0x34;
    net->mac[5] = 0x56;
    
    net->status = VIRTIO_NET_S_LINK_UP;
    
#ifdef __linux__
    net->tap_fd = tap_open("tap0");
    if (net->tap_fd >= 0) {
        printf("virtio-net: opened tap0\n");
    } else {
        printf("virtio-net: failed to open tap device: %s\n", strerror(errno));
        net->tap_fd = -1;
    }
#else
    net->tap_fd = -1;
    printf("virtio-net: TAP not supported on this platform\n");
#endif
}

void hw_virtio_net_destroy(hw_virtio_net *net) {
#ifdef __linux__
    if (net->tap_fd >= 0) {
        close(net->tap_fd);
        net->tap_fd = -1;
    }
#endif
}

void hw_virtio_net_update(hw_virtio_net *net) {
#ifdef __linux__
    if (net->tap_fd < 0)
        return;
    
    // Check for incoming packets
    u8 buffer[2048];
    ssize_t len = read(net->tap_fd, buffer, sizeof(buffer));
    if (len <= 0)
        return;
    
    // Find RX queue (queue 0)
    struct virtqueue *q = &net->vio.queues[0];
    if (!q->ready)
        return;
    
    // Get available descriptor
    struct virtq_avail avail_header;
    u64 avail_addr = q->avail_addr;
    if (mach_bus(net->vio.mach, avail_addr, (u8*)&avail_header, false, 4) != BUS_OK)
        return;
    
    u16 avail_flags, avail_idx;
    rv_endcvt((u8*)&avail_header, (u8*)&avail_flags, 2, 0);
    rv_endcvt((u8*)&avail_header + 2, (u8*)&avail_idx, 2, 0);
    
    if (q->last_avail_idx == avail_idx)
        return; // No available buffers
    
    u16 desc_idx;
    u64 idx_addr = avail_addr + 4 + (q->last_avail_idx % q->num) * 2;
    if (mach_bus(net->vio.mach, idx_addr, (u8*)&desc_idx, false, 2) != BUS_OK)
        return;
    rv_endcvt((u8*)&desc_idx, (u8*)&desc_idx, 2, 0);
    
    u16 first_desc_idx = desc_idx;
    size_t written = 0;
    
    // Write virtio_net_hdr
    struct virtio_net_hdr hdr = {0};
    
    struct virtq_desc desc;
    u64 desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
    if (mach_bus(net->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
        return;
    
    u64 addr;
    u32 desc_len;
    u16 flags;
    rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
    rv_endcvt((u8*)&desc.len, (u8*)&desc_len, 4, 0);
    rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
    
    if (!(flags & VIRTQ_DESC_F_WRITE))
        return;
    
    // Write header
    size_t hdr_len = sizeof(hdr) < desc_len ? sizeof(hdr) : desc_len;
    mach_bus(net->vio.mach, addr, (u8*)&hdr, true, hdr_len);
    written += hdr_len;
    
    // If there's space left in first descriptor for data
    if (desc_len > sizeof(hdr)) {
        size_t data_len = desc_len - sizeof(hdr);
        if (data_len > len)
            data_len = len;
        mach_bus(net->vio.mach, addr + sizeof(hdr), buffer, true, data_len);
        written += data_len;
        len -= data_len;
    }
    
    // Continue with next descriptors if needed
    while (len > 0 && (flags & VIRTQ_DESC_F_NEXT)) {
        rv_endcvt((u8*)&desc.next, (u8*)&desc_idx, 2, 0);
        desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
        if (mach_bus(net->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
            break;
        
        rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
        rv_endcvt((u8*)&desc.len, (u8*)&desc_len, 4, 0);
        rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
        
        if (!(flags & VIRTQ_DESC_F_WRITE))
            break;
        
        size_t copy_len = desc_len < len ? desc_len : len;
        mach_bus(net->vio.mach, addr, buffer + (written - sizeof(hdr)), true, copy_len);
        written += copy_len;
        len -= copy_len;
    }
    
    // Add to used ring
    struct virtq_used_elem used_elem;
    used_elem.id = first_desc_idx;
    used_elem.len = written;
    
    u32 id_le, len_le;
    rv_endcvt((u8*)&used_elem.id, (u8*)&id_le, 4, 1);
    rv_endcvt((u8*)&used_elem.len, (u8*)&len_le, 4, 1);
    
    u64 used_elem_addr = q->used_addr + 4 + (q->used_idx % q->num) * sizeof(struct virtq_used_elem);
    mach_bus(net->vio.mach, used_elem_addr, (u8*)&id_le, true, 4);
    mach_bus(net->vio.mach, used_elem_addr + 4, (u8*)&len_le, true, 4);
    
    q->used_idx++;
    u16 used_idx_le;
    rv_endcvt((u8*)&q->used_idx, (u8*)&used_idx_le, 2, 1);
    mach_bus(net->vio.mach, q->used_addr + 2, (u8*)&used_idx_le, true, 2);
    
    q->last_avail_idx++;
    
    hw_virtio_raise_interrupt(&net->vio, VIRTIO_ISR_QUEUE_INT);
#endif
}