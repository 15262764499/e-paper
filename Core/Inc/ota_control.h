#ifndef OTA_CONTROL_H
#define OTA_CONTROL_H

#include "stm32g0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

bool OTA_Control_Init(RTC_HandleTypeDef *rtc);
bool OTA_Control_RequestBootloader(void);

#endif /* OTA_CONTROL_H */
