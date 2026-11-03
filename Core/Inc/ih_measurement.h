#ifndef IH_MEASUREMENT_H
#define IH_MEASUREMENT_H
#include "main.h"
#include "ih_temperature.h"
#include "ih_workpiece.h"
typedef struct {
  float current_a, voltage_v, current_rms_a, voltage_rms_v;
  float current_peak_a, bus_v, bus_adc_v;
  float bus_peak_v;
  float active_power_w, apparent_power_va, power_factor;
  uint32_t power_sequence, power_sample_tick;
  float phase_deg;
  uint8_t phase_valid;
  uint32_t phase_sequence,phase_sample_tick,phase_frequency100;
  uint8_t power_valid, adc_clipped, current_valid;
  uint8_t adc_valid, bus_valid;
  uint32_t bus_sample_tick;
  uint16_t bus_rank1_raw, bus_rank2_raw;
  uint16_t bus_min_raw, bus_max_raw;
  Temperature components;
  WorkpieceTemperature workpiece;
} Measurement;
void Measurement_Init(void);
void Measurement_Task(void);
/* kHz * 100, from the actual HRTIM period; a change invalidates old phase. */
void Measurement_SetFrequency(uint32_t frequency100);
/* Service from every foreground loop; no polling wait, DMA layout unchanged. */
void Measurement_BusTask(void);
void Measurement_Get(Measurement *out);
float Measurement_BusVoltageFromRaw(uint16_t raw);
/* Nonblocking PA7 ADC2_IN4 injected acquisition; regular DMA layout unchanged. */
void Measurement_BoardBusInit(void);
int Measurement_BoardBusRead(uint16_t *raw);
extern volatile float dbg_i_adc_v, dbg_i_a, dbg_v_adc_v, dbg_v_res_v;
extern volatile float dbg_bus_v, dbg_i_rms_a, dbg_v_rms_v;
extern volatile float dbg_bus_adc_v;
extern volatile float dbg_i_mean_adc_v, dbg_i_ac_rms_adc_v;
extern volatile uint32_t dbg_current_valid;
extern volatile float dbg_power_w, dbg_apparent_va, dbg_power_factor;
extern volatile uint32_t dbg_power_valid, dbg_adc_clipped;
extern volatile float dbg_phase_deg, dbg_phase_quality_i, dbg_phase_quality_v;
extern volatile float dbg_fundamental_power_w;
extern volatile uint32_t dbg_phase_valid, dbg_phase_samples;
extern volatile uint32_t dbg_bus_raw, dbg_bus_samples, dbg_bus_valid, dbg_adc_valid;
extern volatile uint32_t dbg_temp1_raw, dbg_temp2_raw, dbg_temp_valid;
extern volatile uint32_t dbg_bus_rank1_raw, dbg_bus_rank2_raw;
extern volatile uint32_t dbg_bus_min_raw, dbg_bus_max_raw;
extern volatile float dbg_temp1_adc_v, dbg_temp2_adc_v, dbg_temp1_c, dbg_temp2_c;
#endif
