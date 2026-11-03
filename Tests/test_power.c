/* Real PI against a separate nonlinear bridge/load model and sampled sensor. */
#include "ih_power.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
volatile uint32_t dbg_pwm_phase10,dbg_pwm_softstart;
static uint32_t now,commands;
static int pwm_ok=1;
static Measurement sample;
int Pwm_SetPhase10(uint32_t a)
{
  assert(a>=IH_POWER_MIN_PHASE10 && a<=IH_POWER_MAX_PHASE10);
  if (!pwm_ok) return 0;
  dbg_pwm_phase10=a; dbg_pwm_softstart=0; ++commands; return 1;
}
static void start(uint32_t tick,uint32_t angle)
{
  now=tick; commands=0; pwm_ok=1; dbg_pwm_phase10=angle; dbg_pwm_softstart=0;
  memset(&sample,0,sizeof(sample)); sample.adc_valid=sample.power_valid=1;
  Power_Init(); assert(Power_Start(now,50,1500));
}
static void step(float watts,int locked)
{
  now+=10; ++sample.power_sequence; sample.power_sample_tick=now;
  sample.active_power_w=watts; Power_Task(now,&sample,locked);
}
static float steady(float gain)
{
  float s=sinf((float)dbg_pwm_phase10*0.001745329252f/2.0f);
  return gain*s*s;
}
static float run(float gain,float tau,float watts,unsigned count)
{
  unsigned i;
  for(i=0;i<count;++i) {
    uint32_t angle=dbg_pwm_phase10;
    watts+=(1.0f-expf(-0.01f/tau))*(steady(gain)-watts);
    step(watts,1);
    assert(Power_State()!=POWER_FAILED);
    assert((int)dbg_pwm_phase10-(int)angle<=3);
    assert((int)angle-(int)dbg_pwm_phase10<=7);
    assert(watts<IH_POWER_TRIP_W);
  }
  return watts;
}
int main(void)
{
  static const float gains[]={60,80,150,400},taus[]={0.02f,0.08f,0.20f};
  float watts; unsigned i,j;
  for(i=0;i<sizeof(gains)/sizeof(gains[0]);++i) for(j=0;j<sizeof(taus)/sizeof(taus[0]);++j) {
    start(0,200); watts=run(gains[i],taus[j],steady(gains[i]),4000);
    assert(fabsf(watts-50)<1.5f && Power_State()==POWER_REGULATE);
    assert(dbg_power_target_w==50 && dbg_power_reference_w==50);
    if(gains[i]==60) assert(dbg_pwm_phase10>1200); /* old cap could only reach 45 W */
  }
  start(0,200); watts=run(150,0.08f,steady(150),3500);
  watts=run(100,0.08f,watts,2500); assert(fabsf(watts-50)<1.5f);
  watts=run(120,0.08f,watts,2500); assert(fabsf(watts-50)<1.5f);
  /* Infeasible load cannot wind up the integrator; a lower outer-loop target
   * must immediately leave saturation and converge, including sub-LSB steps. */
  start(0,200); watts=run(35,0.08f,steady(35),5000);
  assert(dbg_pwm_phase10==IH_POWER_MAX_PHASE10 && dbg_power_saturated);
  assert(fabsf(dbg_power_integral10)<IH_POWER_MAX_PHASE10+50);
  assert(Power_SetTargetW(20)); step(watts,1);
  assert(dbg_pwm_phase10<IH_POWER_MAX_PHASE10);
  watts=run(35,0.08f,watts,3000); assert(fabsf(watts-20)<0.6f);
  assert(Power_SetLimitW(10)); watts=run(35,0.08f,watts,2500);
  assert(Power_TargetW()==10 && fabsf(watts-10)<0.6f);
  assert(!Power_SetTargetW(-1) && !Power_SetTargetW(51) && !Power_SetTargetW(NAN));
  assert(!Power_SetLimitW(0) && !Power_SetLimitW(NAN));
  assert(Power_SetTargetW(0)); step(watts,1); assert(Power_State()==POWER_STOP_REQUEST);
  /* Probe limiter ends the amplitude ramp at the ceiling, with no increase
   * before lock. Loss of lock cannot accumulate an upward power command. */
  start(0,600); dbg_pwm_softstart=1; step(45,0);
  assert(Power_AllowStartupRise(now) && commands==0);
  step(52,0); assert(dbg_pwm_phase10<600 && !dbg_pwm_softstart && !Power_AllowStartupRise(now));
  i=dbg_pwm_phase10; for(j=0;j<100;++j) step(30,0);
  assert(dbg_pwm_phase10==i);
  step(30,1); assert(dbg_pwm_phase10<=i+3);
  i=dbg_pwm_phase10; for(j=0;j<100;++j) step(20,0);
  assert(dbg_pwm_phase10==i); step(20,1); assert(dbg_pwm_phase10<=i+3);
  /* Cached data never executes another PI update or advances the reference. */
  start(0,200); step(10,1); i=commands; watts=dbg_power_integral10;
  for(j=0;j<90;++j) { ++now; Power_Task(now,&sample,1); }
  assert(commands==i && dbg_power_integral10==watts);
  now+=10; Power_Task(now,&sample,1);
  assert(Power_State()==POWER_FAILED && Power_Error()==POWER_FEEDBACK);
  Power_Stop(); i=commands; step(10,1); assert(commands==i && Power_State()==POWER_OFF);
  /* Reject stale timestamps even if somebody supplies a new sequence. */
  start(0,200); sample.power_sequence=1; now=101;
  Power_Task(now,&sample,1); assert(Power_Error()==POWER_FEEDBACK && commands==0);
  start(0,200); sample.adc_clipped=1; step(10,1); assert(Power_Error()==POWER_FEEDBACK);
  start(0,200); sample.power_valid=0; step(10,1); assert(Power_Error()==POWER_FEEDBACK);
  start(0,200); step(NAN,1); assert(Power_Error()==POWER_FEEDBACK);
  start(0,200); step(75,1); assert(Power_Error()==POWER_EXCESS && commands==0);
  assert(dbg_power_trip_w==75 && dbg_power_trip_phase10==200);
  Power_Stop(); step(0,1); assert(dbg_power_trip_w==75);
  start(0,200); for(j=0;j<12;++j) step(-3,1);
  assert(Power_Error()==POWER_REVERSE && dbg_pwm_phase10==200 && commands==0);
  start(0,200); pwm_ok=0; step(10,1); assert(Power_Error()==POWER_PWM);
  start(0,200); assert(!Power_Start(now,51,50) && Power_Error()==POWER_CONFIG);
  start(0xFFFFFF00U,200); watts=run(150,0.08f,steady(150),4000);
  assert(fabsf(watts-50)<1.5f);
  puts("PASS: 50 W nonlinear plants, load steps, soft reference, slew limits, lock interlock, sub-LSB integration, anti-windup, limit/zero target, invalid/stale/reversed/overpower/PWM faults and tick wrap");
  return 0;
}
