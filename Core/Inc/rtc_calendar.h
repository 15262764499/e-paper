#ifndef RTC_CALENDAR_H
#define RTC_CALENDAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t weekday; /* 1=Monday ... 7=Sunday */
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} RTC_CalendarDateTime;

bool RTC_Calendar_Init(RTC_HandleTypeDef *rtc);
bool RTC_Calendar_IsValid(void);
bool RTC_Calendar_SetUnix(uint64_t epoch_seconds, int16_t utc_offset_minutes);
bool RTC_Calendar_Get(RTC_CalendarDateTime *date_time);
void RTC_Calendar_Poll(void);
bool RTC_Calendar_ConsumeDateChange(void);
uint32_t RTC_Calendar_DateCode(const RTC_CalendarDateTime *date_time);
uint32_t RTC_Calendar_GetLastRenderedDate(void);
void RTC_Calendar_SetLastRenderedDate(uint32_t date_code);
bool RTC_Calendar_IsCalendarActive(void);
void RTC_Calendar_SetCalendarActive(bool active);

#ifdef __cplusplus
}
#endif

#endif /* RTC_CALENDAR_H */
