#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef struct hw_clint hw_clint;
typedef struct hw_plic hw_plic;
typedef struct hw_rtc hw_rtc;
typedef struct hw_uart hw_uart;
typedef struct hw_virtio hw_virtio;
typedef struct hw_virtio_blk hw_virtio_blk;
typedef struct hw_virtio_net hw_virtio_net;
typedef struct hw_virtio_rng hw_virtio_rng;
typedef struct mach mach;
typedef struct rv rv;

typedef enum {
  BUS_OK = 0,          /* Successful bus operation */
  BUS_UNMAPPED = 1,    /* Address not mapped to any device */
  BUS_INVALID = 2,     /* Invalid operation (e.g., unsupported width) */
  BUS_ALIGN = 3,       /* Misaligned access */
} bus_error;

#include "rv.h"
#include "hw/uart.h"
#include "hw/rtc.h"
#include "hw/plic.h"
#include "hw/clint.h"
#include "hw/virtio.h"
#include "hw/virtio_blk.h"
#include "hw/virtio_net.h"
#include "hw/virtio_rng.h"

#include "mach.h"