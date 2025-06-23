#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <curses.h>

#include "rv.h"
#include "mach.h"

int main(int argc, const char *const *argv) {
  rv cpu;
  mach m;
  u32 rtc_period = 0;
  size_t ninst = 0, ctr = 0;
  const char *firmware = NULL;
  const char *dtb = NULL;
  const char *disk = NULL;

  if (argc == 1) {
    firmware = "../linux/fw_payload.bin";
    dtb = "../linux/vibe.dtb";
    disk = "../linux/rootfs.ext2";
  } else if (argc < 3) {
    printf("Usage: %s [firmware dtb [disk] [ninst]]\n", argv[0]);
    exit(EXIT_FAILURE);
  } else {
    firmware = argv[1];
    dtb = argv[2];
    if (argc >= 4) {
      disk = argv[3];
    }
    if (argc >= 5) {
      ninst = (size_t)atol(argv[4]);
    }
  }

  mach_init(&m, &cpu);
  mach_set(&m, firmware, dtb);
  if (disk) {
    mach_set_disk(&m, disk);
  }

  /* ncurses setup */
  initscr();              /* initialize screen */
  cbreak();               /* don't buffer input chars */
  noecho();               /* don't echo input chars */
  scrollok(stdscr, TRUE); /* allow the screen to autoscroll */
  nodelay(stdscr, TRUE);  /* enable nonblocking input */

  /* main emulation loop */
  do {
    mach_step(&m, &rtc_period);
  } while (!ninst || ctr++ < ninst);

  /* cleanup */
  endwin();
  mach_deinit(&m);

  return EXIT_SUCCESS;
}