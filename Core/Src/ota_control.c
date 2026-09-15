#include "ota_control.h"

#include "ota_layout.h"

static RTC_HandleTypeDef *s_ota_rtc;

bool OTA_Control_Init(RTC_HandleTypeDef *rtc)
{
  if ((rtc == NULL) || (rtc->Instance != RTC))
  {
    return false;
  }
  s_ota_rtc = rtc;
  return true;
}

bool OTA_Control_RequestBootloader(void)
{
  if (s_ota_rtc == NULL)
  {
    return false;
  }

  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(s_ota_rtc, RTC_BKP_DR3, OTA_BOOT_REQUEST_MAGIC);
  return HAL_RTCEx_BKUPRead(s_ota_rtc, RTC_BKP_DR3)
         == OTA_BOOT_REQUEST_MAGIC;
}
