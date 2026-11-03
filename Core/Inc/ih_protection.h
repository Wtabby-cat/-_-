#ifndef IH_PROTECTION_H
#define IH_PROTECTION_H
#include <stdint.h>
#define IH_FAULT_TRACK (1UL << 2) /* existing HMI alarm 2: lock failure */
#define IH_FAULT_OV  (1UL << 3)
#define IH_FAULT_UV  (1UL << 4)
#define IH_FAULT_OC  (1UL << 5)
/* Internal faults have no matching V8 alarm text; reported in page footer. */
#define IH_FAULT_ADC (1UL << 9)
#define IH_FAULT_BUS (1UL << 10)
#define IH_FAULT_COM (1UL << 11)
/* Frozen OC source bits can coexist. Hardware input is PB11, not PA0. */
#define IH_OC_SOFTWARE_PEAK 1U
#define IH_OC_COMP6_LEVEL   2U
#define IH_OC_HRTIM_FLT3    4U
typedef struct {
  int32_t ov, uv, oc, ot1, ot2, ot3;
} ProtectionParams;
typedef struct {
  float bus_v, current_peak_a;
  uint8_t bus_valid, adc_valid, hw_ov, hw_oc, running;
} ProtectionInputs;
int Protection_ParamsValid(const ProtectionParams *p);
uint32_t Protection_Evaluate(const ProtectionParams *p, const ProtectionInputs *in);
#endif
