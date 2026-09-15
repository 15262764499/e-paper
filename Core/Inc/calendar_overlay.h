#ifndef CALENDAR_OVERLAY_H
#define CALENDAR_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtc_calendar.h"

#include <stdbool.h>
#include <stdint.h>

#define CALENDAR_OVERLAY_LINE_BYTES 100U

typedef struct
{
  RTC_CalendarDateTime date_time;
  int32_t temperature_centi_c;
  uint32_t humidity_centi_percent;
  bool temperature_valid;
  bool humidity_valid;
  bool android_content_stale;
} CalendarOverlay_Context;

void CalendarOverlay_ApplyLine(uint8_t line[CALENDAR_OVERLAY_LINE_BYTES],
                               uint16_t y,
                               const CalendarOverlay_Context *context);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_OVERLAY_H */
