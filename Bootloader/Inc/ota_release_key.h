#ifndef OTA_RELEASE_KEY_H
#define OTA_RELEASE_KEY_H

#include <stdint.h>

/* DEVELOPMENT KEY ONLY.
 * Replace this value during provisioning and protect Bootloader pages with WRP.
 * The matching value is accepted by tools/package_ota.py only when explicitly
 * passed through --key-hex; it is never sent to the Android/Windows client. */
static const uint8_t OTA_RELEASE_KEY[32] = {
  0x6DU, 0xB5U, 0x35U, 0x9AU, 0x48U, 0xC2U, 0x71U, 0x0FU,
  0xE4U, 0x23U, 0xB8U, 0x61U, 0x9CU, 0xD7U, 0x04U, 0x5EU,
  0x17U, 0xA9U, 0x3CU, 0xF0U, 0x82U, 0x6BU, 0xD1U, 0x34U,
  0x59U, 0xEEU, 0x0AU, 0xC7U, 0xB2U, 0x45U, 0x98U, 0x13U
};

#endif /* OTA_RELEASE_KEY_H */
