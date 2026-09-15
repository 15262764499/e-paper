#ifndef IMAGE_TRANSFER_H
#define IMAGE_TRANSFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g0xx_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMAGE_PROTOCOL_VERSION       1U
#define IMAGE_PROTOCOL_MAX_DATA      128U
#define IMAGE_PROTOCOL_BOARD_ID_SIZE 12U
#define IMAGE_PROTOCOL_FORMAT_NATIVE_2BPP 1U
#define IMAGE_RENDER_PROFILE_PHOTO        0x00U
#define IMAGE_RENDER_PROFILE_CALENDAR_V1  0x11U
#define IMAGE_CAP_CALENDAR_TEMPLATE       0x0001U
#define IMAGE_CAP_TIME_SYNC               0x0002U
#define IMAGE_CAP_RTC_LOCAL_CALENDAR      0x0004U
#define IMAGE_CAP_PERSISTENT_TEMPLATE     0x0008U
#define IMAGE_CAP_BLE_OTA                 0x0010U
#define IMAGE_CAP_LED_CONTROL             0x0020U

typedef enum
{
  IMAGE_MSG_HELLO_REQ = 0x01,
  IMAGE_MSG_AUTH_PROVE = 0x02,
  IMAGE_MSG_SENSOR_REQ = 0x03,
  IMAGE_MSG_TIME_SYNC = 0x04,
  IMAGE_MSG_LED_SET = 0x05,
  IMAGE_MSG_LED_GET = 0x06,
  IMAGE_MSG_IMAGE_BEGIN = 0x10,
  IMAGE_MSG_IMAGE_DATA = 0x11,
  IMAGE_MSG_IMAGE_END = 0x12,
  IMAGE_MSG_CANCEL = 0x13,
  IMAGE_MSG_PING = 0x14,
  IMAGE_MSG_OTA_QUERY = 0x20,
  IMAGE_MSG_OTA_ENTER = 0x21,

  IMAGE_MSG_HELLO_RSP = 0x81,
  IMAGE_MSG_ACK = 0x82,
  IMAGE_MSG_STATUS = 0x83,
  IMAGE_MSG_SENSOR_RSP = 0x84,
  IMAGE_MSG_OTA_INFO = 0x85,
  IMAGE_MSG_LED_STATE = 0x86
} ImageProtocol_MessageType;

typedef enum
{
  IMAGE_STATE_IDLE = 0,
  IMAGE_STATE_AUTHENTICATED = 1,
  IMAGE_STATE_PREPARING = 2,
  IMAGE_STATE_RECEIVING = 3,
  IMAGE_STATE_VERIFYING = 4,
  IMAGE_STATE_REFRESHING = 5,
  IMAGE_STATE_COMPLETE = 6,
  IMAGE_STATE_ERROR = 7
} ImageTransfer_State;

typedef enum
{
  IMAGE_RESULT_OK = 0,
  IMAGE_RESULT_BAD_PACKET = 1,
  IMAGE_RESULT_BAD_VERSION = 2,
  IMAGE_RESULT_WRONG_DEVICE = 3,
  IMAGE_RESULT_NOT_AUTHORIZED = 4,
  IMAGE_RESULT_BUSY = 5,
  IMAGE_RESULT_BAD_SIZE = 6,
  IMAGE_RESULT_BAD_OFFSET = 7,
  IMAGE_RESULT_BAD_CRC = 8,
  IMAGE_RESULT_EPD_ERROR = 9,
  IMAGE_RESULT_TIMEOUT = 10,
  IMAGE_RESULT_UART_OVERFLOW = 11,
  IMAGE_RESULT_AUTH_FAILED = 12,
  IMAGE_RESULT_SENSOR_ERROR = 13,
  IMAGE_RESULT_TIME_ERROR = 14,
  IMAGE_RESULT_FLASH_ERROR = 15,
  IMAGE_RESULT_OTA_REJECTED = 16
} ImageTransfer_Result;

bool ImageTransfer_Init(UART_HandleTypeDef *ble_uart,
                        SPI_HandleTypeDef *epd_spi,
                        I2C_HandleTypeDef *sensor_i2c);
bool ImageTransfer_StartReceive(void);
void ImageTransfer_OnRxEvent(UART_HandleTypeDef *uart, uint16_t size);
void ImageTransfer_OnUartError(UART_HandleTypeDef *uart);
void ImageTransfer_Poll(void);

void ImageTransfer_GetBoardId(uint8_t board_id[IMAGE_PROTOCOL_BOARD_ID_SIZE]);
ImageTransfer_State ImageTransfer_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_TRANSFER_H */
