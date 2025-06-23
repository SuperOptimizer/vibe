#pragma once

#include <time.h>

#define RV_RTC_SIZE 0x10000

struct rv_rtc {
  mach* mach;
  u32 time_low;
  u32 time_high;
  u32 alarm_low;
  u32 alarm_high;
  u32 ctrl;
  u32 status;
  time_t base_time;
};

void rv_rtc_init(rv_rtc *rtc);

rv_res rv_rtc_bus(rv_rtc *rtc, u32 addr, u8 *data, bool is_store, u32 width);

void rv_rtc_update(rv_rtc *rtc);