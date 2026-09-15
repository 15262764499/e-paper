#ifndef OTA_CRYPTO_H
#define OTA_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t block[64];
  size_t block_size;
} OTA_SHA256_Context;

void OTA_SHA256_Init(OTA_SHA256_Context *context);
void OTA_SHA256_Update(OTA_SHA256_Context *context,
                       const uint8_t *data, size_t length);
void OTA_SHA256_Final(OTA_SHA256_Context *context, uint8_t digest[32]);
void OTA_HMAC_SHA256(const uint8_t *key, size_t key_size,
                     const uint8_t *message, size_t message_size,
                     uint8_t digest[32]);
bool OTA_ConstantTimeEqual(const uint8_t *left, const uint8_t *right,
                           size_t size);

#endif /* OTA_CRYPTO_H */
