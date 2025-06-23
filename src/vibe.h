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

typedef struct rv_clint rv_clint;
typedef struct rv_plic rv_plic;
typedef struct rv_rtc rv_rtc;
typedef struct rv_uart rv_uart;
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

#include "mach.h"