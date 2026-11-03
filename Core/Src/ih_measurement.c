#include "ih_measurement.h"
#include "ih_config.h"
#include "ih_phase.h"
#include <math.h>
#if IH_PHASE_SAMPLES != IH_ADC_BUF_SIZE
#error Phase snapshot must contain two complete DMA half blocks
#endif

extern ADC_HandleTypeDef hadc1, hadc2;
static uint32_t dma_buffer[IH_ADC_BUF_SIZE];
static volatile uint64_t current_sum2, voltage_sum2;
static volatile int64_t product_sum, current_sum, voltage_sum;
static volatile uint8_t sample_clipped;
static volatile uint32_t sample_count, last_adc_tick;
static volatile uint32_t peak_counts;
static volatile uint8_t adc_seen, adc_error;
static Measurement measured;
static uint32_t bus_tick, last_bus_tick;
static uint8_t bus_seen;
static volatile uint8_t bus_pending;
static uint32_t bus_conversion_tick, bus_result_tick;
static uint32_t bus_burst_count, bus_sum1, bus_sum2, temp_sum1, temp_sum2;
static uint32_t bus_min, bus_max;
/* Foreground owns a READY snapshot until it has finished the fit. DMA only
 * copies one block per request, never performs trigonometry in the ISR. */
static uint32_t phase_pairs[IH_PHASE_SAMPLES];
static volatile uint8_t phase_capture;
static volatile uint32_t injection_epoch, phase_tick;
static uint32_t callback_epoch, phase_frequency100, phase_result_tick;
static uint8_t phase_seen, phase_good;
volatile float dbg_i_adc_v, dbg_i_a, dbg_v_adc_v, dbg_v_res_v;
volatile float dbg_bus_v, dbg_i_rms_a, dbg_v_rms_v;
volatile float dbg_bus_adc_v;
volatile float dbg_i_mean_adc_v, dbg_i_ac_rms_adc_v;
volatile uint32_t dbg_current_valid;
volatile float dbg_power_w, dbg_apparent_va, dbg_power_factor;
volatile uint32_t dbg_power_valid, dbg_adc_clipped;
volatile float dbg_phase_deg, dbg_phase_quality_i, dbg_phase_quality_v;
volatile float dbg_fundamental_power_w;
volatile uint32_t dbg_phase_valid, dbg_phase_samples;
volatile uint32_t dbg_bus_raw, dbg_bus_samples, dbg_bus_valid, dbg_adc_valid;
volatile uint32_t dbg_temp1_raw, dbg_temp2_raw, dbg_temp_valid;
volatile uint32_t dbg_bus_rank1_raw, dbg_bus_rank2_raw;
volatile uint32_t dbg_bus_min_raw, dbg_bus_max_raw;
volatile float dbg_temp1_adc_v, dbg_temp2_adc_v, dbg_temp1_c, dbg_temp2_c;

void Measurement_BoardBusInit(void)
{
  ADC_InjectionConfTypeDef config = {0};
  /* Both injected groups run synchronously. ADC1 keeps its existing two
   * reserved PA2/PA3 ranks; ADC2 samples PA7 twice with matching sample times.
   * This avoids independently interrupting only the slave regular ADC. */
  config.InjectedChannel = ADC_CHANNEL_4;
  config.InjectedRank = ADC_INJECTED_RANK_1;
  config.InjectedSamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  config.InjectedSingleDiff = ADC_SINGLE_ENDED;
  config.InjectedOffsetNumber = ADC_OFFSET_NONE;
  config.InjectedNbrOfConversion = 2U;
  config.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START;
  config.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_NONE;
  config.AutoInjectedConv = DISABLE;
  config.QueueInjectedContext = DISABLE;
  config.InjecOversamplingMode = DISABLE;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc2, &config) != HAL_OK) Error_Handler();
  config.InjectedRank = ADC_INJECTED_RANK_2;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc2, &config) != HAL_OK) Error_Handler();
}
int Measurement_BoardBusRead(uint16_t *raw)
{
  uint32_t first, second, temp1, temp2;
  if (bus_pending) {
    if (!__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_JEOS) || !__HAL_ADC_GET_FLAG(&hadc2, ADC_FLAG_JEOS)) return 0;
    first = HAL_ADCEx_InjectedGetValue(&hadc2, ADC_INJECTED_RANK_1);
    second = HAL_ADCEx_InjectedGetValue(&hadc2, ADC_INJECTED_RANK_2);
    temp1 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
    temp2 = HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
    /* Flags already checked; a zero timeout makes these calls nonblocking. */
    if (HAL_ADCEx_InjectedPollForConversion(&hadc1, 0U) != HAL_OK ||
        HAL_ADCEx_InjectedPollForConversion(&hadc2, 0U) != HAL_OK) return 0;
    bus_pending = 0U;
    bus_sum1 += first; bus_sum2 += second;
    temp_sum1 += temp1; temp_sum2 += temp2;
    if (second < bus_min) bus_min = second;
    if (second > bus_max) bus_max = second;
    if (++bus_burst_count == IH_BUS_BURST_SCANS) {
      dbg_bus_rank1_raw = (bus_sum1 + IH_BUS_BURST_SCANS / 2U) / IH_BUS_BURST_SCANS;
      dbg_bus_rank2_raw = (bus_sum2 + IH_BUS_BURST_SCANS / 2U) / IH_BUS_BURST_SCANS;
      dbg_bus_min_raw = bus_min; dbg_bus_max_raw = bus_max;
      *raw = (uint16_t)dbg_bus_rank2_raw;
      Temperature_Submit((uint16_t)((temp_sum1 + IH_BUS_BURST_SCANS / 2U) / IH_BUS_BURST_SCANS),
                         (uint16_t)((temp_sum2 + IH_BUS_BURST_SCANS / 2U) / IH_BUS_BURST_SCANS), HAL_GetTick());
      bus_result_tick = bus_conversion_tick;
      bus_burst_count = 0U;
      return 1;
    }
  }
  if (bus_burst_count == 0U) {
    bus_sum1 = bus_sum2 = temp_sum1 = temp_sum2 = 0U;
    bus_min = 4095U; bus_max = 0U;
    bus_conversion_tick = HAL_GetTick();
  }
  /* Restart one scan and yield immediately; no waiting or tight sample loop.
   * Back-to-back scans cover many PWM cycles instead of one fixed phase. */
  ++injection_epoch;
  if (HAL_ADCEx_InjectedStart(&hadc2) == HAL_OK && HAL_ADCEx_InjectedStart(&hadc1) == HAL_OK)
    bus_pending = 1U;
  return 0;
}

void Measurement_Init(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK ||
      HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
    Error_Handler();
  Measurement_BoardBusInit();
  if (HAL_ADC_Start(&hadc2) != HAL_OK ||
      HAL_ADCEx_MultiModeStart_DMA(&hadc1, dma_buffer, IH_ADC_BUF_SIZE) != HAL_OK)
    Error_Handler();
}

static void ProcessBlock(uint32_t start, uint32_t length)
{
  uint32_t i, raw, curr = 0U, volt = 0U, peak = 0U;
  uint32_t sum_i = 0U, sum_v = 0U;
  int32_t sum_iv = 0, dc_i = 0, dc_v = 0;
  uint8_t clipped = 0U;
  int32_t di, dv;
  const int32_t bias_i = (int32_t)(IH_CURRENT_BIAS_V / IH_ADC_VREF_V * 4095.0f + 0.5f);
  const int32_t bias_v = (int32_t)(IH_RESONANT_BIAS_V / IH_ADC_VREF_V * 4095.0f + 0.5f);
  /* A regular block spanning an injected pause has a nonuniform time grid.
   * Discard it for phase; broadband power/protection statistics still run. */
  if (phase_capture == 1U || phase_capture == 2U) {
    if (bus_pending || callback_epoch != injection_epoch) phase_capture = 1U;
    else {
      uint32_t offset = phase_capture == 2U ? length : 0U;
      for (i=0U; i<length; ++i) phase_pairs[offset+i] = dma_buffer[start+i];
      if (phase_capture == 2U) {
        phase_tick = HAL_GetTick();
        __DMB();
        phase_capture = 3U;
      } else phase_capture = 2U;
    }
  }
  callback_epoch = injection_epoch;
  for (i = start; i < start + length; ++i) {
    raw = dma_buffer[i];
    curr = raw & 0xFFFU; volt = (raw >> 16) & 0xFFFU;
    di = (int32_t)curr - bias_i; dv = (int32_t)volt - bias_v;
    dc_i += di; dc_v += dv; sum_iv += di * dv;
    if (curr <= IH_ADC_CLIP_MARGIN || curr >= 4095U - IH_ADC_CLIP_MARGIN ||
        volt <= IH_ADC_CLIP_MARGIN || volt >= 4095U - IH_ADC_CLIP_MARGIN) clipped = 1U;
    sum_i += (uint32_t)(di * di); sum_v += (uint32_t)(dv * dv);
    if (di < 0) di = -di;
    if ((uint32_t)di > peak) peak = (uint32_t)di;
  }
  current_sum2 += sum_i; voltage_sum2 += sum_v; sample_count += length;
  product_sum += sum_iv; current_sum += dc_i; voltage_sum += dc_v;
  if (clipped) sample_clipped = 1U;
  if (peak > peak_counts) peak_counts = peak;
  dbg_i_adc_v = (float)curr * IH_ADC_VREF_V / 4095.0f;
  dbg_v_adc_v = (float)volt * IH_ADC_VREF_V / 4095.0f;
  dbg_i_a = (dbg_i_adc_v - IH_CURRENT_BIAS_V) / IH_CURRENT_SCALE;
  dbg_v_res_v = (dbg_v_adc_v - IH_RESONANT_BIAS_V) / IH_RESONANT_SCALE;
  last_adc_tick = HAL_GetTick(); adc_seen = 1U;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{
  if (adc == &hadc1) ProcessBlock(0U, IH_ADC_BUF_SIZE / 2U);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
  if (adc == &hadc1) ProcessBlock(IH_ADC_BUF_SIZE / 2U, IH_ADC_BUF_SIZE / 2U);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
  if (adc == &hadc1 || adc == &hadc2) adc_error = 1U;
}

void Measurement_SetFrequency(uint32_t frequency100)
{
  uint32_t mask;
  if (phase_frequency100 == frequency100) return;
  mask = __get_PRIMASK(); __disable_irq();
  phase_frequency100 = frequency100;
  phase_seen = phase_good = 0U; measured.phase_valid = 0U;
  phase_capture = 1U;
  __set_PRIMASK(mask);
}

static void PhaseTask(uint32_t now)
{
  if (phase_capture == 3U) {
    PhaseResult result;
    uint32_t captured = phase_tick;
    phase_capture = 4U; /* keep the snapshot immutable during the fit */
    __DMB();
    phase_good = (uint8_t)Phase_Calculate(phase_pairs, IH_PHASE_SAMPLES,
                                        (float)phase_frequency100 * 10.0f, &result);
    measured.phase_deg = result.degrees;
    dbg_phase_quality_i = result.quality_i; dbg_phase_quality_v = result.quality_v;
    dbg_fundamental_power_w = result.fundamental_power_w;
    phase_result_tick = captured; phase_seen = 1U; ++dbg_phase_samples;
    measured.phase_sequence=dbg_phase_samples;
    measured.phase_sample_tick=captured;
    measured.phase_frequency100=phase_frequency100;
    __DMB();
    phase_capture = 1U;
  }
  measured.phase_valid = measured.power_valid && phase_seen && phase_good &&
                        (uint32_t)(now-phase_result_tick) < IH_ADC_STALE_MS;
  dbg_phase_deg = measured.phase_deg; dbg_phase_valid = measured.phase_valid;
}

void Measurement_Task(void)
{
  uint32_t now = HAL_GetTick(), count, peak, mask;
  uint64_t si, sv;
  int64_t siv, sdi, sdv;
  uint8_t clipped;
  mask = __get_PRIMASK(); __disable_irq();
  count = sample_count; si = current_sum2; sv = voltage_sum2; peak = peak_counts;
  siv = product_sum; sdi = current_sum; sdv = voltage_sum; clipped = sample_clipped;
  sample_count = 0U; current_sum2 = 0U; voltage_sum2 = 0U; peak_counts = 0U;
  product_sum = current_sum = voltage_sum = 0; sample_clipped = 0U;
  measured.current_a = dbg_i_a; measured.voltage_v = dbg_v_res_v;
  __set_PRIMASK(mask);
  if (count != 0U) {
    float mean_i = (float)sdi / (float)count, mean_v = (float)sdv / (float)count;
    float k = IH_ADC_VREF_V / 4095.0f;
    /* Covariance and displayed AC RMS remove each channel's measured DC bias.
     * Peak overcurrent protection deliberately retains its fixed reference. */
    float ac_i2 = (float)si / (float)count - mean_i * mean_i;
    float ac_v2 = (float)sv / (float)count - mean_v * mean_v;
    float covariance = (float)siv / (float)count - mean_i * mean_v;
    if (ac_i2 < 0.0f) ac_i2 = 0.0f;
    if (ac_v2 < 0.0f) ac_v2 = 0.0f;
    measured.active_power_w = covariance * k * k / IH_CURRENT_SCALE / IH_RESONANT_SCALE * IH_POWER_POLARITY;
    measured.apparent_power_va = sqrtf(ac_i2 * ac_v2) * k * k / IH_CURRENT_SCALE / IH_RESONANT_SCALE;
    measured.power_factor = measured.apparent_power_va > 0.001f ? measured.active_power_w / measured.apparent_power_va : 0.0f;
    if (measured.power_factor > 1.0f) measured.power_factor = 1.0f;
    if (measured.power_factor < -1.0f) measured.power_factor = -1.0f;
    measured.adc_clipped = clipped;
    dbg_i_mean_adc_v = (mean_i + (float)(int32_t)(IH_CURRENT_BIAS_V / k + 0.5f)) * k;
    dbg_i_ac_rms_adc_v = sqrtf(ac_i2) * k;
    measured.current_rms_a = dbg_i_ac_rms_adc_v / IH_CURRENT_SCALE;
    measured.voltage_rms_v = sqrtf(ac_v2) * k / IH_RESONANT_SCALE;
    measured.current_peak_a = (float)peak * IH_ADC_VREF_V / 4095.0f / IH_CURRENT_SCALE;
  }
  measured.adc_valid = adc_seen && !adc_error && (uint32_t)(now - last_adc_tick) < IH_ADC_STALE_MS;
  measured.current_valid = measured.adc_valid && IH_CURRENT_SENSOR_CONNECTED;
  measured.power_valid = measured.current_valid && !measured.adc_clipped;
  PhaseTask(now);
  dbg_power_w = measured.active_power_w; dbg_apparent_va = measured.apparent_power_va;
  dbg_power_factor = measured.power_factor; dbg_power_valid = measured.power_valid;
  dbg_adc_clipped = measured.adc_clipped;
  dbg_adc_valid = measured.adc_valid;
  dbg_current_valid = measured.current_valid;
  dbg_i_rms_a = measured.current_rms_a; dbg_v_rms_v = measured.voltage_rms_v;
}
void Measurement_BusTask(void)
{
  uint32_t now = HAL_GetTick();
  uint16_t raw;
  if (bus_pending || bus_burst_count != 0U || (uint32_t)(now - bus_tick) >= IH_BUS_BURST_PERIOD_MS) {
    if (!bus_pending && bus_burst_count == 0U) bus_tick = now;
    if (IH_BUS_SCALE > 0.0f && Measurement_BoardBusRead(&raw) && raw <= 4095U) {
      dbg_bus_raw = raw; ++dbg_bus_samples;
      measured.bus_rank1_raw = (uint16_t)dbg_bus_rank1_raw;
      measured.bus_rank2_raw = raw;
      measured.bus_min_raw = (uint16_t)dbg_bus_min_raw;
      measured.bus_max_raw = (uint16_t)dbg_bus_max_raw;
      measured.bus_adc_v = (float)raw * IH_ADC_VREF_V / 4095.0f;
      measured.bus_v = Measurement_BusVoltageFromRaw(raw);
      measured.bus_peak_v = Measurement_BusVoltageFromRaw((uint16_t)dbg_bus_max_raw);
      /* Age is measured from the beginning of the burst, not its readout. */
      last_bus_tick = bus_result_tick; bus_seen = 1U; measured.bus_sample_tick = bus_result_tick;
    }
  }
  measured.bus_valid = bus_seen && (uint32_t)(now - last_bus_tick) < IH_BUS_STALE_MS;
  dbg_bus_adc_v = measured.bus_adc_v; dbg_bus_valid = measured.bus_valid;
  Temperature_Get(&measured.components, now);
  if (adc_error) { measured.components.valid_mask = 0U; measured.components.adc_valid = 0U; }
  Workpiece_Get(&measured.workpiece, now);
  dbg_temp1_raw = measured.components.raw[0]; dbg_temp2_raw = measured.components.raw[1];
  dbg_temp1_adc_v = measured.components.adc_v[0]; dbg_temp2_adc_v = measured.components.adc_v[1];
  dbg_temp1_c = measured.components.celsius[0]; dbg_temp2_c = measured.components.celsius[1];
  dbg_temp_valid = measured.components.valid_mask;
  dbg_bus_v = measured.bus_v;
}
void Measurement_Get(Measurement *out) { *out = measured; }
float Measurement_BusVoltageFromRaw(uint16_t raw)
{
  float nominal = ((float)raw * IH_ADC_VREF_V / 4095.0f - IH_BUS_BIAS_V) / IH_BUS_SCALE;
  float calibrated = nominal * IH_BUS_CAL_GAIN + IH_BUS_CAL_OFFSET_V;
  return calibrated > 0.0f ? calibrated : 0.0f;
}
