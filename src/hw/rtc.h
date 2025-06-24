#pragma once

#define hw_rtc_SIZE 0x10000

struct hw_rtc {
  mach* mach;
  u32 time_low;
  u32 time_high;
  u32 alarm_low;
  u32 alarm_high;
  u32 ctrl;
  u32 status;
  time_t base_time;
};

void hw_rtc_init(hw_rtc *rtc);
bus_error hw_rtc_bus(hw_rtc *rtc, u32 addr, u8 *data, bool is_store, u32 width);
void hw_rtc_update(hw_rtc *rtc);
