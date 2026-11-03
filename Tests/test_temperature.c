#include "ih_temperature.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
  Temperature t;
  Temperature_Get(&t, 0U); assert(t.valid_mask==0U && !t.adc_valid);
  Temperature_Submit(2048, 1024, 10U);
  Temperature_Get(&t, 10U);
  assert(t.raw[0]==2048 && t.raw[1]==1024);
  assert(t.adc_valid);
  assert(fabsf(t.adc_v[0]-1.6504f)<0.001f && fabsf(t.adc_v[1]-0.8252f)<0.001f);
#if IH_TEMP1_CONNECTED
  assert(t.valid_mask==1U && fabsf(t.celsius[0]-165.04f)<0.1f);
#else
  assert(t.valid_mask==0U); /* unbuilt front ends must never look valid */
#endif
  Temperature_Submit(3072, 4095, 30U); Temperature_Get(&t, 30U);
  assert(t.adc_v[0]>1.85f && t.adc_v[0]<1.86f); /* low-pass filter */
  Temperature_Get(&t, 130U); assert(t.valid_mask==0U && !t.adc_valid);
  Temperature_Submit(1024, 0, 200U); Temperature_Get(&t, 200U);
  assert(fabsf(t.adc_v[0]-0.8252f)<0.001f); /* stale filter discarded */
  Temperature_Submit(0, 4095, 220U); Temperature_Get(&t, 220U);
  assert(t.valid_mask==0U); /* rail values invalidate calibrated inputs */
  puts("PASS: PA2/PA3 temperature raw values, filter, calibration gating and stale data");
  return 0;
}
