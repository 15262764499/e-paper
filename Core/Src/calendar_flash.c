#include "calendar_flash.h"

#include "stm32g0xx_hal.h"

#include <string.h>

#define CALENDAR_FLASH_MAGIC         0x43414C46UL /* CALF */
#define CALENDAR_FLASH_VERSION       1UL
#define CALENDAR_FLASH_VALID_MARKER  0x56414C44UL /* VALD */
#define CALENDAR_FLASH_HEADER_ADDRESS \
  (CALENDAR_FLASH_BASE_ADDRESS + CALENDAR_FLASH_FRAME_SIZE)
#define CALENDAR_FLASH_FIRST_PAGE \
  ((CALENDAR_FLASH_BASE_ADDRESS - FLASH_BASE) / FLASH_PAGE_SIZE)
#define CALENDAR_FLASH_PAGE_COUNT \
  (CALENDAR_FLASH_REGION_SIZE / FLASH_PAGE_SIZE)

typedef struct
{
  uint32_t magic;
  uint32_t version_profile;
  uint32_t frame_size;
  uint32_t frame_crc32;
  uint32_t base_date;
  uint32_t reserved0;
  uint32_t reserved1;
  uint32_t valid_marker;
} CalendarFlash_Header;

static bool s_write_active;
static uint32_t s_next_offset;
static uint8_t s_double_word[8];
static uint8_t s_double_word_size;

static bool ProgramDoubleWord(uint32_t address, const uint8_t data[8])
{
  uint64_t value;
  memcpy(&value, data, sizeof(value));
  return HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value)
         == HAL_OK;
}

bool CalendarFlash_Begin(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  bool ok;

  s_write_active = false;
  s_next_offset = 0U;
  s_double_word_size = 0U;
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.Page = CALENDAR_FLASH_FIRST_PAGE;
  erase.NbPages = CALENDAR_FLASH_PAGE_COUNT;
  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
  (void)HAL_FLASH_Lock();
  if (ok)
  {
    s_write_active = true;
  }
  return ok;
}

bool CalendarFlash_Write(uint32_t offset, const uint8_t *data, size_t size)
{
  size_t index;
  bool ok = true;

  if (!s_write_active || (data == NULL) || (size == 0U)
      || (offset != s_next_offset)
      || (size > (CALENDAR_FLASH_FRAME_SIZE - s_next_offset)))
  {
    return false;
  }
  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  for (index = 0U; (index < size) && ok; ++index)
  {
    s_double_word[s_double_word_size++] = data[index];
    if (s_double_word_size == sizeof(s_double_word))
    {
      uint32_t address = CALENDAR_FLASH_BASE_ADDRESS
                         + s_next_offset + (uint32_t)index + 1U
                         - sizeof(s_double_word);
      ok = ProgramDoubleWord(address, s_double_word);
      s_double_word_size = 0U;
    }
  }
  (void)HAL_FLASH_Lock();
  if (!ok)
  {
    s_write_active = false;
    return false;
  }
  s_next_offset += (uint32_t)size;
  return true;
}

bool CalendarFlash_Commit(uint32_t frame_crc32, uint32_t base_date,
                          uint8_t render_profile)
{
  CalendarFlash_Header header = {0};
  uint8_t encoded[sizeof(header)];
  size_t offset;
  bool ok = true;

  if (!s_write_active || (s_next_offset != CALENDAR_FLASH_FRAME_SIZE)
      || (s_double_word_size != 0U)
      || (render_profile != CALENDAR_RENDER_PROFILE_V1))
  {
    return false;
  }

  header.magic = CALENDAR_FLASH_MAGIC;
  header.version_profile = CALENDAR_FLASH_VERSION
                           | ((uint32_t)render_profile << 8U);
  header.frame_size = CALENDAR_FLASH_FRAME_SIZE;
  header.frame_crc32 = frame_crc32;
  header.base_date = base_date;
  header.reserved0 = 0xFFFFFFFFUL;
  header.reserved1 = 0xFFFFFFFFUL;
  header.valid_marker = CALENDAR_FLASH_VALID_MARKER;
  memcpy(encoded, &header, sizeof(encoded));

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }
  /* The final double-word contains the validity marker and is written last. */
  for (offset = 0U; offset < (sizeof(encoded) - 8U); offset += 8U)
  {
    if (!ProgramDoubleWord(CALENDAR_FLASH_HEADER_ADDRESS + (uint32_t)offset,
                           &encoded[offset]))
    {
      ok = false;
      break;
    }
  }
  if (ok)
  {
    ok = ProgramDoubleWord(CALENDAR_FLASH_HEADER_ADDRESS
                           + sizeof(encoded) - 8U,
                           &encoded[sizeof(encoded) - 8U]);
  }
  (void)HAL_FLASH_Lock();
  s_write_active = false;
  return ok;
}

void CalendarFlash_Abort(void)
{
  s_write_active = false;
  s_next_offset = 0U;
  s_double_word_size = 0U;
  memset(s_double_word, 0, sizeof(s_double_word));
}

bool CalendarFlash_IsValid(void)
{
  const CalendarFlash_Header *header =
    (const CalendarFlash_Header *)CALENDAR_FLASH_HEADER_ADDRESS;

  return (header->magic == CALENDAR_FLASH_MAGIC)
         && ((header->version_profile & 0xFFU) == CALENDAR_FLASH_VERSION)
         && (((header->version_profile >> 8U) & 0xFFU)
             == CALENDAR_RENDER_PROFILE_V1)
         && (header->frame_size == CALENDAR_FLASH_FRAME_SIZE)
         && (header->valid_marker == CALENDAR_FLASH_VALID_MARKER);
}

bool CalendarFlash_GetMetadata(CalendarFlash_Metadata *metadata)
{
  const CalendarFlash_Header *header =
    (const CalendarFlash_Header *)CALENDAR_FLASH_HEADER_ADDRESS;

  if ((metadata == NULL) || !CalendarFlash_IsValid())
  {
    return false;
  }
  metadata->frame_crc32 = header->frame_crc32;
  metadata->base_date = header->base_date;
  metadata->render_profile = (uint8_t)(header->version_profile >> 8U);
  return true;
}

const uint8_t *CalendarFlash_FrameAddress(void)
{
  return (const uint8_t *)CALENDAR_FLASH_BASE_ADDRESS;
}
