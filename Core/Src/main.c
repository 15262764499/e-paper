/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "aht20.h"
#include "led_595.h"
#include "calendar_service.h"
#include "gdem042f86.h"
#include "image_transfer.h"
#include "ota_control.h"
#include "pairing_security.h"
#include "rtc_calendar.h"

#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BT_SLEEP_STATE GPIO_PIN_RESET
#define BT_AWAKE_STATE GPIO_PIN_SET
#define BT_SLEEP_SETTLE_MS 20U
#define BT_STARTUP_DELAY_MS 500U
#define ST25DV04K_USER_ADDRESS (0x53U << 1U)
#define ST25DV04K_SYSTEM_ADDRESS (0x57U << 1U)
#define ST25DV04K_IDENTITY_ADDRESS 0x0014U
#define ST25DV04K_IDENTITY_SIZE 13U
#define ST25DV04K_MB_CTRL_DYN_ADDRESS 0x2006U
#define ST25DV04K_MB_EN_MASK 0x01U
#define ST25DV04K_EEPROM_PAGE_SIZE 4U
#define ST25DV04K_I2C_TIMEOUT_MS 100U
#define ST25DV04K_WRITE_TIMEOUT_MS 25U
#define NFC_PAIRING_RETRY_MS 10000U
#define AHT20_POWER_STABILIZE_MS 100U
#define NDEF_CC_SIZE 4U
#define NDEF_MAX_TEXT_SIZE 64U
#define NDEF_MAX_IMAGE_SIZE 96U
#define BT_HEARTBEAT_INTERVAL_MS 1000U
#define EPD_DIAGNOSTIC_MODE 1U
#define EPD_POWER_OFF_TIME_MS 200U
#define EPD_POWER_STABILIZE_MS 300U
#define EPD_HIGH_VOLTAGE_HOLD_MS 30000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */
static bool s_pairing_ndef_ready;
static uint32_t s_last_pairing_ndef_attempt;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */
static void Bluetooth_Enable(void);
static void Bluetooth_BridgePoll(void);
static void Bluetooth_TestPoll(void) __attribute__((unused));
static void SensorNDEF_Demo(void) __attribute__((unused));
static bool PairingNDEF_Init(void);
static bool NFC_BuildPairingNdef(uint8_t *image, uint16_t capacity,
                                 uint16_t *image_size);
static void Debug_LogHex(const char *prefix, const uint8_t *data,
                         uint16_t length);
static void EPD_Demo(void) __attribute__((unused));
static void EPD_Log(const char *message);
static void EPD_LogStatus(const char *operation, GDEM042F86_Status status);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  if (!LED595_Init())
  {
    Error_Handler();
  }
  MX_RTC_Init();
  /* NFC, AHT20 and I2C pull-ups now use the always-on 3V3_SYS rail. */
  HAL_Delay(AHT20_POWER_STABILIZE_MS);
  MX_DMA_Init();
  MX_I2C1_Init();
  LED595_AttachI2C(&hi2c1);
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  if (!RTC_Calendar_Init(&hrtc))
  {
    Error_Handler();
  }
  if (!OTA_Control_Init(&hrtc))
  {
    Error_Handler();
  }
  Bluetooth_Enable();
  s_pairing_ndef_ready = PairingNDEF_Init();
  s_last_pairing_ndef_attempt = HAL_GetTick();
  if (!ImageTransfer_Init(&huart2, &hspi1, &hi2c1)
      || !ImageTransfer_StartReceive()
      || !CalendarService_Init(&hi2c1))
  {
    EPD_Log("Image transfer init: FAIL\r\n");
    Error_Handler();
  }
  EPD_Log("NFC + authenticated BLE image transfer: ready\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    LED595_Poll();
    ImageTransfer_Poll();
    CalendarService_Poll((ImageTransfer_GetState() == IMAGE_STATE_IDLE)
                         || (ImageTransfer_GetState() == IMAGE_STATE_COMPLETE)
                         || (ImageTransfer_GetState() == IMAGE_STATE_ERROR));
    if (!s_pairing_ndef_ready
        && ((ImageTransfer_GetState() == IMAGE_STATE_IDLE)
            || (ImageTransfer_GetState() == IMAGE_STATE_COMPLETE)
            || (ImageTransfer_GetState() == IMAGE_STATE_ERROR))
        && ((uint32_t)(HAL_GetTick() - s_last_pairing_ndef_attempt)
            >= NFC_PAIRING_RETRY_MS))
    {
      s_last_pairing_ndef_attempt = HAL_GetTick();
      s_pairing_ndef_ready = PairingNDEF_Init();
    }
    HAL_Delay(1U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI
                                     | RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{
  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */
  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  /* Nominal 32 kHz LSI / (127+1) / (249+1) = 1 Hz. */
  hrtc.Init.AsynchPrediv = 127U;
  hrtc.Init.SynchPrediv = 249U;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00303D5B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  /* DMA1_Channel2_3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, EPD_CS_Pin|EPD_DC_Pin,
                    GPIO_PIN_SET);

  /* Keep the e-paper power path disabled until an authenticated image starts. */
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, BT_SLP_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : EPD_RST_Pin */
  GPIO_InitStruct.Pin = EPD_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(EPD_RST_GPIO_Port, &GPIO_InitStruct);

  /* Always-on BLE/NFC/AHT20 need no power-enable GPIOs. */
  GPIO_InitStruct.Pin = EPD_CS_Pin|EPD_POWER_EN_Pin|BT_SLP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : EPD_DC_Pin */
  GPIO_InitStruct.Pin = EPD_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(EPD_DC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : EPD_BUSY_Pin */
  GPIO_InitStruct.Pin = EPD_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(EPD_BUSY_GPIO_Port, &GPIO_InitStruct);

  /* SW2 shorts PB12 to GND; R48 is the external 10k pull-up. */
  GPIO_InitStruct.Pin = MODE_SWICH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MODE_SWICH_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
/* Keep field detection alive through long panel refresh and sensor waits.
 * NFC polling is main-context only, guarded against reentry and a busy I2C.
 * PWM/animation and the key are independently serviced by timer/SysTick. */
void HAL_Delay(uint32_t delay)
{
  uint32_t started = HAL_GetTick();
  uint32_t wait = delay;
  if (wait < HAL_MAX_DELAY) wait += (uint32_t)uwTickFreq;
  while ((uint32_t)(HAL_GetTick() - started) < wait)
  {
    LED595_PollNfc();
  }
}

void Bluetooth_Sleep(void)
{
  /* Call only after the application has finished BLE transfers. */
  HAL_GPIO_WritePin(BT_SLP_GPIO_Port, BT_SLP_Pin, BT_SLEEP_STATE);
}

void Bluetooth_Wake(void)
{
  HAL_GPIO_WritePin(BT_SLP_GPIO_Port, BT_SLP_Pin, BT_AWAKE_STATE);
  HAL_Delay(BT_STARTUP_DELAY_MS);
}

static void Bluetooth_Enable(void)
{
  /* CH9140 is directly powered; SLEEP low sleeps, high wakes. */
  Bluetooth_Sleep();
  HAL_Delay(BT_SLEEP_SETTLE_MS);
  Bluetooth_Wake();
  EPD_Log("CH9140: always powered, SLEEP HIGH, UART2 115200 8N1\r\n");
}

static void Bluetooth_BridgePoll(void)
{
  uint8_t byte;

  /* USB serial (USART1/COM3) -> Bluetooth module (USART2). */
  if (HAL_UART_Receive(&huart1, &byte, 1U, 0U) == HAL_OK)
  {
    (void)HAL_UART_Transmit(&huart2, &byte, 1U, 10U);
  }

  /* Bluetooth module (USART2) -> USB serial (USART1/COM3). */
  if (HAL_UART_Receive(&huart2, &byte, 1U, 0U) == HAL_OK)
  {
    (void)HAL_UART_Transmit(&huart1, &byte, 1U, 10U);
  }
}

static void Bluetooth_TestPoll(void)
{
  static uint32_t last_heartbeat_tick;
  static const uint8_t heartbeat[] = "CH9140 alive\r\n";
  uint32_t now = HAL_GetTick();

  /* Keep the directly powered module awake for the UART bridge demo. */
  HAL_GPIO_WritePin(BT_SLP_GPIO_Port, BT_SLP_Pin, BT_AWAKE_STATE);

  Bluetooth_BridgePoll();

  if ((uint32_t)(now - last_heartbeat_tick) >= BT_HEARTBEAT_INTERVAL_MS)
  {
    last_heartbeat_tick = now;
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)heartbeat,
                            sizeof(heartbeat) - 1U, 50U);
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)heartbeat,
                            sizeof(heartbeat) - 1U, 50U);
  }
}

static bool NFC_AddressReady(uint16_t device_address)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, device_address, 3U, 10U) == HAL_OK;
}

static bool NFC_CheckReady(void)
{
  bool user_ready = NFC_AddressReady(ST25DV04K_USER_ADDRESS);
  bool system_ready = NFC_AddressReady(ST25DV04K_SYSTEM_ADDRESS);

  EPD_Log(user_ready ? "NFC: always-on rail, user address 0x53 ACK\r\n"
                     : "NFC: user address 0x53 did not ACK\r\n");
  EPD_Log(system_ready ? "NFC: system address 0x57 ACK\r\n"
                       : "NFC: system address 0x57 did not ACK\r\n");
  return user_ready;
}

static HAL_StatusTypeDef NFC_WritePageAndWait(uint16_t memory_address,
                                               const uint8_t *data,
                                               uint16_t length)
{
  HAL_StatusTypeDef status;
  uint32_t start_tick;

  status = HAL_I2C_Mem_Write(&hi2c1, ST25DV04K_USER_ADDRESS,
                             memory_address, I2C_MEMADD_SIZE_16BIT,
                             (uint8_t *)data, length,
                             ST25DV04K_I2C_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  /* ST25DV04K NACKs every I2C request while its EEPROM write is active. */
  start_tick = HAL_GetTick();
  do
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, ST25DV04K_USER_ADDRESS,
                              1U, 2U) == HAL_OK)
    {
      return HAL_OK;
    }
  } while ((HAL_GetTick() - start_tick) < ST25DV04K_WRITE_TIMEOUT_MS);

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef NFC_WriteBytes(uint16_t memory_address,
                                        const uint8_t *data,
                                        uint16_t length)
{
  while (length > 0U)
  {
    uint16_t page_remaining = ST25DV04K_EEPROM_PAGE_SIZE
                              - (memory_address
                                 & (ST25DV04K_EEPROM_PAGE_SIZE - 1U));
    uint16_t chunk = length < page_remaining ? length : page_remaining;
    HAL_StatusTypeDef status = NFC_WritePageAndWait(memory_address, data,
                                                    chunk);

    if (status != HAL_OK)
    {
      return status;
    }
    memory_address += chunk;
    data += chunk;
    length -= chunk;
  }

  return HAL_OK;
}

static bool NFC_DisableMailbox(void)
{
  uint8_t mailbox_control;

  if (HAL_I2C_Mem_Read(&hi2c1, ST25DV04K_USER_ADDRESS,
                       ST25DV04K_MB_CTRL_DYN_ADDRESS,
                       I2C_MEMADD_SIZE_16BIT, &mailbox_control, 1U,
                       ST25DV04K_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }

  if ((mailbox_control & ST25DV04K_MB_EN_MASK) != 0U)
  {
    mailbox_control &= (uint8_t)~ST25DV04K_MB_EN_MASK;
    if (HAL_I2C_Mem_Write(&hi2c1, ST25DV04K_USER_ADDRESS,
                          ST25DV04K_MB_CTRL_DYN_ADDRESS,
                          I2C_MEMADD_SIZE_16BIT, &mailbox_control, 1U,
                          ST25DV04K_I2C_TIMEOUT_MS) != HAL_OK)
    {
      return false;
    }
  }

  return true;
}

static bool NFC_BuildTextNdef(const char *text, uint8_t *image,
                              uint16_t capacity, uint16_t *image_size)
{
  size_t text_length;
  uint16_t total_size;

  if ((text == NULL) || (image == NULL) || (image_size == NULL))
  {
    return false;
  }

  text_length = strlen(text);
  total_size = (uint16_t)(14U + text_length);
  if ((text_length > 247U) || (total_size > capacity))
  {
    return false;
  }

  image[0] = 0xE1U;
  image[1] = 0x40U;
  image[2] = 0x40U;
  image[3] = 0x00U;
  image[4] = 0x03U;
  image[5] = (uint8_t)(text_length + 7U);
  image[6] = 0xD1U;
  image[7] = 0x01U;
  image[8] = (uint8_t)(text_length + 3U);
  image[9] = 0x54U;
  image[10] = 0x02U;
  image[11] = 'e';
  image[12] = 'n';
  memcpy(&image[13], text, text_length);
  image[13U + text_length] = 0xFEU;

  *image_size = total_size;
  return true;
}

static bool NFC_BuildPairingNdef(uint8_t *image, uint16_t capacity,
                                 uint16_t *image_size)
{
  static const uint8_t mime_type[] = "application/vnd.epaper.pair";
  const uint8_t type_size = (uint8_t)(sizeof(mime_type) - 1U);
  const uint8_t payload_size = 34U;
  const uint8_t record_size = (uint8_t)(3U + type_size + payload_size);
  uint8_t board_id[IMAGE_PROTOCOL_BOARD_ID_SIZE];
  uint8_t pairing_key[PAIRING_KEY_SIZE];
  uint16_t payload_offset;
  uint16_t total_size = (uint16_t)(4U + 2U + record_size + 1U);

  if ((image == NULL) || (image_size == NULL) || (total_size > capacity))
  {
    return false;
  }

  ImageTransfer_GetBoardId(board_id);
  PairingSecurity_GetKey(pairing_key);

  image[0] = 0xE1U; /* NFC Forum Type 5 mapping, version 1.0. */
  image[1] = 0x40U; /* Read/write access. */
  image[2] = 0x40U; /* 0x40 * 8 = 512 bytes. */
  image[3] = 0x00U;
  image[4] = 0x03U; /* NDEF Message TLV. */
  image[5] = record_size;
  image[6] = 0xD2U; /* MB | ME | SR | MIME media TNF. */
  image[7] = type_size;
  image[8] = payload_size;
  memcpy(&image[9], mime_type, type_size);

  payload_offset = (uint16_t)(9U + type_size);
  memcpy(&image[payload_offset], "EPD1", 4U);
  image[payload_offset + 4U] = IMAGE_PROTOCOL_VERSION;
  memcpy(&image[payload_offset + 5U], board_id, sizeof(board_id));
  memcpy(&image[payload_offset + 17U], pairing_key, sizeof(pairing_key));
  image[payload_offset + 33U] = 0x01U; /* HMAC challenge-response required. */
  image[total_size - 1U] = 0xFEU;

  memset(pairing_key, 0, sizeof(pairing_key));
  *image_size = total_size;
  return true;
}

static bool NFC_WriteAndVerifyNdef(const uint8_t *image,
                                   uint16_t image_size)
{
  const uint8_t empty_tlv[] = {0x03U, 0x00U};
  uint8_t final_length;
  uint8_t readback[NDEF_MAX_IMAGE_SIZE];

  if ((image == NULL) || (image_size < 14U)
      || (image_size > sizeof(readback)))
  {
    return false;
  }

  if (HAL_I2C_Mem_Read(&hi2c1, ST25DV04K_USER_ADDRESS, 0x0000U,
                       I2C_MEMADD_SIZE_16BIT, readback, image_size,
                       ST25DV04K_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }
  if (memcmp(readback, image, image_size) == 0)
  {
    return true;
  }

  /* Publish an empty TLV before changing either the CC or the record. */
  if (NFC_WriteBytes(0x0004U, empty_tlv,
                     (uint16_t)sizeof(empty_tlv)) != HAL_OK)
  {
    return false;
  }

  if (memcmp(readback, image, NDEF_CC_SIZE) != 0)
  {
    if (NFC_WriteBytes(0x0000U, image, NDEF_CC_SIZE) != HAL_OK)
    {
      return false;
    }
  }
  if (NFC_WriteBytes(0x0006U, &image[6],
                     (uint16_t)(image_size - 6U)) != HAL_OK)
  {
    return false;
  }

  /* Commit the message by writing the non-zero TLV length last. */
  final_length = image[5];
  if (NFC_WriteBytes(0x0005U, &final_length, 1U) != HAL_OK)
  {
    return false;
  }

  if (HAL_I2C_Mem_Read(&hi2c1, ST25DV04K_USER_ADDRESS, 0x0000U,
                       I2C_MEMADD_SIZE_16BIT, readback, image_size,
                       ST25DV04K_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }

  return memcmp(readback, image, image_size) == 0;
}

static void Debug_LogHex(const char *prefix, const uint8_t *data,
                         uint16_t length)
{
  static const char digits[] = "0123456789ABCDEF";
  char text[4];
  uint16_t index;

  EPD_Log(prefix);
  for (index = 0U; index < length; ++index)
  {
    text[0] = digits[data[index] >> 4U];
    text[1] = digits[data[index] & 0x0FU];
    text[2] = (index + 1U == length) ? '\r' : ' ';
    text[3] = '\0';
    EPD_Log(text);
  }
  EPD_Log("\n");
}

static bool PairingNDEF_Init(void)
{
  uint8_t identity[ST25DV04K_IDENTITY_SIZE];
  uint8_t ndef_image[NDEF_MAX_IMAGE_SIZE];
  uint16_t ndef_size;
  bool identity_verified;

  EPD_Log("NFC pairing record: start\r\n");
  if (!NFC_CheckReady())
  {
    EPD_Log("NFC pairing record: tag not found\r\n");
    return false;
  }

  if (HAL_I2C_Mem_Read(&hi2c1, ST25DV04K_SYSTEM_ADDRESS,
                       ST25DV04K_IDENTITY_ADDRESS,
                       I2C_MEMADD_SIZE_16BIT, identity,
                       ST25DV04K_IDENTITY_SIZE,
                       ST25DV04K_I2C_TIMEOUT_MS) != HAL_OK)
  {
    EPD_Log("NFC pairing record: identity read failed\r\n");
    return false;
  }
  identity_verified = (identity[0] == 0x7FU)
                      && (identity[1] == 0x00U)
                      && (identity[2] == 0x03U)
                      && (identity[3] == 0x24U)
                      && ((identity[9] == 0x24U)
                          || (identity[9] == 0x25U))
                      && (identity[10] == 0x02U)
                      && (identity[11] == 0xE0U);
  if (!identity_verified)
  {
    EPD_Log("NFC pairing record: unexpected tag identity\r\n");
    return false;
  }
  if (!NFC_DisableMailbox())
  {
    EPD_Log("NFC pairing record: could not disable mailbox\r\n");
    return false;
  }
  if (!NFC_BuildPairingNdef(ndef_image, sizeof(ndef_image), &ndef_size)
      || !NFC_WriteAndVerifyNdef(ndef_image, ndef_size))
  {
    EPD_Log("NFC pairing record: write/verify failed\r\n");
    return false;
  }

  EPD_Log("NFC pairing record: ready\r\n");
  return true;
}

static void SensorNDEF_Demo(void)
{
  AHT20_Measurement measurement;
  AHT20_Status sensor_status;
  uint8_t identity[ST25DV04K_IDENTITY_SIZE];
  uint8_t ndef_image[NDEF_MAX_IMAGE_SIZE];
  char ndef_text[NDEF_MAX_TEXT_SIZE];
  uint32_t temperature_magnitude;
  int text_length;
  uint16_t ndef_size;
  HAL_StatusTypeDef status;
  bool identity_verified;

  EPD_Log("\r\nAHT20 -> ST25DV04K NDEF demo: start\r\n");
  EPD_Log("NFC: keep phones away from the antenna while data is updated\r\n");

  /* AHT20 and the shared I2C pull-ups are directly powered. */
  HAL_Delay(AHT20_POWER_STABILIZE_MS);

  sensor_status = AHT20_ReadMeasurement(&hi2c1, &measurement);
  if (sensor_status != AHT20_OK)
  {
    EPD_Log("AHT20 FAIL: ");
    EPD_Log(AHT20_StatusString(sensor_status));
    EPD_Log("\r\n");
    EPD_Log("NFC: EEPROM left unchanged because no valid sensor sample exists\r\n");
    return;
  }

  temperature_magnitude = measurement.temperature_centi_c < 0
                            ? (uint32_t)(-measurement.temperature_centi_c)
                            : (uint32_t)measurement.temperature_centi_c;
  text_length = snprintf(ndef_text, sizeof(ndef_text),
                         "Temperature: %s%lu.%02lu C\r\n"
                         "Humidity: %lu.%02lu %%RH",
                         measurement.temperature_centi_c < 0 ? "-" : "",
                         (unsigned long)(temperature_magnitude / 100U),
                         (unsigned long)(temperature_magnitude % 100U),
                         (unsigned long)(measurement.humidity_centi_percent
                                         / 100U),
                         (unsigned long)(measurement.humidity_centi_percent
                                         % 100U));
  if ((text_length < 0) || ((size_t)text_length >= sizeof(ndef_text)))
  {
    EPD_Log("NFC FAIL: sensor text buffer is too small\r\n");
    return;
  }
  if (!NFC_BuildTextNdef(ndef_text, ndef_image, sizeof(ndef_image),
                         &ndef_size))
  {
    EPD_Log("NFC FAIL: could not build NDEF Text record\r\n");
    return;
  }

  EPD_Log("AHT20 measurement:\r\n");
  EPD_Log(ndef_text);
  EPD_Log("\r\n");

  if (!NFC_CheckReady())
  {
    EPD_Log("NFC FAIL: user address 0x53 did not ACK on the always-on rail\r\n");
    EPD_Log("NFC: check 3.3 V and that PB7(SDA)/PB8(SCL) idle near 3.3 V\r\n");
    return;
  }

  status = HAL_I2C_Mem_Read(&hi2c1, ST25DV04K_SYSTEM_ADDRESS,
                            ST25DV04K_IDENTITY_ADDRESS,
                            I2C_MEMADD_SIZE_16BIT, identity,
                            ST25DV04K_IDENTITY_SIZE,
                            ST25DV04K_I2C_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    Debug_LogHex("NFC identity 0x0014..0x0020: ", identity,
                 ST25DV04K_IDENTITY_SIZE);
    Debug_LogHex("NFC UID: ", &identity[4], 8U);

    identity_verified = (identity[0] == 0x7FU)
                        && (identity[1] == 0x00U)
                        && (identity[2] == 0x03U)
                        && (identity[3] == 0x24U)
                        && ((identity[9] == 0x24U)
                            || (identity[9] == 0x25U))
                        && (identity[10] == 0x02U)
                        && (identity[11] == 0xE0U);
    if (!identity_verified)
    {
      EPD_Log("NFC FAIL: identity is not ST25DV04K; EEPROM left unchanged\r\n");
      return;
    }
    EPD_Log("NFC: identity matches ST25DV04K (512 B, 4 B block)\r\n");
  }
  else
  {
    EPD_Log("NFC FAIL: could not verify ST25DV04K identity; EEPROM left unchanged\r\n");
    return;
  }

  if (!NFC_DisableMailbox())
  {
    EPD_Log("NFC FAIL: could not disable Fast Transfer Mode\r\n");
    return;
  }

  if (!NFC_WriteAndVerifyNdef(ndef_image, ndef_size))
  {
    EPD_Log("NFC NDEF write/read: FAIL\r\n");
    EPD_Log("NFC: check EEPROM protection and keep the phone away while writing\r\n");
    return;
  }

  EPD_Log("NFC NDEF write/read: PASS\r\n");
  EPD_Log("Phone-readable NDEF Text:\r\n");
  EPD_Log(ndef_text);
  EPD_Log("\r\nAHT20 -> ST25DV04K NDEF demo: PASS\r\n");
}

static void EPD_Log(const char *message)
{
  size_t length = strlen(message);

  if (length > UINT16_MAX)
  {
    length = UINT16_MAX;
  }
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)length, 1000U);
}

static void EPD_LogStatus(const char *operation, GDEM042F86_Status status)
{
  EPD_Log("EPD ");
  EPD_Log(operation);
  EPD_Log(": ");
  EPD_Log(GDEM042F86_StatusString(status));
  EPD_Log("\r\n");
}

static void EPD_Demo(void)
{
  const GDEM042F86_Config config = {
    .spi = &hspi1,
    .rst_port = EPD_RST_GPIO_Port,
    .rst_pin = EPD_RST_Pin,
    .dc_port = EPD_DC_GPIO_Port,
    .dc_pin = EPD_DC_Pin,
    .cs_port = EPD_CS_GPIO_Port,
    .cs_pin = EPD_CS_Pin,
    .busy_port = EPD_BUSY_GPIO_Port,
    .busy_pin = EPD_BUSY_Pin,
    .spi_timeout_ms = 1000U,
    .busy_timeout_ms = 60000U
  };
  GDEM042F86_Status status;

  EPD_Log("\r\nGDEM042F86 STM32 test: start\r\n");

  /* A MCU reset does not remove panel power.  Force a real power cycle so a
     controller left in deep sleep cannot retain stale internal state. */
#if EPD_DIAGNOSTIC_MODE
  /* Directly powered BLE enters low power through SLP. */
  Bluetooth_Sleep();
#endif
  HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin, GPIO_PIN_RESET);
  HAL_Delay(EPD_POWER_OFF_TIME_MS);

  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(EPD_POWER_STABILIZE_MS);
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(20U);

  status = GDEM042F86_Begin(&config);
  EPD_LogStatus("begin", status);
  if (status != GDEM042F86_OK)
  {
    return;
  }

  EPD_Log(HAL_GPIO_ReadPin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin) == GPIO_PIN_SET
            ? "EPD BUSY before reset: HIGH (ready)\r\n"
            : "EPD BUSY before reset: LOW (busy)\r\n");

  status = GDEM042F86_Wake(false);
  EPD_LogStatus("wake", status);
  if (status != GDEM042F86_OK)
  {
    return;
  }

  status = GDEM042F86_DisplayTestPattern();
  EPD_LogStatus("four-color test pattern", status);
  if (status != GDEM042F86_OK)
  {
    return;
  }

  /* Keep the controller power-on state long enough to measure PREVGH with a
     multimeter or oscilloscope. */
  HAL_Delay(EPD_HIGH_VOLTAGE_HOLD_MS);

  status = GDEM042F86_Sleep();
  EPD_LogStatus("sleep", status);
  if (status == GDEM042F86_OK)
  {
    HAL_Delay(20U);
    HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                      GPIO_PIN_RESET);
    EPD_Log("GDEM042F86 STM32 test: done\r\n");
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  ImageTransfer_OnRxEvent(huart, size);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  ImageTransfer_OnUartError(huart);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
