#ifndef OTA_BOOTLOADER_H
#define OTA_BOOTLOADER_H

#include "stm32g0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  OTA_BOOT_STATE_IDLE = 0,
  OTA_BOOT_STATE_ERASING = 1,
  OTA_BOOT_STATE_RECEIVING = 2,
  OTA_BOOT_STATE_VERIFYING = 3,
  OTA_BOOT_STATE_COMPLETE = 4,
  OTA_BOOT_STATE_ERROR = 5
} OTA_BootState;

bool OTA_Bootloader_Init(UART_HandleTypeDef *uart);
void OTA_Bootloader_Poll(void);
bool OTA_Bootloader_IsApplicationValid(void);
bool OTA_Bootloader_IsTransferActive(void);
uint32_t OTA_Bootloader_GetCurrentVersion(void);
void OTA_Bootloader_JumpToApplication(void);

#endif /* OTA_BOOTLOADER_H */
