#ifndef IH_TEMPERATURE_H
#define IH_TEMPERATURE_H
#include <stdint.h>
typedef struct {
  uint16_t raw[2];
  float adc_v[2], celsius[2];
  uint8_t valid_mask;
  uint8_t adc_valid; /* Fresh ADC voltages, independent of Celsius calibration. */
} Temperature;
/* Component temperatures ONLY: PA2/ADC1 JDR1 and PA3/ADC1 JDR2.
 * Workpiece temperature is independent, via USART2 / ih_workpiece.h. */
void Temperature_Submit(uint16_t pa2, uint16_t pa3, uint32_t now);
void Temperature_Get(Temperature *out, uint32_t now);
#endif
