#include "ota_bootloader.h"

#include "ota_crypto.h"
#include "ota_layout.h"
#include "ota_release_key.h"

#include <stddef.h>
#include <string.h>

#define PROTOCOL_MAGIC_0            0x45U
#define PROTOCOL_MAGIC_1            0x50U
#define PROTOCOL_VERSION            1U
#define PROTOCOL_HEADER_SIZE        8U
#define PROTOCOL_CRC_SIZE           4U
#define PROTOCOL_MAX_PAYLOAD        132U
#define PROTOCOL_MAX_PACKET         144U
#define OTA_DATA_MAX_SIZE           128U
#define OTA_BEGIN_PAYLOAD_SIZE      80U
#define OTA_TRANSFER_TIMEOUT_MS     15000U

#define BOOT_MSG_HELLO              0x30U
#define BOOT_MSG_OTA_BEGIN          0x31U
#define BOOT_MSG_OTA_DATA           0x32U
#define BOOT_MSG_OTA_END            0x33U
#define BOOT_MSG_OTA_ABORT          0x34U
#define BOOT_MSG_PING               0x14U
#define BOOT_MSG_HELLO_RSP          0xB0U
#define BOOT_MSG_ACK                0xB1U
#define BOOT_MSG_STATUS             0xB2U

typedef enum
{
  OTA_RESULT_OK = 0,
  OTA_RESULT_BAD_PACKET = 1,
  OTA_RESULT_BAD_VERSION = 2,
  OTA_RESULT_WRONG_DEVICE = 3,
  OTA_RESULT_BAD_STATE = 4,
  OTA_RESULT_BAD_SIZE = 5,
  OTA_RESULT_BAD_OFFSET = 6,
  OTA_RESULT_BAD_HASH = 7,
  OTA_RESULT_AUTH_FAILED = 8,
  OTA_RESULT_FLASH_ERROR = 9,
  OTA_RESULT_TIMEOUT = 10,
  OTA_RESULT_DOWNGRADE = 11,
  OTA_RESULT_BAD_VECTOR = 12
} OTA_Result;

typedef struct
{
  uint32_t progress_magic;
  uint32_t progress_inverse;
  uint32_t firmware_version;
  uint32_t firmware_size;
  uint8_t firmware_hash[32];
  uint8_t release_tag[32];
  uint32_t valid_magic;
  uint32_t valid_inverse;
} OTA_Metadata;

_Static_assert((sizeof(OTA_Metadata) % 8U) == 0U,
               "OTA metadata must be programmed in double words");
_Static_assert((offsetof(OTA_Metadata, valid_magic) % 8U) == 0U,
               "OTA validity marker must be double-word aligned");

typedef struct
{
  UART_HandleTypeDef *uart;
  uint8_t board_id[12];
  OTA_BootState state;
  uint32_t current_version;
  uint32_t firmware_version;
  uint32_t expected_size;
  uint32_t received_size;
  uint32_t programmed_size;
  uint32_t last_activity_tick;
  uint8_t expected_hash[32];
  uint8_t release_tag[32];
  uint8_t tail[8];
  uint8_t tail_size;
  OTA_SHA256_Context running_hash;
} OTA_Context;

static OTA_Context s_ota;
static uint8_t s_packet[PROTOCOL_MAX_PACKET];
static uint16_t s_packet_size;
static uint16_t s_packet_expected;

__attribute__((naked, noreturn))
static void JumpToResetHandler(
  uint32_t stack __attribute__((unused)),
  uint32_t reset __attribute__((unused)))
{
  __asm volatile (
    "msr msp, r0\n"
    "cpsie i\n"
    "bx r1\n"
  );
}

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
         | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
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

static uint32_t CRC32Update(uint32_t crc, const uint8_t *data, size_t size)
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

static uint32_t CRC32Calculate(const uint8_t *data, size_t size)
{
  return CRC32Update(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

static void GetBoardId(uint8_t board_id[12])
{
  uint32_t uid[3];
  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();
  memcpy(board_id, uid, sizeof(uid));
}

static bool SendPacket(uint8_t type, uint16_t sequence,
                       const uint8_t *payload, uint16_t payload_size)
{
  uint8_t packet[PROTOCOL_HEADER_SIZE + 48U + PROTOCOL_CRC_SIZE];
  uint32_t crc;
  uint16_t total;

  if ((payload_size > 48U) || ((payload_size > 0U) && (payload == NULL)))
  {
    return false;
  }
  packet[0] = PROTOCOL_MAGIC_0;
  packet[1] = PROTOCOL_MAGIC_1;
  packet[2] = PROTOCOL_VERSION;
  packet[3] = type;
  WriteU16(&packet[4], sequence);
  WriteU16(&packet[6], payload_size);
  if (payload_size > 0U)
  {
    memcpy(&packet[8], payload, payload_size);
  }
  crc = CRC32Calculate(&packet[2], 6U + payload_size);
  WriteU32(&packet[8U + payload_size], crc);
  total = (uint16_t)(PROTOCOL_HEADER_SIZE + payload_size + PROTOCOL_CRC_SIZE);
  return HAL_UART_Transmit(s_ota.uart, packet, total, 500U) == HAL_OK;
}

static void SendAck(uint16_t sequence, uint8_t type, OTA_Result result)
{
  uint8_t payload[7];
  payload[0] = type;
  payload[1] = (uint8_t)result;
  payload[2] = (uint8_t)s_ota.state;
  WriteU32(&payload[3], s_ota.received_size);
  (void)SendPacket(BOOT_MSG_ACK, sequence, payload, sizeof(payload));
}

static void SendStatus(uint16_t sequence, OTA_Result result)
{
  uint8_t payload[12];
  uint16_t progress = 0U;
  if (s_ota.expected_size > 0U)
  {
    uint32_t value = (s_ota.received_size * 1000U) / s_ota.expected_size;
    progress = (uint16_t)(value > 1000U ? 1000U : value);
  }
  payload[0] = (uint8_t)s_ota.state;
  payload[1] = (uint8_t)result;
  WriteU16(&payload[2], progress);
  WriteU32(&payload[4], s_ota.received_size);
  WriteU32(&payload[8], s_ota.expected_size);
  (void)SendPacket(BOOT_MSG_STATUS, sequence, payload, sizeof(payload));
}

static bool ApplicationVectorValid(uint32_t image_size)
{
  uint32_t stack = *(const uint32_t *)OTA_APPLICATION_BASE_ADDRESS;
  uint32_t reset = *(const uint32_t *)(OTA_APPLICATION_BASE_ADDRESS + 4U);
  uint32_t reset_address = reset & ~1UL;
  uint32_t upper = OTA_APPLICATION_BASE_ADDRESS + image_size;

  return (stack >= SRAM_BASE) && (stack <= (SRAM_BASE + 36U * 1024U))
         && ((stack & 0x3U) == 0U) && ((reset & 1U) == 1U)
         && (reset_address >= OTA_APPLICATION_BASE_ADDRESS)
         && (reset_address < upper);
}

static void BuildReleaseMessage(uint32_t firmware_version,
                                uint32_t firmware_size,
                                const uint8_t hash[32],
                                uint8_t message[66])
{
  static const uint8_t domain[] = "EPD-OTA-RELEASE-V2";
  size_t offset = sizeof(domain) - 1U;

  memcpy(message, domain, sizeof(domain) - 1U);
  WriteU16(&message[offset], OTA_STM32_DEVICE_ID);
  offset += 2U;
  WriteU16(&message[offset], OTA_HARDWARE_REVISION);
  offset += 2U;
  WriteU32(&message[offset], OTA_PRODUCT_ID);
  offset += 4U;
  WriteU32(&message[offset], firmware_version);
  offset += 4U;
  WriteU32(&message[offset], firmware_size);
  offset += 4U;
  memcpy(&message[offset], hash, 32U);
}

static bool ReleaseTagValid(uint32_t firmware_version,
                            uint32_t firmware_size,
                            const uint8_t hash[32],
                            const uint8_t tag[32])
{
  uint8_t message[66];
  uint8_t calculated[32];
  BuildReleaseMessage(firmware_version, firmware_size, hash, message);
  OTA_HMAC_SHA256(OTA_RELEASE_KEY, sizeof(OTA_RELEASE_KEY),
                  message, sizeof(message), calculated);
  return OTA_ConstantTimeEqual(calculated, tag, sizeof(calculated));
}

static void HashFlash(uint32_t size, uint8_t digest[32])
{
  OTA_SHA256_Context context;
  OTA_SHA256_Init(&context);
  OTA_SHA256_Update(&context,
                    (const uint8_t *)OTA_APPLICATION_BASE_ADDRESS, size);
  OTA_SHA256_Final(&context, digest);
}

static bool MetadataIsErased(const OTA_Metadata *metadata)
{
  return (metadata->progress_magic == 0xFFFFFFFFUL)
         && (metadata->valid_magic == 0xFFFFFFFFUL);
}

static bool InstalledMetadataValid(const OTA_Metadata *metadata)
{
  uint8_t calculated_hash[32];

  if ((metadata->valid_magic != OTA_METADATA_VALID_MAGIC)
      || (metadata->valid_inverse != ~OTA_METADATA_VALID_MAGIC)
      || (metadata->firmware_version == 0U)
      || (metadata->firmware_size < 8U)
      || (metadata->firmware_size > OTA_APPLICATION_MAX_SIZE)
      || !ApplicationVectorValid(metadata->firmware_size)
      || !ReleaseTagValid(metadata->firmware_version,
                          metadata->firmware_size,
                          metadata->firmware_hash,
                          metadata->release_tag))
  {
    return false;
  }
  HashFlash(metadata->firmware_size, calculated_hash);
  return OTA_ConstantTimeEqual(calculated_hash,
                               metadata->firmware_hash, 32U);
}

bool OTA_Bootloader_IsApplicationValid(void)
{
  const OTA_Metadata *metadata =
    (const OTA_Metadata *)OTA_METADATA_BASE_ADDRESS;

  if (InstalledMetadataValid(metadata))
  {
    return true;
  }
  /* Factory migration path: an erased metadata page allows one SWD-programmed
     relocated image to boot. OTA always writes the progress marker before it
     erases the application, so an interrupted OTA cannot use this fallback. */
  return MetadataIsErased(metadata)
         && ApplicationVectorValid(OTA_APPLICATION_MAX_SIZE);
}

uint32_t OTA_Bootloader_GetCurrentVersion(void)
{
  const OTA_Metadata *metadata =
    (const OTA_Metadata *)OTA_METADATA_BASE_ADDRESS;
  return InstalledMetadataValid(metadata) ? metadata->firmware_version : 0U;
}

static bool ProgramDoubleWord(uint32_t address, const uint8_t bytes[8])
{
  uint64_t value;
  memcpy(&value, bytes, sizeof(value));
  if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value)
      != HAL_OK)
  {
    return false;
  }
  return memcmp((const void *)address, bytes, 8U) == 0;
}

static bool ErasePage(uint32_t address)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error;
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.Page = (address - FLASH_BASE) / FLASH_PAGE_SIZE;
  erase.NbPages = 1U;
  return HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
}

static bool StartFlashTransaction(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error;
  uint8_t marker[8];
  bool ok;

  WriteU32(&marker[0], OTA_METADATA_PROGRESS_MAGIC);
  WriteU32(&marker[4], ~OTA_METADATA_PROGRESS_MAGIC);
  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  ok = ErasePage(OTA_METADATA_BASE_ADDRESS)
       && ProgramDoubleWord(OTA_METADATA_BASE_ADDRESS, marker);
  if (ok)
  {
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = (OTA_APPLICATION_BASE_ADDRESS - FLASH_BASE)
                 / FLASH_PAGE_SIZE;
    erase.NbPages = OTA_APPLICATION_MAX_SIZE / FLASH_PAGE_SIZE;
    ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
  }
  (void)HAL_FLASH_Lock();
  return ok;
}

static bool ProgramData(const uint8_t *data, uint32_t size)
{
  uint32_t index;
  bool ok = true;

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  for (index = 0U; (index < size) && ok; ++index)
  {
    s_ota.tail[s_ota.tail_size++] = data[index];
    if (s_ota.tail_size == sizeof(s_ota.tail))
    {
      ok = ProgramDoubleWord(OTA_APPLICATION_BASE_ADDRESS
                             + s_ota.programmed_size, s_ota.tail);
      if (ok)
      {
        s_ota.programmed_size += sizeof(s_ota.tail);
        s_ota.tail_size = 0U;
      }
    }
  }
  (void)HAL_FLASH_Lock();
  return ok;
}

static bool FlushTail(void)
{
  bool ok;
  if (s_ota.tail_size == 0U)
  {
    return true;
  }
  memset(&s_ota.tail[s_ota.tail_size], 0xFF,
         sizeof(s_ota.tail) - s_ota.tail_size);
  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  ok = ProgramDoubleWord(OTA_APPLICATION_BASE_ADDRESS
                         + s_ota.programmed_size, s_ota.tail);
  (void)HAL_FLASH_Lock();
  if (ok)
  {
    s_ota.programmed_size += sizeof(s_ota.tail);
    s_ota.tail_size = 0U;
  }
  return ok;
}

static bool CommitMetadata(void)
{
  OTA_Metadata metadata;
  uint8_t encoded[sizeof(metadata)];
  size_t offset;
  bool ok = true;

  memset(&metadata, 0xFF, sizeof(metadata));
  metadata.progress_magic = OTA_METADATA_PROGRESS_MAGIC;
  metadata.progress_inverse = ~OTA_METADATA_PROGRESS_MAGIC;
  metadata.firmware_version = s_ota.firmware_version;
  metadata.firmware_size = s_ota.expected_size;
  memcpy(metadata.firmware_hash, s_ota.expected_hash, 32U);
  memcpy(metadata.release_tag, s_ota.release_tag, 32U);
  metadata.valid_magic = OTA_METADATA_VALID_MAGIC;
  metadata.valid_inverse = ~OTA_METADATA_VALID_MAGIC;
  memcpy(encoded, &metadata, sizeof(encoded));

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  for (offset = 8U; offset < offsetof(OTA_Metadata, valid_magic);
       offset += 8U)
  {
    if (!ProgramDoubleWord(OTA_METADATA_BASE_ADDRESS + (uint32_t)offset,
                           &encoded[offset]))
    {
      ok = false;
      break;
    }
  }
  if (ok)
  {
    ok = ProgramDoubleWord(OTA_METADATA_BASE_ADDRESS
                           + offsetof(OTA_Metadata, valid_magic),
                           &encoded[offsetof(OTA_Metadata, valid_magic)]);
  }
  (void)HAL_FLASH_Lock();
  return ok;
}

static void HandleHello(uint16_t sequence, uint16_t payload_size)
{
  uint8_t response[37];
  if (payload_size != 0U)
  {
    SendAck(sequence, BOOT_MSG_HELLO, OTA_RESULT_BAD_SIZE);
    return;
  }
  response[0] = OTA_RESULT_OK;
  response[1] = 1U; /* mode: bootloader */
  WriteU16(&response[2], OTA_BOOT_PROTOCOL_VERSION);
  response[4] = (uint8_t)s_ota.state;
  memcpy(&response[5], s_ota.board_id, sizeof(s_ota.board_id));
  WriteU32(&response[17], s_ota.current_version);
  WriteU32(&response[21], OTA_APPLICATION_MAX_SIZE);
  WriteU32(&response[25], s_ota.received_size);
  WriteU16(&response[29], OTA_STM32_DEVICE_ID);
  WriteU16(&response[31], OTA_HARDWARE_REVISION);
  WriteU32(&response[33], OTA_PRODUCT_ID);
  (void)SendPacket(BOOT_MSG_HELLO_RSP, sequence, response,
                   sizeof(response));
}

static void HandleBegin(uint16_t sequence, const uint8_t *payload,
                        uint16_t payload_size)
{
  uint32_t firmware_version;
  uint32_t firmware_size;

  if (payload_size != OTA_BEGIN_PAYLOAD_SIZE)
  {
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_BAD_SIZE);
    return;
  }
  firmware_version = ReadU32(&payload[8]);
  firmware_size = ReadU32(&payload[12]);
  if ((ReadU16(&payload[0]) != OTA_STM32_DEVICE_ID)
      || (ReadU16(&payload[2]) != OTA_HARDWARE_REVISION)
      || (ReadU32(&payload[4]) != OTA_PRODUCT_ID))
  {
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_WRONG_DEVICE);
    return;
  }
  if ((firmware_version == 0U) || (firmware_size < 8U)
      || (firmware_size > OTA_APPLICATION_MAX_SIZE))
  {
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_BAD_SIZE);
    return;
  }
  if ((s_ota.current_version != 0U)
      && (firmware_version <= s_ota.current_version))
  {
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_DOWNGRADE);
    return;
  }
  if (!ReleaseTagValid(firmware_version, firmware_size,
                       &payload[16], &payload[48]))
  {
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_AUTH_FAILED);
    return;
  }

  s_ota.state = OTA_BOOT_STATE_ERASING;
  s_ota.expected_size = firmware_size;
  s_ota.received_size = 0U;
  SendStatus(sequence, OTA_RESULT_OK);
  if (!StartFlashTransaction())
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_FLASH_ERROR);
    return;
  }

  s_ota.firmware_version = firmware_version;
  s_ota.programmed_size = 0U;
  s_ota.tail_size = 0U;
  memcpy(s_ota.expected_hash, &payload[16], 32U);
  memcpy(s_ota.release_tag, &payload[48], 32U);
  OTA_SHA256_Init(&s_ota.running_hash);
  s_ota.last_activity_tick = HAL_GetTick();
  s_ota.state = OTA_BOOT_STATE_RECEIVING;
  SendAck(sequence, BOOT_MSG_OTA_BEGIN, OTA_RESULT_OK);
}

static void HandleData(uint16_t sequence, const uint8_t *payload,
                       uint16_t payload_size)
{
  uint32_t offset;
  uint32_t data_size;

  if (s_ota.state != OTA_BOOT_STATE_RECEIVING)
  {
    SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_BAD_STATE);
    return;
  }
  if ((payload_size <= 4U)
      || (payload_size > (OTA_DATA_MAX_SIZE + 4U)))
  {
    SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_BAD_SIZE);
    return;
  }
  offset = ReadU32(payload);
  data_size = payload_size - 4U;
  if ((offset < s_ota.received_size)
      && ((offset + data_size) <= s_ota.received_size))
  {
    SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_OK);
    return;
  }
  if ((offset != s_ota.received_size)
      || (data_size > (s_ota.expected_size - s_ota.received_size)))
  {
    SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_BAD_OFFSET);
    return;
  }
  if (!ProgramData(&payload[4], data_size))
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_FLASH_ERROR);
    return;
  }
  OTA_SHA256_Update(&s_ota.running_hash, &payload[4], data_size);
  s_ota.received_size += data_size;
  s_ota.last_activity_tick = HAL_GetTick();
  SendAck(sequence, BOOT_MSG_OTA_DATA, OTA_RESULT_OK);
}

static void HandleEnd(uint16_t sequence, uint16_t payload_size)
{
  uint8_t stream_hash[32];
  uint8_t flash_hash[32];

  if (s_ota.state != OTA_BOOT_STATE_RECEIVING)
  {
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_BAD_STATE);
    return;
  }
  if ((payload_size != 0U) || (s_ota.received_size != s_ota.expected_size))
  {
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_BAD_SIZE);
    return;
  }
  s_ota.state = OTA_BOOT_STATE_VERIFYING;
  SendStatus(sequence, OTA_RESULT_OK);
  if (!FlushTail())
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_FLASH_ERROR);
    return;
  }
  OTA_SHA256_Final(&s_ota.running_hash, stream_hash);
  HashFlash(s_ota.expected_size, flash_hash);
  if (!OTA_ConstantTimeEqual(stream_hash, s_ota.expected_hash, 32U)
      || !OTA_ConstantTimeEqual(flash_hash, s_ota.expected_hash, 32U))
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_BAD_HASH);
    return;
  }
  if (!ApplicationVectorValid(s_ota.expected_size))
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_BAD_VECTOR);
    return;
  }
  if (!CommitMetadata())
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_FLASH_ERROR);
    return;
  }

  s_ota.current_version = s_ota.firmware_version;
  s_ota.state = OTA_BOOT_STATE_COMPLETE;
  SendAck(sequence, BOOT_MSG_OTA_END, OTA_RESULT_OK);
  SendStatus(sequence, OTA_RESULT_OK);
  HAL_Delay(200U);
  NVIC_SystemReset();
}

static void HandleAbort(uint16_t sequence, uint16_t payload_size)
{
  if (payload_size != 0U)
  {
    SendAck(sequence, BOOT_MSG_OTA_ABORT, OTA_RESULT_BAD_SIZE);
    return;
  }
  memset(s_ota.tail, 0, sizeof(s_ota.tail));
  s_ota.tail_size = 0U;
  s_ota.received_size = 0U;
  s_ota.expected_size = 0U;
  s_ota.state = OTA_BOOT_STATE_IDLE;
  SendAck(sequence, BOOT_MSG_OTA_ABORT, OTA_RESULT_OK);
}

static void HandlePacket(const uint8_t *packet)
{
  uint8_t type = packet[3];
  uint16_t sequence = ReadU16(&packet[4]);
  uint16_t payload_size = ReadU16(&packet[6]);
  const uint8_t *payload = &packet[8];
  uint32_t received_crc = ReadU32(&packet[8U + payload_size]);
  uint32_t calculated_crc = CRC32Calculate(&packet[2], 6U + payload_size);

  if (packet[2] != PROTOCOL_VERSION)
  {
    SendAck(sequence, type, OTA_RESULT_BAD_VERSION);
  }
  else if (received_crc != calculated_crc)
  {
    SendAck(sequence, type, OTA_RESULT_BAD_PACKET);
  }
  else
  {
    switch (type)
    {
      case BOOT_MSG_HELLO:
        HandleHello(sequence, payload_size);
        break;
      case BOOT_MSG_OTA_BEGIN:
        HandleBegin(sequence, payload, payload_size);
        break;
      case BOOT_MSG_OTA_DATA:
        HandleData(sequence, payload, payload_size);
        break;
      case BOOT_MSG_OTA_END:
        HandleEnd(sequence, payload_size);
        break;
      case BOOT_MSG_OTA_ABORT:
        HandleAbort(sequence, payload_size);
        break;
      case BOOT_MSG_PING:
        SendAck(sequence, BOOT_MSG_PING, OTA_RESULT_OK);
        break;
      default:
        SendAck(sequence, type, OTA_RESULT_BAD_PACKET);
        break;
    }
  }
}

static void ParserReset(void)
{
  s_packet_size = 0U;
  s_packet_expected = 0U;
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
  if (s_packet_size >= sizeof(s_packet))
  {
    ParserReset();
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
    HandlePacket(s_packet);
    ParserReset();
  }
}

bool OTA_Bootloader_Init(UART_HandleTypeDef *uart)
{
  if ((uart == NULL) || (uart->Instance != USART2))
  {
    return false;
  }
  memset(&s_ota, 0, sizeof(s_ota));
  s_ota.uart = uart;
  s_ota.state = OTA_BOOT_STATE_IDLE;
  GetBoardId(s_ota.board_id);
  s_ota.current_version = OTA_Bootloader_GetCurrentVersion();
  ParserReset();
  return true;
}

void OTA_Bootloader_Poll(void)
{
  uint8_t byte;
  HAL_StatusTypeDef status = HAL_UART_Receive(s_ota.uart, &byte, 1U, 1U);

  if (status == HAL_OK)
  {
    ParserConsume(byte);
  }
  else if (status == HAL_ERROR)
  {
    /* A line glitch must not leave the polling receiver permanently stuck. */
    __HAL_UART_CLEAR_FLAG(s_ota.uart,
                          UART_CLEAR_PEF | UART_CLEAR_FEF
                          | UART_CLEAR_NEF | UART_CLEAR_OREF);
    __HAL_UART_SEND_REQ(s_ota.uart, UART_RXDATA_FLUSH_REQUEST);
    s_ota.uart->ErrorCode = HAL_UART_ERROR_NONE;
    ParserReset();
  }
  if ((s_ota.state == OTA_BOOT_STATE_RECEIVING)
      && ((uint32_t)(HAL_GetTick() - s_ota.last_activity_tick)
          >= OTA_TRANSFER_TIMEOUT_MS))
  {
    s_ota.state = OTA_BOOT_STATE_ERROR;
    SendStatus(0U, OTA_RESULT_TIMEOUT);
  }
}

bool OTA_Bootloader_IsTransferActive(void)
{
  return (s_ota.state == OTA_BOOT_STATE_ERASING)
         || (s_ota.state == OTA_BOOT_STATE_RECEIVING)
         || (s_ota.state == OTA_BOOT_STATE_VERIFYING);
}

void OTA_Bootloader_JumpToApplication(void)
{
  uint32_t stack = *(const uint32_t *)OTA_APPLICATION_BASE_ADDRESS;
  uint32_t reset = *(const uint32_t *)(OTA_APPLICATION_BASE_ADDRESS + 4U);

  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  if (s_ota.uart != NULL)
  {
    (void)HAL_UART_DeInit(s_ota.uart);
  }
  NVIC->ICER[0] = 0xFFFFFFFFUL;
  NVIC->ICPR[0] = 0xFFFFFFFFUL;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  SCB->VTOR = OTA_APPLICATION_BASE_ADDRESS;
  __set_CONTROL(0U);
  __DSB();
  __ISB();
  JumpToResetHandler(stack, reset);
}
