#ifndef AHT20_H
#define AHT20_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

#include <stdint.h>

typedef enum
{
  AHT20_OK = 0,
  AHT20_INVALID_ARGUMENT,
  AHT20_NOT_FOUND,
  AHT20_NOT_CALIBRATED,
  AHT20_I2C_ERROR,
  AHT20_TIMEOUT,
  AHT20_CRC_ERROR,
  AHT20_BUSY
} AHT20_Status;

typedef struct
{
  int32_t temperature_centi_c;
  uint32_t humidity_centi_percent;
} AHT20_Measurement;

AHT20_Status AHT20_ReadMeasurement(I2C_HandleTypeDef *i2c,
                                   AHT20_Measurement *measurement);
const char *AHT20_StatusString(AHT20_Status status);
/* Main-context split-phase measurement. Finish after >=80 ms; retry BUSY. */
AHT20_Status AHT20_BeginMeasurement(I2C_HandleTypeDef *i2c);
AHT20_Status AHT20_FinishMeasurement(I2C_HandleTypeDef *i2c,
                                     AHT20_Measurement *measurement);

#ifdef __cplusplus
}
#endif

#endif
