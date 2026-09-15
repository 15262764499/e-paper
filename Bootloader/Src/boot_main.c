#include "ota_bootloader.h"
#include "ota_layout.h"
#include "stm32g0xx_hal.h"

#define BT_SLEEP_PIN             GPIO_PIN_15
#define BT_SLEEP_PORT            GPIOB
#define BOOT_REQUEST_TIMEOUT_MS  120000UL

static UART_HandleTypeDef s_uart2;

static void SystemClock_Config(void);
static void Bluetooth_GPIO_Init(void);
static void USART2_Init(void);
static void BackupAccess_Init(void);
static bool ConsumeBootRequest(void);
void Error_Handler(void);

int main(void)
{
  bool requested;
  uint32_t boot_wait_start;

  HAL_Init();
  SystemClock_Config();
  BackupAccess_Init();
  requested = ConsumeBootRequest();

  if (!requested && OTA_Bootloader_IsApplicationValid())
  {
    OTA_Bootloader_JumpToApplication();
  }

  Bluetooth_GPIO_Init();
  USART2_Init();
  if (!OTA_Bootloader_Init(&s_uart2))
  {
    Error_Handler();
  }
  boot_wait_start = HAL_GetTick();

  while (1)
  {
    OTA_Bootloader_Poll();
    if (requested && !OTA_Bootloader_IsTransferActive()
        && ((uint32_t)(HAL_GetTick() - boot_wait_start)
            >= BOOT_REQUEST_TIMEOUT_MS)
        && OTA_Bootloader_IsApplicationValid())
    {
      OTA_Bootloader_JumpToApplication();
    }
  }
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clocks = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1)
      != HAL_OK)
  {
    Error_Handler();
  }
  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  oscillator.HSIState = RCC_HSI_ON;
  oscillator.HSIDiv = RCC_HSI_DIV1;
  oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  oscillator.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    Error_Handler();
  }
  clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                     | RCC_CLOCKTYPE_PCLK1;
  clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clocks.APB1CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

static void Bluetooth_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* Keep the 595 blank during bootloader recovery/OTA waits. */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);
  __HAL_RCC_GPIOB_CLK_ENABLE();
  /* BLE is always powered; keep SLP high throughout OTA. */
  HAL_GPIO_WritePin(BT_SLEEP_PORT, BT_SLEEP_PIN, GPIO_PIN_SET);
  gpio.Pin = BT_SLEEP_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
  HAL_Delay(520U);
}

static void USART2_Init(void)
{
  s_uart2.Instance = USART2;
  s_uart2.Init.BaudRate = 115200;
  s_uart2.Init.WordLength = UART_WORDLENGTH_8B;
  s_uart2.Init.StopBits = UART_STOPBITS_1;
  s_uart2.Init.Parity = UART_PARITY_NONE;
  s_uart2.Init.Mode = UART_MODE_TX_RX;
  s_uart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  s_uart2.Init.OverSampling = UART_OVERSAMPLING_16;
  s_uart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  s_uart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  s_uart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if ((HAL_UART_Init(&s_uart2) != HAL_OK)
      || (HAL_UARTEx_SetTxFifoThreshold(&s_uart2,
                                        UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
      || (HAL_UARTEx_SetRxFifoThreshold(&s_uart2,
                                        UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
      || (HAL_UARTEx_DisableFifoMode(&s_uart2) != HAL_OK))
  {
    Error_Handler();
  }
}

static void BackupAccess_Init(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  SET_BIT(PWR->CR1, PWR_CR1_DBP);
  __HAL_RCC_RTCAPB_CLK_ENABLE();
}

static bool ConsumeBootRequest(void)
{
  bool requested = TAMP->BKP3R == OTA_BOOT_REQUEST_MAGIC;
  if (requested)
  {
    TAMP->BKP3R = 0U;
  }
  return requested;
}

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
  GPIO_InitTypeDef gpio = {0};
  if (uart->Instance != USART2)
  {
    return;
  }
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF1_USART2;
  HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *uart)
{
  if (uart->Instance == USART2)
  {
    __HAL_RCC_USART2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
