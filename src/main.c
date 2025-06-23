#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <curses.h>
#include <getopt.h>
#include <string.h>

#include "rv.h"
#include "mach.h"

/* Global debug level */
unsigned int debug_level = 0;

int main(int argc, const char *const *argv) {
  rv cpu;
  mach m;
  u32 rtc_period = 0;
  size_t ninst = 1000000000, ctr = 0;  /* 1 billion instructions */
  const char *firmware = NULL;
  const char *dtb = NULL;
  const char *disk = NULL;


  if (argc == 1 || (argc == 2 && debug_level)) {
    firmware = "../linux/fw_payload.bin";
    dtb = "../linux/vibe.dtb";
    disk = "../linux/bookworm.img";
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

  mach_init(&m, &cpu);
  mach_set(&m, firmware, dtb);


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
    if (ctr > 0 && ctr % 10000000 == 0 && ctr != last_print) {
      fprintf(stderr, "Executed %zu million instructions\n", ctr / 1000000);
      last_print = ctr;
    }
    
    /* Track if we're stuck */
    static u32 pc_history[1000];
    static int pc_idx = 0;
    pc_history[pc_idx] = cpu.pc;
    pc_idx = (pc_idx + 1) % 1000;
    
    /* Check if all recent PCs are the same small set */
    if (ctr > 100000000 && ctr % 1000000 == 0) {
      u32 unique_pcs = 0;
      u32 seen[10] = {0};
      for (int i = 0; i < 1000; i++) {
        int found = 0;
        for (int j = 0; j < unique_pcs && j < 10; j++) {
          if (seen[j] == pc_history[i]) {
            found = 1;
            break;
          }
        }
        if (!found && unique_pcs < 10) {
          seen[unique_pcs++] = pc_history[i];
        }
      }
      if (unique_pcs < 5) {
        fprintf(stderr, "Detected loop with %d unique PCs at %zu instructions: ", unique_pcs, ctr);
        for (int i = 0; i < unique_pcs; i++) {
          fprintf(stderr, "0x%08x ", seen[i]);
        }
        fprintf(stderr, "\n");
      }
    }
  } while (!ninst || ctr++ < ninst);

  /* cleanup */
  endwin();
  mach_deinit(&m);

  return EXIT_SUCCESS;
}