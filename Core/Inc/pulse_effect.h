#ifndef PULSE_EFFECT_H
#define PULSE_EFFECT_H
#include <stdbool.h>
#include <stdint.h>
typedef struct
{
  bool active;
  uint16_t value;
  uint8_t brightness, speed;
  uint32_t updated, tick, phase; /* phase units: 1/100 ms, modulo 3000 ms */
} PulseEffect;
static inline void PulseEffect_Advance(PulseEffect *e, uint32_t now)
{
  /* Bound before multiplication, also works across HAL tick wrap. */
  e->phase=(e->phase+((uint32_t)(now-e->tick)%300000U)*e->speed)%300000U;
  e->tick=now;
}
static inline void PulseEffect_Set(PulseEffect *e, uint32_t now, uint16_t value,
                                    uint8_t brightness, uint8_t speed)
{
  if (e->active && (uint32_t)(now-e->updated)<=3000U) PulseEffect_Advance(e,now);
  else { e->phase=0; e->tick=now; }
  e->value=value; e->brightness=brightness; e->speed=speed;
  e->updated=now; e->active=true;
}
static inline void PulseEffect_Duty(PulseEffect *e, uint32_t now, uint8_t duty[8])
{
  for (unsigned q=0;q<8;++q) duty[q]=0;
  if (!e->active) return;
  if ((uint32_t)(now-e->updated)>3000U) { e->active=false;return; }
  PulseEffect_Advance(e,now);
  for (unsigned q=2;q<=6;++q)
  {
    uint32_t cap=q==2 ? 80U : 100U;
    if (e->value>=10000) { duty[q]=(uint8_t)(cap*e->brightness/100);continue; }
    uint32_t base=(q-2)*2000U;
    if (e->value<=base) continue;
    uint32_t fill=e->value-base;
    if (fill>2000) fill=2000;
    uint32_t phase=(e->phase/100+3000-(q-2)*300)%3000;
    if (phase>=1200) continue;
    uint32_t ramp=phase<=600 ? phase : 1200-phase;
    uint64_t numerator=(uint64_t)ramp*ramp*fill*cap*e->brightness;
    const uint64_t denominator=360000ULL*2000*100;
    duty[q]=(uint8_t)((numerator+denominator/2)/denominator);
  }
}
#endif
