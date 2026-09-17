#ifndef PULSE_PROTOCOL_H
#define PULSE_PROTOCOL_H
#include <stdint.h>
typedef struct { uint8_t bytes[41], used; uint32_t last_byte; } PulseParser;
typedef uint8_t (*PulseCommand)(uint8_t type, const uint8_t *payload);
typedef void (*PulseSend)(const uint8_t *bytes, uint8_t size);
uint16_t Pulse_Crc(const uint8_t *bytes, uint8_t size);
void PulseParser_Byte(PulseParser *parser, uint8_t byte, uint32_t now,
                      PulseCommand command, PulseSend send);
#endif
