#include "ih_protection.h"

int Protection_ParamsValid(const ProtectionParams *p)
{
  return p->uv > 0 && p->ov > p->uv && p->ov <= 9999 &&
         p->oc > 0 && p->oc <= 9999 && p->ot1 > 0 && p->ot1 <= 999 &&
         p->ot2 > 0 && p->ot2 <= 999 && p->ot3 > 0 && p->ot3 <= 999;
}

uint32_t Protection_Evaluate(const ProtectionParams *p, const ProtectionInputs *in)
{
  uint32_t faults = 0U;
  if (in->hw_ov) faults |= IH_FAULT_OV;
  if (in->hw_oc) faults |= IH_FAULT_OC;
  if (in->bus_valid) {
    if (p->ov > 0 && in->bus_v >= (float)p->ov) faults |= IH_FAULT_OV;
    /* A disconnected/unpowered bus in standby is expected. */
    if (in->running && p->uv > 0 && in->bus_v <= (float)p->uv) faults |= IH_FAULT_UV;
  } else if (in->running) faults |= IH_FAULT_BUS;
  if (in->running && !in->adc_valid) faults |= IH_FAULT_ADC;
  if (in->running && in->adc_valid && p->oc > 0 && in->current_peak_a >= (float)p->oc)
    faults |= IH_FAULT_OC;
  return faults;
}
