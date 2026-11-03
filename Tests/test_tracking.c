/* Real controller with an independent deterministic resonant phase plant. */
#include "ih_tracking.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t frequency,commands,ramps,last_command;
static int pwm_ok=1;
volatile uint32_t dbg_pwm_phase10,dbg_pwm_softstart;
uint32_t Pwm_Frequency100(void) { return frequency; }
int Pwm_SetFrequency100(uint32_t f)
{
  assert(f>=1500 && f<=2500);
  if(!pwm_ok) return 0;
  frequency=f; ++commands; return 1;
}
void Pwm_RampTo(uint32_t a,uint32_t now)
{ (void)a; (void)now; ++ramps; assert(!"PLL must not command the power angle"); }
static Measurement sample;
static uint32_t now,origin;
static void start(uint32_t time)
{
  now=origin=time; frequency=2500; commands=ramps=0; pwm_ok=1; last_command=0;
  memset(&sample,0,sizeof(sample));
  Tracking_Init(); assert(Tracking_SetBounds(1500,2500));
  assert(!Tracking_SetBounds(1000,2500) && !Tracking_SetBounds(2500,1500));
  Tracking_Start(now); dbg_pwm_softstart=0; Tracking_Task(now,&sample);
  assert(Tracking_State()==TRACK_SEARCH);
}
static void step(float phase,int fresh)
{
  uint32_t before=commands,old=frequency;
  now+=10; sample.phase_valid=1; sample.phase_deg=phase;
  if(fresh) { ++sample.phase_sequence; sample.phase_sample_tick=now; sample.phase_frequency100=frequency; }
  Tracking_Task(now,&sample);
  if(commands!=before) {
    assert(now-last_command>=IH_TRACK_SETTLE_MS || last_command==0);
    if(Tracking_State()!=TRACK_SEARCH) assert(abs((int)old-(int)frequency)<=2);
    last_command=now;
  }
}
int main(void)
{
  uint32_t i,locked_frequency;
  start(0);
  for(i=0;i<1500 && !Tracking_Locked();++i) step(10.0f+((float)frequency-2000.0f)*0.1f,1);
  assert(Tracking_Locked() && now-origin<IH_SEARCH_TIMEOUT_MS && frequency>=1970 && frequency<=2030);
  assert(ramps==0 && !Tracking_AllowRamp()); locked_frequency=frequency;
  for(i=0;i<450;++i) step(10.0f+((float)frequency-2070.0f)*0.1f,1);
  assert(Tracking_Locked() && frequency>locked_frequency && frequency<=2100 && ramps==0);
  for(i=0;i<30;++i) step(10,0);
  assert(Tracking_State()==TRACK_FAILED && Tracking_Error()==TRACK_SIGNAL);
  Tracking_Stop(); assert(!Tracking_Locked() && !Tracking_AllowRamp());
  start(0); for(i=0;i<20;++i) step(163,1); assert(Tracking_Error()==TRACK_PHASE);
  start(0); for(i=0;i<20;++i) step(-17,1); assert(Tracking_Error()==TRACK_PHASE);
  start(0); for(i=0;i<20;++i) step(5,1); assert(Tracking_Error()==TRACK_RANGE);
  start(0); for(i=0;i<1400 && Tracking_State()!=TRACK_FAILED;++i) step(60,1);
  assert(Tracking_Error()==TRACK_RANGE && frequency==1500);
  start(0); sample.phase_frequency100=2010;
  for(i=0;i<30;++i) { now+=10; ++sample.phase_sequence; sample.phase_sample_tick=now; sample.phase_valid=1; Tracking_Task(now,&sample); }
  assert(Tracking_Error()==TRACK_SIGNAL && commands==0);
  start(0); pwm_ok=0; for(i=0;i<20;++i) step(60,1); assert(Tracking_Error()==TRACK_PWM);
  start(0); for(i=0;i<9;++i) step(10,1);
  assert(Tracking_State()==TRACK_ACQUIRE);
  for(i=0;i<600 && Tracking_State()!=TRACK_FAILED;++i) step(15,1);
  assert(Tracking_Error()==TRACK_TIMEOUT); /* no false lock when phase cannot converge */
  start(0xFFFFFF00U); for(i=0;i<120;++i) step(10,1); assert(Tracking_Locked());
  Tracking_Stop(); i=commands; step(10,1); assert(commands==i && Tracking_State()==TRACK_OFF);
  puts("PASS: descending sweep, signed PI load tracking/relock, bounds, stale/duplicate/wrong-frequency rejection, polarity guard, PWM failure, STOP and tick wrap");
  return 0;
}
