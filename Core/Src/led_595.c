#include "led_595.h"
#include "led_effect.h"
#include "pulse_effect.h"
#include "aht20.h"
#include "main.h"

#define PWM_STEP_US 40U /* 100 steps / 4 ms = 250 Hz, at most 8 IRQs/frame. */
#define NFC_ADDRESS (0x53U << 1U)
#define NFC_EH_CTRL_DYN 0x2002U
#define NFC_FIELD_ON 0x04U
#define NFC_POLL_MS 40U
#define NFC_RELEASE_MS 300U
#define SENSOR_POLL_MS 2000U

typedef struct
{
  uint16_t duration[8];
  uint8_t mask[8];
  uint8_t count;
} PWM_Frame;

static SPI_HandleTypeDef s_spi;
static TIM_HandleTypeDef s_timer;
static volatile bool s_ready;
static I2C_HandleTypeDef *s_i2c;
static volatile bool s_nfc;
static volatile LED_Mode s_mode;
/* Updated in main under IRQ lock; advanced/expired by SysTick. */
static PulseEffect s_computer;
static uint32_t s_nfc_started;
/* UINT32_MAX represents an invalid sample; one atomic write publishes it. */
static volatile uint32_t s_humidity = UINT32_MAX;
static uint32_t s_started;
static uint32_t s_last_update;
static uint32_t s_button_changed;
static bool s_button_raw;
static bool s_button_stable;
static PWM_Frame s_pending;
static PWM_Frame s_active;
static uint8_t s_slot;
static uint32_t s_last_nfc_poll;
static uint32_t s_last_field;
static uint32_t s_last_sensor;
static bool s_sensor_pending;
static uint32_t s_sensor_retry;
static bool s_polling_nfc;

/* SPI2 belongs exclusively to this driver. Bounded polling is safe in IRQs:
 * no HAL timeout depending on SysTick, and no SPI shared with the display. */
static bool ShiftMask(uint8_t mask)
{
  uint32_t guard = 128U;
  while ((SPI2->SR & SPI_SR_TXE) == 0U && --guard != 0U) {}
  if (guard == 0U) return false;
  *(__IO uint8_t *)&SPI2->DR = (uint8_t)~mask;
  guard = 128U;
  while ((SPI2->SR & SPI_SR_TXE) == 0U && --guard != 0U) {}
  if (guard == 0U) return false;
  guard = 128U;
  while ((SPI2->SR & SPI_SR_BSY) != 0U && --guard != 0U) {}
  if (guard == 0U) return false;
  GPIOB->BSRR = GPIO_PIN_14;
  __NOP();
  __NOP();
  GPIOB->BRR = GPIO_PIN_14;
  return true;
}

static void PublishDuty(const uint8_t duty[8])
{
  PWM_Frame frame = {0};
  uint32_t start = 0U;
  do
  {
    uint32_t next = 100U;
    uint8_t mask = 0U;
    for (uint32_t q = 1U; q <= 7U; ++q)
    {
      if (duty[q] > start)
      {
        mask |= (uint8_t)(1U << q);
        if (duty[q] < next) next = duty[q];
      }
    }
    frame.mask[frame.count] = mask;
    frame.duration[frame.count++] = (uint16_t)((next - start) * PWM_STEP_US);
    start = next;
  } while (start < 100U);

  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  s_pending = frame;
  __set_PRIMASK(saved);
}

bool LED595_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
  s_ready = false;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  /* OE stays high until the first valid frame. Q0 is always off. */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_14;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pin = GPIO_PIN_0;
  gpio.Alternate = GPIO_AF0_SPI2;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_4;
  gpio.Alternate = GPIO_AF1_SPI2;
  HAL_GPIO_Init(GPIOA, &gpio);

  s_spi.Instance = SPI2;
  s_spi.Init.Mode = SPI_MODE_MASTER;
  s_spi.Init.Direction = SPI_DIRECTION_1LINE;
  s_spi.Init.DataSize = SPI_DATASIZE_8BIT;
  s_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  s_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  s_spi.Init.NSS = SPI_NSS_SOFT;
  s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
  s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  s_spi.Init.CRCPolynomial = 7U;
  s_spi.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  s_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&s_spi) != HAL_OK) return false;
  SPI_1LINE_TX(&s_spi);
  __HAL_SPI_ENABLE(&s_spi);
  if (!ShiftMask(0U)) return false;

  if ((RCC->CFGR & RCC_CFGR_PPRE) != 0U) timer_clock *= 2U;
  s_timer.Instance = TIM1;
  s_timer.Init.Prescaler = timer_clock / 1000000U - 1U;
  s_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_timer.Init.Period = 100U * PWM_STEP_US - 1U;
  s_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_timer.Init.RepetitionCounter = 0U;
  s_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&s_timer) != HAL_OK) return false;

  s_pending.count = 1U;
  s_pending.mask[0] = 0U;
  s_pending.duration[0] = 100U * PWM_STEP_US;
  s_slot = 0U;
  s_started = HAL_GetTick();
  s_last_update = s_started;
  s_button_changed = s_started;
  s_button_raw = HAL_GPIO_ReadPin(MODE_SWICH_GPIO_Port, MODE_SWICH_Pin)
                 == GPIO_PIN_RESET;
  s_button_stable = s_button_raw; /* A held key at boot is not a press. */
  s_mode = LED_MODE_OFF;
  s_computer.active = false;
  s_nfc = false;
  s_humidity = UINT32_MAX;
  __HAL_TIM_CLEAR_FLAG(&s_timer, TIM_FLAG_UPDATE);
  HAL_NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
  s_ready = true;
  if (HAL_TIM_Base_Start_IT(&s_timer) != HAL_OK)
  {
    s_ready = false;
    return false;
  }
  return true;
}

void LED595_TimerIRQ(void)
{
  if ((TIM1->SR & TIM_SR_UIF) == 0U) return;
  TIM1->SR = ~TIM_SR_UIF;
  if (!s_ready) return;
  /* Brief blanking prevents intermediate shift data from appearing. */
  GPIOA->BSRR = GPIO_PIN_8;
  TIM1->CR1 &= ~TIM_CR1_CEN;
  if (s_slot == 0U) s_active = s_pending;
  bool ok = ShiftMask(s_active.mask[s_slot]);
  TIM1->ARR = s_active.duration[s_slot] - 1U;
  TIM1->CNT = 0U;
  TIM1->CR1 |= TIM_CR1_CEN;
  if (ok) GPIOA->BRR = GPIO_PIN_8;
  if (++s_slot >= s_active.count) s_slot = 0U;
}

void LED595_Tick(void)
{
  if (!s_ready) return;
  uint32_t now = HAL_GetTick();
  bool pressed = HAL_GPIO_ReadPin(MODE_SWICH_GPIO_Port, MODE_SWICH_Pin)
                 == GPIO_PIN_RESET;
  if (pressed != s_button_raw)
  {
    s_button_raw = pressed;
    s_button_changed = now;
  }
  if (pressed != s_button_stable && (uint32_t)(now - s_button_changed) >= 30U)
  {
    s_button_stable = pressed;
    /* During NFC contact the selected background mode remains unchanged. */
    if (pressed && !s_nfc)
    {
      LED595_SetMode((LED_Mode)((s_mode + 1U) % LED_MODE_COUNT));
    }
  }
  if ((uint32_t)(now - s_last_update) < 10U) return;
  s_last_update = now;
  uint8_t duty[8];
  uint32_t humidity = s_humidity;
  PulseEffect_Duty(&s_computer, now, duty);
  if (s_nfc || !s_computer.active)
    LED_Effect((uint32_t)(now - (s_nfc ? s_nfc_started : s_started)), s_nfc,
                s_mode, humidity != UINT32_MAX, humidity, duty);
  PublishDuty(duty);
}

bool LED595_SetMode(LED_Mode mode)
{
  if ((unsigned)mode >= LED_MODE_COUNT) return false;
  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  s_computer.active = false;
  if (s_mode != mode)
  {
    s_mode = mode;
    s_started = HAL_GetTick();
  }
  __set_PRIMASK(saved);
  return true;
}

void LED595_SetComputer(uint16_t value, uint8_t brightness, uint8_t speed)
{
  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  s_mode = LED_MODE_OFF; /* STOP or expiry cannot revive a stale local mode. */
  PulseEffect_Set(&s_computer, HAL_GetTick(), value, brightness, speed);
  __set_PRIMASK(saved);
}

void LED595_StopComputer(void)
{
  (void)LED595_SetMode(LED_MODE_OFF);
}

void LED595_GetStatus(LED595_Status *status)
{
  if (status == NULL) return;
  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  uint32_t now = HAL_GetTick();
  bool nfc = s_nfc;
  PulseEffect computer = s_computer;
  status->mode = (uint8_t)s_mode;
  status->effective_mode = nfc ? 3U : (uint8_t)s_mode;
  status->humidity = s_humidity;
  status->humidity_valid = s_humidity != UINT32_MAX;
  status->elapsed_ms = now - (nfc ? s_nfc_started : s_started);
  __set_PRIMASK(saved);
  PulseEffect_Duty(&computer, now, status->duty);
  if (!nfc && computer.active)
  {
    status->effective_mode = 4U;
    status->elapsed_ms = computer.phase / 100U;
    return;
  }
  status->elapsed_ms %= status->effective_mode == LED_MODE_SPREAD
                         ? LED595_SPREAD_PERIOD_MS : LED595_BREATH_PERIOD_MS;
  LED_Effect(status->elapsed_ms, nfc, (LED_Mode)status->mode,
             status->humidity_valid, status->humidity, status->duty);
}

void LED595_AttachI2C(I2C_HandleTypeDef *i2c)
{
  s_i2c = i2c;
  s_last_nfc_poll = HAL_GetTick() - NFC_POLL_MS;
  s_last_sensor = HAL_GetTick() - SENSOR_POLL_MS;
}

void LED595_PollNfc(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t status;
  if (s_i2c == NULL || s_polling_nfc || __get_IPSR() != 0U
      || HAL_I2C_GetState(s_i2c) != HAL_I2C_STATE_READY
      || (uint32_t)(now - s_last_nfc_poll) < NFC_POLL_MS) return;
  s_polling_nfc = true;
  s_last_nfc_poll = now;
  /* ST25DV04K datasheet: EH_CTRL_Dyn (user address), bit 2 FIELD_ON.
   * Read-only polling preserves GPO, energy harvesting and NDEF settings. */
  HAL_StatusTypeDef result = HAL_I2C_Mem_Read(s_i2c, NFC_ADDRESS,
      NFC_EH_CTRL_DYN, I2C_MEMADD_SIZE_16BIT, &status, 1U, 5U);
  if (result == HAL_OK && (status & NFC_FIELD_ON) != 0U)
  {
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    if (!s_nfc) s_nfc_started = now;
    s_last_field = now;
    s_nfc = true;
    __set_PRIMASK(saved);
  }
  else if ((uint32_t)(now - s_last_field) >= NFC_RELEASE_MS)
  {
    /* Bridge short phone RF gaps/bus errors, but never remain lit forever. */
    s_nfc = false;
  }
  s_polling_nfc = false;
}

void LED595_Poll(void)
{
  LED595_PollNfc();
  uint32_t now = HAL_GetTick();
  if (s_i2c == NULL) return;
  if (s_sensor_pending)
  {
    if ((uint32_t)(now - s_last_sensor) < 80U
        || (uint32_t)(now - s_sensor_retry) < 10U) return;
    AHT20_Measurement measurement;
    s_sensor_retry = now;
    AHT20_Status status = AHT20_FinishMeasurement(s_i2c, &measurement);
    if (status == AHT20_BUSY && (uint32_t)(now - s_last_sensor) < 330U) return;
    s_humidity = status == AHT20_OK ? measurement.humidity_centi_percent
                                  : UINT32_MAX;
    s_sensor_pending = false;
  }
  else if (s_mode == LED_MODE_HUMIDITY && !s_nfc
           && (uint32_t)(now - s_last_sensor) >= SENSOR_POLL_MS)
  {
    s_last_sensor = now;
    s_sensor_retry = now;
    s_sensor_pending = AHT20_BeginMeasurement(s_i2c) == AHT20_OK;
    if (!s_sensor_pending) s_humidity = UINT32_MAX;
  }
}
