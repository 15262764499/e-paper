#ifndef PULSE_UART_H
#define PULSE_UART_H
#include "stm32g0xx_hal.h"
#include <stdbool.h>
bool PulseUART_Init(UART_HandleTypeDef *uart);
void PulseUART_Poll(void);
void PulseUART_OnRx(UART_HandleTypeDef *uart);
void PulseUART_OnError(UART_HandleTypeDef *uart);
#endif
