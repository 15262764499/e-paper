#ifndef PULSE_ENVIRONMENT_H
#define PULSE_ENVIRONMENT_H
#include <stdint.h>
/* Main-context unsolicited Pulse v1 sensor frame (0x90). */
void PulseUART_ReportEnvironment(uint8_t status, int32_t temperature,
                                 uint32_t humidity);
#endif
