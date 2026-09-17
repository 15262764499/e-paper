#ifndef LED_595_H
#define LED_595_H

#include "stm32g0xx_hal.h"
#include "led_effect.h"
#include <stdbool.h>
#include <stdint.h>

#define LED595_BREATH_PERIOD_MS 3000U

bool LED595_Init(void);
typedef struct
{
  uint8_t mode;
  uint8_t effective_mode; /* 3 = temporary NFC override; 4 = computer. */
  bool humidity_valid;
  uint32_t humidity;
  uint32_t elapsed_ms;
  uint8_t duty[8];
} LED595_Status;
bool LED595_SetMode(LED_Mode mode);
/* Main context; caller validates Pulse parameter ranges. */
void LED595_SetComputer(uint16_t value, uint8_t brightness, uint8_t speed);
void LED595_StopComputer(void);
void LED595_GetStatus(LED595_Status *status);
void LED595_AttachI2C(I2C_HandleTypeDef *i2c);
/* Main context: RF detection and a sensor sample every 2 s in humidity mode. */
void LED595_Poll(void);
/* Also serviced during main-context HAL_Delay, including panel refresh. */
void LED595_PollNfc(void);
/* Called from the 1 ms SysTick; no SPI, delay or blocking HAL calls. */
void LED595_Tick(void);
void LED595_TimerIRQ(void);

#endif
