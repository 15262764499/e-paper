#include "../led_stubs/stm32g0xx_hal.h"
#ifndef PULSE_UART_STUBS
#define PULSE_UART_STUBS
typedef struct { uint32_t RxState; } UART_HandleTypeDef;
#define HAL_UART_STATE_READY 0U
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
#endif
