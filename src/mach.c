#include <curses.h>
#include <stdlib.h>
#include <string.h>

#include "rv.h"
#include "hw/clint.h"
#include "hw/plic.h"
#include "hw/uart.h"
#include "hw/rtc.h"
#include "hw/virtio_net.h"
#include "hw/virtio_blk.h"
#include "hw/virtio_rng.h"

#include "mach.h"

/* machine general bus access */
rv_res mach_bus(void *user, u32 addr, u8 *data, u32 store,
                u32 width) {
  mach *m = (mach *)user;
  if (addr >= MACH_RAM_BASE && addr < MACH_RAM_BASE + MACH_RAM_SIZE) {
    u8 *ram = m->ram + addr - MACH_RAM_BASE;
    memcpy(store ? ram : data, store ? data : ram, width);
    return RV_OK;
  } else if (addr >= MACH_PLIC0_BASE && addr < MACH_PLIC0_BASE + RV_PLIC_SIZE) {
    return rv_plic_bus(&m->plic0, addr - MACH_PLIC0_BASE, data, store, width);
  } else if (addr >= MACH_CLINT0_BASE &&
             addr < MACH_CLINT0_BASE + RV_CLINT_SIZE) {
    return rv_clint_bus(&m->clint0, addr - MACH_CLINT0_BASE, data, store,
                        width);
  } else if (addr >= MACH_UART0_BASE && addr < MACH_UART0_BASE + RV_UART_SIZE) {
    return rv_uart_bus(&m->uart0, addr - MACH_UART0_BASE, data, store, width);
  } else if (addr >= MACH_UART1_BASE && addr < MACH_UART1_BASE + RV_UART_SIZE) {
    return rv_uart_bus(&m->uart1, addr - MACH_UART1_BASE, data, store, width);
  } else if (addr >= MACH_RTC0_BASE && addr < MACH_RTC0_BASE + RV_RTC_SIZE) {
    return rv_rtc_bus(&m->rtc0, addr - MACH_RTC0_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO0_BASE && addr < MACH_VIRTIO0_BASE + 0x1000) {
    return virtio_net_bus(&m->vnet0, addr - MACH_VIRTIO0_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO1_BASE && addr < MACH_VIRTIO1_BASE + 0x1000) {
    return virtio_blk_bus(&m->vblk0, addr - MACH_VIRTIO1_BASE, data, store, width);
  } else if (addr >= MACH_VIRTIO2_BASE && addr < MACH_VIRTIO2_BASE + 0x1000) {
    return virtio_rng_bus(&m->vrng0, addr - MACH_VIRTIO2_BASE, data, store, width);
  } else {
    return RV_BAD;
  }
}

/* uart0 I/O callback */
rv_res uart0_io(void *user, u8 *byte, u32 write) {
  int ch;
  static int thrott = 0; /* prevent getch() from being called too much */
  (void)user;
  if (write && *byte != '\r') /* curses bugs out if we echo '\r' */
    echochar(*byte);
  else if (!write && ((thrott = (thrott + 1) & 0xFFF) || (ch = getch()) == ERR))
    return RV_BAD;
  else if (!write)
    *byte = (u8)ch;
  return RV_OK;
}

/* uart1 I/O callback */
rv_res uart1_io(void *user, u8 *byte, u32 write) {
  (void)user, (void)byte, (void)write;
  /* your very own uart, do whatever you want with it! */
  return RV_BAD; /* stubbed for now */
}

/* dumb bootrom */
void load(const char *path, u8 *buf, u32 max_size) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("unable to load file %s\n", path);
    exit(EXIT_FAILURE);
  }
  fread(buf, 1, max_size, f);
  fclose(f);
}

/* initialize machine */
void mach_init(mach *m, rv *cpu) {
  memset(m, 0, sizeof(mach));
  m->cpu = cpu;
  m->ram = malloc(MACH_RAM_SIZE);
  memset(m->ram, 0, MACH_RAM_SIZE);

  /* peripheral setup */
  rv_init(cpu, m, &mach_bus);
  rv_plic_init(&m->plic0);
  rv_clint_init(&m->clint0, cpu);
  rv_uart_init(&m->uart0, NULL, &uart0_io);
  rv_uart_init(&m->uart1, m, &uart1_io);
  rv_rtc_init(&m->rtc0);
  virtio_net_init(&m->vnet0, m, 3);
  virtio_blk_init(&m->vblk0, m, 4, NULL);
  virtio_rng_init(&m->vrng0, m, 5);
}

/* deinitialize machine */
void mach_deinit(mach *m) {
  if (m->ram) {
    free(m->ram);
    m->ram = NULL;
  }
  virtio_net_deinit(&m->vnet0);
  virtio_blk_deinit(&m->vblk0);
  virtio_rng_deinit(&m->vrng0);
}

/* set up machine for boot */
void mach_set(mach *m, const char *firmware, const char *dtb) {
  /* load kernel and dtb */
  load(firmware, m->ram, MACH_RAM_SIZE);
  load(dtb, m->ram + MACH_DTB_OFFSET, MACH_RAM_SIZE - MACH_DTB_OFFSET);

  /* the bootloader and linux expect the following: */
  m->cpu->r[10] /* a0 */ = 0;                               /* hartid */
  m->cpu->r[11] /* a1 */ = MACH_RAM_BASE + MACH_DTB_OFFSET; /* dtb ptr */
}

/* set disk image path */
void mach_set_disk(mach *m, const char *disk_path) {
  virtio_blk_deinit(&m->vblk0);
  virtio_blk_init(&m->vblk0, m, 4, disk_path);
}

/* run one machine cycle */
void mach_step(mach *m, u32 *rtc_period) {
  u32 irq = 0;

  /* update RTC */
  if (!(*rtc_period = (*rtc_period + 1) & 0xFFF))
    if (!++m->cpu->csr.mtime)
      m->cpu->csr.mtimeh++;

  /* execute one instruction */
  rv_step(m->cpu);

  /* update peripherals and interrupts */
  if (rv_uart_update(&m->uart0))
    rv_plic_irq(&m->plic0, 1);
  if (rv_uart_update(&m->uart1))
    rv_plic_irq(&m->plic0, 2);
  
  rv_rtc_update(&m->rtc0);
  virtio_net_update(&m->vnet0);
  virtio_blk_update(&m->vblk0);
  virtio_rng_update(&m->vrng0);

  irq = RV_CSI * rv_clint_msi(&m->clint0, 0) |
        RV_CTI * rv_clint_mti(&m->clint0, 0) |
        RV_CEI * rv_plic_mei(&m->plic0, 0);
  rv_irq(m->cpu, irq);
}
