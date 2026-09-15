#include "gdem042f86.h"

#include <string.h>

#define EPD_TRANSFER_CHUNK 256U
#define EPD_BUSY_POLL_MS   10U

typedef struct
{
  GDEM042F86_Config config;
  bool initialized;
  bool stream_active;
  size_t stream_bytes;
} EPD_Context;

static EPD_Context s_epd;
static uint8_t s_transfer_buffer[EPD_TRANSFER_CHUNK];

static GDEM042F86_Status EPD_Transfer(bool data_mode,
                                      const uint8_t *data,
                                      size_t size)
{
  HAL_StatusTypeDef hal_status;

  if (!s_epd.initialized)
  {
    return GDEM042F86_INVALID_STATE;
  }

  if ((data == NULL) || (size == 0U) || (size > UINT16_MAX))
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  HAL_GPIO_WritePin(s_epd.config.dc_port, s_epd.config.dc_pin,
                    data_mode ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(s_epd.config.cs_port, s_epd.config.cs_pin, GPIO_PIN_RESET);

  hal_status = HAL_SPI_Transmit(s_epd.config.spi, (uint8_t *)data,
                                (uint16_t)size,
                                s_epd.config.spi_timeout_ms);

  HAL_GPIO_WritePin(s_epd.config.cs_port, s_epd.config.cs_pin, GPIO_PIN_SET);

  if (hal_status == HAL_OK)
  {
    return GDEM042F86_OK;
  }
  if (hal_status == HAL_TIMEOUT)
  {
    return GDEM042F86_TIMEOUT;
  }
  return GDEM042F86_SPI_ERROR;
}

static GDEM042F86_Status EPD_Command(uint8_t command)
{
  return EPD_Transfer(false, &command, 1U);
}

static GDEM042F86_Status EPD_CommandData(uint8_t command,
                                         const uint8_t *data,
                                         size_t size)
{
  GDEM042F86_Status status = EPD_Command(command);

  if ((status != GDEM042F86_OK) || (size == 0U))
  {
    return status;
  }
  return EPD_Transfer(true, data, size);
}

static GDEM042F86_Status EPD_WaitReady(void)
{
  uint32_t start_tick = HAL_GetTick();

  /* The reference GDEM042F86 driver defines BUSY=HIGH as ready. */
  while (HAL_GPIO_ReadPin(s_epd.config.busy_port,
                          s_epd.config.busy_pin) == GPIO_PIN_RESET)
  {
    if ((HAL_GetTick() - start_tick) >= s_epd.config.busy_timeout_ms)
    {
      return GDEM042F86_TIMEOUT;
    }
    HAL_Delay(EPD_BUSY_POLL_MS);
  }
  return GDEM042F86_OK;
}

static GDEM042F86_Status EPD_Reset(void)
{
  HAL_Delay(20U);
  HAL_GPIO_WritePin(s_epd.config.rst_port, s_epd.config.rst_pin,
                    GPIO_PIN_RESET);
  HAL_Delay(40U);
  HAL_GPIO_WritePin(s_epd.config.rst_port, s_epd.config.rst_pin,
                    GPIO_PIN_SET);
  HAL_Delay(50U);
  return EPD_WaitReady();
}

static GDEM042F86_Status EPD_Update(void)
{
  static const uint8_t update_data[] = {0x00U};
  GDEM042F86_Status status;

  status = EPD_CommandData(0x12U, update_data, sizeof(update_data));
  if (status != GDEM042F86_OK)
  {
    return status;
  }
  return EPD_WaitReady();
}

GDEM042F86_Status GDEM042F86_Begin(const GDEM042F86_Config *config)
{
  if ((config == NULL) || (config->spi == NULL) ||
      (config->rst_port == NULL) || (config->dc_port == NULL) ||
      (config->cs_port == NULL) || (config->busy_port == NULL) ||
      (config->rst_pin == 0U) || (config->dc_pin == 0U) ||
      (config->cs_pin == 0U) || (config->busy_pin == 0U) ||
      (config->spi_timeout_ms == 0U) ||
      (config->busy_timeout_ms == 0U))
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }
  if (s_epd.initialized)
  {
    return GDEM042F86_INVALID_STATE;
  }

  s_epd.config = *config;
  s_epd.initialized = true;

  HAL_GPIO_WritePin(s_epd.config.cs_port, s_epd.config.cs_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(s_epd.config.dc_port, s_epd.config.dc_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(s_epd.config.rst_port, s_epd.config.rst_pin, GPIO_PIN_SET);

  return GDEM042F86_OK;
}

GDEM042F86_Status GDEM042F86_Wake(bool fast_mode)
{
  static const uint8_t booster[] = {0x0FU, 0x8BU, 0x9CU, 0x96U};
  static const uint8_t panel_setting[] = {0x2FU, 0x69U};
  static const uint8_t power_setting[] = {0x07U, 0xF0U};
  static const uint8_t cdi[] = {0x37U};
  static const uint8_t resolution[] = {
    (uint8_t)(GDEM042F86_WIDTH >> 8),
    (uint8_t)(GDEM042F86_WIDTH & 0xFFU),
    (uint8_t)(GDEM042F86_HEIGHT >> 8),
    (uint8_t)(GDEM042F86_HEIGHT & 0xFFU)
  };
  static const uint8_t vcom[] = {0x64U, 0x53U};
  static const uint8_t source_setting[] = {0x00U, 0x00U, 0x00U, 0x00U};
  static const uint8_t pll[] = {0x08U};
  static const uint8_t e9[] = {0x01U};
  GDEM042F86_Status status;

#define EPD_TRY(expression)                     \
  do                                            \
  {                                             \
    status = (expression);                      \
    if (status != GDEM042F86_OK) return status; \
  } while (0)

  if (!s_epd.initialized)
  {
    return GDEM042F86_INVALID_STATE;
  }

  s_epd.stream_active = false;
  s_epd.stream_bytes = 0U;

  EPD_TRY(EPD_Reset());
  EPD_TRY(EPD_CommandData(0x06U, booster, sizeof(booster)));
  EPD_TRY(EPD_CommandData(0x00U, panel_setting, sizeof(panel_setting)));
  EPD_TRY(EPD_CommandData(0x01U, power_setting, sizeof(power_setting)));
  EPD_TRY(EPD_CommandData(0x50U, cdi, sizeof(cdi)));
  EPD_TRY(EPD_CommandData(0x61U, resolution, sizeof(resolution)));
  EPD_TRY(EPD_CommandData(0x62U, vcom, sizeof(vcom)));
  EPD_TRY(EPD_CommandData(0x65U, source_setting, sizeof(source_setting)));
  EPD_TRY(EPD_CommandData(0x30U, pll, sizeof(pll)));
  EPD_TRY(EPD_CommandData(0xE9U, e9, sizeof(e9)));
  EPD_TRY(EPD_Command(0x04U));
  EPD_TRY(EPD_WaitReady());

  if (fast_mode)
  {
    static const uint8_t one[] = {0x01U};
    static const uint8_t zero[] = {0x00U};
    static const uint8_t f6[] = {0x15U};
    static const uint8_t e0[] = {0x02U};
    static const uint8_t e6[] = {0x5AU};

    EPD_TRY(EPD_CommandData(0xEFU, one, sizeof(one)));
    EPD_TRY(EPD_CommandData(0xF6U, f6, sizeof(f6)));
    EPD_TRY(EPD_CommandData(0xEFU, zero, sizeof(zero)));
    EPD_TRY(EPD_CommandData(0xE0U, e0, sizeof(e0)));
    EPD_TRY(EPD_CommandData(0xE6U, e6, sizeof(e6)));
    EPD_TRY(EPD_Command(0xA5U));
    EPD_TRY(EPD_WaitReady());
  }

#undef EPD_TRY
  return GDEM042F86_OK;
}

GDEM042F86_Status GDEM042F86_StreamBegin(void)
{
  GDEM042F86_Status status;

  if (!s_epd.initialized || s_epd.stream_active)
  {
    return GDEM042F86_INVALID_STATE;
  }

  status = EPD_Command(0x10U);
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  s_epd.stream_active = true;
  s_epd.stream_bytes = 0U;
  return GDEM042F86_OK;
}

GDEM042F86_Status GDEM042F86_StreamWrite(const uint8_t *data, size_t size)
{
  GDEM042F86_Status status;

  if (!s_epd.initialized || !s_epd.stream_active)
  {
    return GDEM042F86_INVALID_STATE;
  }
  if ((data == NULL) || (size == 0U)
      || (size > (GDEM042F86_FRAME_BYTES - s_epd.stream_bytes)))
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  status = EPD_Transfer(true, data, size);
  if (status == GDEM042F86_OK)
  {
    s_epd.stream_bytes += size;
  }
  return status;
}

GDEM042F86_Status GDEM042F86_StreamCommit(void)
{
  GDEM042F86_Status status;

  if (!s_epd.initialized || !s_epd.stream_active)
  {
    return GDEM042F86_INVALID_STATE;
  }
  if (s_epd.stream_bytes != GDEM042F86_FRAME_BYTES)
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  status = EPD_Update();
  s_epd.stream_active = false;
  return status;
}

GDEM042F86_Status GDEM042F86_StreamAbort(void)
{
  if (!s_epd.initialized)
  {
    return GDEM042F86_INVALID_STATE;
  }

  s_epd.stream_active = false;
  s_epd.stream_bytes = 0U;
  return GDEM042F86_OK;
}

size_t GDEM042F86_StreamBytesWritten(void)
{
  return s_epd.stream_bytes;
}

GDEM042F86_Status GDEM042F86_Display(const uint8_t *frame,
                                     size_t frame_size)
{
  GDEM042F86_Status status;

  if ((frame == NULL) || (frame_size != GDEM042F86_FRAME_BYTES))
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  status = EPD_Command(0x10U);
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  for (size_t offset = 0U; offset < frame_size; offset += EPD_TRANSFER_CHUNK)
  {
    size_t transfer_size = frame_size - offset;
    if (transfer_size > EPD_TRANSFER_CHUNK)
    {
      transfer_size = EPD_TRANSFER_CHUNK;
    }

    status = EPD_Transfer(true, frame + offset, transfer_size);
    if (status != GDEM042F86_OK)
    {
      return status;
    }
  }

  return EPD_Update();
}

GDEM042F86_Status GDEM042F86_DisplayArduinoImage(const uint8_t *frame,
                                                 size_t frame_size)
{
  static const uint8_t palette[] = {
    GDEM042F86_WHITE,
    GDEM042F86_YELLOW,
    GDEM042F86_RED,
    GDEM042F86_BLACK
  };
  GDEM042F86_Status status;

  if ((frame == NULL) || (frame_size != GDEM042F86_FRAME_BYTES))
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  status = EPD_Command(0x10U);
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  for (size_t offset = 0U; offset < frame_size; offset += EPD_TRANSFER_CHUNK)
  {
    size_t transfer_size = frame_size - offset;
    if (transfer_size > EPD_TRANSFER_CHUNK)
    {
      transfer_size = EPD_TRANSFER_CHUNK;
    }

    for (size_t i = 0U; i < transfer_size; ++i)
    {
      uint8_t source = frame[offset + i];
      s_transfer_buffer[i] =
        (uint8_t)((palette[(source >> 6) & 0x03U] << 6) |
                  (palette[(source >> 4) & 0x03U] << 4) |
                  (palette[(source >> 2) & 0x03U] << 2) |
                   palette[source & 0x03U]);
    }

    status = EPD_Transfer(true, s_transfer_buffer, transfer_size);
    if (status != GDEM042F86_OK)
    {
      return status;
    }
  }

  return EPD_Update();
}

GDEM042F86_Status GDEM042F86_Fill(GDEM042F86_Color color)
{
  GDEM042F86_Status status;
  size_t sent = 0U;
  uint8_t packed;

  if ((unsigned int)color > (unsigned int)GDEM042F86_RED)
  {
    return GDEM042F86_INVALID_ARGUMENT;
  }

  status = EPD_Command(0x10U);
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  packed = (uint8_t)((uint8_t)color * 0x55U);
  memset(s_transfer_buffer, packed, sizeof(s_transfer_buffer));

  while (sent < GDEM042F86_FRAME_BYTES)
  {
    size_t transfer_size = GDEM042F86_FRAME_BYTES - sent;
    if (transfer_size > sizeof(s_transfer_buffer))
    {
      transfer_size = sizeof(s_transfer_buffer);
    }

    status = EPD_Transfer(true, s_transfer_buffer, transfer_size);
    if (status != GDEM042F86_OK)
    {
      return status;
    }
    sent += transfer_size;
  }

  return EPD_Update();
}

GDEM042F86_Status GDEM042F86_DisplayTestPattern(void)
{
  const size_t bytes_per_line = GDEM042F86_WIDTH / 4U;
  GDEM042F86_Status status;

  status = EPD_Command(0x10U);
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  memset(s_transfer_buffer, 0x00, 25U);       /* black */
  memset(s_transfer_buffer + 25U, 0x55, 25U); /* white */
  memset(s_transfer_buffer + 50U, 0xAA, 25U); /* yellow */
  memset(s_transfer_buffer + 75U, 0xFF, 25U); /* red */

  for (size_t row = 0U; row < GDEM042F86_HEIGHT; ++row)
  {
    status = EPD_Transfer(true, s_transfer_buffer, bytes_per_line);
    if (status != GDEM042F86_OK)
    {
      return status;
    }
  }

  return EPD_Update();
}

GDEM042F86_Status GDEM042F86_Sleep(void)
{
  static const uint8_t power_off_data[] = {0x00U};
  static const uint8_t sleep_key[] = {0xA5U};
  GDEM042F86_Status status;

  s_epd.stream_active = false;
  s_epd.stream_bytes = 0U;

  status = EPD_CommandData(0x02U, power_off_data, sizeof(power_off_data));
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  status = EPD_WaitReady();
  if (status != GDEM042F86_OK)
  {
    return status;
  }

  return EPD_CommandData(0x07U, sleep_key, sizeof(sleep_key));
}

GDEM042F86_Status GDEM042F86_End(void)
{
  if (!s_epd.initialized)
  {
    return GDEM042F86_INVALID_STATE;
  }

  memset(&s_epd, 0, sizeof(s_epd));
  return GDEM042F86_OK;
}

const char *GDEM042F86_StatusString(GDEM042F86_Status status)
{
  switch (status)
  {
    case GDEM042F86_OK:
      return "OK";
    case GDEM042F86_INVALID_ARGUMENT:
      return "invalid argument";
    case GDEM042F86_INVALID_STATE:
      return "invalid state";
    case GDEM042F86_SPI_ERROR:
      return "SPI error";
    case GDEM042F86_TIMEOUT:
      return "timeout (check BUSY)";
    default:
      return "unknown error";
  }
}
