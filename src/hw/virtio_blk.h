#pragma once

#include "virtio.h"
#include <stdio.h>

#define VIRTIO_BLK_F_SIZE_MAX (1 << 1)
#define VIRTIO_BLK_F_SEG_MAX (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY (1 << 4)
#define VIRTIO_BLK_F_RO (1 << 5)
#define VIRTIO_BLK_F_BLK_SIZE (1 << 6)
#define VIRTIO_BLK_F_FLUSH (1 << 9)
#define VIRTIO_BLK_F_TOPOLOGY (1 << 10)
#define VIRTIO_BLK_F_CONFIG_WCE (1 << 11)

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_T_GET_ID 8
#define VIRTIO_BLK_T_DISCARD 11
#define VIRTIO_BLK_T_WRITE_ZEROES 13

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

typedef struct virtio_blk_config {
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
  u8 unused0[3];
  u32 max_discard_sectors;
  u32 max_discard_seg;
  u32 discard_sector_alignment;
  u32 max_write_zeroes_sectors;
  u32 max_write_zeroes_seg;
  u8 write_zeroes_may_unmap;
  u8 unused1[3];
} virtio_blk_config;

typedef struct virtio_blk_req {
  u32 type;
  u32 reserved;
  u64 sector;
} virtio_blk_req;

typedef struct virtio_blk {
  virtio_mmio mmio;
  virtio_blk_config config;
  void *mach;
  u32 irq;
  FILE *disk_file;
  u64 disk_size;
  u8 io_buffer[8192];
} virtio_blk;

void virtio_blk_init(virtio_blk *blk, void *mach, u32 irq, const char *disk_path);
void virtio_blk_deinit(virtio_blk *blk);
rv_res virtio_blk_bus(virtio_blk *blk, u32 addr, u8 *data, u32 store, u32 width);
void virtio_blk_update(virtio_blk *blk);