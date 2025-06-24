#pragma once

#define VIRTIO_BLK_F_SIZE_MAX   1
#define VIRTIO_BLK_F_SEG_MAX    2  
#define VIRTIO_BLK_F_GEOMETRY   4
#define VIRTIO_BLK_F_RO         5
#define VIRTIO_BLK_F_BLK_SIZE   6
#define VIRTIO_BLK_F_FLUSH      9
#define VIRTIO_BLK_F_TOPOLOGY   10
#define VIRTIO_BLK_F_CONFIG_WCE 11

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

struct virtio_blk_config {
    u64 capacity;
    u32 size_max;
    u32 seg_max;
    struct {
        u16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry;
    u32 blk_size;
    struct {
        u8 physical_block_exp;
        u8 alignment_offset;
        u16 min_io_size;
        u32 opt_io_size;
    } topology;
    u8 writeback;
};

struct virtio_blk_req {
    u32 type;
    u32 reserved;
    u64 sector;
};

typedef struct hw_virtio_blk {
    hw_virtio vio;
    FILE *disk_file;
    u64 capacity;
    bool readonly;
} hw_virtio_blk;

void hw_virtio_blk_init(hw_virtio_blk *blk, mach *m, const char *disk_path);
void hw_virtio_blk_destroy(hw_virtio_blk *blk);