/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* Main-context controls: quiesce BLE traffic before sleep; wake waits 500 ms. */
void Bluetooth_Sleep(void);
void Bluetooth_Wake(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BT_RXD_Pin GPIO_PIN_2
#define BT_RXD_GPIO_Port GPIOA
#define BT_TXD_Pin GPIO_PIN_3
#define BT_TXD_GPIO_Port GPIOA
#define EPD_CLK_Pin GPIO_PIN_5
#define EPD_CLK_GPIO_Port GPIOA
#define EPD_RST_Pin GPIO_PIN_6
#define EPD_RST_GPIO_Port GPIOA
#define EPD_MOSI_Pin GPIO_PIN_7
#define EPD_MOSI_GPIO_Port GPIOA
#define EPD_CS_Pin GPIO_PIN_0
#define EPD_CS_GPIO_Port GPIOB
#define EPD_BUSY_Pin GPIO_PIN_1
#define EPD_BUSY_GPIO_Port GPIOB
#define EPD_DC_Pin GPIO_PIN_2
#define EPD_DC_GPIO_Port GPIOB
#define EPD_POWER_EN_Pin GPIO_PIN_11
#define EPD_POWER_EN_GPIO_Port GPIOB
#define MODE_SWICH_Pin GPIO_PIN_12
#define MODE_SWICH_GPIO_Port GPIOB
#define BT_SLP_Pin GPIO_PIN_15
#define BT_SLP_GPIO_Port GPIOB
#define CH340_RXD_Pin GPIO_PIN_9
#define CH340_RXD_GPIO_Port GPIOA
#define CH340_TXD_Pin GPIO_PIN_10
#define CH340_TXD_GPIO_Port GPIOA
#define NFC_SDA_AHD_SDA_Pin GPIO_PIN_7
#define NFC_SDA_AHD_SDA_GPIO_Port GPIOB
#define NFC_SCL_AHD_SCL_Pin GPIO_PIN_8
#define NFC_SCL_AHD_SCL_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
