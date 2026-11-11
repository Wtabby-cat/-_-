#include "ih_protection.h"
#include "ih_config.h"

int Protection_ParamsValid(const ProtectionParams *p)
{
  return p->uv > 0 && p->uv <= IH_LIMIT_UV_MAX_V &&
         p->ov > p->uv && p->ov <= IH_LIMIT_OV_MAX_V &&
         p->oc > 0 && p->oc <= IH_LIMIT_OC_PEAK_MAX_A &&
         p->ot1 > 0 && p->ot1 <= IH_LIMIT_OT_MAX_C &&
         p->ot2 > 0 && p->ot2 <= IH_LIMIT_OT_MAX_C &&
         p->ot3 > 0 && p->ot3 <= IH_LIMIT_OT_MAX_C;
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
