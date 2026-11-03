#include "ih_temperature.h"
#include "ih_config.h"

static Temperature temperatures;
static uint32_t sample_tick;
static uint8_t seen;
static const uint8_t connected[2] = {IH_TEMP1_CONNECTED, IH_TEMP2_CONNECTED};
static const float gain[2] = {IH_TEMP1_GAIN_C_PER_V, IH_TEMP2_GAIN_C_PER_V};
static const float offset[2] = {IH_TEMP1_OFFSET_C, IH_TEMP2_OFFSET_C};

void Temperature_Submit(uint16_t pa2, uint16_t pa3, uint32_t now)
{
  uint16_t raw[2];
  uint8_t i;
  raw[0] = pa2; raw[1] = pa3;
  temperatures.valid_mask = 0U;
  for (i = 0U; i < 2U; ++i) {
    float v = (float)raw[i] * IH_ADC_VREF_V / 4095.0f;
    temperatures.raw[i] = raw[i];
    /* Keep raw voltages visible even with conversion disabled. Reset the
     * filter after stale data rather than blending with an old reading. */
    if (seen && (uint32_t)(now - sample_tick) < IH_ADC_STALE_MS)
      temperatures.adc_v[i] += IH_TEMP_FILTER_ALPHA * (v - temperatures.adc_v[i]);
    else temperatures.adc_v[i] = v;
    if (connected[i] && gain[i] != 0.0f && raw[i] > 0U && raw[i] < 4095U) {
      float c = temperatures.adc_v[i] * gain[i] + offset[i];
      temperatures.celsius[i] = c;
      temperatures.valid_mask |= (uint8_t)(1U << i);
    } else temperatures.celsius[i] = 0.0f; /* valid_mask is authoritative */
  }
  sample_tick = now; seen = 1U;
}
void Temperature_Get(Temperature *out, uint32_t now)
{
  *out = temperatures;
  out->adc_valid = seen && (uint32_t)(now - sample_tick) < IH_ADC_STALE_MS;
  if (!out->adc_valid) out->valid_mask = 0U;
}
