#include "aht20.h"

#define AHT20_I2C_ADDRESS (0x38U << 1U)
#define AHT20_IO_TIMEOUT_MS 100U
#define AHT20_MEASUREMENT_TIME_MS 80U
#define AHT20_MEASUREMENT_TIMEOUT_MS 250U
#define AHT20_STATUS_CALIBRATED_MASK 0x18U
#define AHT20_STATUS_BUSY_MASK 0x80U

static uint8_t AHT20_CalculateCrc(const uint8_t *data, uint32_t length)
{
  uint8_t crc = 0xFFU;
  uint32_t byte_index;

  for (byte_index = 0U; byte_index < length; ++byte_index)
  {
    uint32_t bit_index;

    crc ^= data[byte_index];
    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
      crc = (crc & 0x80U) != 0U
              ? (uint8_t)((crc << 1U) ^ 0x31U)
              : (uint8_t)(crc << 1U);
    }
  }

  return crc;
}

static AHT20_Status AHT20_DecodeFrame(const uint8_t frame[7],
                                      AHT20_Measurement *measurement)
{
  uint32_t raw_humidity;
  uint32_t raw_temperature;
  uint64_t scaled;
  if ((frame[0] & AHT20_STATUS_BUSY_MASK) != 0U)
  {
    return AHT20_TIMEOUT;
  }
  if (AHT20_CalculateCrc(frame, 6U) != frame[6])
  {
    return AHT20_CRC_ERROR;
  }

  raw_humidity = ((uint32_t)frame[1] << 12U)
                 | ((uint32_t)frame[2] << 4U)
                 | ((uint32_t)frame[3] >> 4U);
  raw_temperature = (((uint32_t)frame[3] & 0x0FU) << 16U)
                    | ((uint32_t)frame[4] << 8U)
                    | (uint32_t)frame[5];

  scaled = ((uint64_t)raw_humidity * 10000U) + (1U << 19U);
  measurement->humidity_centi_percent = (uint32_t)(scaled >> 20U);
  if (measurement->humidity_centi_percent > 10000U)
  {
    measurement->humidity_centi_percent = 10000U;
  }

  scaled = ((uint64_t)raw_temperature * 20000U) + (1U << 19U);
  measurement->temperature_centi_c = (int32_t)(scaled >> 20U) - 5000;

  return AHT20_OK;
}

/* Split-phase access for the LED service; no conversion wait blocks BLE RX. */
AHT20_Status AHT20_BeginMeasurement(I2C_HandleTypeDef *i2c)
{
  uint8_t status;
  uint8_t command[] = {0xACU, 0x33U, 0x00U};
  if (i2c == NULL) return AHT20_INVALID_ARGUMENT;
  if (HAL_I2C_Master_Receive(i2c, AHT20_I2C_ADDRESS, &status, 1U, 5U) != HAL_OK)
    return AHT20_I2C_ERROR;
  if ((status & AHT20_STATUS_BUSY_MASK) != 0U) return AHT20_BUSY;
  if ((status & AHT20_STATUS_CALIBRATED_MASK) != AHT20_STATUS_CALIBRATED_MASK)
    return AHT20_NOT_CALIBRATED;
  return HAL_I2C_Master_Transmit(i2c, AHT20_I2C_ADDRESS, command,
                                 sizeof(command), 5U) == HAL_OK
           ? AHT20_OK : AHT20_I2C_ERROR;
}

/* Caller waits at least 80 ms after Begin, then retries BUSY up to a deadline. */
AHT20_Status AHT20_FinishMeasurement(I2C_HandleTypeDef *i2c,
                                     AHT20_Measurement *measurement)
{
  uint8_t frame[7];
  if (i2c == NULL || measurement == NULL) return AHT20_INVALID_ARGUMENT;
  if (HAL_I2C_Master_Receive(i2c, AHT20_I2C_ADDRESS, frame,
                             sizeof(frame), 5U) != HAL_OK)
    return AHT20_I2C_ERROR;
  if ((frame[0] & AHT20_STATUS_BUSY_MASK) != 0U) return AHT20_BUSY;
  return AHT20_DecodeFrame(frame, measurement);
}

AHT20_Status AHT20_ReadMeasurement(I2C_HandleTypeDef *i2c,
                                   AHT20_Measurement *measurement)
{
  static const uint8_t trigger_command[] = {0xACU, 0x33U, 0x00U};
  uint8_t status_byte;
  uint8_t frame[7];
  uint32_t start_tick;

  if ((i2c == NULL) || (measurement == NULL))
  {
    return AHT20_INVALID_ARGUMENT;
  }

  if (HAL_I2C_IsDeviceReady(i2c, AHT20_I2C_ADDRESS, 3U, 10U) != HAL_OK)
  {
    return AHT20_NOT_FOUND;
  }

  if (HAL_I2C_Master_Receive(i2c, AHT20_I2C_ADDRESS, &status_byte, 1U,
                            AHT20_IO_TIMEOUT_MS) != HAL_OK)
  {
    return AHT20_I2C_ERROR;
  }

  /* AHT20 is factory calibrated. Do not use the AHT10-only 0xBE fallback. */
  if ((status_byte & AHT20_STATUS_CALIBRATED_MASK)
      != AHT20_STATUS_CALIBRATED_MASK)
  {
    return AHT20_NOT_CALIBRATED;
  }

  HAL_Delay(10U);
  if (HAL_I2C_Master_Transmit(i2c, AHT20_I2C_ADDRESS,
                             (uint8_t *)trigger_command,
                             (uint16_t)sizeof(trigger_command),
                             AHT20_IO_TIMEOUT_MS) != HAL_OK)
  {
    return AHT20_I2C_ERROR;
  }

  HAL_Delay(AHT20_MEASUREMENT_TIME_MS);
  start_tick = HAL_GetTick();
  do
  {
    if (HAL_I2C_Master_Receive(i2c, AHT20_I2C_ADDRESS, &status_byte, 1U,
                              AHT20_IO_TIMEOUT_MS) != HAL_OK)
    {
      return AHT20_I2C_ERROR;
    }
    if ((status_byte & AHT20_STATUS_BUSY_MASK) == 0U)
    {
      break;
    }
    HAL_Delay(10U);
  } while ((HAL_GetTick() - start_tick) < AHT20_MEASUREMENT_TIMEOUT_MS);

  if ((status_byte & AHT20_STATUS_BUSY_MASK) != 0U)
  {
    return AHT20_TIMEOUT;
  }

  if (HAL_I2C_Master_Receive(i2c, AHT20_I2C_ADDRESS, frame,
                            (uint16_t)sizeof(frame),
                            AHT20_IO_TIMEOUT_MS) != HAL_OK)
  {
    return AHT20_I2C_ERROR;
  }
  return AHT20_DecodeFrame(frame, measurement);
}

const char *AHT20_StatusString(AHT20_Status status)
{
  switch (status)
  {
    case AHT20_BUSY:
      return "busy";
    case AHT20_OK:
      return "OK";
    case AHT20_INVALID_ARGUMENT:
      return "invalid argument";
    case AHT20_NOT_FOUND:
      return "not found at I2C address 0x38";
    case AHT20_NOT_CALIBRATED:
      return "calibration status is not ready";
    case AHT20_I2C_ERROR:
      return "I2C error";
    case AHT20_TIMEOUT:
      return "measurement timeout";
    case AHT20_CRC_ERROR:
      return "CRC error";
    default:
      return "unknown error";
  }
}
