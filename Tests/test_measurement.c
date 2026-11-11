/* Exercise the actual DMA callback and power statistics, with real sample
 * pairs, independent DC offsets, quadrature, reversed CT and clipped ADC. */
#include "ih_measurement.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(v) ((void)(v))
#define __DMB() ((void)0)
#include "../Core/Src/ih_measurement.c"
ADC_HandleTypeDef hadc1, hadc2;
static uint32_t tick;
uint32_t HAL_GetTick(void) { return tick; }
void Error_Handler(void) { abort(); }
HAL_StatusTypeDef HAL_ADCEx_InjectedConfigChannel(ADC_HandleTypeDef *a, const ADC_InjectionConfTypeDef *c)
{ (void)a; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *a) { (void)a; return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_InjectedPollForConversion(ADC_HandleTypeDef *a,uint32_t t)
{ (void)a; (void)t; return HAL_OK; }
uint32_t HAL_ADCEx_InjectedGetValue(const ADC_HandleTypeDef *a,uint32_t r)
{ return r==ADC_INJECTED_RANK_1 ? a->Instance->JDR1 : a->Instance->JDR2; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *a,uint32_t d)
{ (void)a; (void)d; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *a) { (void)a; return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_MultiModeStart_DMA(ADC_HandleTypeDef *a,uint32_t *b,uint32_t n)
{ (void)a; (void)b; (void)n; return HAL_OK; }
void Temperature_Submit(uint16_t a,uint16_t b,uint32_t t) { (void)a; (void)b; (void)t; }
void Temperature_Get(Temperature *t,uint32_t n) { (void)n; memset(t,0,sizeof(*t)); }
void Workpiece_Get(WorkpieceTemperature *t,uint32_t n)
{ memset(t,0,sizeof(*t)); t->celsius=55.5f; t->valid=1; t->sample_tick=n; }
static Measurement acquire(int phase, int dc_i, int dc_v)
{
  static const int wave[4] = {0, 100, 0, -100};
  Measurement m;
  unsigned i, repeat;
  for (repeat=0;repeat<4;++repeat) {
    for(i=0;i<IH_ADC_BUF_SIZE;++i) {
      int a=dc_i+wave[i%4], b=dc_v+2*wave[(i+(unsigned)phase)%4];
      dma_buffer[i]=(uint32_t)a|((uint32_t)b<<16);
    }
    HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
  }
  ++tick; Measurement_Task(); Measurement_Get(&m); return m;
}
static Measurement acquire_sine(float degrees, int dc_i, int dc_v, int reverse, float amplitude)
{
  Measurement m;
  unsigned i;
  Measurement_SetFrequency(2010);
  for(i=0;i<IH_ADC_BUF_SIZE;++i) {
    float angle=6.28318530718f*20100.0f*(float)i/IH_ADC_REGULAR_SAMPLE_HZ+0.73f;
    int a=(int)lroundf((float)dc_i+amplitude*sinf(angle)*(reverse ? -1.0f : 1.0f));
    int b=(int)lroundf((float)dc_v+2.0f*amplitude*sinf(angle+degrees*0.01745329252f));
    dma_buffer[i]=(uint32_t)a|((uint32_t)b<<16);
  }
  HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
  ++tick; Measurement_Task(); Measurement_Get(&m); return m;
}
int main(void)
{
  ADC_TypeDef regs1={0},regs2={0};
  Measurement m;
  float k=IH_ADC_VREF_V/4095.0f;
  float expected=10000.0f*k*k/IH_CURRENT_SCALE/IH_RESONANT_SCALE;
  unsigned i;
  hadc1.Instance=&regs1; hadc2.Instance=&regs2;
  for(i=0;i<IH_ADC_BUF_SIZE;++i) dma_buffer[i]=1983U|(1983U<<16);
  HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
  Measurement_Task(); Measurement_Get(&m);
  assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED && m.current_valid==IH_CURRENT_SENSOR_CONNECTED);
  assert(m.power_sequence==1 && m.power_sample_tick==tick);
  ++tick; Measurement_Task(); Measurement_Get(&m);
  assert(m.power_sequence==1 && m.power_sample_tick==tick-1U);
  assert(fabsf(m.active_power_w)<0.0001f);
  assert(m.current_rms_a<0.01f && m.voltage_rms_v<0.02f); /* measured 1.632 V midpoint */
  assert(m.current_peak_a<0.0001f && fabsf(dbg_i_mean_adc_v-1.6319194f)<0.0001f);
  /* A constant 1.62 V input is a DC offset, not resonant AC current. The
   * conservative fixed-reference peak check must still see the offset. */
  for(i=0;i<IH_ADC_BUF_SIZE;++i) dma_buffer[i]=1969U|(1983U<<16);
  HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
  Measurement_Task(); Measurement_Get(&m);
  assert(m.adc_valid && m.current_valid==IH_CURRENT_SENSOR_CONNECTED);
  assert(m.current_rms_a<0.0001f && fabsf(m.active_power_w)<0.0001f);
  assert(m.current_peak_a>0.62f && m.current_peak_a<0.63f);
  assert(fabsf(dbg_i_mean_adc_v-1.620398f)<0.0001f && dbg_i_ac_rms_adc_v<0.0001f);
  m=acquire(0,1800,2200);
  assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED && fabsf(m.active_power_w-expected)<0.0001f);
  assert(fabsf(m.current_rms_a-sqrtf(5000.0f)*k/IH_CURRENT_SCALE)<0.0001f);
  assert(fabsf(m.voltage_rms_v-sqrtf(20000.0f)*k/IH_RESONANT_SCALE)<0.0001f);
  assert(fabsf(m.power_factor-1.0f)<0.0001f);
  m=acquire(0,1500,1900); /* power does not depend on either DC offset */
  assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED && fabsf(m.active_power_w-expected)<0.0001f);
  assert(fabsf(m.current_rms_a-sqrtf(5000.0f)*k/IH_CURRENT_SCALE)<0.0001f);
  assert(fabsf(m.voltage_rms_v-sqrtf(20000.0f)*k/IH_RESONANT_SCALE)<0.0001f);
  m=acquire(1,1800,2200); /* quadrature: VA exists, active W is zero */
  assert(m.apparent_power_va>0 && fabsf(m.active_power_w)<0.0001f);
  m=acquire(2,1800,2200);
  assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED && fabsf(m.active_power_w+expected)<0.0001f);
  assert(fabsf(m.power_factor+1.0f)<0.0001f);
  m=acquire(0,100,2200); /* one current sample hits the rail */
  assert(m.adc_valid && m.adc_clipped && !m.power_valid);
  m=acquire(0,1800,2200); assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED);
  tick+=IH_ADC_STALE_MS; Measurement_Task(); Measurement_Get(&m);
  assert(!m.adc_valid && !m.power_valid && !m.current_valid);
  m=acquire(0,1800,2200); assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED);
  /* Actual snapshot -> foreground fit, including independent DC offsets,
   * fractional cycles, signed lead/lag, reversed CT and negative power. */
  {
    static const float angles[]={0,25,-25,90,-90,170,-170};
    unsigned j;
    for(j=0;j<sizeof(angles)/sizeof(angles[0]);++j) {
      m=acquire_sine(angles[j],1800,2200,0,200);
      assert(m.phase_valid==IH_CURRENT_SENSOR_CONNECTED);
      assert(m.phase_frequency100==2010 && m.phase_sequence==dbg_phase_samples && tick-m.phase_sample_tick<=1U);
      assert(fabsf(m.phase_deg-angles[j])<0.15f);
      assert(dbg_phase_quality_i>0.999f && dbg_phase_quality_v>0.999f);
    }
    m=acquire_sine(25,1500,1900,0,200);
    assert(fabsf(m.phase_deg-25)<0.15f);
    m=acquire_sine(25,1800,2200,1,200);
    assert(m.active_power_w<0 && dbg_fundamental_power_w<0);
    assert(fabsf(m.phase_deg+155)<0.15f); /* not abs(), not acos(PF) */
    m=acquire_sine(25,1800,2200,0,2);
    assert(!m.phase_valid); /* weak AC, even though ADC/current are valid */
    m=acquire_sine(25,200,2200,0,200); assert(!m.phase_valid && m.adc_clipped);
    m=acquire_sine(25,1800,2200,0,200);
    assert(m.phase_valid==IH_CURRENT_SENSOR_CONNECTED);
    phase_capture=4; tick+=IH_ADC_STALE_MS;
    m=acquire_sine(25,1800,2200,0,200);
    assert(m.adc_valid && !m.phase_valid); /* only phase snapshot has gone stale */
    phase_capture=1;
    m=acquire_sine(25,1800,2200,0,200);
    tick+=IH_ADC_STALE_MS; Measurement_Task(); Measurement_Get(&m);
    assert(!m.phase_valid);
    m=acquire(0,1800,2200); assert(!m.phase_valid); /* wrong frequency */
    /* Preserve baseline capture scheduling while comparing hardware OCP. */
    phase_capture=1; callback_epoch=injection_epoch; bus_pending=1;
    regs1.ISR=regs2.ISR=ADC_FLAG_JEOS;
    m=acquire_sine(25,1800,2200,0,200); assert(phase_capture==1);
    bus_pending=0;
    m=acquire_sine(25,1800,2200,0,200);
    assert(m.phase_valid==IH_CURRENT_SENSOR_CONNECTED);
    regs1.ISR=regs2.ISR=0;
    /* Injected conversions split a regular block: never fit that time gap. */
    phase_capture=1; callback_epoch=injection_epoch; bus_pending=1;
    m=acquire_sine(25,1800,2200,0,200); assert(phase_capture==1);
    bus_pending=0;
    HAL_ADC_ConvHalfCpltCallback(&hadc1); assert(phase_capture==2);
    ++injection_epoch;
    HAL_ADC_ConvCpltCallback(&hadc1); assert(phase_capture==1);
    m=acquire_sine(25,1800,2200,0,200);
    assert(m.phase_valid==IH_CURRENT_SENSOR_CONNECTED);
    Measurement_SetFrequency(0); Measurement_Get(&m); assert(!m.phase_valid);
    /* A 60-degree bridge pulse has substantial harmonics. Display its
     * fundamental angle, not acos(total-waveform power factor). */
    Measurement_SetFrequency(2010);
    for(i=0;i<IH_ADC_BUF_SIZE;++i) {
      float angle=6.28318530718f*20100.0f*(float)i/IH_ADC_REGULAR_SAMPLE_HZ+0.73f;
      float x=sinf(angle+25.0f*0.01745329252f);
      int a=(int)lroundf(1800.0f+200.0f*sinf(angle));
      int b=2200+(x>0.8660254f ? 400 : (x < -0.8660254f ? -400 : 0));
      dma_buffer[i]=(uint32_t)a|((uint32_t)b<<16);
    }
    HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
    ++tick; Measurement_Task(); Measurement_Get(&m);
    assert(m.phase_valid==IH_CURRENT_SENSOR_CONNECTED && fabsf(m.phase_deg-25.0f)<5.0f);
    assert(dbg_phase_quality_v>0.35f && dbg_phase_quality_v<0.70f);
    assert(fabsf(acosf(m.power_factor)*57.2957795f-m.phase_deg)>10.0f);
  }
  /* Independent board vector: new CT/burden gives 0.0924 V at 5 A peak;
   * new voltage chain gives 0.0658354 V at 20 V peak. Quantized 4-point
   * in-phase waves must measure ~3.54 Arms, ~14.14 Vrms and ~50 W. */
  {
    static const int unit[]={0,1,0,-1};
    for(i=0;i<IH_ADC_BUF_SIZE;++i)
      dma_buffer[i]=(uint32_t)(1983+112*unit[i%4])|
                    ((uint32_t)(1983+80*unit[i%4])<<16);
    HAL_ADC_ConvHalfCpltCallback(&hadc1); HAL_ADC_ConvCpltCallback(&hadc1);
    ++tick; Measurement_Task(); Measurement_Get(&m);
    assert(m.power_valid==IH_CURRENT_SENSOR_CONNECTED);
    assert(fabsf(m.current_rms_a-3.535534f)<0.03f);
    assert(fabsf(m.current_peak_a-5.0f)<0.05f);
    assert(fabsf(m.voltage_rms_v-14.142136f)<0.15f);
    assert(fabsf(m.active_power_w-50.0f)<0.7f);
  }
  hadc2.ErrorCode=HAL_ADC_ERROR_OVR|HAL_ADC_ERROR_DMA;
  HAL_ADC_ErrorCallback(&hadc2); Measurement_Task(); Measurement_Get(&m);
  assert(!m.adc_valid && !m.power_valid && dbg_adc_error_count==1);
  assert(dbg_adc1_error_bits==0 && dbg_adc2_error_bits==(HAL_ADC_ERROR_OVR|HAL_ADC_ERROR_DMA));
  hadc1.ErrorCode=HAL_ADC_ERROR_JQOVF; HAL_ADC_ErrorCallback(&hadc1);
  hadc1.ErrorCode=0; HAL_ADC_ErrorCallback(&hadc1);
  assert(dbg_adc_error_count==3 && dbg_adc1_error_bits==HAL_ADC_ERROR_JQOVF);
  assert(dbg_adc2_error_bits==(HAL_ADC_ERROR_OVR|HAL_ADC_ERROR_DMA));
  Measurement_BusTask(); Measurement_Get(&m);
  assert(m.workpiece.valid && m.workpiece.celsius==55.5f && !m.components.adc_valid);
  /* Exercise the actual BusTask -> 32 scan mean -> Measurement_Get chain,
   * not just BoardBusRead. A serial temperature sample must not overwrite bus. */
  bus_pending=0; bus_burst_count=0; bus_tick=0; bus_seen=0;
  regs1.ISR=regs2.ISR=0; tick=50;
  Measurement_BusTask();
  for(i=0;i<IH_BUS_BURST_SCANS;++i) {
    ++tick; regs1.ISR=regs2.ISR=ADC_FLAG_JEOS;
    regs1.JDR1=101; regs1.JDR2=202; regs2.JDR1=920; regs2.JDR2=925;
    Measurement_BusTask();
  }
  Measurement_Get(&m);
  assert(m.bus_valid && dbg_bus_samples==1 && dbg_bus_raw==925);
  assert(fabsf(m.bus_v-92.50137f)<0.001f && m.workpiece.celsius==55.5f);
  /* Independently quantized nominal 1:401-divider vectors. There must be
   * no leftover fitted gain, low segment, zero suppression or -0.5 V trim. */
  {
    static const uint16_t raw[]={0,50,100,200,300,3800};
    static const float volts[]={0,5,10,20,30,380};
    float previous=0;
    unsigned j;
    for(j=0;j<sizeof(raw)/sizeof(raw[0]);++j)
      assert(fabsf(Measurement_BusVoltageFromRaw(raw[j])-volts[j])<0.05f);
    /* Entire 12-bit range stays linear and monotonic. */
    for(j=0;j<=4095;++j) {
      float v=Measurement_BusVoltageFromRaw((uint16_t)j);
      assert(isfinite(v) && v>=previous && v-previous<0.101f);
      assert(fabsf(v-(float)j*3.37f/4095.0f*401.0f/3.3f)<0.0001f);
      previous=v;
    }
    assert(Measurement_BusVoltageFromRaw(1)>0.0f && Measurement_BusVoltageFromRaw(1)<0.101f);
    assert(fabsf(Measurement_BusVoltageFromRaw(4)-0.4000059f)<0.0001f);
  }
  /* Above the measured range, test inverse algebra only, not board accuracy. */
  assert(Measurement_BusVoltageFromRaw(3799)<380.0f);
  assert(Measurement_BusVoltageFromRaw(3800)>380.0f);
  assert(Measurement_BusVoltageFromRaw(0)==0.0f);
  assert(fabsf(m.bus_peak_v-m.bus_v)<0.0001f);
  assert(m.bus_rank1_raw==920 && m.bus_rank2_raw==925);
  ++tick; Measurement_BusTask();
  for(i=0;i<IH_BUS_BURST_SCANS;++i) {
    ++tick; regs1.ISR=regs2.ISR=ADC_FLAG_JEOS;
    regs2.JDR1=regs2.JDR2=4;
    if(i+1U==IH_BUS_BURST_SCANS) regs2.JDR2=43; /* preserve peak independently of mean */
    Measurement_BusTask();
  }
  Measurement_Get(&m);
  assert(m.bus_valid && dbg_bus_samples==2 && dbg_bus_raw==5);
  assert(fabsf(m.bus_v-0.5000074f)<0.0001f);
  assert(m.bus_peak_v>4.3f && m.bus_peak_v<4.4f && m.bus_max_raw==43);
  puts("PASS: nominal 1:401 bus conversion, no empirical compensation, 0/5/10/20/30/380 V, full-range linearity and shared mean/peak/DAC consistency");
  puts("PASS: 3.3-ohm CT burden and new resonant divider recover physical RMS, peak and 50 W vectors");
  puts("PASS: paired-sample signed W, VA, power factor, DC bias, clipping and stale/error gating");
  puts("PASS: measured-mean AC RMS rejects 1.62 V DC; fixed-reference peak protection and disconnected-CT validity remain independent");
  puts("PASS: signed fundamental phase, independent DC offsets, weak/clipped/stale/incoherent gating, injection-gap rejection and CT reversal");
  puts("PASS: 1.632 V midpoint and sticky per-ADC HAL error evidence without automatic fault clearing");
  return 0;
}
