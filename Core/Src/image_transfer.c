#include "image_transfer.h"
#include "led_595.h"

#include "aht20.h"
#include "calendar_flash.h"
#include "calendar_service.h"
#include "gdem042f86.h"
#include "main.h"
#include "ota_control.h"
#include "ota_layout.h"
#include "pairing_security.h"
#include "rtc_calendar.h"

#include <string.h>

#define PROTOCOL_MAGIC_0              0x45U
#define PROTOCOL_MAGIC_1              0x50U
#define PROTOCOL_HEADER_SIZE          8U
#define PROTOCOL_CRC_SIZE             4U
#define PROTOCOL_MAX_PAYLOAD          (IMAGE_PROTOCOL_MAX_DATA + 4U)
#define PROTOCOL_MAX_PACKET           \
  (PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD + PROTOCOL_CRC_SIZE)
#define UART_DMA_BUFFER_SIZE          256U
#define UART_RING_SIZE                1024U
#define IMAGE_BEGIN_PAYLOAD_SIZE      18U
#define IMAGE_END_PAYLOAD_SIZE        (8U + PAIRING_PROOF_SIZE)
#define SENSOR_REPORT_PAYLOAD_SIZE    (10U + PAIRING_PROOF_SIZE)
#define TIME_SYNC_PAYLOAD_SIZE        (10U + PAIRING_PROOF_SIZE)
#define OTA_ENTER_PAYLOAD_SIZE        (8U + PAIRING_PROOF_SIZE)
#define IMAGE_AUTH_TIMEOUT_MS         60000U
#define IMAGE_TRANSFER_TIMEOUT_MS     10000U
#define EPD_POWER_OFF_MS              200U
#define EPD_POWER_STABILIZE_MS        300U
#define EPD_RESET_RELEASE_MS          20U
#define EPD_SPI_TIMEOUT_MS            1000U
#define EPD_BUSY_TIMEOUT_MS           60000U

typedef struct
{
  UART_HandleTypeDef *uart;
  I2C_HandleTypeDef *sensor_i2c;
  uint8_t board_id[IMAGE_PROTOCOL_BOARD_ID_SIZE];
  uint8_t challenge[PAIRING_CHALLENGE_SIZE];
  bool initialized;
  bool challenge_valid;
  bool authorized;
  bool uart_restart_pending;
  volatile bool uart_overflow;
  ImageTransfer_State state;
  ImageTransfer_Result last_result;
  uint32_t challenge_tick;
  uint32_t last_activity_tick;
  uint32_t image_id;
  uint32_t expected_size;
  uint32_t expected_crc;
  uint32_t received_size;
  uint32_t running_crc;
  uint8_t render_profile;
  AHT20_Measurement sensor_measurement;
  AHT20_Status sensor_status;
  bool sensor_report_valid;
} ImageTransfer_Context;

static ImageTransfer_Context s_transfer;
static uint8_t s_dma_rx[UART_DMA_BUFFER_SIZE];
static uint8_t s_ring[UART_RING_SIZE];
static volatile uint16_t s_ring_head;
static volatile uint16_t s_ring_tail;
static uint8_t s_packet[PROTOCOL_MAX_PACKET];
static uint16_t s_packet_size;
static uint16_t s_packet_expected;

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
         | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t ReadU64(const uint8_t *data)
{
  uint64_t value = 0U;
  uint8_t index;
  for (index = 0U; index < 8U; ++index)
  {
    value |= (uint64_t)data[index] << (index * 8U);
  }
  return value;
}

static void WriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

static uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, size_t size)
{
  size_t index;

  for (index = 0U; index < size; ++index)
  {
    uint32_t bit;
    crc ^= data[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return crc;
}

static uint32_t CRC32_Calculate(const uint8_t *data, size_t size)
{
  return CRC32_Update(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

static bool UART_StartReceive(void)
{
  HAL_StatusTypeDef status;

  status = HAL_UARTEx_ReceiveToIdle_DMA(s_transfer.uart, s_dma_rx,
                                        sizeof(s_dma_rx));
  if (status != HAL_OK)
  {
    return false;
  }
  if (s_transfer.uart->hdmarx != NULL)
  {
    __HAL_DMA_DISABLE_IT(s_transfer.uart->hdmarx, DMA_IT_HT);
  }
  s_transfer.uart_restart_pending = false;
  return true;
}

static bool RingPop(uint8_t *value)
{
  uint16_t tail = s_ring_tail;

  if (tail == s_ring_head)
  {
    return false;
  }
  *value = s_ring[tail];
  s_ring_tail = (uint16_t)((tail + 1U) % UART_RING_SIZE);
  return true;
}

static void ParserReset(void)
{
  s_packet_size = 0U;
  s_packet_expected = 0U;
}

static bool SendPacket(uint8_t type, uint16_t sequence,
                       const uint8_t *payload, uint16_t payload_size)
{
  uint8_t packet[PROTOCOL_HEADER_SIZE + 48U + PROTOCOL_CRC_SIZE];
  uint32_t crc;
  uint16_t packet_size;

  if ((payload_size > 48U) || ((payload_size > 0U) && (payload == NULL)))
  {
    return false;
  }

  packet[0] = PROTOCOL_MAGIC_0;
  packet[1] = PROTOCOL_MAGIC_1;
  packet[2] = IMAGE_PROTOCOL_VERSION;
  packet[3] = type;
  WriteU16(&packet[4], sequence);
  WriteU16(&packet[6], payload_size);
  if (payload_size > 0U)
  {
    memcpy(&packet[8], payload, payload_size);
  }
  crc = CRC32_Calculate(&packet[2], 6U + payload_size);
  WriteU32(&packet[8U + payload_size], crc);
  packet_size = (uint16_t)(PROTOCOL_HEADER_SIZE + payload_size
                           + PROTOCOL_CRC_SIZE);
  return HAL_UART_Transmit(s_transfer.uart, packet, packet_size, 250U)
         == HAL_OK;
}

static void SendAck(uint16_t sequence, uint8_t acknowledged_type,
                    ImageTransfer_Result result)
{
  uint8_t payload[7];

  payload[0] = acknowledged_type;
  payload[1] = (uint8_t)result;
  WriteU32(&payload[2], s_transfer.received_size);
  payload[6] = (uint8_t)s_transfer.state;
  (void)SendPacket(IMAGE_MSG_ACK, sequence, payload, sizeof(payload));
}

static void SendStatus(uint16_t sequence, ImageTransfer_Result result)
{
  uint8_t payload[12];
  uint16_t progress = 0U;

  if (s_transfer.expected_size > 0U)
  {
    uint32_t calculated = (s_transfer.received_size * 1000U)
                          / s_transfer.expected_size;
    progress = (uint16_t)(calculated > 1000U ? 1000U : calculated);
  }
  payload[0] = (uint8_t)s_transfer.state;
  payload[1] = (uint8_t)result;
  WriteU16(&payload[2], progress);
  WriteU32(&payload[4], s_transfer.received_size);
  WriteU32(&payload[8], s_transfer.expected_size);
  (void)SendPacket(IMAGE_MSG_STATUS, sequence, payload, sizeof(payload));
}

static void ClearAuthorization(void)
{
  s_transfer.authorized = false;
  s_transfer.challenge_valid = false;
  memset(s_transfer.challenge, 0, sizeof(s_transfer.challenge));
  memset(&s_transfer.sensor_measurement, 0,
         sizeof(s_transfer.sensor_measurement));
  s_transfer.sensor_status = AHT20_INVALID_ARGUMENT;
  s_transfer.sensor_report_valid = false;
  PairingSecurity_ClearSession();
}

static void SendSensorReport(uint16_t sequence)
{
  uint8_t payload[SENSOR_REPORT_PAYLOAD_SIZE];
  ImageTransfer_Result result;

  if (!s_transfer.sensor_report_valid)
  {
    s_transfer.sensor_status = AHT20_ReadMeasurement(
      s_transfer.sensor_i2c, &s_transfer.sensor_measurement);
    s_transfer.sensor_report_valid = true;
  }

  result = s_transfer.sensor_status == AHT20_OK
             ? IMAGE_RESULT_OK : IMAGE_RESULT_SENSOR_ERROR;
  payload[0] = (uint8_t)result;
  payload[1] = (uint8_t)s_transfer.sensor_status;
  WriteU32(&payload[2],
           (uint32_t)s_transfer.sensor_measurement.temperature_centi_c);
  WriteU32(&payload[6],
           s_transfer.sensor_measurement.humidity_centi_percent);
  if (!PairingSecurity_SensorTag(
        sequence, payload[0], payload[1],
        s_transfer.sensor_measurement.temperature_centi_c,
        s_transfer.sensor_measurement.humidity_centi_percent,
        &payload[10]))
  {
    return;
  }
  (void)SendPacket(IMAGE_MSG_SENSOR_RSP, sequence, payload,
                   sizeof(payload));
}

static void PanelForceOff(void)
{
  CalendarFlash_Abort();
  (void)GDEM042F86_StreamAbort();
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
}

static void AbortTransfer(ImageTransfer_Result result, uint16_t sequence)
{
  PanelForceOff();
  ClearAuthorization();
  s_transfer.state = IMAGE_STATE_ERROR;
  s_transfer.last_result = result;
  SendStatus(sequence, result);
}

static GDEM042F86_Status PanelPrepare(void)
{
  GDEM042F86_Status status;

  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
  HAL_Delay(EPD_POWER_OFF_MS);
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(EPD_POWER_STABILIZE_MS);
  HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(EPD_RESET_RELEASE_MS);

  status = GDEM042F86_Wake(false);
  if (status == GDEM042F86_OK)
  {
    status = GDEM042F86_StreamBegin();
  }
  return status;
}

static void HandleHello(uint16_t sequence, const uint8_t *payload,
                        uint16_t payload_size)
{
  uint8_t response[43];

  if ((s_transfer.state == IMAGE_STATE_PREPARING)
      || (s_transfer.state == IMAGE_STATE_RECEIVING)
      || (s_transfer.state == IMAGE_STATE_VERIFYING)
      || (s_transfer.state == IMAGE_STATE_REFRESHING))
  {
    SendAck(sequence, IMAGE_MSG_HELLO_REQ, IMAGE_RESULT_BUSY);
    return;
  }
  if ((payload_size != IMAGE_PROTOCOL_BOARD_ID_SIZE)
      || (memcmp(payload, s_transfer.board_id,
                 IMAGE_PROTOCOL_BOARD_ID_SIZE) != 0))
  {
    SendAck(sequence, IMAGE_MSG_HELLO_REQ, IMAGE_RESULT_WRONG_DEVICE);
    return;
  }

  ClearAuthorization();
  PairingSecurity_GenerateChallenge(s_transfer.board_id,
                                    s_transfer.challenge);
  s_transfer.challenge_valid = true;
  s_transfer.challenge_tick = HAL_GetTick();
  s_transfer.state = IMAGE_STATE_IDLE;
  s_transfer.last_result = IMAGE_RESULT_OK;
  s_transfer.received_size = 0U;
  s_transfer.expected_size = GDEM042F86_FRAME_BYTES;

  response[0] = IMAGE_RESULT_OK;
  memcpy(&response[1], s_transfer.board_id, IMAGE_PROTOCOL_BOARD_ID_SIZE);
  memcpy(&response[13], s_transfer.challenge, PAIRING_CHALLENGE_SIZE);
  WriteU16(&response[29], GDEM042F86_WIDTH);
  WriteU16(&response[31], GDEM042F86_HEIGHT);
  WriteU32(&response[33], GDEM042F86_FRAME_BYTES);
  response[37] = IMAGE_PROTOCOL_FORMAT_NATIVE_2BPP;
  WriteU16(&response[38], IMAGE_PROTOCOL_MAX_DATA);
  response[40] = (uint8_t)s_transfer.state;
  WriteU16(&response[41], IMAGE_CAP_CALENDAR_TEMPLATE | IMAGE_CAP_TIME_SYNC
                          | IMAGE_CAP_RTC_LOCAL_CALENDAR
                          | IMAGE_CAP_PERSISTENT_TEMPLATE
                          | IMAGE_CAP_BLE_OTA | IMAGE_CAP_LED_CONTROL);
  (void)SendPacket(IMAGE_MSG_HELLO_RSP, sequence, response,
                   sizeof(response));
}

static void HandleTimeSync(uint16_t sequence, const uint8_t *payload,
                           uint16_t payload_size)
{
  uint64_t epoch_seconds;
  int16_t utc_offset_minutes;

  if (!s_transfer.authorized)
  {
    SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if ((s_transfer.state == IMAGE_STATE_PREPARING)
      || (s_transfer.state == IMAGE_STATE_RECEIVING)
      || (s_transfer.state == IMAGE_STATE_VERIFYING)
      || (s_transfer.state == IMAGE_STATE_REFRESHING))
  {
    SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_BUSY);
    return;
  }
  if (payload_size != TIME_SYNC_PAYLOAD_SIZE)
  {
    SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_BAD_SIZE);
    return;
  }
  epoch_seconds = ReadU64(payload);
  utc_offset_minutes = (int16_t)ReadU16(&payload[8]);
  if (!PairingSecurity_VerifyTimeSync(sequence, epoch_seconds,
                                      utc_offset_minutes, &payload[10]))
  {
    SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_AUTH_FAILED);
    return;
  }
  if (!RTC_Calendar_SetUnix(epoch_seconds, utc_offset_minutes))
  {
    SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_TIME_ERROR);
    return;
  }
  s_transfer.last_activity_tick = HAL_GetTick();
  SendAck(sequence, IMAGE_MSG_TIME_SYNC, IMAGE_RESULT_OK);
}

static void HandleAuth(uint16_t sequence, const uint8_t *payload,
                       uint16_t payload_size)
{
  if (!s_transfer.challenge_valid
      || ((uint32_t)(HAL_GetTick() - s_transfer.challenge_tick)
          >= IMAGE_AUTH_TIMEOUT_MS))
  {
    ClearAuthorization();
    SendAck(sequence, IMAGE_MSG_AUTH_PROVE, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if ((payload_size != PAIRING_PROOF_SIZE)
      || !PairingSecurity_VerifyProof(s_transfer.board_id,
                                      s_transfer.challenge, payload))
  {
    ClearAuthorization();
    SendAck(sequence, IMAGE_MSG_AUTH_PROVE, IMAGE_RESULT_AUTH_FAILED);
    return;
  }

  s_transfer.authorized = true;
  s_transfer.state = IMAGE_STATE_AUTHENTICATED;
  s_transfer.challenge_valid = false;
  memset(s_transfer.challenge, 0, sizeof(s_transfer.challenge));
  s_transfer.last_activity_tick = HAL_GetTick();
  SendAck(sequence, IMAGE_MSG_AUTH_PROVE, IMAGE_RESULT_OK);
  SendSensorReport(sequence);
}

static void HandleSensorRequest(uint16_t sequence, uint16_t payload_size)
{
  if (!s_transfer.authorized)
  {
    SendAck(sequence, IMAGE_MSG_SENSOR_REQ, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (payload_size != 0U)
  {
    SendAck(sequence, IMAGE_MSG_SENSOR_REQ, IMAGE_RESULT_BAD_SIZE);
    return;
  }
  SendSensorReport(sequence);
}

static void HandleLed(uint16_t sequence, uint8_t type,
                       const uint8_t *payload, uint16_t payload_size)
{
  if (!s_transfer.authorized)
  {
    SendAck(sequence, type, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (s_transfer.state != IMAGE_STATE_AUTHENTICATED)
  {
    SendAck(sequence, type, IMAGE_RESULT_BUSY);
    return;
  }
  if (payload_size != (type == IMAGE_MSG_LED_SET ? 17U : 0U))
  {
    SendAck(sequence, type, IMAGE_RESULT_BAD_SIZE);
    return;
  }
  if (type == IMAGE_MSG_LED_SET)
  {
    if (payload[0] >= LED_MODE_COUNT)
    {
      SendAck(sequence, type, IMAGE_RESULT_BAD_PACKET);
      return;
    }
    if (!PairingSecurity_VerifyLedSet(sequence, payload[0], &payload[1]))
    {
      SendAck(sequence, type, IMAGE_RESULT_AUTH_FAILED);
      return;
    }
    (void)LED595_SetMode((LED_Mode)payload[0]);
  }
  LED595_Status state;
  uint8_t response[35];
  LED595_GetStatus(&state);
  response[0] = IMAGE_RESULT_OK;
  response[1] = state.mode;
  response[2] = state.effective_mode;
  response[3] = state.humidity_valid ? 1U : 0U;
  WriteU32(&response[4], state.humidity);
  WriteU32(&response[8], state.elapsed_ms);
  memcpy(&response[12], &state.duty[1], 7U);
  if (!PairingSecurity_LedStateTag(sequence, response, &response[19]))
  {
    SendAck(sequence, type, IMAGE_RESULT_AUTH_FAILED);
    return;
  }
  s_transfer.last_activity_tick = HAL_GetTick();
  (void)SendPacket(IMAGE_MSG_LED_STATE, sequence, response, sizeof(response));
}

static void HandleOtaQuery(uint16_t sequence, uint16_t payload_size)
{
  uint8_t response[23];

  if (!s_transfer.authorized)
  {
    SendAck(sequence, IMAGE_MSG_OTA_QUERY, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (payload_size != 0U)
  {
    SendAck(sequence, IMAGE_MSG_OTA_QUERY, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  response[0] = IMAGE_RESULT_OK;
  WriteU32(&response[1], OTA_APPLICATION_VERSION);
  WriteU16(&response[5], OTA_BOOT_PROTOCOL_VERSION);
  WriteU32(&response[7], OTA_APPLICATION_MAX_SIZE);
  WriteU32(&response[11], OTA_APPLICATION_BASE_ADDRESS);
  WriteU16(&response[15], OTA_STM32_DEVICE_ID);
  WriteU16(&response[17], OTA_HARDWARE_REVISION);
  WriteU32(&response[19], OTA_PRODUCT_ID);
  (void)SendPacket(IMAGE_MSG_OTA_INFO, sequence, response,
                   sizeof(response));
}

static void HandleOtaEnter(uint16_t sequence, const uint8_t *payload,
                           uint16_t payload_size)
{
  uint32_t firmware_version;
  uint32_t nonce;

  if (!s_transfer.authorized
      || (s_transfer.state != IMAGE_STATE_AUTHENTICATED))
  {
    SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (payload_size != OTA_ENTER_PAYLOAD_SIZE)
  {
    SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  firmware_version = ReadU32(payload);
  nonce = ReadU32(&payload[4]);
  if ((firmware_version == 0U)
      || !PairingSecurity_VerifyOtaEnter(sequence, firmware_version, nonce,
                                         &payload[8]))
  {
    SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_AUTH_FAILED);
    return;
  }
  if (firmware_version <= OTA_APPLICATION_VERSION)
  {
    SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_OTA_REJECTED);
    return;
  }
  if (!OTA_Control_RequestBootloader())
  {
    SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_OTA_REJECTED);
    return;
  }

  PanelForceOff();
  SendAck(sequence, IMAGE_MSG_OTA_ENTER, IMAGE_RESULT_OK);
  HAL_Delay(50U);
  NVIC_SystemReset();
}

static void HandleImageBegin(uint16_t sequence, const uint8_t *payload,
                             uint16_t payload_size)
{
  uint16_t width;
  uint16_t height;
  uint32_t total_size;
  GDEM042F86_Status epd_status;

  if (!s_transfer.authorized)
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (s_transfer.state == IMAGE_STATE_RECEIVING)
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_BUSY);
    return;
  }
  if (payload_size != IMAGE_BEGIN_PAYLOAD_SIZE)
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  width = ReadU16(&payload[4]);
  height = ReadU16(&payload[6]);
  total_size = ReadU32(&payload[10]);
  s_transfer.render_profile = payload[9];
  if ((width != GDEM042F86_WIDTH) || (height != GDEM042F86_HEIGHT)
      || (payload[8] != IMAGE_PROTOCOL_FORMAT_NATIVE_2BPP)
      || ((s_transfer.render_profile != IMAGE_RENDER_PROFILE_PHOTO)
          && (s_transfer.render_profile
              != IMAGE_RENDER_PROFILE_CALENDAR_V1))
      || (total_size != GDEM042F86_FRAME_BYTES))
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  s_transfer.image_id = ReadU32(&payload[0]);
  s_transfer.expected_size = total_size;
  s_transfer.expected_crc = ReadU32(&payload[14]);
  s_transfer.received_size = 0U;
  s_transfer.running_crc = 0xFFFFFFFFU;
  if ((s_transfer.render_profile == IMAGE_RENDER_PROFILE_CALENDAR_V1)
      && !RTC_Calendar_IsValid())
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_TIME_ERROR);
    return;
  }
  if (!PairingSecurity_ImageBegin(s_transfer.image_id,
                                  s_transfer.expected_size,
                                  s_transfer.expected_crc,
                                  s_transfer.render_profile))
  {
    AbortTransfer(IMAGE_RESULT_NOT_AUTHORIZED, sequence);
    return;
  }
  s_transfer.state = IMAGE_STATE_PREPARING;
  SendStatus(sequence, IMAGE_RESULT_OK);

  if (s_transfer.render_profile == IMAGE_RENDER_PROFILE_CALENDAR_V1)
  {
    CalendarService_SetActive(false);
    if (!CalendarFlash_Begin())
    {
      AbortTransfer(IMAGE_RESULT_FLASH_ERROR, sequence);
      return;
    }
  }
  else
  {
    epd_status = PanelPrepare();
    if (epd_status != GDEM042F86_OK)
    {
      AbortTransfer(IMAGE_RESULT_EPD_ERROR, sequence);
      return;
    }
  }

  s_transfer.state = IMAGE_STATE_RECEIVING;
  s_transfer.last_activity_tick = HAL_GetTick();
  SendAck(sequence, IMAGE_MSG_IMAGE_BEGIN, IMAGE_RESULT_OK);
}

static void HandleImageData(uint16_t sequence, const uint8_t *payload,
                            uint16_t payload_size)
{
  uint32_t offset;
  uint16_t data_size;

  if (!s_transfer.authorized || (s_transfer.state != IMAGE_STATE_RECEIVING))
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_DATA, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if ((payload_size <= 4U)
      || (payload_size > (IMAGE_PROTOCOL_MAX_DATA + 4U)))
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_DATA, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  offset = ReadU32(payload);
  data_size = (uint16_t)(payload_size - 4U);
  if ((offset < s_transfer.received_size)
      && ((offset + data_size) <= s_transfer.received_size))
  {
    /* A lost ACK may cause the app to repeat an already committed chunk.
       Re-ACK it without writing it into the panel RAM a second time. */
    SendAck(sequence, IMAGE_MSG_IMAGE_DATA, IMAGE_RESULT_OK);
    return;
  }
  if ((offset != s_transfer.received_size)
      || (data_size > (s_transfer.expected_size - s_transfer.received_size)))
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_DATA, IMAGE_RESULT_BAD_OFFSET);
    return;
  }

  if (s_transfer.render_profile == IMAGE_RENDER_PROFILE_CALENDAR_V1)
  {
    if (!CalendarFlash_Write(offset, &payload[4], data_size))
    {
      AbortTransfer(IMAGE_RESULT_FLASH_ERROR, sequence);
      return;
    }
  }
  else if (GDEM042F86_StreamWrite(&payload[4], data_size)
           != GDEM042F86_OK)
  {
    AbortTransfer(IMAGE_RESULT_EPD_ERROR, sequence);
    return;
  }
  s_transfer.running_crc = CRC32_Update(s_transfer.running_crc, &payload[4],
                                        data_size);
  if (!PairingSecurity_ImageUpdate(&payload[4], data_size))
  {
    AbortTransfer(IMAGE_RESULT_AUTH_FAILED, sequence);
    return;
  }
  s_transfer.received_size += data_size;
  s_transfer.last_activity_tick = HAL_GetTick();
  SendAck(sequence, IMAGE_MSG_IMAGE_DATA, IMAGE_RESULT_OK);
}

static void HandleImageEnd(uint16_t sequence, const uint8_t *payload,
                           uint16_t payload_size)
{
  uint32_t image_id;
  uint32_t declared_crc;
  uint32_t calculated_crc;
  GDEM042F86_Status status;

  if (!s_transfer.authorized || (s_transfer.state != IMAGE_STATE_RECEIVING))
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_END, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  if (payload_size != IMAGE_END_PAYLOAD_SIZE)
  {
    SendAck(sequence, IMAGE_MSG_IMAGE_END, IMAGE_RESULT_BAD_SIZE);
    return;
  }

  image_id = ReadU32(payload);
  declared_crc = ReadU32(&payload[4]);
  calculated_crc = s_transfer.running_crc ^ 0xFFFFFFFFU;
  s_transfer.state = IMAGE_STATE_VERIFYING;
  if ((image_id != s_transfer.image_id)
      || (s_transfer.received_size != s_transfer.expected_size)
      || (declared_crc != s_transfer.expected_crc)
      || (calculated_crc != s_transfer.expected_crc))
  {
    AbortTransfer(IMAGE_RESULT_BAD_CRC, sequence);
    return;
  }
  if (!PairingSecurity_ImageVerify(&payload[8]))
  {
    AbortTransfer(IMAGE_RESULT_AUTH_FAILED, sequence);
    return;
  }

  s_transfer.state = IMAGE_STATE_REFRESHING;
  SendStatus(sequence, IMAGE_RESULT_OK);
  if (s_transfer.render_profile == IMAGE_RENDER_PROFILE_CALENDAR_V1)
  {
    RTC_CalendarDateTime now;
    if (!RTC_Calendar_Get(&now)
        || !CalendarFlash_Commit(s_transfer.expected_crc,
                                RTC_Calendar_DateCode(&now),
                                s_transfer.render_profile))
    {
      AbortTransfer(IMAGE_RESULT_FLASH_ERROR, sequence);
      return;
    }
    CalendarService_SetActive(true);
    status = CalendarService_RenderNow() ? GDEM042F86_OK
                                         : GDEM042F86_INVALID_STATE;
  }
  else
  {
    status = GDEM042F86_StreamCommit();
    if (status == GDEM042F86_OK)
    {
      status = GDEM042F86_Sleep();
    }
    if (status == GDEM042F86_OK)
    {
      CalendarService_SetActive(false);
    }
  }
  HAL_GPIO_WritePin(EPD_POWER_EN_GPIO_Port, EPD_POWER_EN_Pin,
                    GPIO_PIN_RESET);
  if (status != GDEM042F86_OK)
  {
    AbortTransfer(IMAGE_RESULT_EPD_ERROR, sequence);
    return;
  }

  ClearAuthorization();
  s_transfer.state = IMAGE_STATE_COMPLETE;
  s_transfer.last_result = IMAGE_RESULT_OK;
  SendAck(sequence, IMAGE_MSG_IMAGE_END, IMAGE_RESULT_OK);
  SendStatus(sequence, IMAGE_RESULT_OK);
}

static void HandleCancel(uint16_t sequence)
{
  bool transfer_active = (s_transfer.state == IMAGE_STATE_PREPARING)
                         || (s_transfer.state == IMAGE_STATE_RECEIVING)
                         || (s_transfer.state == IMAGE_STATE_VERIFYING)
                         || (s_transfer.state == IMAGE_STATE_REFRESHING);

  if (transfer_active && !s_transfer.authorized)
  {
    SendAck(sequence, IMAGE_MSG_CANCEL, IMAGE_RESULT_NOT_AUTHORIZED);
    return;
  }
  PanelForceOff();
  ClearAuthorization();
  s_transfer.state = IMAGE_STATE_IDLE;
  s_transfer.last_result = IMAGE_RESULT_OK;
  SendAck(sequence, IMAGE_MSG_CANCEL, IMAGE_RESULT_OK);
}

static void HandlePacket(const uint8_t *packet, uint16_t packet_size)
{
  uint8_t type = packet[3];
  uint16_t sequence = ReadU16(&packet[4]);
  uint16_t payload_size = ReadU16(&packet[6]);
  const uint8_t *payload = &packet[8];
  uint32_t received_crc = ReadU32(&packet[8U + payload_size]);
  uint32_t calculated_crc = CRC32_Calculate(&packet[2], 6U + payload_size);

  (void)packet_size;
  if (packet[2] != IMAGE_PROTOCOL_VERSION)
  {
    SendAck(sequence, type, IMAGE_RESULT_BAD_VERSION);
    return;
  }
  if (received_crc != calculated_crc)
  {
    SendAck(sequence, type, IMAGE_RESULT_BAD_PACKET);
    return;
  }

  switch (type)
  {
    case IMAGE_MSG_HELLO_REQ:
      HandleHello(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_AUTH_PROVE:
      HandleAuth(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_SENSOR_REQ:
      HandleSensorRequest(sequence, payload_size);
      break;
    case IMAGE_MSG_LED_SET:
    case IMAGE_MSG_LED_GET:
      HandleLed(sequence, type, payload, payload_size);
      break;
    case IMAGE_MSG_TIME_SYNC:
      HandleTimeSync(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_IMAGE_BEGIN:
      HandleImageBegin(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_IMAGE_DATA:
      HandleImageData(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_IMAGE_END:
      HandleImageEnd(sequence, payload, payload_size);
      break;
    case IMAGE_MSG_CANCEL:
      HandleCancel(sequence);
      break;
    case IMAGE_MSG_PING:
      SendAck(sequence, IMAGE_MSG_PING, IMAGE_RESULT_OK);
      break;
    case IMAGE_MSG_OTA_QUERY:
      HandleOtaQuery(sequence, payload_size);
      break;
    case IMAGE_MSG_OTA_ENTER:
      HandleOtaEnter(sequence, payload, payload_size);
      break;
    default:
      SendAck(sequence, type, IMAGE_RESULT_BAD_PACKET);
      break;
  }
}

static void ParserConsume(uint8_t value)
{
  if (s_packet_size == 0U)
  {
    if (value == PROTOCOL_MAGIC_0)
    {
      s_packet[s_packet_size++] = value;
    }
    return;
  }
  if (s_packet_size == 1U)
  {
    if (value == PROTOCOL_MAGIC_1)
    {
      s_packet[s_packet_size++] = value;
    }
    else if (value != PROTOCOL_MAGIC_0)
    {
      ParserReset();
    }
    return;
  }

  s_packet[s_packet_size++] = value;
  if (s_packet_size == PROTOCOL_HEADER_SIZE)
  {
    uint16_t payload_size = ReadU16(&s_packet[6]);
    if (payload_size > PROTOCOL_MAX_PAYLOAD)
    {
      ParserReset();
      return;
    }
    s_packet_expected = (uint16_t)(PROTOCOL_HEADER_SIZE + payload_size
                                   + PROTOCOL_CRC_SIZE);
  }
  if ((s_packet_expected > 0U) && (s_packet_size == s_packet_expected))
  {
    HandlePacket(s_packet, s_packet_size);
    ParserReset();
  }
}

bool ImageTransfer_Init(UART_HandleTypeDef *ble_uart,
                        SPI_HandleTypeDef *epd_spi,
                        I2C_HandleTypeDef *sensor_i2c)
{
  const GDEM042F86_Config epd_config = {
    .spi = epd_spi,
    .rst_port = EPD_RST_GPIO_Port,
    .rst_pin = EPD_RST_Pin,
    .dc_port = EPD_DC_GPIO_Port,
    .dc_pin = EPD_DC_Pin,
    .cs_port = EPD_CS_GPIO_Port,
    .cs_pin = EPD_CS_Pin,
    .busy_port = EPD_BUSY_GPIO_Port,
    .busy_pin = EPD_BUSY_Pin,
    .spi_timeout_ms = EPD_SPI_TIMEOUT_MS,
    .busy_timeout_ms = EPD_BUSY_TIMEOUT_MS
  };

  if ((ble_uart == NULL) || (epd_spi == NULL) || (sensor_i2c == NULL)
      || !PairingSecurity_SelfTest())
  {
    return false;
  }

  memset(&s_transfer, 0, sizeof(s_transfer));
  s_ring_head = 0U;
  s_ring_tail = 0U;
  ParserReset();
  s_transfer.uart = ble_uart;
  s_transfer.sensor_i2c = sensor_i2c;
  ImageTransfer_GetBoardId(s_transfer.board_id);
  s_transfer.state = IMAGE_STATE_IDLE;
  s_transfer.last_result = IMAGE_RESULT_OK;
  s_transfer.expected_size = GDEM042F86_FRAME_BYTES;
  PanelForceOff();
  if (GDEM042F86_Begin(&epd_config) != GDEM042F86_OK)
  {
    return false;
  }
  s_transfer.initialized = true;
  return true;
}

bool ImageTransfer_StartReceive(void)
{
  if (!s_transfer.initialized)
  {
    return false;
  }
  return UART_StartReceive();
}

void ImageTransfer_OnRxEvent(UART_HandleTypeDef *uart, uint16_t size)
{
  uint16_t index;

  if (!s_transfer.initialized || (uart != s_transfer.uart))
  {
    return;
  }
  if (size > sizeof(s_dma_rx))
  {
    size = sizeof(s_dma_rx);
  }
  for (index = 0U; index < size; ++index)
  {
    uint16_t head = s_ring_head;
    uint16_t next = (uint16_t)((head + 1U) % UART_RING_SIZE);
    if (next == s_ring_tail)
    {
      s_transfer.uart_overflow = true;
      break;
    }
    s_ring[head] = s_dma_rx[index];
    s_ring_head = next;
  }
  if (!UART_StartReceive())
  {
    s_transfer.uart_restart_pending = true;
  }
}

void ImageTransfer_OnUartError(UART_HandleTypeDef *uart)
{
  if (s_transfer.initialized && (uart == s_transfer.uart))
  {
    s_transfer.uart_restart_pending = true;
  }
}

void ImageTransfer_Poll(void)
{
  uint8_t value;

  if (!s_transfer.initialized)
  {
    return;
  }
  if (s_transfer.uart_restart_pending)
  {
    (void)HAL_UART_AbortReceive(s_transfer.uart);
    (void)UART_StartReceive();
  }
  if (s_transfer.uart_overflow)
  {
    s_transfer.uart_overflow = false;
    s_ring_tail = s_ring_head;
    ParserReset();
    AbortTransfer(IMAGE_RESULT_UART_OVERFLOW, 0U);
  }

  while (RingPop(&value))
  {
    ParserConsume(value);
  }

  if (s_transfer.challenge_valid
      && ((uint32_t)(HAL_GetTick() - s_transfer.challenge_tick)
          >= IMAGE_AUTH_TIMEOUT_MS))
  {
    ClearAuthorization();
  }
  if (s_transfer.authorized
      && (s_transfer.state == IMAGE_STATE_AUTHENTICATED)
      && ((uint32_t)(HAL_GetTick() - s_transfer.last_activity_tick)
          >= IMAGE_AUTH_TIMEOUT_MS))
  {
    ClearAuthorization();
    s_transfer.state = IMAGE_STATE_IDLE;
  }
  if ((s_transfer.state == IMAGE_STATE_RECEIVING)
      && ((uint32_t)(HAL_GetTick() - s_transfer.last_activity_tick)
          >= IMAGE_TRANSFER_TIMEOUT_MS))
  {
    AbortTransfer(IMAGE_RESULT_TIMEOUT, 0U);
  }
}

void ImageTransfer_GetBoardId(uint8_t board_id[IMAGE_PROTOCOL_BOARD_ID_SIZE])
{
  uint32_t uid[3];

  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();
  memcpy(board_id, uid, sizeof(uid));
}

ImageTransfer_State ImageTransfer_GetState(void)
{
  return s_transfer.state;
}
