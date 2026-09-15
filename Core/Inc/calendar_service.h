#ifndef CALENDAR_SERVICE_H
#define CALENDAR_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

#include <stdbool.h>

bool CalendarService_Init(I2C_HandleTypeDef *sensor_i2c);
bool CalendarService_RenderNow(void);
void CalendarService_Poll(bool panel_available);
void CalendarService_SetActive(bool active);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_SERVICE_H */
