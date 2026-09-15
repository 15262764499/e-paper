#include "calendar_service.h"

#include "aht20.h"
#include "calendar_flash.h"
#include "calendar_overlay.h"
#include "gdem042f86.h"
#include "main.h"
#include "rtc_calendar.h"

#include <string.h>

#define EPD_POWER_OFF_MS        200U
#define EPD_POWER_STABILIZE_MS  300U
#define EPD_RESET_RELEASE_MS    20U
#define CALENDAR_RETRY_MS       60000U
#define SENSOR_REFRESH_MS       300000U

static I2C_HandleTypeDef *s_sensor_i2c;
static bool s_refresh_pending;
static uint32_t s_last_refresh_attempt;
static uint32_t s_last_sensor_refresh;

static void PanelOff(void)
{
  (void)GDEM042F86_StreamAbort();
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
}

static bool PanelPrepare(void)
{
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
  HAL_Delay(EPD_POWER_OFF_MS);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(EPD_POWER_STABILIZE_MS);
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(EPD_RESET_RELEASE_MS);
  return (GDEM042F86_Wake(false) == GDEM042F86_OK)
         && (GDEM042F86_StreamBegin() == GDEM042F86_OK);
}

bool CalendarService_Init(I2C_HandleTypeDef *sensor_i2c)
{
  RTC_CalendarDateTime now;

  if (sensor_i2c == NULL)
  {
    return false;
  }
  s_sensor_i2c = sensor_i2c;
  s_refresh_pending = false;
  s_last_refresh_attempt = HAL_GetTick() - CALENDAR_RETRY_MS;
  s_last_sensor_refresh = HAL_GetTick() - SENSOR_REFRESH_MS;
  if (RTC_Calendar_IsCalendarActive() && CalendarFlash_IsValid()
      && RTC_Calendar_Get(&now)
      && (RTC_Calendar_GetLastRenderedDate()
          != RTC_Calendar_DateCode(&now)))
  {
    s_refresh_pending = true;
  }
  return true;
}

bool CalendarService_RenderNow(void)
{
  CalendarFlash_Metadata metadata;
  CalendarOverlay_Context overlay = {0};
  AHT20_Measurement measurement = {0};
  const uint8_t *frame;
  uint8_t line[CALENDAR_OVERLAY_LINE_BYTES];
  uint16_t y;
  GDEM042F86_Status epd_status;

  s_last_refresh_attempt = HAL_GetTick();
  s_refresh_pending = true;

  if ((s_sensor_i2c == NULL) || !CalendarFlash_GetMetadata(&metadata)
      || !RTC_Calendar_Get(&overlay.date_time))
  {
    return false;
  }
  overlay.temperature_valid =
    AHT20_ReadMeasurement(s_sensor_i2c, &measurement) == AHT20_OK;
  overlay.humidity_valid = overlay.temperature_valid;
  if (overlay.temperature_valid)
  {
    overlay.temperature_centi_c = measurement.temperature_centi_c;
    overlay.humidity_centi_percent = measurement.humidity_centi_percent;
  }
  overlay.android_content_stale =
    metadata.base_date != RTC_Calendar_DateCode(&overlay.date_time);

  if (!PanelPrepare())
  {
    PanelOff();
    return false;
  }
  frame = CalendarFlash_FrameAddress();
  for (y = 0U; y < GDEM042F86_HEIGHT; ++y)
  {
    memcpy(line, &frame[(uint32_t)y * CALENDAR_OVERLAY_LINE_BYTES],
           sizeof(line));
    CalendarOverlay_ApplyLine(line, y, &overlay);
    if (GDEM042F86_StreamWrite(line, sizeof(line)) != GDEM042F86_OK)
    {
      PanelOff();
      return false;
    }
  }
  epd_status = GDEM042F86_StreamCommit();
  if (epd_status == GDEM042F86_OK)
  {
    epd_status = GDEM042F86_Sleep();
  }
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
  if (epd_status != GDEM042F86_OK)
  {
    PanelOff();
    return false;
  }

  RTC_Calendar_SetLastRenderedDate(
    RTC_Calendar_DateCode(&overlay.date_time));
  s_refresh_pending = false;
  s_last_sensor_refresh = HAL_GetTick();
  return true;
}

void CalendarService_Poll(bool panel_available)
{
  RTC_Calendar_Poll();
  if (RTC_Calendar_ConsumeDateChange()
      && RTC_Calendar_IsCalendarActive())
  {
    s_refresh_pending = true;
  }
  if (RTC_Calendar_IsCalendarActive() && CalendarFlash_IsValid()
      && ((uint32_t)(HAL_GetTick() - s_last_sensor_refresh)
          >= SENSOR_REFRESH_MS))
  {
    s_refresh_pending = true;
  }
  if (panel_available && RTC_Calendar_IsCalendarActive()
      && s_refresh_pending && CalendarFlash_IsValid()
      && ((uint32_t)(HAL_GetTick() - s_last_refresh_attempt)
          >= CALENDAR_RETRY_MS))
  {
    (void)CalendarService_RenderNow();
  }
}

void CalendarService_SetActive(bool active)
{
  RTC_Calendar_SetCalendarActive(active);
  if (!active)
  {
    s_refresh_pending = false;
  }
}
