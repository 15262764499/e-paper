#ifndef GDEM042F86_H
#define GDEM042F86_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GDEM042F86_WIDTH       400U
#define GDEM042F86_HEIGHT      300U
#define GDEM042F86_FRAME_BYTES \
  (GDEM042F86_WIDTH * GDEM042F86_HEIGHT / 4U)

/* Native two-bit color codes used by the panel controller. */
typedef enum
{
  GDEM042F86_BLACK = 0x00,
  GDEM042F86_WHITE = 0x01,
  GDEM042F86_YELLOW = 0x02,
  GDEM042F86_RED = 0x03
} GDEM042F86_Color;

typedef enum
{
  GDEM042F86_OK = 0,
  GDEM042F86_INVALID_ARGUMENT,
  GDEM042F86_INVALID_STATE,
  GDEM042F86_SPI_ERROR,
  GDEM042F86_TIMEOUT
} GDEM042F86_Status;

typedef struct
{
  SPI_HandleTypeDef *spi;

  GPIO_TypeDef *rst_port;
  uint16_t rst_pin;
  GPIO_TypeDef *dc_port;
  uint16_t dc_pin;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  GPIO_TypeDef *busy_port;
  uint16_t busy_pin;

  uint32_t spi_timeout_ms;
  uint32_t busy_timeout_ms;
} GDEM042F86_Config;

/* Attach the driver to GPIO and SPI peripherals initialized by STM32Cube HAL. */
GDEM042F86_Status GDEM042F86_Begin(const GDEM042F86_Config *config);

/* Reset and initialize the panel for a full update. */
GDEM042F86_Status GDEM042F86_Wake(bool fast_mode);

/* Stream one native framebuffer directly into the panel controller RAM.
   Begin sends command 0x10, Write appends bytes in strict order, and Commit
   accepts only an exact 30000-byte frame before issuing display refresh 0x12. */
GDEM042F86_Status GDEM042F86_StreamBegin(void);
GDEM042F86_Status GDEM042F86_StreamWrite(const uint8_t *data, size_t size);
GDEM042F86_Status GDEM042F86_StreamCommit(void);
GDEM042F86_Status GDEM042F86_StreamAbort(void);
size_t GDEM042F86_StreamBytesWritten(void);

/* Display an exact 30000-byte native packed two-bit framebuffer. */
GDEM042F86_Status GDEM042F86_Display(const uint8_t *frame,
                                     size_t frame_size);

/* Convert and display data using the palette order of the ESP32/Arduino demo. */
GDEM042F86_Status GDEM042F86_DisplayArduinoImage(const uint8_t *frame,
                                                 size_t frame_size);

GDEM042F86_Status GDEM042F86_Fill(GDEM042F86_Color color);

/* Display four vertical bands: black, white, yellow and red. */
GDEM042F86_Status GDEM042F86_DisplayTestPattern(void);

/* Power off the panel and enter deep sleep after the final refresh. */
GDEM042F86_Status GDEM042F86_Sleep(void);

GDEM042F86_Status GDEM042F86_End(void);

const char *GDEM042F86_StatusString(GDEM042F86_Status status);

#ifdef __cplusplus
}
#endif

#endif /* GDEM042F86_H */
