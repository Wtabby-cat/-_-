#ifndef IH_PHASE_H
#define IH_PHASE_H
#include <stdint.h>

typedef struct {
  float degrees; /* voltage phase minus current phase; [-180,180] */
  float quality_i, quality_v;
  float fundamental_power_w;
} PhaseResult;

/* Fit DC + sine + cosine at the actual PWM frequency to a contiguous block
 * of paired ADC samples. No PLL/control action and no assumption PF=cos(phi)
 * for distorted waveforms. Returns 0 for weak, clipped or incoherent input. */
int Phase_Calculate(const uint32_t *pairs, uint32_t count, float frequency_hz, PhaseResult *out);
#endif
