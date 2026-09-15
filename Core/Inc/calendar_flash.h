#ifndef CALENDAR_FLASH_H
#define CALENDAR_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_layout.h"

#define CALENDAR_FLASH_BASE_ADDRESS OTA_CALENDAR_BASE_ADDRESS
#define CALENDAR_FLASH_REGION_SIZE  OTA_CALENDAR_REGION_SIZE
#define CALENDAR_FLASH_FRAME_SIZE   30000UL
#define CALENDAR_RENDER_PROFILE_V1  0x11U

typedef struct
{
  uint32_t frame_crc32;
  uint32_t base_date;
  uint8_t render_profile;
} CalendarFlash_Metadata;

bool CalendarFlash_Begin(void);
bool CalendarFlash_Write(uint32_t offset, const uint8_t *data, size_t size);
bool CalendarFlash_Commit(uint32_t frame_crc32, uint32_t base_date,
                          uint8_t render_profile);
void CalendarFlash_Abort(void);
bool CalendarFlash_IsValid(void);
bool CalendarFlash_GetMetadata(CalendarFlash_Metadata *metadata);
const uint8_t *CalendarFlash_FrameAddress(void);

#ifdef __cplusplus
}
#endif

#endif /* CALENDAR_FLASH_H */
