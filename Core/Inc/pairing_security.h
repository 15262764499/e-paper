#ifndef PAIRING_SECURITY_H
#define PAIRING_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define PAIRING_KEY_SIZE       16U
#define PAIRING_CHALLENGE_SIZE 16U
#define PAIRING_PROOF_SIZE     16U

bool PairingSecurity_SelfTest(void);
void PairingSecurity_GetKey(uint8_t key[PAIRING_KEY_SIZE]);
void PairingSecurity_GenerateChallenge(const uint8_t board_id[12],
                                       uint8_t challenge[PAIRING_CHALLENGE_SIZE]);
bool PairingSecurity_VerifyProof(const uint8_t board_id[12],
                                 const uint8_t challenge[PAIRING_CHALLENGE_SIZE],
                                 const uint8_t proof[PAIRING_PROOF_SIZE]);
bool PairingSecurity_ImageBegin(uint32_t image_id, uint32_t image_size,
                                uint32_t image_crc32, uint8_t render_profile);
bool PairingSecurity_ImageUpdate(const uint8_t *data, uint32_t size);
bool PairingSecurity_ImageVerify(const uint8_t proof[PAIRING_PROOF_SIZE]);
bool PairingSecurity_VerifyTimeSync(
  uint16_t sequence, uint64_t epoch_seconds, int16_t utc_offset_minutes,
  const uint8_t proof[PAIRING_PROOF_SIZE]);
bool PairingSecurity_VerifyOtaEnter(
  uint16_t sequence, uint32_t firmware_version, uint32_t nonce,
  const uint8_t proof[PAIRING_PROOF_SIZE]);
bool PairingSecurity_SensorTag(uint16_t sequence, uint8_t result,
                               uint8_t sensor_status,
                               int32_t temperature_centi_c,
                               uint32_t humidity_centi_percent,
                               uint8_t tag[PAIRING_PROOF_SIZE]);
void PairingSecurity_ClearSession(void);
bool PairingSecurity_VerifyLedSet(uint16_t sequence, uint8_t mode,
                                  const uint8_t proof[PAIRING_PROOF_SIZE]);
bool PairingSecurity_LedStateTag(uint16_t sequence, const uint8_t state[19],
                                uint8_t tag[PAIRING_PROOF_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* PAIRING_SECURITY_H */
