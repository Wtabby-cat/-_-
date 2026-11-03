#include "ih_power.h"
#include "ih_config.h"
#include "ih_pwm.h"
#include <math.h>

static PowerState state;
static PowerError error;
static float target,limit,reference,filtered,integral,last_raw;
static uint32_t last_sequence,last_sample,control_tick,reverse_since;
static uint8_t seen,was_locked,reverse_tracking,allow_startup;
volatile uint32_t dbg_power_state,dbg_power_error,dbg_power_saturated;
volatile float dbg_power_target_w,dbg_power_reference_w,dbg_power_filtered_w;
volatile float dbg_power_error_w,dbg_power_phase10,dbg_power_integral10;
volatile float dbg_power_trip_w,dbg_power_trip_phase10;
static float Clamp(float x,float lo,float hi) { return x<lo ? lo : x>hi ? hi : x; }
float Power_TargetW(void) { return target<limit ? target : limit; }
static void Publish(void)
{
  dbg_power_state=state; dbg_power_error=error; dbg_power_target_w=Power_TargetW();
  dbg_power_reference_w=reference; dbg_power_filtered_w=filtered;
  dbg_power_phase10=(float)dbg_pwm_phase10; dbg_power_integral10=integral;
}
static void Fail(PowerError why)
{
  dbg_power_trip_w=last_raw; dbg_power_trip_phase10=(float)dbg_pwm_phase10;
  state=POWER_FAILED; error=why; allow_startup=0U; Publish();
}
void Power_Init(void)
{ state=POWER_OFF; error=POWER_OK; target=limit=IH_POWER_TARGET_W; reference=filtered=integral=0; Publish(); }
int Power_SetTargetW(float watts)
{
  if (!isfinite(watts) || watts<0 || watts>IH_POWER_MAX_W) return 0;
  target=watts; Publish(); return 1;
}
int Power_SetLimitW(float watts)
{
  if (!isfinite(watts) || watts<=0) return 0;
  limit=watts<IH_POWER_MAX_W ? watts : IH_POWER_MAX_W; Publish(); return 1;
}
int Power_Start(uint32_t now,float watts,float ceiling)
{
  if (!Power_SetTargetW(watts) || !Power_SetLimitW(ceiling)) { Fail(POWER_CONFIG); return 0; }
  state=POWER_WAIT_LOCK; error=POWER_OK; seen=was_locked=reverse_tracking=allow_startup=0U;
  last_sequence=0; last_sample=control_tick=now; reference=filtered=integral=0;
  dbg_power_saturated=0; dbg_power_error_w=0; Publish(); return 1;
}
void Power_Stop(void)
{ state=POWER_OFF; seen=was_locked=reverse_tracking=allow_startup=0; reference=integral=0; Publish(); }
PowerState Power_State(void) { return state; }
PowerError Power_Error(void) { return error; }
int Power_AllowStartupRise(uint32_t now)
{ return state==POWER_WAIT_LOCK && allow_startup && seen && now-last_sample<=IH_POWER_FRESH_MS; }
static int Command(float angle10)
{
  if (!Pwm_SetPhase10((uint32_t)(angle10+0.5f))) { Fail(POWER_PWM); return 0; }
  return 1;
}
void Power_Task(uint32_t now,const Measurement *m,int locked)
{
  float raw,goal,dt,angle,e,proposal,lo,hi,command,next_integral;
  uint32_t elapsed;
  if (state==POWER_OFF || state==POWER_FAILED || state==POWER_STOP_REQUEST) return;
  last_raw=m->active_power_w;
  goal=Power_TargetW();
  if (goal<=0) { state=POWER_STOP_REQUEST; allow_startup=0; Publish(); return; }
  if (!m->adc_valid || !m->power_valid || m->adc_clipped || !isfinite(m->active_power_w) ||
      now-m->power_sample_tick>=IH_POWER_STALE_MS) { Fail(POWER_FEEDBACK); return; }
  if (seen && m->power_sequence==last_sequence) {
    if (now-last_sample>=IH_POWER_STALE_MS) Fail(POWER_FEEDBACK);
    return;
  }
  raw=m->active_power_w;
  if (raw>=IH_POWER_TRIP_W) { Fail(POWER_EXCESS); return; }
  elapsed=now-control_tick; control_tick=now;
  if (elapsed>20U) elapsed=20U; /* never catch up after a foreground stall */
  dt=(float)elapsed/1000.0f;
  if (!seen) filtered=raw; else filtered+=IH_POWER_FILTER_ALPHA*(raw-filtered);
  seen=1; last_sequence=m->power_sequence; last_sample=m->power_sample_tick;
  angle=(float)dbg_pwm_phase10;
  if (raw<IH_POWER_REVERSE_W) {
    allow_startup=0; was_locked=0; state=POWER_WAIT_LOCK;
    if (!reverse_tracking) { reverse_tracking=1; reverse_since=m->power_sample_tick; }
    if (m->power_sample_tick-reverse_since>=IH_POWER_REVERSE_MS) { Fail(POWER_REVERSE); return; }
    Publish(); return; /* never take abs(P) or increase with reversed feedback */
  }
  reverse_tracking=0;
  if (!locked || dbg_pwm_softstart) {
    state=POWER_WAIT_LOCK; was_locked=0; integral=angle;
    reference=Clamp(filtered,0,goal); dbg_power_error_w=goal-filtered;
    allow_startup=raw<goal;
    if (raw>=goal) {
      /* Reaching the power ceiling ends the amplitude ramp early. Search may
       * continue at this angle; only reductions are permitted before lock. */
      command=raw>goal+0.5f ? angle-IH_POWER_DOWN_PHASE10_S*dt : angle;
      if (!Command(Clamp(command,IH_POWER_MIN_PHASE10,IH_POWER_MAX_PHASE10))) return;
    }
    Publish(); return;
  }
  allow_startup=0;
  if (!was_locked) {
    reference=Clamp(filtered,0,goal); integral=angle-IH_POWER_KP_PHASE10_W*(reference-filtered);
    was_locked=1;
  }
  if (reference<goal) reference=Clamp(reference+IH_POWER_RISE_W_S*dt,0,goal);
  else if (reference>goal) reference=goal;
  state=reference<goal ? POWER_RAMP : POWER_REGULATE;
  e=reference-filtered; dbg_power_error_w=e;
  next_integral=integral+IH_POWER_KI_PHASE10_WS*e*dt;
  proposal=next_integral+IH_POWER_KP_PHASE10_W*e;
  lo=Clamp(angle-IH_POWER_DOWN_PHASE10_S*dt,IH_POWER_MIN_PHASE10,IH_POWER_MAX_PHASE10);
  hi=Clamp(angle+IH_POWER_UP_PHASE10_S*dt,IH_POWER_MIN_PHASE10,IH_POWER_MAX_PHASE10);
  if (raw>goal+0.5f) hi=lo; /* raw ceiling takes priority over filter lag */
  command=Clamp(proposal,lo,hi);
  dbg_power_saturated=(proposal<IH_POWER_MIN_PHASE10 || proposal>IH_POWER_MAX_PHASE10);
  if (!Command(command)) return;
  /* Track saturation/rate limiting, but retain sub-LSB integration when
   * unconstrained. Resetting to the integer PWM angle would leave a large
   * steady-state error for small PI increments. */
  integral=command!=proposal ? command-IH_POWER_KP_PHASE10_W*e : next_integral;
  Publish();
}
