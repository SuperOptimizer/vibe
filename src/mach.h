#pragma once

#include "hw/plic.h"
#include "hw/clint.h"
#include "hw/uart.h"
#include "hw/rtc.h"

#define MACH_RAM_BASE 0x80000000UL
#define MACH_RAM_SIZE (1024UL * 1024UL * 128UL) /* 128MiB of ram */
#define MACH_DTB_OFFSET 0x2000000UL             /* dtb is @32MiB */

#define MACH_PLIC0_BASE 0xC000000UL  /* plic0 base address */
#define MACH_CLINT0_BASE 0x2000000UL /* clint0 base address */
#define MACH_UART0_BASE 0x3000000UL  /* uart0 base address */
#define MACH_UART1_BASE 0x6000000UL  /* uart1 base address */
#define MACH_RTC0_BASE 0x101000UL    /* rtc0 base address */
#define MACH_VIRTIO0_BASE 0x10001000UL /* virtio-net base address */
#define MACH_VIRTIO1_BASE 0x10002000UL /* virtio-blk base address */
#define MACH_VIRTIO2_BASE 0x10003000UL /* virtio-rng base address */
#define MACH_NVME0_BASE 0x10004000UL   /* NVMe controller base address */

typedef struct mach {
  rv *cpu;
  u8 *ram;
  rv_plic plic0;
  rv_clint clint0;
  rv_uart uart0, uart1;
  rv_rtc rtc0;
} mach;

/* machine functions */
void mach_init(mach *m, rv *cpu);

void mach_deinit(mach *m);

void mach_set(mach *m, const char *firmware, const char *dtb);

void mach_set_disk(mach *m, const char *disk_path);

void mach_step(mach *m, u32 *rtc_period);

rv_res mach_bus(void *user, u32 addr, u8 *data, u32 store, u32 width);

/* UART callbacks - these could be in mach.c but declared here for flexibility */
rv_res uart0_io(void *user, u8 *byte, u32 write);

rv_res uart1_io(void *user, u8 *byte, u32 write);
