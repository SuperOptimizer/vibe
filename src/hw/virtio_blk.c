#include "vibe.h"
#include <stdlib.h>

static void virtio_blk_reset(hw_virtio *vio) {
}

static u32 virtio_blk_get_config(hw_virtio *vio, u32 offset) {
    hw_virtio_blk *blk = (hw_virtio_blk *)vio;
    u32 value = 0;
    
    // virtio_blk_config structure per spec:
    // le64 capacity;                    // offset 0-7
    // le32 size_max;                    // offset 8-11
    // le32 seg_max;                     // offset 12-15
    // struct virtio_blk_geometry {      // offset 16-19
    //     le16 cylinders;
    //     u8 heads;
    //     u8 sectors;
    // } geometry;
    // le32 blk_size;                    // offset 20-23
    
    if (offset < 8) {
        // capacity (8 bytes) - little endian
        value = (blk->capacity >> (offset * 8)) & 0xFF;
    } else if (offset >= 8 && offset < 12) {
        // size_max - not set (0 means no limit)
        value = 0;
    } else if (offset >= 12 && offset < 16) {
        // seg_max
        u32 seg_max = 128;
        value = (seg_max >> ((offset - 12) * 8)) & 0xFF;
    } else if (offset >= 16 && offset < 20) {
        // geometry struct (4 bytes) - not used, return 0
        value = 0;
    } else if (offset >= 20 && offset < 24) {
        // blk_size (4 bytes at offset 20) - little endian
        u32 blk_size = 512;
        value = (blk_size >> ((offset - 20) * 8)) & 0xFF;
    }
    
    return value;
}

static void virtio_blk_set_config(hw_virtio *vio, u32 offset, u32 value) {
}

static void virtio_blk_process_request(hw_virtio_blk *blk, struct virtqueue *q) {
    struct virtq_desc desc;
    struct virtq_avail avail_header;
    struct virtio_blk_req req;
    u8 status = VIRTIO_BLK_S_OK;
    
    u64 avail_addr = q->avail_addr;
    if (mach_bus(blk->vio.mach, avail_addr, (u8*)&avail_header, false, 4) != BUS_OK)
        return;
    
    u16 avail_flags, avail_idx;
    rv_endcvt((u8*)&avail_header, (u8*)&avail_flags, 2, 0);
    rv_endcvt((u8*)&avail_header + 2, (u8*)&avail_idx, 2, 0);
    
    while (q->last_avail_idx != avail_idx) {
        u16 desc_idx;
        u64 idx_addr = avail_addr + 4 + (q->last_avail_idx % q->num) * 2;
        if (mach_bus(blk->vio.mach, idx_addr, (u8*)&desc_idx, false, 2) != BUS_OK)
            return;
        rv_endcvt((u8*)&desc_idx, (u8*)&desc_idx, 2, 0);
        
        u64 desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
        if (mach_bus(blk->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
            return;
        
        u64 addr;
        u32 len;
        u16 flags, next;
        rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
        rv_endcvt((u8*)&desc.len, (u8*)&len, 4, 0);
        rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
        rv_endcvt((u8*)&desc.next, (u8*)&next, 2, 0);
        
        if (mach_bus(blk->vio.mach, addr, (u8*)&req, false, sizeof(req)) != BUS_OK)
            return;
        
        u32 type, reserved;
        u64 sector;
        rv_endcvt((u8*)&req.type, (u8*)&type, 4, 0);
        rv_endcvt((u8*)&req.reserved, (u8*)&reserved, 4, 0);
        rv_endcvt((u8*)&req.sector, (u8*)&sector, 8, 0);
        
        if (!(flags & VIRTQ_DESC_F_NEXT)) {
            status = VIRTIO_BLK_S_IOERR;
            goto done;
        }
        
        desc_idx = next;
        desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
        if (mach_bus(blk->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
            return;
        
        rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
        rv_endcvt((u8*)&desc.len, (u8*)&len, 4, 0);
        rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
        rv_endcvt((u8*)&desc.next, (u8*)&next, 2, 0);
        
        u8 *buffer = malloc(len);
        if (!buffer) {
            status = VIRTIO_BLK_S_IOERR;
            goto done;
        }
        
        if (type == VIRTIO_BLK_T_IN) {
            if (!(flags & VIRTQ_DESC_F_WRITE)) {
                free(buffer);
                status = VIRTIO_BLK_S_IOERR;
                goto done;
            }
            
            if (blk->disk_file) {
                fseek(blk->disk_file, sector * 512, SEEK_SET);
                size_t read = fread(buffer, 1, len, blk->disk_file);
                if (read != len) {
                    memset(buffer + read, 0, len - read);
                }
            } else {
                memset(buffer, 0, len);
            }
            
            mach_bus(blk->vio.mach, addr, buffer, true, len);
        } else if (type == VIRTIO_BLK_T_OUT) {
            if (flags & VIRTQ_DESC_F_WRITE) {
                free(buffer);
                status = VIRTIO_BLK_S_IOERR;
                goto done;
            }
            
            if (blk->readonly) {
                status = VIRTIO_BLK_S_IOERR;
            } else {
                mach_bus(blk->vio.mach, addr, buffer, false, len);
                
                if (blk->disk_file) {
                    fseek(blk->disk_file, sector * 512, SEEK_SET);
                    fwrite(buffer, 1, len, blk->disk_file);
                    fflush(blk->disk_file);
                }
            }
        } else if (type == VIRTIO_BLK_T_FLUSH) {
            if (blk->disk_file) {
                fflush(blk->disk_file);
            }
        } else {
            status = VIRTIO_BLK_S_UNSUPP;
        }
        
        free(buffer);
        
        if (!(flags & VIRTQ_DESC_F_NEXT)) {
            status = VIRTIO_BLK_S_IOERR;
            goto done;
        }
        
        desc_idx = next;
        desc_addr = q->desc_addr + desc_idx * sizeof(struct virtq_desc);
        if (mach_bus(blk->vio.mach, desc_addr, (u8*)&desc, false, sizeof(desc)) != BUS_OK)
            return;
        
        rv_endcvt((u8*)&desc.addr, (u8*)&addr, 8, 0);
        rv_endcvt((u8*)&desc.flags, (u8*)&flags, 2, 0);
        
        if (!(flags & VIRTQ_DESC_F_WRITE)) {
            status = VIRTIO_BLK_S_IOERR;
            goto done;
        }
        
        mach_bus(blk->vio.mach, addr, &status, true, 1);
        
done:
        u16 start_desc_idx;
        u64 idx_addr_start = avail_addr + 4 + (q->last_avail_idx % q->num) * 2;
        mach_bus(blk->vio.mach, idx_addr_start, (u8*)&start_desc_idx, false, 2);
        rv_endcvt((u8*)&start_desc_idx, (u8*)&start_desc_idx, 2, 0);
        
        struct virtq_used_elem used_elem;
        used_elem.id = start_desc_idx;
        used_elem.len = 1;
        
        u32 id_le, len_le;
        rv_endcvt((u8*)&used_elem.id, (u8*)&id_le, 4, 1);
        rv_endcvt((u8*)&used_elem.len, (u8*)&len_le, 4, 1);
        
        u64 used_elem_addr = q->used_addr + 4 + (q->used_idx % q->num) * sizeof(struct virtq_used_elem);
        mach_bus(blk->vio.mach, used_elem_addr, (u8*)&id_le, true, 4);
        mach_bus(blk->vio.mach, used_elem_addr + 4, (u8*)&len_le, true, 4);
        
        q->used_idx++;
        u16 used_idx_le;
        rv_endcvt((u8*)&q->used_idx, (u8*)&used_idx_le, 2, 1);
        mach_bus(blk->vio.mach, q->used_addr + 2, (u8*)&used_idx_le, true, 2);
        
        q->last_avail_idx++;
    }
    
    hw_virtio_raise_interrupt(&blk->vio, VIRTIO_ISR_QUEUE_INT);
}

static void virtio_blk_queue_notify(hw_virtio *vio, u32 queue) {
    hw_virtio_blk *blk = (hw_virtio_blk *)vio;
    
    if (queue == 0 && vio->queues[0].ready) {
        virtio_blk_process_request(blk, &vio->queues[0]);
    }
}

void hw_virtio_blk_init(hw_virtio_blk *blk, mach *m, const char *disk_path) {
    memset(blk, 0, sizeof(*blk));
    hw_virtio_init(&blk->vio, m, VIRTIO_BLK_ID);
    
    blk->vio.device_features |= (1ULL << VIRTIO_BLK_F_BLK_SIZE);
    blk->vio.reset = virtio_blk_reset;
    blk->vio.get_config = virtio_blk_get_config;
    blk->vio.set_config = virtio_blk_set_config;
    blk->vio.queue_notify = virtio_blk_queue_notify;
    
    if (disk_path) {
        blk->disk_file = fopen(disk_path, "r+b");
        if (!blk->disk_file) {
            blk->disk_file = fopen(disk_path, "rb");
            blk->readonly = true;
            if (blk->disk_file) {
                blk->vio.device_features |= (1 << VIRTIO_BLK_F_RO);
            }
        }
        
        if (blk->disk_file) {
            fseek(blk->disk_file, 0, SEEK_END);
            long size = ftell(blk->disk_file);
            fseek(blk->disk_file, 0, SEEK_SET);
            blk->capacity = size / 512;
            printf("virtio-blk: %s opened, %llu sectors%s\n", 
                   disk_path, (unsigned long long)blk->capacity, blk->readonly ? " (read-only)" : "");
        } else {
            printf("virtio-blk: failed to open %s\n", disk_path);
        }
    }
}

void hw_virtio_blk_destroy(hw_virtio_blk *blk) {
    if (blk->disk_file) {
        fclose(blk->disk_file);
        blk->disk_file = NULL;
    }
}