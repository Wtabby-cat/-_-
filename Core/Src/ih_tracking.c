#include "ih_tracking.h"
#include "ih_pwm.h"
#include "ih_config.h"
#include <math.h>
static TrackState state;
static TrackError error;
static uint32_t lo,hi,started,changed,last_good,last_sequence,lock_since,acquire_since,last_control;
static uint8_t samples,first_high,lock_tracking;
static float sum_phase,frequency,last_error;
volatile uint32_t dbg_track_state,dbg_track_error,dbg_track_freq100;
volatile float dbg_track_phase_deg,dbg_track_error_deg;
static void Publish(void)
{ dbg_track_state=state; dbg_track_error=error; dbg_track_freq100=Pwm_Frequency100(); }
static void Fail(TrackError why) { state=TRACK_FAILED; error=why; Publish(); }
void Tracking_Init(void)
{ lo=IH_TRACK_DEFAULT_LO100; hi=IH_TRACK_DEFAULT_HI100; state=TRACK_OFF; error=TRACK_OK; Publish(); }
int Tracking_SetBounds(uint32_t lower,uint32_t upper)
{
  if (state!=TRACK_OFF || lower<IH_TRACK_MIN_FREQ100 || upper>IH_TRACK_MAX_FREQ100 || lower>=upper) return 0;
  lo=lower; hi=upper; return 1;
}
uint32_t Tracking_Lower100(void) { return lo; }
uint32_t Tracking_Upper100(void) { return hi; }
void Tracking_Start(uint32_t now)
{
  state=TRACK_RAMP; error=TRACK_OK; started=changed=last_good=acquire_since=last_control=now;
  last_sequence=0U; samples=lock_tracking=0U; first_high=1U; sum_phase=last_error=0;
  frequency=(float)hi; Publish();
}
void Tracking_Stop(void)
{ state=TRACK_OFF; lock_tracking=0U; Publish(); /* retain error until next explicit start */ }
TrackState Tracking_State(void) { return state; }
TrackError Tracking_Error(void) { return error; }
int Tracking_Locked(void) { return state==TRACK_LOCKED; }
int Tracking_AllowRamp(void) { return state==TRACK_RAMP; }
static int SetFrequency(uint32_t requested,uint32_t now)
{
  if (!Pwm_SetFrequency100(requested)) { Fail(TRACK_PWM); return 0; }
  changed=last_good=now; samples=0U; sum_phase=0; Publish(); return 1;
}
void Tracking_Task(uint32_t now,const Measurement *m)
{
  float phase,e,delta,next,dt;
  uint32_t command,current=Pwm_Frequency100();
  if (state==TRACK_OFF || state==TRACK_FAILED) return;
  if (state==TRACK_RAMP) {
    if (now-started>2500U) { Fail(TRACK_TIMEOUT); return; }
    if (dbg_pwm_softstart) return;
    state=TRACK_SEARCH; started=changed=last_good=now; last_sequence=m->phase_sequence;
    Publish(); return;
  }
  if ((state==TRACK_SEARCH && now-started>IH_SEARCH_TIMEOUT_MS) ||
      ((state==TRACK_ACQUIRE || state==TRACK_LOCKED) && !Tracking_Locked() && now-acquire_since>IH_PLL_TIMEOUT_MS)) {
    Fail(TRACK_TIMEOUT); return;
  }
  /* Do not reuse a cached angle to advance the sweep or declare lock. Every
   * accepted snapshot must be fresh, at this frequency, and after settling. */
  if (now-changed<IH_TRACK_SETTLE_MS) return;
  if (!m->phase_valid || now-m->phase_sample_tick>=IH_ADC_STALE_MS ||
      m->phase_frequency100+1U<current || m->phase_frequency100>current+1U) {
    samples=0U; sum_phase=0; lock_tracking=0U;
    if (state==TRACK_LOCKED) { state=TRACK_ACQUIRE; acquire_since=now; Publish(); }
    if (now-last_good>=IH_TRACK_SIGNAL_MS) Fail(TRACK_SIGNAL);
    return;
  }
  if (m->phase_sequence==last_sequence || (int32_t)(m->phase_sample_tick-changed)<(int32_t)IH_TRACK_SETTLE_MS) {
    if (now-last_good>=IH_TRACK_SIGNAL_MS) Fail(TRACK_SIGNAL);
    return;
  }
  last_sequence=m->phase_sequence; last_good=now;
  phase=m->phase_deg;
  if (!isfinite(phase) || phase<IH_PLL_MIN_PHASE_DEG || phase>120.0f) { Fail(TRACK_PHASE); return; }
  sum_phase+=phase;
  if (++samples<3U) return;
  phase=sum_phase/3.0f; samples=0U; sum_phase=0;
  dbg_track_phase_deg=phase; e=IH_PLL_TARGET_DEG-phase; dbg_track_error_deg=e;
  if (state==TRACK_SEARCH) {
    if (first_high && phase<IH_PLL_TARGET_DEG-IH_PLL_BAND_DEG) { Fail(TRACK_RANGE); return; }
    first_high=0U;
    if (phase>IH_PLL_TARGET_DEG+IH_PLL_BAND_DEG) {
      if (current<=lo) { Fail(TRACK_RANGE); return; }
      command=current-lo>IH_SEARCH_STEP100 ? current-IH_SEARCH_STEP100 : lo;
      frequency=(float)command; SetFrequency(command,now); return;
    }
    state=TRACK_ACQUIRE; acquire_since=last_control=now; last_error=e;
    frequency=(float)current;
  }
  if (fabsf(e)<=IH_PLL_BAND_DEG) {
    if (!lock_tracking) { lock_tracking=1U; lock_since=now; }
    if (now-lock_since>=IH_PLL_LOCK_MS && state!=TRACK_LOCKED) {
      /* Lock changes frequency-control status only. Power modulation owns
       * the inter-leg angle; never increase it as a side effect of locking. */
      state=TRACK_LOCKED;
    }
    last_error=e; last_control=now; Publish(); return;
  }
  lock_tracking=0U;
  if (state==TRACK_LOCKED) { state=TRACK_ACQUIRE; acquire_since=now; }
  dt=(float)(now-last_control)/1000.0f;
  if (dt>0.2f) dt=0.2f;
  /* Incremental PI in Hz, converted to 10 Hz units. A low phase requires
   * increasing frequency for a series resonant load. No integral windup. */
  delta=(IH_PLL_KP_HZ_PER_DEG*(e-last_error)+IH_PLL_KI_HZ_PER_DEG_S*e*dt)/10.0f;
  if (delta>IH_PLL_MAX_STEP100) delta=IH_PLL_MAX_STEP100;
  if (delta<-IH_PLL_MAX_STEP100) delta=-IH_PLL_MAX_STEP100;
  next=frequency+delta;
  if (next<(float)lo) next=(float)lo;
  if (next>(float)hi) next=(float)hi;
  if ((current<=lo && e<0) || (current>=hi && e>0)) { Fail(TRACK_RANGE); return; }
  frequency=next; last_error=e; last_control=now;
  command=(uint32_t)(frequency+0.5f);
  if (command!=current) SetFrequency(command,now);
  Publish();
}
