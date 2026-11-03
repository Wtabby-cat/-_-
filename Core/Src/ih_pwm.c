#include "ih_pwm.h"
#include "ih_config.h"

extern HRTIM_HandleTypeDef hhrtim1;
static uint32_t period, half_period, target_phase, initial_phase, requested_phase;
static uint32_t ramp_tick, ramp_elapsed;
static uint8_t initialized, active;
volatile uint32_t dbg_pwm_phase10, dbg_pwm_softstart;

static void SetPhase(uint32_t phase)
{
  /* Block transfer while writing the pair. Release to the next repetition
   * event; never software-update live compare values in the middle of a pulse.
   * Timer B REP=0 means one update opportunity at each period boundary. */
  SET_BIT(hhrtim1.Instance->sCommonRegs.CR1, HRTIM_CR1_TBUDIS);
  __DSB();
  __HAL_HRTIM_SETCOMPARE(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_1, phase);
  __HAL_HRTIM_SETCOMPARE(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_2, phase + half_period);
  __DSB();
  CLEAR_BIT(hhrtim1.Instance->sCommonRegs.CR1, HRTIM_CR1_TBUDIS);
  requested_phase = phase;
  dbg_pwm_phase10 = (phase * 3600U + period / 2U) / period;
}
int Pwm_Init(void)
{
  uint32_t cmp3, cmp4, deadtime;
  period = __HAL_HRTIM_GETPERIOD(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A);
  half_period = __HAL_HRTIM_GETCOMPARE(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_COMPAREUNIT_1);
  target_phase = __HAL_HRTIM_GETCOMPARE(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_1);
  if (period < 1024U || period > 65535U || half_period != period / 2U ||
      __HAL_HRTIM_GETPERIOD(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B) != period ||
      __HAL_HRTIM_GETCOMPARE(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_2) != target_phase + half_period ||
      target_phase < 96U || target_phase + half_period >= period - 96U ||
      (hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxCR2 & HRTIM_TIMCR2_GTCMP1) != 0U)
    return 0;
  initial_phase = (period * IH_PWM_START_MDEG + 180000U) / 360000U;
  if (initial_phase < 96U) initial_phase = 96U;
  if (initial_phase > target_phase) initial_phase = target_phase;
  cmp3 = hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP3xR;
  cmp4 = hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP4xR;
  deadtime = hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].DTxR;
  SET_BIT(hhrtim1.Instance->sCommonRegs.CR1, HRTIM_CR1_TBUDIS);
  SET_BIT(hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxCR,
          HRTIM_TIMCR_PREEN | HRTIM_TIMCR_TREPU);
  /* Explicitly seed the preload timebase before the first transfer. */
  hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].PERxR = period;
  hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].REPxR = 0U;
  hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP3xR = cmp3;
  hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP4xR = cmp4;
  hhrtim1.Instance->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].DTxR = deadtime;
  SetPhase(target_phase);
  if (HAL_HRTIM_SoftwareUpdate(&hhrtim1, HRTIM_TIMERUPDATE_B) != HAL_OK) return 0;
  initialized = 1U;
  return 1;
}
int Pwm_Prepare(void)
{
  if (!initialized) return 0;
  Pwm_Cancel();
  SetPhase(initial_phase);
  /* Only called while stopped, before the gate-off priming interval. */
  return HAL_HRTIM_SoftwareUpdate(&hhrtim1, HRTIM_TIMERUPDATE_B) == HAL_OK;
}
void Pwm_RampStart(uint32_t now)
{
  ramp_tick = now; ramp_elapsed = 0U; active = 1U;
  dbg_pwm_softstart = initial_phase < target_phase;
}
void Pwm_Task(uint32_t now, uint8_t allow_increase)
{
  uint32_t step, next;
  if (!active || !dbg_pwm_softstart) return;
  step = now - ramp_tick; ramp_tick = now;
  if (!allow_increase || step == 0U) return;
  /* A delayed foreground task must not jump directly to full phase. */
  if (step > IH_PWM_RAMP_MAX_STEP_MS) step = IH_PWM_RAMP_MAX_STEP_MS;
  ramp_elapsed += step;
  if (ramp_elapsed > IH_PWM_SOFTSTART_MS) ramp_elapsed = IH_PWM_SOFTSTART_MS;
  next = initial_phase + (target_phase - initial_phase) * ramp_elapsed / IH_PWM_SOFTSTART_MS;
  if (next != requested_phase) SetPhase(next);
  if (ramp_elapsed == IH_PWM_SOFTSTART_MS) dbg_pwm_softstart = 0U;
}
void Pwm_Cancel(void)
{
  active = 0U; dbg_pwm_softstart = 0U;
}
