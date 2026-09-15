#include "ota_crypto.h"

#include <string.h>

static const uint32_t s_constants[64] = {
  0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
  0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
  0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
  0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
  0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
  0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
  0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
  0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
  0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
  0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
  0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
  0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
  0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
  0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
  0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
  0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
};

static uint32_t RotateRight(uint32_t value, uint32_t count)
{
  return (value >> count) | (value << (32U - count));
}

static void Transform(OTA_SHA256_Context *context, const uint8_t block[64])
{
  uint32_t words[64];
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t index;

  for (index = 0U; index < 16U; ++index)
  {
    size_t offset = (size_t)index * 4U;
    words[index] = ((uint32_t)block[offset] << 24U)
                   | ((uint32_t)block[offset + 1U] << 16U)
                   | ((uint32_t)block[offset + 2U] << 8U)
                   | (uint32_t)block[offset + 3U];
  }
  for (index = 16U; index < 64U; ++index)
  {
    uint32_t x = words[index - 15U];
    uint32_t y = words[index - 2U];
    uint32_t sigma0 = RotateRight(x, 7U) ^ RotateRight(x, 18U) ^ (x >> 3U);
    uint32_t sigma1 = RotateRight(y, 17U) ^ RotateRight(y, 19U) ^ (y >> 10U);
    words[index] = words[index - 16U] + sigma0 + words[index - 7U]
                   + sigma1;
  }

  a = context->state[0]; b = context->state[1];
  c = context->state[2]; d = context->state[3];
  e = context->state[4]; f = context->state[5];
  g = context->state[6]; h = context->state[7];
  for (index = 0U; index < 64U; ++index)
  {
    uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U)
                    ^ RotateRight(e, 25U);
    uint32_t choice = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + sum1 + choice + s_constants[index] + words[index];
    uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U)
                    ^ RotateRight(a, 22U);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = sum0 + majority;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  context->state[0] += a; context->state[1] += b;
  context->state[2] += c; context->state[3] += d;
  context->state[4] += e; context->state[5] += f;
  context->state[6] += g; context->state[7] += h;
}

void OTA_SHA256_Init(OTA_SHA256_Context *context)
{
  static const uint32_t initial[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
  };
  memcpy(context->state, initial, sizeof(initial));
  context->bit_count = 0U;
  context->block_size = 0U;
}

void OTA_SHA256_Update(OTA_SHA256_Context *context,
                       const uint8_t *data, size_t length)
{
  while (length > 0U)
  {
    size_t room = sizeof(context->block) - context->block_size;
    size_t count = length < room ? length : room;
    memcpy(&context->block[context->block_size], data, count);
    context->block_size += count;
    context->bit_count += (uint64_t)count * 8U;
    data += count;
    length -= count;
    if (context->block_size == sizeof(context->block))
    {
      Transform(context, context->block);
      context->block_size = 0U;
    }
  }
}

void OTA_SHA256_Final(OTA_SHA256_Context *context, uint8_t digest[32])
{
  uint64_t bits = context->bit_count;
  uint32_t index;

  context->block[context->block_size++] = 0x80U;
  if (context->block_size > 56U)
  {
    while (context->block_size < 64U)
    {
      context->block[context->block_size++] = 0U;
    }
    Transform(context, context->block);
    context->block_size = 0U;
  }
  while (context->block_size < 56U)
  {
    context->block[context->block_size++] = 0U;
  }
  for (index = 0U; index < 8U; ++index)
  {
    context->block[63U - index] = (uint8_t)(bits >> (index * 8U));
  }
  Transform(context, context->block);
  for (index = 0U; index < 8U; ++index)
  {
    digest[index * 4U] = (uint8_t)(context->state[index] >> 24U);
    digest[index * 4U + 1U] = (uint8_t)(context->state[index] >> 16U);
    digest[index * 4U + 2U] = (uint8_t)(context->state[index] >> 8U);
    digest[index * 4U + 3U] = (uint8_t)context->state[index];
  }
}

void OTA_HMAC_SHA256(const uint8_t *key, size_t key_size,
                     const uint8_t *message, size_t message_size,
                     uint8_t digest[32])
{
  uint8_t key_block[64] = {0};
  uint8_t inner_digest[32];
  uint8_t pad[64];
  OTA_SHA256_Context context;
  size_t index;

  if (key_size > sizeof(key_block))
  {
    OTA_SHA256_Init(&context);
    OTA_SHA256_Update(&context, key, key_size);
    OTA_SHA256_Final(&context, key_block);
  }
  else
  {
    memcpy(key_block, key, key_size);
  }
  for (index = 0U; index < sizeof(pad); ++index)
  {
    pad[index] = key_block[index] ^ 0x36U;
  }
  OTA_SHA256_Init(&context);
  OTA_SHA256_Update(&context, pad, sizeof(pad));
  OTA_SHA256_Update(&context, message, message_size);
  OTA_SHA256_Final(&context, inner_digest);
  for (index = 0U; index < sizeof(pad); ++index)
  {
    pad[index] = key_block[index] ^ 0x5CU;
  }
  OTA_SHA256_Init(&context);
  OTA_SHA256_Update(&context, pad, sizeof(pad));
  OTA_SHA256_Update(&context, inner_digest, sizeof(inner_digest));
  OTA_SHA256_Final(&context, digest);
  memset(key_block, 0, sizeof(key_block));
  memset(inner_digest, 0, sizeof(inner_digest));
  memset(pad, 0, sizeof(pad));
}

bool OTA_ConstantTimeEqual(const uint8_t *left, const uint8_t *right,
                           size_t size)
{
  uint8_t difference = 0U;
  size_t index;
  for (index = 0U; index < size; ++index)
  {
    difference |= left[index] ^ right[index];
  }
  return difference == 0U;
}
