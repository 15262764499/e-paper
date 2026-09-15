#ifndef LED_EFFECT_H
#define LED_EFFECT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  LED_MODE_OFF = 0,
  LED_MODE_HUMIDITY = 1,
  LED_MODE_SPREAD = 2,
  LED_MODE_COUNT = 3
} LED_Mode;

#define LED595_SPREAD_PERIOD_MS 3100U

/* Pure effect calculation; duty is a percentage indexed by physical Q0..Q7. */
static inline void LED_Effect(uint32_t elapsed, bool nfc, LED_Mode mode,
                              bool valid, uint32_t humidity, uint8_t duty[8])
{
  uint32_t q;
  for (q = 0; q < 8; ++q) duty[q] = 0;
  for (q = 1; q < 8; ++q)
  {
    uint32_t level = 0;
    uint32_t cap = (q == 1 || q == 2 || q == 7) ? 80U : 100U;
    if (nfc)
    {
      uint32_t phase = elapsed % 3000U;
      uint32_t ramp = phase <= 1500U ? phase : 3000U - phase;
      level = ramp * ramp * 100U / (1500U * 1500U);
    }
    else if (mode == LED_MODE_SPREAD)
    {
      /* 1500 ms outward expansion, 1000 ms hold, 600 ms shared fade-out. */
      uint32_t phase = elapsed % LED595_SPREAD_PERIOD_MS;
      uint32_t distance = q > 4U ? q - 4U : 4U - q;
      uint32_t delay = distance * 300U;
      if (phase >= 2500U)
      {
        uint32_t ramp = LED595_SPREAD_PERIOD_MS - phase;
        level = ramp * ramp * 100U / (600U * 600U);
      }
      else if (phase > delay)
      {
        uint32_t ramp = phase - delay;
        if (ramp > 600U) ramp = 600U;
        level = ramp * ramp * 100U / (600U * 600U);
      }
    }
    else if (mode == LED_MODE_HUMIDITY && valid && q >= 2 && q <= 6)
    {
      if (humidity >= 8000U)
        level = 100U;
      else
      {
        /* 16 %RH per segment; fractional last segment scales its brightness.
         * Overlapping 1.2 s breaths travel Q2 -> Q6 with a 300 ms offset. */
        uint32_t base = (q - 2U) * 1600U;
        if (humidity > base)
        {
          uint32_t fill = humidity - base;
          uint32_t phase = (elapsed % 3000U + 3000U - (q - 2U) * 300U) % 3000U;
          if (fill > 1600U) fill = 1600U;
          if (phase < 1200U)
          {
            uint32_t ramp = phase <= 600U ? phase : 1200U - phase;
            level = (ramp * ramp * 100U / (600U * 600U)) * fill / 1600U;
          }
        }
      }
    }
    duty[q] = (uint8_t)(level * cap / 100U);
  }
}

#endif
