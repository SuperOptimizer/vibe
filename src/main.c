#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <curses.h>
#include <getopt.h>
#include <string.h>

#include "vibe.h"

/* Global debug level */
unsigned int debug_level = 0;

int main(int argc, const char *const *argv) {
  mach m;
  u32 rtc_period = 0;
  size_t ninst = 1000000000, ctr = 0; /* 1 billion instructions */
  const char *firmware = NULL;
  const char *dtb = NULL;
  const char *disk = NULL;

  if (argc == 1 || (argc == 2 && debug_level)) {
    firmware = "../linux/fw_payload.bin";
    dtb = "../linux/vibe.dtb";
    disk = "../linux/debian_rootfs.ext2";
  } else if (argc < 3) {
    printf("Usage: %s [--debug=<level>] [firmware dtb [disk] [ninst]]\n", argv[0]);
    printf("Debug levels: all, cpu, mem, virtio, inst, or hex value\n");
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

  mach_init(&m);
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
  size_t last_print = 0;
  do {
    mach_step(&m, &rtc_period);
    if (ctr > 0 && ctr % 100000000 == 0 && ctr != last_print) {
      fprintf(stderr, "Executed %zu million instructions\n", ctr / 1000000);
      last_print = ctr;
    }
  } while (!ninst || ctr++ < ninst);

  /* cleanup */
  endwin();
  mach_deinit(&m);

  return EXIT_SUCCESS;
}