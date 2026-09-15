#include "pairing_security.h"

#include "stm32g0xx_hal.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t block[64];
  size_t block_size;
} SHA256_Context;

/* Prototype root key.  A per-device NFC pairing key is derived from this root
   and the STM32 UID.  Production units should provision the root through a
   protected manufacturing process (or use a secure element). */
static const uint8_t s_pairing_root_key[PAIRING_KEY_SIZE] = {
  0x9DU, 0x63U, 0x21U, 0xF4U, 0xB8U, 0x0EU, 0x57U, 0xCAU,
  0x46U, 0xADU, 0x91U, 0x38U, 0x7BU, 0xD2U, 0x05U, 0xEEU
};

static uint32_t s_challenge_counter;
static uint8_t s_session_key[32];
static bool s_session_valid;
static bool s_image_mac_active;
static SHA256_Context s_image_inner;

static const uint32_t s_sha256_constants[64] = {
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

static uint32_t SHA256_RotateRight(uint32_t value, uint32_t count)
{
  return (value >> count) | (value << (32U - count));
}

static void SHA256_Transform(SHA256_Context *context,
                             const uint8_t block[64])
{
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
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
    uint32_t sigma0 = SHA256_RotateRight(x, 7U)
                      ^ SHA256_RotateRight(x, 18U) ^ (x >> 3U);
    uint32_t sigma1 = SHA256_RotateRight(y, 17U)
                      ^ SHA256_RotateRight(y, 19U) ^ (y >> 10U);
    words[index] = words[index - 16U] + sigma0 + words[index - 7U]
                   + sigma1;
  }

  a = context->state[0];
  b = context->state[1];
  c = context->state[2];
  d = context->state[3];
  e = context->state[4];
  f = context->state[5];
  g = context->state[6];
  h = context->state[7];

  for (index = 0U; index < 64U; ++index)
  {
    uint32_t sum1 = SHA256_RotateRight(e, 6U)
                    ^ SHA256_RotateRight(e, 11U)
                    ^ SHA256_RotateRight(e, 25U);
    uint32_t choice = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + sum1 + choice + s_sha256_constants[index]
                     + words[index];
    uint32_t sum0 = SHA256_RotateRight(a, 2U)
                    ^ SHA256_RotateRight(a, 13U)
                    ^ SHA256_RotateRight(a, 22U);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = sum0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
  context->state[5] += f;
  context->state[6] += g;
  context->state[7] += h;
}

static void SHA256_Init(SHA256_Context *context)
{
  static const uint32_t initial_state[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
  };

  memcpy(context->state, initial_state, sizeof(initial_state));
  context->bit_count = 0U;
  context->block_size = 0U;
}

static void SHA256_Update(SHA256_Context *context,
                          const uint8_t *data,
                          size_t length)
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
      SHA256_Transform(context, context->block);
      context->block_size = 0U;
    }
  }
}

static void SHA256_Final(SHA256_Context *context, uint8_t digest[32])
{
  uint64_t bit_count = context->bit_count;
  uint32_t index;

  context->block[context->block_size++] = 0x80U;
  if (context->block_size > 56U)
  {
    while (context->block_size < 64U)
    {
      context->block[context->block_size++] = 0U;
    }
    SHA256_Transform(context, context->block);
    context->block_size = 0U;
  }
  while (context->block_size < 56U)
  {
    context->block[context->block_size++] = 0U;
  }
  for (index = 0U; index < 8U; ++index)
  {
    context->block[63U - index] = (uint8_t)(bit_count >> (index * 8U));
  }
  SHA256_Transform(context, context->block);

  for (index = 0U; index < 8U; ++index)
  {
    digest[index * 4U] = (uint8_t)(context->state[index] >> 24U);
    digest[index * 4U + 1U] = (uint8_t)(context->state[index] >> 16U);
    digest[index * 4U + 2U] = (uint8_t)(context->state[index] >> 8U);
    digest[index * 4U + 3U] = (uint8_t)context->state[index];
  }
}

static void HMAC_SHA256(const uint8_t *key, size_t key_size,
                        const uint8_t *message, size_t message_size,
                        uint8_t digest[32])
{
  uint8_t key_block[64] = {0};
  uint8_t inner_digest[32];
  uint8_t pad[64];
  SHA256_Context context;
  size_t index;

  if (key_size > sizeof(key_block))
  {
    SHA256_Init(&context);
    SHA256_Update(&context, key, key_size);
    SHA256_Final(&context, key_block);
  }
  else
  {
    memcpy(key_block, key, key_size);
  }

  for (index = 0U; index < sizeof(pad); ++index)
  {
    pad[index] = key_block[index] ^ 0x36U;
  }
  SHA256_Init(&context);
  SHA256_Update(&context, pad, sizeof(pad));
  SHA256_Update(&context, message, message_size);
  SHA256_Final(&context, inner_digest);

  for (index = 0U; index < sizeof(pad); ++index)
  {
    pad[index] = key_block[index] ^ 0x5CU;
  }
  SHA256_Init(&context);
  SHA256_Update(&context, pad, sizeof(pad));
  SHA256_Update(&context, inner_digest, sizeof(inner_digest));
  SHA256_Final(&context, digest);
}

static void HMAC_SHA256_Begin(SHA256_Context *context,
                              const uint8_t key[32])
{
  uint8_t pad[64];
  size_t index;

  memset(pad, 0x36, sizeof(pad));
  for (index = 0U; index < 32U; ++index)
  {
    pad[index] ^= key[index];
  }
  SHA256_Init(context);
  SHA256_Update(context, pad, sizeof(pad));
}

static void HMAC_SHA256_End(SHA256_Context *inner,
                            const uint8_t key[32], uint8_t digest[32])
{
  uint8_t inner_digest[32];
  uint8_t pad[64];
  SHA256_Context outer;
  size_t index;

  SHA256_Final(inner, inner_digest);
  memset(pad, 0x5C, sizeof(pad));
  for (index = 0U; index < 32U; ++index)
  {
    pad[index] ^= key[index];
  }
  SHA256_Init(&outer);
  SHA256_Update(&outer, pad, sizeof(pad));
  SHA256_Update(&outer, inner_digest, sizeof(inner_digest));
  SHA256_Final(&outer, digest);
}

static void GetDeviceKey(uint8_t key[PAIRING_KEY_SIZE])
{
  static const uint8_t domain[] = "EPD-PAIR-V1";
  uint8_t message[(sizeof(domain) - 1U) + 12U];
  uint8_t digest[32];
  uint32_t uid[3];

  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();
  memcpy(message, domain, sizeof(domain) - 1U);
  memcpy(&message[sizeof(domain) - 1U], uid, sizeof(uid));
  HMAC_SHA256(s_pairing_root_key, sizeof(s_pairing_root_key), message,
              sizeof(message), digest);
  memcpy(key, digest, PAIRING_KEY_SIZE);
}

bool PairingSecurity_SelfTest(void)
{
  static const uint8_t key[] = "key";
  static const uint8_t message[] =
    "The quick brown fox jumps over the lazy dog";
  static const uint8_t expected[32] = {
    0xF7U, 0xBCU, 0x83U, 0xF4U, 0x30U, 0x53U, 0x84U, 0x24U,
    0xB1U, 0x32U, 0x98U, 0xE6U, 0xAAU, 0x6FU, 0xB1U, 0x43U,
    0xEFU, 0x4DU, 0x59U, 0xA1U, 0x49U, 0x46U, 0x17U, 0x59U,
    0x97U, 0x47U, 0x9DU, 0xBCU, 0x2DU, 0x1AU, 0x3CU, 0xD8U
  };
  uint8_t digest[32];
  uint8_t difference = 0U;
  size_t index;

  HMAC_SHA256(key, sizeof(key) - 1U, message, sizeof(message) - 1U, digest);
  for (index = 0U; index < sizeof(expected); ++index)
  {
    difference |= digest[index] ^ expected[index];
  }
  return difference == 0U;
}

void PairingSecurity_GetKey(uint8_t key[PAIRING_KEY_SIZE])
{
  GetDeviceKey(key);
}

void PairingSecurity_GenerateChallenge(const uint8_t board_id[12],
                                       uint8_t challenge[PAIRING_CHALLENGE_SIZE])
{
  uint8_t material[24];
  uint8_t digest[32];
  uint8_t device_key[PAIRING_KEY_SIZE];
  uint32_t tick = HAL_GetTick();
  uint32_t systick_value = SysTick->VAL;
  uint32_t counter = ++s_challenge_counter;

  memcpy(material, board_id, 12U);
  memcpy(&material[12], &tick, sizeof(tick));
  memcpy(&material[16], &systick_value, sizeof(systick_value));
  memcpy(&material[20], &counter, sizeof(counter));
  GetDeviceKey(device_key);
  HMAC_SHA256(device_key, sizeof(device_key), material,
              sizeof(material), digest);
  memcpy(challenge, digest, PAIRING_CHALLENGE_SIZE);
  memset(device_key, 0, sizeof(device_key));
}

bool PairingSecurity_VerifyProof(const uint8_t board_id[12],
                                 const uint8_t challenge[PAIRING_CHALLENGE_SIZE],
                                 const uint8_t proof[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-AUTH-V1";
  uint8_t message[(sizeof(domain) - 1U) + 12U + PAIRING_CHALLENGE_SIZE];
  uint8_t digest[32];
  uint8_t device_key[PAIRING_KEY_SIZE];
  uint8_t session_message[14U + 12U + PAIRING_CHALLENGE_SIZE];
  static const uint8_t session_domain[] = "EPD-SESSION-V1";
  uint8_t difference = 0U;
  size_t index;

  memcpy(message, domain, sizeof(domain) - 1U);
  memcpy(&message[sizeof(domain) - 1U], board_id, 12U);
  memcpy(&message[(sizeof(domain) - 1U) + 12U], challenge,
         PAIRING_CHALLENGE_SIZE);
  GetDeviceKey(device_key);
  HMAC_SHA256(device_key, sizeof(device_key), message,
              sizeof(message), digest);

  for (index = 0U; index < PAIRING_PROOF_SIZE; ++index)
  {
    difference |= digest[index] ^ proof[index];
  }
  if (difference != 0U)
  {
    memset(device_key, 0, sizeof(device_key));
    PairingSecurity_ClearSession();
    return false;
  }

  memcpy(session_message, session_domain, sizeof(session_domain) - 1U);
  memcpy(&session_message[sizeof(session_domain) - 1U], board_id, 12U);
  memcpy(&session_message[(sizeof(session_domain) - 1U) + 12U], challenge,
         PAIRING_CHALLENGE_SIZE);
  HMAC_SHA256(device_key, sizeof(device_key), session_message,
              sizeof(session_message), s_session_key);
  memset(device_key, 0, sizeof(device_key));
  s_session_valid = true;
  return true;
}

bool PairingSecurity_ImageBegin(uint32_t image_id, uint32_t image_size,
                                uint32_t image_crc32, uint8_t render_profile)
{
  static const uint8_t image_domain[] = "EPD-IMAGE-V1";
  static const uint8_t calendar_domain[] = "EPD-CALENDAR-V1";
  const uint8_t *domain = image_domain;
  size_t domain_size = sizeof(image_domain) - 1U;
  uint8_t metadata[13];

  if (!s_session_valid)
  {
    return false;
  }
  metadata[0] = (uint8_t)image_id;
  metadata[1] = (uint8_t)(image_id >> 8U);
  metadata[2] = (uint8_t)(image_id >> 16U);
  metadata[3] = (uint8_t)(image_id >> 24U);
  metadata[4] = (uint8_t)image_size;
  metadata[5] = (uint8_t)(image_size >> 8U);
  metadata[6] = (uint8_t)(image_size >> 16U);
  metadata[7] = (uint8_t)(image_size >> 24U);
  metadata[8] = (uint8_t)image_crc32;
  metadata[9] = (uint8_t)(image_crc32 >> 8U);
  metadata[10] = (uint8_t)(image_crc32 >> 16U);
  metadata[11] = (uint8_t)(image_crc32 >> 24U);
  metadata[12] = render_profile;

  if (render_profile != 0U)
  {
    domain = calendar_domain;
    domain_size = sizeof(calendar_domain) - 1U;
  }

  HMAC_SHA256_Begin(&s_image_inner, s_session_key);
  SHA256_Update(&s_image_inner, domain, domain_size);
  SHA256_Update(&s_image_inner, metadata,
                render_profile == 0U ? 12U : sizeof(metadata));
  s_image_mac_active = true;
  return true;
}

bool PairingSecurity_VerifyTimeSync(
  uint16_t sequence, uint64_t epoch_seconds, int16_t utc_offset_minutes,
  const uint8_t proof[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-TIME-V1";
  uint8_t message[(sizeof(domain) - 1U) + 12U];
  uint8_t digest[32];
  uint8_t difference = 0U;
  uint16_t offset_value = (uint16_t)utc_offset_minutes;
  size_t offset = sizeof(domain) - 1U;
  size_t index;

  if (!s_session_valid || (proof == NULL))
  {
    return false;
  }

  memcpy(message, domain, sizeof(domain) - 1U);
  message[offset++] = (uint8_t)sequence;
  message[offset++] = (uint8_t)(sequence >> 8U);
  for (index = 0U; index < 8U; ++index)
  {
    message[offset++] = (uint8_t)(epoch_seconds >> (index * 8U));
  }
  message[offset++] = (uint8_t)offset_value;
  message[offset] = (uint8_t)(offset_value >> 8U);

  HMAC_SHA256(s_session_key, sizeof(s_session_key), message,
              sizeof(message), digest);
  for (index = 0U; index < PAIRING_PROOF_SIZE; ++index)
  {
    difference |= digest[index] ^ proof[index];
  }
  return difference == 0U;
}

bool PairingSecurity_VerifyOtaEnter(
  uint16_t sequence, uint32_t firmware_version, uint32_t nonce,
  const uint8_t proof[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-OTA-ENTER-V1";
  uint8_t message[(sizeof(domain) - 1U) + 10U];
  uint8_t digest[32];
  uint8_t difference = 0U;
  size_t offset = sizeof(domain) - 1U;
  size_t index;

  if (!s_session_valid || (proof == NULL))
  {
    return false;
  }

  memcpy(message, domain, sizeof(domain) - 1U);
  message[offset++] = (uint8_t)sequence;
  message[offset++] = (uint8_t)(sequence >> 8U);
  message[offset++] = (uint8_t)firmware_version;
  message[offset++] = (uint8_t)(firmware_version >> 8U);
  message[offset++] = (uint8_t)(firmware_version >> 16U);
  message[offset++] = (uint8_t)(firmware_version >> 24U);
  message[offset++] = (uint8_t)nonce;
  message[offset++] = (uint8_t)(nonce >> 8U);
  message[offset++] = (uint8_t)(nonce >> 16U);
  message[offset] = (uint8_t)(nonce >> 24U);

  HMAC_SHA256(s_session_key, sizeof(s_session_key), message,
              sizeof(message), digest);
  for (index = 0U; index < PAIRING_PROOF_SIZE; ++index)
  {
    difference |= digest[index] ^ proof[index];
  }
  return difference == 0U;
}

bool PairingSecurity_ImageUpdate(const uint8_t *data, uint32_t size)
{
  if (!s_image_mac_active || ((data == NULL) && (size > 0U)))
  {
    return false;
  }
  SHA256_Update(&s_image_inner, data, size);
  return true;
}

bool PairingSecurity_ImageVerify(const uint8_t proof[PAIRING_PROOF_SIZE])
{
  uint8_t digest[32];
  uint8_t difference = 0U;
  size_t index;

  if (!s_image_mac_active || (proof == NULL))
  {
    return false;
  }
  HMAC_SHA256_End(&s_image_inner, s_session_key, digest);
  s_image_mac_active = false;
  for (index = 0U; index < PAIRING_PROOF_SIZE; ++index)
  {
    difference |= digest[index] ^ proof[index];
  }
  return difference == 0U;
}

bool PairingSecurity_SensorTag(uint16_t sequence, uint8_t result,
                               uint8_t sensor_status,
                               int32_t temperature_centi_c,
                               uint32_t humidity_centi_percent,
                               uint8_t tag[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-SENSOR-V1";
  uint8_t message[(sizeof(domain) - 1U) + 12U];
  uint8_t digest[32];
  uint32_t temperature = (uint32_t)temperature_centi_c;
  size_t offset = sizeof(domain) - 1U;

  if (!s_session_valid || (tag == NULL))
  {
    return false;
  }

  memcpy(message, domain, sizeof(domain) - 1U);
  message[offset++] = (uint8_t)sequence;
  message[offset++] = (uint8_t)(sequence >> 8U);
  message[offset++] = result;
  message[offset++] = sensor_status;
  message[offset++] = (uint8_t)temperature;
  message[offset++] = (uint8_t)(temperature >> 8U);
  message[offset++] = (uint8_t)(temperature >> 16U);
  message[offset++] = (uint8_t)(temperature >> 24U);
  message[offset++] = (uint8_t)humidity_centi_percent;
  message[offset++] = (uint8_t)(humidity_centi_percent >> 8U);
  message[offset++] = (uint8_t)(humidity_centi_percent >> 16U);
  message[offset] = (uint8_t)(humidity_centi_percent >> 24U);

  HMAC_SHA256(s_session_key, sizeof(s_session_key), message,
              sizeof(message), digest);
  memcpy(tag, digest, PAIRING_PROOF_SIZE);
  return true;
}

void PairingSecurity_ClearSession(void)
{
  memset(s_session_key, 0, sizeof(s_session_key));
  memset(&s_image_inner, 0, sizeof(s_image_inner));
  s_session_valid = false;
  s_image_mac_active = false;
}
bool PairingSecurity_VerifyLedSet(uint16_t sequence, uint8_t mode,
                                  const uint8_t proof[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-LED-SET-V1";
  uint8_t message[sizeof(domain) - 1U + 3U];
  uint8_t digest[32];
  uint8_t difference = 0U;
  size_t offset = sizeof(domain) - 1U;
  if (!s_session_valid || proof == NULL) return false;
  memcpy(message, domain, offset);
  message[offset++] = (uint8_t)sequence;
  message[offset++] = (uint8_t)(sequence >> 8U);
  message[offset] = mode;
  HMAC_SHA256(s_session_key, sizeof(s_session_key), message, sizeof(message), digest);
  for (size_t i = 0; i < PAIRING_PROOF_SIZE; ++i) difference |= digest[i] ^ proof[i];
  return difference == 0U;
}

bool PairingSecurity_LedStateTag(uint16_t sequence, const uint8_t state[19],
                                uint8_t tag[PAIRING_PROOF_SIZE])
{
  static const uint8_t domain[] = "EPD-LED-STATE-V1";
  uint8_t message[sizeof(domain) - 1U + 21U];
  uint8_t digest[32];
  size_t offset = sizeof(domain) - 1U;
  if (!s_session_valid || state == NULL || tag == NULL) return false;
  memcpy(message, domain, offset);
  message[offset++] = (uint8_t)sequence;
  message[offset++] = (uint8_t)(sequence >> 8U);
  memcpy(&message[offset], state, 19U);
  HMAC_SHA256(s_session_key, sizeof(s_session_key), message, sizeof(message), digest);
  memcpy(tag, digest, PAIRING_PROOF_SIZE);
  return true;
}
