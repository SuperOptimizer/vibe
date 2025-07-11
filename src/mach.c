#include <stdlib.h>
#include <string.h>

#include "vibe.h"


bus_error mach_bus(void *user, u32 addr, u8 *data, bool store, u32 width) {
  mach *m = (mach *)user;
  if (addr >= MACH_RAM_BASE && addr < MACH_RAM_BASE + MACH_RAM_SIZE) {
    u8 *ram = m->ram + addr - MACH_RAM_BASE;
    memcpy(store ? ram : data, store ? data : ram, width);
    return BUS_OK;
  } else if (addr >= MACH_PLIC0_BASE && addr < MACH_PLIC0_BASE + hw_plic_SIZE) {
    return hw_plic_bus(&m->plic0, addr - MACH_PLIC0_BASE, data, store, width);
  } else if (addr >= MACH_CLINT0_BASE &&
             addr < MACH_CLINT0_BASE + hw_clint_SIZE) {
    return hw_clint_bus(&m->clint0, addr - MACH_CLINT0_BASE, data, store,
                        width);
  } else if (addr >= MACH_UART0_BASE && addr < MACH_UART0_BASE + hw_uart_SIZE) {
    return hw_uart_bus(&m->uart0, addr - MACH_UART0_BASE, data, store, width);
  } else if (addr >= MACH_RTC0_BASE && addr < MACH_RTC0_BASE + hw_rtc_SIZE) {
    return hw_rtc_bus(&m->rtc0, addr - MACH_RTC0_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO0_BASE && addr < MACH_VIRTIO0_BASE + VIRTIO_MMIO_SIZE) {
    return hw_virtio_mmio_bus(&m->virtio_net0.vio, addr - MACH_VIRTIO0_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO1_BASE && addr < MACH_VIRTIO1_BASE + VIRTIO_MMIO_SIZE) {
    return hw_virtio_mmio_bus(&m->virtio_blk0.vio, addr - MACH_VIRTIO1_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO2_BASE && addr < MACH_VIRTIO2_BASE + VIRTIO_MMIO_SIZE) {
    return hw_virtio_mmio_bus(&m->virtio_rng0.vio, addr - MACH_VIRTIO2_BASE, data, store, width);
  } else {
    return BUS_UNMAPPED;
  }
}




void load(const char *path, u8 *buf, u32 max_size) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("unable to load file %s\n", path);
    exit(EXIT_FAILURE);
  }
  fread(buf, 1, max_size, f);
  fclose(f);
}

void mach_init(mach *m) {
  memset(m, 0, sizeof(mach));
  m->ram = malloc(MACH_RAM_SIZE);
  memset(m->ram, 0, MACH_RAM_SIZE);

  rv_init(&m->cpu, m);
  hw_plic_init(&m->plic0);
  hw_clint_init(&m->clint0, &m->cpu);
  hw_uart_init(&m->uart0);
  hw_rtc_init(&m->rtc0);
  hw_virtio_net_init(&m->virtio_net0, m);
  hw_virtio_rng_init(&m->virtio_rng0, m);
}

void mach_deinit(mach *m) {
  if (m->ram) {
    free(m->ram);
    m->ram = NULL;
  }
  hw_virtio_net_destroy(&m->virtio_net0);
  hw_virtio_blk_destroy(&m->virtio_blk0);
  hw_virtio_rng_destroy(&m->virtio_rng0);
}

void mach_set(mach *m, const char *firmware, const char *dtb) {
  load(firmware, m->ram, MACH_RAM_SIZE);
  load(dtb, m->ram + MACH_DTB_OFFSET, MACH_RAM_SIZE - MACH_DTB_OFFSET);

  /* the bootloader and linux expect the following: */
  m->cpu.r[10] /* a0 */ = 0;                               /* hartid */
  m->cpu.r[11] /* a1 */ = MACH_RAM_BASE + MACH_DTB_OFFSET; /* dtb ptr */
}

void mach_set_disk(mach *m, const char *disk_path) {
  if (disk_path) {
    hw_virtio_blk_init(&m->virtio_blk0, m, disk_path);
  }
}


void mach_step(mach *m, u32 *rtc_period) {
  u32 irq = 0;

  if (!(*rtc_period = (*rtc_period + 1) & 0xFFF))
    if (!++m->cpu.csr.mtime)
      m->cpu.csr.mtimeh++;

  rv_step(&m->cpu);

  /* update peripherals and interrupts */
  if (hw_uart_update(&m->uart0))
    hw_plic_irq(&m->plic0, 1);

  hw_rtc_update(&m->rtc0);
  hw_virtio_net_update(&m->virtio_net0);

  irq = RV_CSI * hw_clint_msi(&m->clint0, 0) |
        RV_CTI * hw_clint_mti(&m->clint0, 0) |
        RV_CEI * hw_plic_mei(&m->plic0, 0);
  rv_irq(&m->cpu, irq);
}
