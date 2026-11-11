/* Execute the real application state machine against mock peripheral registers. */
#include "ih_app.h"
#include "ih_hmi.h"
#include "ih_config.h"
#include "ih_settings.h"
#include "ih_tracking.h"
#include "ih_power.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RCC_TypeDef fake_rcc;
static GPIO_TypeDef fake_gpio;
static HRTIM_TypeDef fake_hrtim;
#undef RCC
#undef GPIOB
#undef HRTIM1
#define RCC (&fake_rcc)
#define GPIOB (&fake_gpio)
#define HRTIM1 (&fake_hrtim)
static void hardware_barrier(void);
#define __DSB() hardware_barrier()
#include "../Core/Src/ih_app.c"
#include "../Core/Src/ih_pwm.c"

HRTIM_HandleTypeDef hhrtim1;
COMP_HandleTypeDef hcomp2,hcomp3,hcomp5,hcomp6;
DAC_HandleTypeDef hdac1,hdac4;
static uint32_t tick;
static int comparator_ov,comparator_oc,link_ok=1,timer_starts,output_starts,save_ok=1;
static int load_ok;
static uint32_t dac_oc_code,dac_ov_code,dac_zc_code;
static unsigned relay_closes;
/* Saved OC affects software protection; hardware stays at the 70 A ceiling. */
#define EXPECT_OCP_CODE(peak) IH_DAC_CODE(IH_HW_OCP_V)
static int freeze_bus_tick;
static int phase_enabled=1;
static int force_power;
static float forced_power;
static Measurement sample;
volatile float dbg_i_mean_adc_v=IH_CURRENT_BIAS_V;
volatile uint32_t dbg_phase_reject,dbg_phase_samples;
volatile float dbg_phase_quality_i,dbg_phase_quality_v;
volatile float dbg_phase_rms_i_adc_v,dbg_phase_rms_v_adc_v;
static ProtectionParams saved={28,18,10,85,90,150};
static void hardware_barrier(void)
{
  fake_hrtim.sCommonRegs.ISR &= ~fake_hrtim.sCommonRegs.ICR;
  fake_hrtim.sCommonRegs.ICR = 0;
}
uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t ms) { tick+=ms; }
uint32_t HAL_RCC_GetSysClockFreq(void) { return 170000000U; }
void Error_Handler(void) { abort(); }
void Hmi_Init(void) { }
void Hmi_Task(void) { }
void Workpiece_Init(void) { }
void Workpiece_Task(void) { }
int Hmi_LinkHealthy(void) { return link_ok; }
void Measurement_Init(void) { }
void Measurement_Task(void) {
  float sine=sinf((float)dbg_pwm_phase10*0.001745329252f/2.0f);
  float detuning=((float)Pwm_Frequency100()-2000.0f)/200.0f;
  sample.active_power_w=force_power ? forced_power : dbg_pwm_en ? 300.0f*sine*sine/(1.0f+detuning*detuning) : 0;
  sample.power_valid=sample.adc_valid && !sample.adc_clipped;
  ++sample.power_sequence; sample.power_sample_tick=tick;
  sample.phase_frequency100=Pwm_Frequency100(); sample.phase_sample_tick=tick;
  ++sample.phase_sequence; sample.phase_valid=(uint8_t)phase_enabled;
  sample.phase_deg=10.0f+((float)Pwm_Frequency100()-2000.0f)*0.1f;
}
void Measurement_SetFrequency(uint32_t f) { assert(f>=1100 && f<=4000); }
void Measurement_BusTask(void) { }
void Measurement_Get(Measurement *m) { *m=sample; }
int Settings_Load(ProtectionParams *p) { if(!load_ok) return 0; *p=saved; return 1; }
int Settings_Save(const ProtectionParams *p) { if(!save_ok) return 0; saved=*p; return 1; }
uint32_t HAL_COMP_GetOutputLevel(const COMP_HandleTypeDef *c)
{ return c==&hcomp2 ? (uint32_t)comparator_ov : c==&hcomp6 ? (uint32_t)comparator_oc : 0U; }
HAL_StatusTypeDef HAL_COMP_Start(COMP_HandleTypeDef *c) { (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_DAC_Start(DAC_HandleTypeDef *d,uint32_t c) { (void)d;(void)c;return HAL_OK; }
HAL_StatusTypeDef HAL_DAC_SetValue(DAC_HandleTypeDef *d,uint32_t c,uint32_t a,uint32_t v)
{
  assert(a==DAC_ALIGN_12B_R && v<=4095U);
  if (d==&hdac4 && c==DAC_CHANNEL_2) {
    assert(!dbg_pwm_en); dac_oc_code=v;
  }
  if (d==&hdac1 && c==DAC_CHANNEL_1) dac_zc_code=v;
  if (d==&hdac1 && c==DAC_CHANNEL_2) dac_ov_code=v;
  return HAL_OK;
}
void HAL_HRTIM_FaultModeCtl(HRTIM_HandleTypeDef *h,uint32_t f,uint32_t e)
{ (void)h;(void)f;(void)e; }
HAL_StatusTypeDef HAL_HRTIM_WaveformCountStart(HRTIM_HandleTypeDef *h,uint32_t mask)
{ (void)h;(void)mask;++timer_starts;return HAL_OK; }
HAL_StatusTypeDef HAL_HRTIM_WaveformOutputStart(HRTIM_HandleTypeDef *h,uint32_t mask)
{ (void)h;(void)mask;++output_starts;return HAL_OK; }
HAL_StatusTypeDef HAL_HRTIM_SoftwareUpdate(HRTIM_HandleTypeDef *h,uint32_t timers)
{ assert(!dbg_pwm_en && timers==UPDATE_ALL); h->Instance->sCommonRegs.CR2=timers; return HAL_OK; }
HAL_StatusTypeDef HAL_HRTIM_WaveformOutputStop(HRTIM_HandleTypeDef *h,uint32_t mask)
{ (void)h;(void)mask;return HAL_OK; }
HAL_StatusTypeDef HAL_HRTIM_WaveformCountStop(HRTIM_HandleTypeDef *h,uint32_t mask)
{ (void)h;(void)mask;return HAL_OK; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState v)
{
  if(pin==RELAY_CTRL_Pin && v==GPIO_PIN_SET) ++relay_closes;
  if(pin==PWM_EN_N_Pin && v==GPIO_PIN_RESET) {
    assert(pwm_priming && tick-pwm_prime_tick>=IH_PWM_PRIME_MS);
    assert(timer_starts>0 && dbg_pwm_phase10==20U);
  }
  if(v==GPIO_PIN_SET) p->ODR|=pin; else p->ODR&=~pin;
}
void HAL_GPIO_TogglePin(GPIO_TypeDef *p,uint16_t pin) { (void)p;(void)pin; }
static void step(unsigned ms)
{ while(ms--) { ++tick; if (!freeze_bus_tick && tick%20U==0U) sample.bus_sample_tick=tick; App_Task(); } }
static void start_and_wait(void)
{ uint32_t generation=History_Generation(),wait; assert(App_Start());
  assert(History_Recording() && History_Generation()==generation+1U);
  for(wait=0;wait<8000 && (!Tracking_Locked() || dbg_pwm_softstart || App_State()!=IH_RUNNING);++wait) step(1);
  assert(App_State()==IH_RUNNING && Tracking_Locked() && !dbg_pwm_softstart);
  assert(dbg_pwm_phase10>=IH_POWER_MIN_PHASE10 && dbg_pwm_phase10<=IH_POWER_MAX_PHASE10); }
static void reset(void)
{ step(IH_RESET_STABLE_MS+1U); assert(App_ResetFault()); assert(App_State()==IH_IDLE); }
int main(int argc, char **argv)
{
  ProtectionParams changed={27,17,9,80,85,140};
  hhrtim1.Instance=&fake_hrtim;
  fake_rcc.APB2ENR=RCC_APB2ENR_HRTIM1EN;
  fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].PERxR=33830;
  fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].CMP1xR=16915;
  fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].PERxR=33830;
  fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP1xR=5638;
  fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP2xR=22553;
  sample.bus_valid=sample.adc_valid=sample.power_valid=1; sample.bus_v=0; sample.current_peak_a=1; link_ok=0;
  if (argc>1 && strcmp(argv[1],"boot_saved")==0) load_ok=1;
  if(argc>1 && strcmp(argv[1],"relay_wrap")==0) tick=0xFFFFFFF0U;
  App_Init(); hardware_barrier();
  assert(dac_zc_code==IH_DAC_CODE(IH_ZC_THRESHOLD_V));
  assert(dac_ov_code==3799 && dac_oc_code==3554);
  if (load_ok) {
    assert(dac_oc_code==EXPECT_OCP_CODE(10.0f));
    assert(!dbg_pwm_en && App_Protection()->oc==10);
    assert(dbg_ov_limit_v==28);
    puts("PASS: 380 V / 70 A hardware DAC ceilings survive saved settings; common zero reference is 1.632 V");
    return 0;
  }
  assert(dac_oc_code==IH_DAC_CODE(IH_HW_OCP_V));
  assert(!History_Recording() && !History_HasData());
  if(argc>1 && strcmp(argv[1],"ov_sources")==0) {
    ProtectionParams limit={25,1,70,85,90,150};
    assert(dbg_ov_limit_v==0);
    link_ok=1; sample.bus_v=24; sample.bus_peak_v=24;
    assert(App_SaveProtection(&limit) && dbg_ov_limit_v==25);
    limit.ov=30; save_ok=0;
    assert(!App_SaveProtection(&limit) && dbg_ov_limit_v==25);
    save_ok=1;
    assert(App_SaveProtection(&limit) && dbg_ov_limit_v==30);
    sample.bus_v=25; sample.bus_peak_v=29.9f; step(10);
    assert(App_Faults()==0 && dbg_ov_limit_v==30);
    start_and_wait();
    sample.bus_peak_v=30.1f; sample.bus_max_raw=308; step(1);
    assert(App_Faults()==IH_FAULT_OV && dbg_trip_mask==IH_FAULT_OV);
    assert(dbg_trip_ov_source==IH_OV_SOFTWARE_PEAK && dbg_trip_pwm_en==1);
    assert(dbg_trip_bus_v==25 && dbg_trip_bus_peak_v==30.1f && dbg_trip_ov_limit_v==30);
    assert(dbg_trip_bus_max_raw==308 && dbg_relay && !dbg_pwm_en);
    sample.bus_peak_v=24.9f; sample.bus_v=24.9f; reset();
    limit.ov=25; assert(App_SaveProtection(&limit) && dbg_ov_limit_v==25);
    sample.bus_v=25; sample.bus_peak_v=25; step(1);
    assert(dbg_trip_ov_source==(IH_OV_SOFTWARE_MEAN|IH_OV_SOFTWARE_PEAK));
    assert(dbg_trip_ov_limit_v==25);
    sample.bus_v=24; sample.bus_peak_v=24; reset();
    comparator_ov=1; step(1);
    assert(dbg_trip_ov_source==IH_OV_COMP2_LEVEL && dbg_trip_bus_peak_v==24);
    comparator_ov=0; reset();
    fake_hrtim.sCommonRegs.ISR=HRTIM_ISR_FLT1; step(1);
    assert(dbg_trip_ov_source==IH_OV_HRTIM_FLT1 && dbg_trip_bus_v==24);
    limit.ov=30; assert(App_SaveProtection(&limit) && dbg_ov_limit_v==30);
    sample.bus_v=23; sample.bus_peak_v=23; step(30);
    assert(dbg_trip_ov_source==IH_OV_HRTIM_FLT1 && dbg_trip_ov_limit_v==25);
    assert(dbg_trip_bus_v==24 && dbg_trip_bus_peak_v==24);
    puts("PASS: global OV limit follows load/save only on success; 25/30 V thresholds, peak-only/mean/COMP2/FLT1 source bits and frozen first evidence");
    return 0;
  }
  if(argc>1 && strcmp(argv[1],"relay_wrap")==0) {
    freeze_bus_tick=1; sample.bus_v=10; sample.bus_sample_tick=0xFFFFFFF0U;
    tick=20; step(1);
    assert(dbg_relay && relay_closes==1 && !dbg_pwm_en && timer_starts==0);
    sample.bus_v=0; App_Stop(); step(300);
    assert(dbg_relay && relay_closes==1 && !dbg_pwm_en);
    App_EmergencyOff(); assert(!dbg_relay && !dbg_pwm_en);
    puts("PASS: 10 V relay freshness across tick wrap, one-shot hold and fatal off");
    return 0;
  }
  if(argc>1 && strcmp(argv[1],"relay_start")==0) {
    ProtectionParams low={28,1,10,85,90,150}; unsigned wait;
    link_ok=1; sample.bus_v=7;
    assert(App_SaveProtection(&low)); step(10);
    assert(App_Start()); step(3000);
    assert(App_State()==IH_PRECHARGE && !dbg_relay && !dbg_pwm_en && timer_starts==0);
    sample.bus_v=10; sample.bus_sample_tick=tick; step(1);
    assert(dbg_relay && relay_closes==1 && !dbg_pwm_en && timer_starts==0);
    step(IH_RELAY_SETTLE_MS-1U); assert(timer_starts==0 && !dbg_pwm_en);
    step(1); assert(timer_starts==1 && pwm_priming && !dbg_pwm_en);
    step(IH_PWM_PRIME_MS-1U); assert(!dbg_pwm_en);
    step(1);
    assert(App_State()==IH_RUNNING && dbg_pwm_en && Tracking_Error()==TRACK_OK);
    for(wait=0;wait<8000 && !Tracking_Locked() && App_State()==IH_RUNNING;++wait) step(1);
    assert(App_State()==IH_RUNNING && Tracking_Locked());
    sample.bus_v=7; step(50); App_Stop(); step(100);
    assert(dbg_relay && relay_closes==1 && !dbg_pwm_en && App_State()==IH_IDLE);
    puts("PASS: START waits >3 s below 10 V without PWM, relay settles 20 ms, gate primes and search timing starts at enable");
    return 0;
  }
  if (argc>1 && strcmp(argv[1],"boot_fault")==0) {
    /* Heating alarms preserve independent voltage-triggered NTC bypass. */
    comparator_ov=1;
    step(1500);
    assert(App_State()==IH_FAULT && (App_Faults()&IH_FAULT_OV));
    assert(!dbg_relay && !dbg_pwm_en && timer_starts==0);
    sample.bus_v=10; sample.bus_valid=0; step(1); assert(!dbg_relay);
    sample.bus_valid=1; sample.bus_sample_tick=tick; step(1);
    assert(dbg_relay && !dbg_pwm_en && App_State()==IH_FAULT);
    App_Stop(); step(500);
    assert(dbg_relay && relay_closes==1 && !dbg_pwm_en && (App_Faults()&IH_FAULT_OV));
    puts("PASS: 10 V NTC bypass remains independent of heating alarms, but requires a valid fresh bus");
    return 0;
  }
  step(1500); /* elapsed boot time alone must NEVER close the bypass */
  assert(!dbg_relay && !dbg_pwm_en && App_State()==IH_IDLE);
  sample.bus_v=12; sample.bus_valid=0; step(1); assert(!dbg_relay);
  sample.bus_valid=1; sample.bus_v=NAN; step(1); assert(!dbg_relay);
  sample.bus_v=INFINITY; step(1); assert(!dbg_relay);
  sample.bus_v=12; freeze_bus_tick=1;
  sample.bus_sample_tick=tick-IH_BUS_STALE_MS; step(1); assert(!dbg_relay);
  sample.bus_sample_tick=tick+1000U; step(1); assert(!dbg_relay);
  sample.bus_v=9.99f; sample.bus_peak_v=12; sample.bus_sample_tick=tick;
  step(1); assert(!dbg_relay); /* mean, not the isolated sampled peak */
  sample.bus_v=10; sample.bus_peak_v=0; sample.bus_sample_tick=tick;
  step(1); freeze_bus_tick=0;
  /* Valid mean >=10 V closes even without START/HMI/saved settings. */
  assert(dbg_relay && !dbg_pwm_en && App_State()==IH_IDLE && timer_starts==0);
  assert(relay_closes==1);
  sample.bus_v=9.9f; step(5); sample.bus_v=10.1f; step(5);
  sample.bus_v=0; step(500);
  assert(dbg_relay && relay_closes==1 && !dbg_pwm_en);
  assert(!App_Start() && (dbg_start_block&IH_START_PARAMS));
  step(IH_RELAY_SETTLE_MS);
  sample.bus_v=24; link_ok=1;
  assert(App_SaveProtection(&saved) && dbg_relay && !dbg_pwm_en);
  assert(dac_oc_code==EXPECT_OCP_CODE(10.0f));
  assert(App_Frequency100()==2010);
  /* Regression for a rejected START after the automatic NTC bypass closes:
   * a zero/undervoltage bus must reject PWM without dropping the relay. */
  sample.bus_v=0;
  assert(!App_Start() && dbg_start_block==IH_START_UV);
  assert(dbg_relay && !dbg_pwm_en && App_Faults()==0U && App_State()==IH_IDLE);
  step(50);
  assert(dbg_relay && !dbg_pwm_en && App_Faults()==0U);
  sample.bus_v=24;
  sample.bus_valid=0; assert(!App_Start()); assert(dbg_start_block&IH_START_BUS); sample.bus_valid=1;
  sample.adc_valid=0; assert(!App_Start()); assert(dbg_start_block&IH_START_ADC); sample.adc_valid=1;
  link_ok=0; assert(!App_Start()); assert(dbg_start_block&IH_START_LINK); link_ok=1;
  sample.bus_v=17; assert(!App_Start()); assert(dbg_start_block&IH_START_UV); sample.bus_v=24;
  sample.bus_peak_v=28; assert(!App_Start()); assert(dbg_start_block&IH_START_OV);
  sample.bus_peak_v=0;
  assert(App_Start()); App_Stop(); step(1000);
  assert(App_State()==IH_IDLE && timer_starts==0 && dbg_relay && !dbg_pwm_en);
  assert(App_Start());
  assert(dbg_relay && !dbg_pwm_en && App_State()==IH_PRECHARGE);
  step(1);
  assert(App_State()==IH_PRECHARGE && !dbg_pwm_en && timer_starts==1);
  step(IH_PWM_PRIME_MS-1U);
  assert(App_State()==IH_PRECHARGE && !dbg_pwm_en);
  step(1);
  assert(App_State()==IH_RUNNING && dbg_relay && dbg_pwm_en && timer_starts==1);
  assert(dbg_pwm_softstart && dbg_pwm_phase10==20U);
  assert(!App_SaveProtection(&changed));
  assert(dac_oc_code==EXPECT_OCP_CODE(10.0f));
  /* Brief hardware fault is caught even after comparator output returns low. */
  fake_hrtim.sCommonRegs.ISR=HRTIM_ISR_FLT3;
  step(1); assert(App_State()==IH_FAULT && (App_Faults()&IH_FAULT_OC));
  assert(!dbg_pwm_en && dbg_relay && !App_Start());
  App_Stop(); assert(dbg_relay && !dbg_pwm_en && App_State()==IH_FAULT);
  comparator_oc=1; step(300); assert(!App_ResetFault());
  comparator_oc=0; step(199); assert(!App_ResetFault()); reset();
  step(1000); assert(dbg_relay && !dbg_pwm_en && timer_starts==1); /* reset never restarts PWM */
  start_and_wait(); sample.bus_v=17; step(19);
  assert(App_Faults()==0U && dbg_pwm_en); /* isolated low sample */
  sample.bus_v=24; step(40);
  assert(App_Faults()==0U && dbg_pwm_en);
  freeze_bus_tick=1; sample.bus_v=17; step(90);
  assert(App_Faults()==0U && dbg_pwm_en); /* a cached result cannot confirm UV */
  sample.bus_v=24; step(1); freeze_bus_tick=0;
  sample.bus_v=17; sample.bus_adc_v=17.0f*IH_BUS_SCALE;
  step(IH_UV_CONFIRM_MS+21U);
  assert((App_Faults()&IH_FAULT_UV) && dbg_relay && !dbg_pwm_en);
  assert(dbg_trip_mask==IH_FAULT_UV && dbg_trip_bus_v==17.0f && dbg_trip_uv_v==18.0f);
  assert(dbg_trip_bus_raw==170U); /* 17 V, 1:401 divider, measured VREF+=3.37 V */
  assert(dbg_start_bus_v==24.0f && dbg_trip_pwm_en==1U);
  assert(dbg_trip_state==IH_RUNNING && dbg_trip_phase10>=IH_POWER_MIN_PHASE10 && dbg_trip_phase10<=IH_POWER_MAX_PHASE10);
  assert(dbg_trip_sample_age_ms<20U);
  assert(dbg_trip_inputs==7U && dbg_trip_adc_age_ms<20U);
  {
    uint32_t starts=dbg_start_count, pwm_starts=dbg_pwm_start_count, trips=dbg_trip_count;
    sample.bus_v=24;
    assert(!App_Start()); step(100);
    assert(dbg_start_count==starts && dbg_pwm_start_count==pwm_starts && dbg_trip_count==trips);
    assert(dbg_trip_bus_v==17.0f); /* history, not a second undervoltage */
    comparator_ov=1; step(1);
    assert((App_Faults()&IH_FAULT_OV) && (App_Faults()&IH_FAULT_UV));
    assert(dbg_trip_count==trips && dbg_trip_mask==IH_FAULT_UV);
    assert(dbg_trip_bus_v==17.0f && dbg_trip_state==IH_RUNNING && dbg_trip_pwm_en==1U);
    assert(dbg_trip_inputs==7U); /* later causes cannot overwrite first input evidence */
    comparator_ov=0;
    sample.bus_v=17;
  }
  step(500); assert(!App_ResetFault() && dbg_relay);
  sample.bus_v=24; reset();
  start_and_wait(); sample.adc_valid=0; step(1);
  assert((App_Faults()&IH_FAULT_ADC) && dbg_relay && !dbg_pwm_en);
  assert(!(dbg_trip_inputs&1U));
  assert(!App_ResetFault()); sample.adc_valid=1; reset();
  start_and_wait(); sample.bus_valid=0; step(1);
  assert((App_Faults()&IH_FAULT_BUS) && dbg_relay && !dbg_pwm_en);
  assert(!(dbg_trip_inputs&2U));
  sample.bus_valid=1; reset();
  start_and_wait(); sample.current_rms_a=5.0f; sample.current_peak_a=7.1f; step(50);
  assert(App_State()==IH_RUNNING && App_Faults()==0U);
  sample.current_peak_a=10.1f; step(1);
  assert((App_Faults()&IH_FAULT_OC) && dbg_relay && !dbg_pwm_en);
  sample.current_rms_a=0; sample.current_peak_a=0; step(20);
  sample.current_peak_a=1; reset();
  start_and_wait(); comparator_oc=1; step(1);
  assert(App_Faults()&IH_FAULT_OC);
  comparator_oc=0; reset();
  start_and_wait(); phase_enabled=0;
  dbg_phase_reject=1; dbg_phase_samples=42;
  dbg_phase_quality_i=0.82f; dbg_phase_quality_v=0.4f;
  dbg_phase_rms_i_adc_v=0.3f; dbg_phase_rms_v_adc_v=0.6f;
  step(IH_TRACK_SIGNAL_MS+30U);
  assert((App_Faults()&IH_FAULT_TRACK) && !dbg_pwm_en && dbg_relay && !Tracking_Locked());
  dbg_phase_reject=7; dbg_phase_quality_i=0; dbg_phase_rms_i_adc_v=0; step(30);
  assert(!History_Recording()); phase_enabled=1; reset();
  /* Cancelling during gate-off priming must never enable the driver later. */
  assert(App_Start()); step(1); assert(pwm_priming && !dbg_pwm_en);
  App_Stop(); step(IH_PWM_PRIME_MS+1U);
  assert(App_State()==IH_IDLE && !dbg_pwm_en && !dbg_pwm_softstart && dbg_relay);
  assert(App_Start()); step(IH_PWM_PRIME_MS+1U); step(20);
  assert(dbg_pwm_softstart && dbg_pwm_phase10>20U && dbg_pwm_phase10<IH_PWM_FIXED_PHASE10);
  sample.current_peak_a=10; step(1);
  assert((App_Faults()&IH_FAULT_OC) && !dbg_pwm_en && !dbg_pwm_softstart && dbg_relay);
  sample.current_peak_a=1; reset();
  assert(App_Start()); sample.current_peak_a=10; step(1);
  assert((App_Faults()&IH_FAULT_OC) && !(App_Faults()&IH_FAULT_BUS));
  assert(!dbg_pwm_en && dbg_relay); /* pre-PWM failure retains its true cause */
  sample.current_peak_a=1; reset();
  App_CommunicationFault(); link_ok=0; step(500); assert(!App_ResetFault());
  assert(dbg_relay && !dbg_pwm_en && (App_Faults()&IH_FAULT_COM));
  link_ok=1; reset();
  save_ok=0; assert(!App_SaveProtection(&changed) && App_Protection()->ov==28);
  assert(dac_oc_code==EXPECT_OCP_CODE(10.0f));
  save_ok=1; assert(App_SaveProtection(&changed) && App_Protection()->ov==27);
  assert(dac_oc_code==EXPECT_OCP_CODE(9.0f));
  {
    ProtectionParams limit=changed;
    limit.oc=IH_LIMIT_OC_PEAK_MAX_A;
    assert(App_SaveProtection(&limit));
    ++limit.oc;
    assert(!App_SaveProtection(&limit) && App_Protection()->oc==IH_LIMIT_OC_PEAK_MAX_A);
    limit.oc=IH_LIMIT_OC_PEAK_MAX_A; limit.ov=381;
    assert(!App_SaveProtection(&limit));
    assert(dac_oc_code==3554 && dac_ov_code==3799);
    assert(dac_zc_code==IH_DAC_CODE(IH_ZC_THRESHOLD_V));
  }
  /* Reproduce the user's exact 7 V bus / 1 V UV setting. Stable 7 V must
   * survive priming and the full ramp; a real 0.6 V input must still trip. */
  {
    ProtectionParams low_voltage={28,1,10,85,90,150};
    assert(App_SaveProtection(&low_voltage));
    sample.bus_v=7.0f; sample.bus_adc_v=7.0f*IH_BUS_SCALE;
    assert(App_Start());
    sample.bus_v=0.6f; sample.bus_adc_v=0.6f*IH_BUS_SCALE;
    step(1);
    assert((App_Faults()&IH_FAULT_UV) && !dbg_pwm_en && dbg_trip_pwm_en==0U);
    assert(dbg_trip_state==IH_PRECHARGE);
    sample.bus_v=7.0f; sample.bus_adc_v=7.0f*IH_BUS_SCALE; reset();
    start_and_wait(); step(500);
    assert(App_Faults()==0U && dbg_pwm_en && dbg_pwm_phase10>=IH_POWER_MIN_PHASE10 && dbg_pwm_phase10<=IH_POWER_MAX_PHASE10);
    sample.bus_v=0.6f; sample.bus_adc_v=0.6f*IH_BUS_SCALE;
    sample.bus_rank1_raw=80; sample.bus_rank2_raw=79;
    sample.bus_min_raw=0; sample.bus_max_raw=82;
    step(IH_UV_CONFIRM_MS+21U);
    assert((App_Faults()&IH_FAULT_UV) && !dbg_pwm_en && dbg_trip_pwm_en==1U);
    assert(dbg_trip_uv_v==1.0f && dbg_trip_bus_v==0.6f);
    assert(dbg_trip_bus_rank1_raw==80U && dbg_trip_bus_rank2_raw==79U);
    assert(dbg_trip_bus_min_raw==0U && dbg_trip_bus_max_raw==82U);
  }
  assert(output_starts==timer_starts && !dbg_pwm_en && dbg_relay);
  sample.bus_v=7.0f; sample.bus_adc_v=7.0f*IH_BUS_SCALE; reset();
  start_and_wait(); step(12000);
  assert(Tracking_Locked() && Power_State()==POWER_REGULATE && fabsf(sample.active_power_w-50)<1.5f);
  assert(Power_SetTargetW(0)); step(1);
  assert(App_State()==IH_IDLE && !dbg_pwm_en && dbg_relay && App_Faults()==0);
  start_and_wait(); force_power=1; forced_power=80; step(20);
  assert((App_Faults()&IH_FAULT_POWER) && !dbg_pwm_en && dbg_relay && Power_Error()==POWER_EXCESS);
  assert(dbg_power_trip_w==80);
  force_power=0; reset();
  start_and_wait(); sample.adc_clipped=1; step(10);
  assert((App_Faults()&IH_FAULT_POWER) && Power_Error()==POWER_FEEDBACK && (dbg_trip_inputs&8U));
  assert(!dbg_pwm_en && dbg_relay);
  sample.adc_clipped=0; step(20); reset();
  puts("PASS: exact 7 V/1 V UV startup, 0.6 V trip before/after gate and frozen raw capture");
  puts("PASS: standby/start/relay timing, hardware/software faults, stable reset and save interlocks");
  return 0;
}
