#include "rtc_calendar.h"

#include <string.h>

#define RTC_CALENDAR_MAGIC              0x43414C31U /* CAL1 */
#define RTC_CALENDAR_ACTIVE_MAGIC       0x41435431U /* ACT1 */
#define RTC_CALENDAR_POLL_INTERVAL_MS   1000U
#define UNIX_DAYS_AT_2000_01_01         10957LL
#define UNIX_SECONDS_2000_01_01         946684800ULL
#define UNIX_SECONDS_2100_01_01         4102444800ULL

static RTC_HandleTypeDef *s_rtc;
static bool s_date_changed;
static uint32_t s_last_poll_tick;
static uint32_t s_last_date_code;

static bool IsLeapYear(uint16_t year)
{
  return ((year % 4U) == 0U)
         && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint8_t DaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days[] = {
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
  };

  if ((month == 0U) || (month > 12U))
  {
    return 0U;
  }
  if ((month == 2U) && IsLeapYear(year))
  {
    return 29U;
  }
  return days[month - 1U];
}

static bool UnixToLocal(uint64_t epoch_seconds, int16_t utc_offset_minutes,
                        RTC_CalendarDateTime *date_time)
{
  int64_t local_seconds;
  int64_t days;
  uint32_t seconds_of_day;
  uint16_t year = 1970U;
  uint8_t month = 1U;

  if ((date_time == NULL) || (epoch_seconds < UNIX_SECONDS_2000_01_01)
      || (epoch_seconds >= UNIX_SECONDS_2100_01_01)
      || (utc_offset_minutes < -840) || (utc_offset_minutes > 840))
  {
    return false;
  }

  local_seconds = (int64_t)epoch_seconds
                  + ((int64_t)utc_offset_minutes * 60LL);
  if ((local_seconds < (int64_t)UNIX_SECONDS_2000_01_01)
      || (local_seconds >= (int64_t)UNIX_SECONDS_2100_01_01))
  {
    return false;
  }

  days = local_seconds / 86400LL;
  seconds_of_day = (uint32_t)(local_seconds % 86400LL);
  while (year < 2100U)
  {
    uint16_t year_days = IsLeapYear(year) ? 366U : 365U;
    if (days < year_days)
    {
      break;
    }
    days -= year_days;
    ++year;
  }
  while (month <= 12U)
  {
    uint8_t month_days = DaysInMonth(year, month);
    if (days < month_days)
    {
      break;
    }
    days -= month_days;
    ++month;
  }

  date_time->year = year;
  date_time->month = month;
  date_time->day = (uint8_t)days + 1U;
  /* 1970-01-01 was Thursday; STM32 uses Monday=1. */
  date_time->weekday = (uint8_t)(((local_seconds / 86400LL + 3LL) % 7LL)
                                 + 1LL);
  date_time->hour = (uint8_t)(seconds_of_day / 3600U);
  date_time->minute = (uint8_t)((seconds_of_day % 3600U) / 60U);
  date_time->second = (uint8_t)(seconds_of_day % 60U);
  return true;
}

uint32_t RTC_Calendar_DateCode(const RTC_CalendarDateTime *date_time)
{
  if (date_time == NULL)
  {
    return 0U;
  }
  return ((uint32_t)date_time->year * 10000U)
         + ((uint32_t)date_time->month * 100U) + date_time->day;
}

bool RTC_Calendar_Init(RTC_HandleTypeDef *rtc)
{
  RTC_CalendarDateTime now;

  if (rtc == NULL)
  {
    return false;
  }
  s_rtc = rtc;
  s_date_changed = false;
  s_last_poll_tick = HAL_GetTick();
  s_last_date_code = 0U;
  if (RTC_Calendar_IsValid() && RTC_Calendar_Get(&now))
  {
    s_last_date_code = RTC_Calendar_DateCode(&now);
  }
  return true;
}

bool RTC_Calendar_IsValid(void)
{
  return (s_rtc != NULL)
         && (HAL_RTCEx_BKUPRead(s_rtc, RTC_BKP_DR0) == RTC_CALENDAR_MAGIC);
}

bool RTC_Calendar_SetUnix(uint64_t epoch_seconds, int16_t utc_offset_minutes)
{
  RTC_CalendarDateTime local;
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};

  if ((s_rtc == NULL)
      || !UnixToLocal(epoch_seconds, utc_offset_minutes, &local))
  {
    return false;
  }

  time.Hours = local.hour;
  time.Minutes = local.minute;
  time.Seconds = local.second;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;
  date.WeekDay = local.weekday;
  date.Month = local.month;
  date.Date = local.day;
  date.Year = (uint8_t)(local.year - 2000U);
  if ((HAL_RTC_SetTime(s_rtc, &time, RTC_FORMAT_BIN) != HAL_OK)
      || (HAL_RTC_SetDate(s_rtc, &date, RTC_FORMAT_BIN) != HAL_OK))
  {
    return false;
  }

  HAL_RTCEx_BKUPWrite(s_rtc, RTC_BKP_DR0, RTC_CALENDAR_MAGIC);
  s_last_date_code = RTC_Calendar_DateCode(&local);
  /* TIME_SYNC is normally followed by a new template.  Daily changes are
     detected by Poll; do not refresh a previously stored template here. */
  s_date_changed = false;
  return true;
}

bool RTC_Calendar_Get(RTC_CalendarDateTime *date_time)
{
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};

  if ((s_rtc == NULL) || (date_time == NULL) || !RTC_Calendar_IsValid())
  {
    return false;
  }
  /* STM32 requires reading time before date to unlock the shadow registers. */
  if ((HAL_RTC_GetTime(s_rtc, &time, RTC_FORMAT_BIN) != HAL_OK)
      || (HAL_RTC_GetDate(s_rtc, &date, RTC_FORMAT_BIN) != HAL_OK))
  {
    return false;
  }
  date_time->year = (uint16_t)date.Year + 2000U;
  date_time->month = date.Month;
  date_time->day = date.Date;
  date_time->weekday = date.WeekDay;
  date_time->hour = time.Hours;
  date_time->minute = time.Minutes;
  date_time->second = time.Seconds;
  return true;
}

void RTC_Calendar_Poll(void)
{
  RTC_CalendarDateTime now;
  uint32_t date_code;

  if ((uint32_t)(HAL_GetTick() - s_last_poll_tick)
      < RTC_CALENDAR_POLL_INTERVAL_MS)
  {
    return;
  }
  s_last_poll_tick = HAL_GetTick();
  if (!RTC_Calendar_Get(&now))
  {
    return;
  }
  date_code = RTC_Calendar_DateCode(&now);
  if ((s_last_date_code != 0U) && (date_code != s_last_date_code))
  {
    s_date_changed = true;
  }
  s_last_date_code = date_code;
}

bool RTC_Calendar_ConsumeDateChange(void)
{
  bool changed = s_date_changed;
  s_date_changed = false;
  return changed;
}

uint32_t RTC_Calendar_GetLastRenderedDate(void)
{
  if (s_rtc == NULL)
  {
    return 0U;
  }
  return HAL_RTCEx_BKUPRead(s_rtc, RTC_BKP_DR1);
}

void RTC_Calendar_SetLastRenderedDate(uint32_t date_code)
{
  if (s_rtc != NULL)
  {
    HAL_RTCEx_BKUPWrite(s_rtc, RTC_BKP_DR1, date_code);
  }
}

bool RTC_Calendar_IsCalendarActive(void)
{
  return (s_rtc != NULL)
         && (HAL_RTCEx_BKUPRead(s_rtc, RTC_BKP_DR2)
             == RTC_CALENDAR_ACTIVE_MAGIC);
}

void RTC_Calendar_SetCalendarActive(bool active)
{
  if (s_rtc != NULL)
  {
    HAL_RTCEx_BKUPWrite(s_rtc, RTC_BKP_DR2,
                        active ? RTC_CALENDAR_ACTIVE_MAGIC : 0U);
  }
}
