#include "vibe.h"

void rv_rtc_init(rv_rtc *rtc) {
  memset(rtc, 0, sizeof(*rtc));
  rtc->base_time = time(NULL);
}

rv_res rv_rtc_bus(rv_rtc *rtc, u32 addr, u8 *data, bool is_store, u32 width) {
  addr &= 0xFFFF;
  u32 value = 0;

  if (width != 4)
    return RV_BAD_ALIGN;

  if (is_store) {
    rv_endcvt(data, (u8 *)&value, 4, 0);

    switch (addr) {
    case 0x00:
      rtc->time_low = value;
      break;
    case 0x04:
      rtc->time_high = value;
      break;
    case 0x08:
      rtc->alarm_low = value;
      break;
    case 0x0C:
      rtc->alarm_high = value;
      break;
    case 0x10:
      rtc->ctrl = value;
      break;
    case 0x14:
      rtc->status &= ~value;
      break;
    default:
      break;
    }
  } else {
    switch (addr) {
    case 0x00:
      value = rtc->time_low;
      break;
    case 0x04:
      value = rtc->time_high;
      break;
    case 0x08:
      value = rtc->alarm_low;
      break;
    case 0x0C:
      value = rtc->alarm_high;
      break;
    case 0x10:
      value = rtc->ctrl;
      break;
    case 0x14:
      value = rtc->status;
      break;
    default:
      break;
    }

    rv_endcvt((u8 *)&value, data, 4, 1);
  }

  return RV_OK;
}

void rv_rtc_update(rv_rtc *rtc) {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  u64 current_time = (u64)(tv.tv_sec - rtc->base_time) * 1000000ULL + tv.tv_usec;

  rtc->time_low = (u32)(current_time & 0xFFFFFFFF);
  rtc->time_high = (u32)(current_time >> 32);

  if (rtc->ctrl & 1) {
    u64 alarm = ((u64)rtc->alarm_high << 32) | rtc->alarm_low;
    if (current_time >= alarm) {
      rtc->status |= 1;
    }
  }
}