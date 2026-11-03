#include "ih_phase.h"
#include "ih_config.h"
#include <math.h>
#include <string.h>

int Phase_Calculate(const uint32_t *pairs, uint32_t count, float frequency_hz, PhaseResult *out)
{
  float ss=0, cc=0, sc=0, sum_s=0, sum_c=0;
  float sum_i=0, sum_v=0, ii=0, vv=0, is=0, ic=0, vs=0, vc=0;
  float s=0, c=1, step_s, step_c, n, a, b, d, det;
  float win_s=0,win_c=1,win_step_s,win_step_c,sum_w=0;
  float ai, bi, av, bv, energy_i, energy_v, dot, cross, k;
  uint32_t j;
  memset(out, 0, sizeof(*out));
  if (count < 128U || frequency_hz < 10000.0f || frequency_hz > 40000.0f) return 0;
  step_s = sinf(6.28318530718f * frequency_hz / IH_ADC_REGULAR_SAMPLE_HZ);
  step_c = cosf(6.28318530718f * frequency_hz / IH_ADC_REGULAR_SAMPLE_HZ);
  win_step_s=sinf(6.28318530718f/(float)(count-1U));
  win_step_c=cosf(6.28318530718f/(float)(count-1U));
  for (j=0; j<count; ++j) {
    uint32_t ri=pairs[j]&0xFFFU, rv=(pairs[j]>>16)&0xFFFU;
    /* Centre for numerical conditioning; the fitted DC term, not this
     * arbitrary midpoint, determines each channel's actual zero. */
    float i=(float)ri-2048.0f, v=(float)rv-2048.0f, next_s;
    float weight=0.5f*(1.0f-win_c),next_win_s;
    if (ri<=IH_ADC_CLIP_MARGIN || ri>=4095U-IH_ADC_CLIP_MARGIN ||
        rv<=IH_ADC_CLIP_MARGIN || rv>=4095U-IH_ADC_CLIP_MARGIN) return 0;
    /* Hann-weighted least squares suppresses bridge-voltage harmonics leaking
     * into the fitted fundamental when the snapshot spans fractional cycles. */
    if (weight<0) weight=0;
    sum_w+=weight;
    sum_s+=weight*s; sum_c+=weight*c; ss+=weight*s*s; cc+=weight*c*c; sc+=weight*s*c;
    sum_i+=weight*i; sum_v+=weight*v; ii+=weight*i*i; vv+=weight*v*v;
    is+=weight*i*s; ic+=weight*i*c; vs+=weight*v*s; vc+=weight*v*c;
    next_s=s*step_c+c*step_s; c=c*step_c-s*step_s; s=next_s;
    next_win_s=win_s*win_step_c+win_c*win_step_s;
    win_c=win_c*win_step_c-win_s*win_step_s; win_s=next_win_s;
  }
  /* Solve the centred 2x2 normal equations. Unlike a raw DFT, this removes
   * independent DC offsets even when the block contains fractional cycles. */
  n=sum_w;
  a=ss-sum_s*sum_s/n; b=sc-sum_s*sum_c/n; d=cc-sum_c*sum_c/n;
  det=a*d-b*b;
  if (det<1.0f) return 0;
  is-=sum_i*sum_s/n; ic-=sum_i*sum_c/n;
  vs-=sum_v*sum_s/n; vc-=sum_v*sum_c/n;
  ai=(is*d-ic*b)/det; bi=(ic*a-is*b)/det;
  av=(vs*d-vc*b)/det; bv=(vc*a-vs*b)/det;
  energy_i=ii-sum_i*sum_i/n; energy_v=vv-sum_v*sum_v/n;
  k=IH_ADC_VREF_V/4095.0f;
  if (energy_i<=0 || energy_v<=0 ||
      energy_i*k*k/n<IH_PHASE_MIN_RMS_ADC_V*IH_PHASE_MIN_RMS_ADC_V ||
      energy_v*k*k/n<IH_PHASE_MIN_RMS_ADC_V*IH_PHASE_MIN_RMS_ADC_V) return 0;
  out->quality_i=(ai*is+bi*ic)/energy_i;
  out->quality_v=(av*vs+bv*vc)/energy_v;
  if (out->quality_i>1.0f) out->quality_i=1.0f;
  if (out->quality_v>1.0f) out->quality_v=1.0f;
  dot=(av*ai+bv*bi)*IH_POWER_POLARITY;
  cross=(bv*ai-av*bi)*IH_POWER_POLARITY;
  out->degrees=atan2f(cross,dot)*57.2957795131f;
  out->fundamental_power_w=0.5f*dot*k*k/IH_CURRENT_SCALE/IH_RESONANT_SCALE;
  return out->quality_i>=IH_PHASE_MIN_CURRENT_QUALITY && out->quality_v>=IH_PHASE_MIN_VOLTAGE_QUALITY;
}
