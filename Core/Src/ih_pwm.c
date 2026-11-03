#include "ih_pwm.h"
#include "ih_config.h"

extern HRTIM_HandleTypeDef hhrtim1;
static uint32_t period, nominal_period, nominal_phase10, target10, requested_phase;
static uint32_t ramp_from, ramp_tick, ramp_elapsed;
static uint8_t initialized, active;
volatile uint32_t dbg_pwm_phase10, dbg_pwm_softstart;
#define UPDATE_MASK (HRTIM_CR1_MUDIS | HRTIM_CR1_TAUDIS | HRTIM_CR1_TBUDIS)
#define UPDATE_ALL (HRTIM_TIMERUPDATE_MASTER | HRTIM_TIMERUPDATE_A | HRTIM_TIMERUPDATE_B)

static uint32_t Ticks(uint32_t angle10)
{
  uint32_t ticks=(period*angle10+1800U)/3600U;
  return ticks<96U ? 96U : ticks;
}
static void Stage(uint32_t new_period, uint32_t phase)
{
  uint32_t saved=hhrtim1.Instance->sCommonRegs.CR1;
  /* Master repetition transfers all preloads at one boundary. A/B reset
   * from that same master period event. Never force a live software update. */
  SET_BIT(hhrtim1.Instance->sCommonRegs.CR1, UPDATE_MASK);
  __DSB();
  hhrtim1.Instance->sMasterRegs.MPER=new_period;
  hhrtim1.Instance->sTimerxRegs[0].PERxR=new_period;
  hhrtim1.Instance->sTimerxRegs[0].CMP1xR=new_period/2U;
  hhrtim1.Instance->sTimerxRegs[1].PERxR=new_period;
  hhrtim1.Instance->sTimerxRegs[1].CMP1xR=phase;
  hhrtim1.Instance->sTimerxRegs[1].CMP2xR=phase+new_period/2U;
  __DSB();
  hhrtim1.Instance->sCommonRegs.CR1=saved;
  period=new_period; requested_phase=phase;
  dbg_pwm_phase10=(phase*3600U+period/2U)/period;
}
int Pwm_Init(void)
{
  uint32_t half, phase, i;
  period=__HAL_HRTIM_GETPERIOD(&hhrtim1,HRTIM_TIMERINDEX_TIMER_A);
  half=__HAL_HRTIM_GETCOMPARE(&hhrtim1,HRTIM_TIMERINDEX_TIMER_A,HRTIM_COMPAREUNIT_1);
  phase=__HAL_HRTIM_GETCOMPARE(&hhrtim1,HRTIM_TIMERINDEX_TIMER_B,HRTIM_COMPAREUNIT_1);
  if (period<1024U || period>65534U || (period&1U) || half!=period/2U ||
      __HAL_HRTIM_GETPERIOD(&hhrtim1,HRTIM_TIMERINDEX_TIMER_B)!=period ||
      __HAL_HRTIM_GETCOMPARE(&hhrtim1,HRTIM_TIMERINDEX_TIMER_B,HRTIM_COMPAREUNIT_2)!=phase+half ||
      phase<96U || phase>half ||
      (hhrtim1.Instance->sTimerxRegs[1].TIMxCR2&HRTIM_TIMCR2_GTCMP1)) return 0;
  nominal_period=period; nominal_phase10=(phase*3600U+period/2U)/period;
  target10=nominal_phase10;
  CLEAR_BIT(hhrtim1.Instance->sCommonRegs.CR1,UPDATE_MASK);
  SET_BIT(hhrtim1.Instance->sMasterRegs.MCR,HRTIM_MCR_PREEN|HRTIM_MCR_MREPU);
  hhrtim1.Instance->sMasterRegs.MREP=0U;
  for (i=0;i<2U;++i) {
    MODIFY_REG(hhrtim1.Instance->sTimerxRegs[i].TIMxCR,HRTIM_TIMCR_TREPU,
               HRTIM_TIMCR_PREEN|HRTIM_TIMCR_MSTU);
    hhrtim1.Instance->sTimerxRegs[i].REPxR=0U;
    SET_BIT(hhrtim1.Instance->sTimerxRegs[i].RSTxR,HRTIM_RSTR_MSTPER);
  }
  /* A starts at the explicit master boundary; B starts at its delayed CMP1.
   * Dead-time, complementary outputs and hardware fault paths stay armed. */
  MODIFY_REG(hhrtim1.Instance->sTimerxRegs[0].SETx1R,
             HRTIM_SET1R_PER|HRTIM_SET1R_MSTPER,HRTIM_SET1R_MSTPER);
  /* Keep this output reset route fixed while running. At 180 degrees CMP2
   * equals PER and may coincide with the counter reset; the master-period
   * event independently resets B. For smaller angles it is an idle reset.
   * No live SET/RST-source changes or wrapped/zero compare are required. */
  hhrtim1.Instance->sTimerxRegs[1].RSTx1R=HRTIM_RST1R_CMP2|HRTIM_RST1R_MSTPER;
  Stage(period,phase);
  if (HAL_HRTIM_SoftwareUpdate(&hhrtim1,UPDATE_ALL)!=HAL_OK) return 0;
  initialized=1U; return 1;
}
uint32_t Pwm_Frequency100(void)
{ return period ? (HAL_RCC_GetSysClockFreq()/10U*4U+period/2U)/period : 0U; }
int Pwm_SetFrequency100(uint32_t frequency100)
{
  uint32_t new_period, phase;
  if (!initialized || frequency100<IH_TRACK_MIN_FREQ100 || frequency100>IH_TRACK_MAX_FREQ100) return 0;
  new_period=((HAL_RCC_GetSysClockFreq()*4U+frequency100*10U)/(frequency100*20U))*2U;
  if (new_period<1024U || new_period>65534U) return 0;
  if (new_period==period) return 1;
  /* Once the power angle has reached its setpoint, derive ticks from that
   * setpoint on every frequency change. Re-scaling already rounded ticks
   * indefinitely can otherwise drift the angle during frequency tracking. */
  phase=dbg_pwm_softstart ? (requested_phase*new_period+period/2U)/period :
                          (new_period*target10+1800U)/3600U;
  if (phase<96U) phase=96U;
  if (phase>new_period/2U) return 0;
  ramp_from=(ramp_from*new_period+period/2U)/period;
  Stage(new_period,phase); return 1;
}
int Pwm_PrepareAt(uint32_t frequency100,uint32_t target_phase10)
{
  uint32_t phase;
  if (!initialized || target_phase10<IH_POWER_MIN_PHASE10 || target_phase10>IH_POWER_MAX_PHASE10 ||
      frequency100<IH_TRACK_MIN_FREQ100 || frequency100>IH_TRACK_MAX_FREQ100) return 0;
  Pwm_Cancel();
  if (!Pwm_SetFrequency100(frequency100)) return 0;
  target10=target_phase10;
  phase=Ticks(IH_PWM_START_MDEG/100U);
  if (phase<96U) phase=96U;
  if (phase>Ticks(target10)) phase=Ticks(target10);
  Stage(period,phase);
  return HAL_HRTIM_SoftwareUpdate(&hhrtim1,UPDATE_ALL)==HAL_OK;
}
int Pwm_SetPhase10(uint32_t phase10)
{
  uint32_t phase;
  if (!initialized || phase10<IH_POWER_MIN_PHASE10 || phase10>IH_POWER_MAX_PHASE10) return 0;
  phase=Ticks(phase10);
  if (phase<96U) phase=96U;
  if (phase>period/2U) return 0;
  Pwm_Cancel(); target10=phase10;
  Stage(period,phase);
  return 1;
}
int Pwm_Prepare(void)
{
  uint32_t phase;
  if (!initialized) return 0;
  Pwm_Cancel(); target10=nominal_phase10;
  phase=(nominal_period*(IH_PWM_START_MDEG/100U)+1800U)/3600U;
  if (phase<96U) phase=96U;
  Stage(nominal_period,phase);
  return HAL_HRTIM_SoftwareUpdate(&hhrtim1,UPDATE_ALL)==HAL_OK;
}
void Pwm_RampStart(uint32_t now)
{
  ramp_tick=now; ramp_elapsed=0U; ramp_from=requested_phase; active=1U;
  dbg_pwm_softstart=requested_phase!=Ticks(target10);
}
void Pwm_RampTo(uint32_t phase10,uint32_t now)
{
  if (phase10<IH_POWER_MIN_PHASE10 || phase10>IH_POWER_MAX_PHASE10) return;
  target10=phase10; Pwm_RampStart(now);
}
void Pwm_Task(uint32_t now,uint8_t allow_increase)
{
  uint32_t step,next,target;
  if (!active || !dbg_pwm_softstart) return;
  step=now-ramp_tick; ramp_tick=now;
  target=Ticks(target10);
  if (!step || (!allow_increase && target>requested_phase)) return;
  if (step>IH_PWM_RAMP_MAX_STEP_MS) step=IH_PWM_RAMP_MAX_STEP_MS;
  ramp_elapsed+=step;
  if (ramp_elapsed>IH_PWM_SOFTSTART_MS) ramp_elapsed=IH_PWM_SOFTSTART_MS;
  next=target>=ramp_from ? ramp_from+(target-ramp_from)*ramp_elapsed/IH_PWM_SOFTSTART_MS :
                         ramp_from-(ramp_from-target)*ramp_elapsed/IH_PWM_SOFTSTART_MS;
  if (next!=requested_phase) Stage(period,next);
  if (ramp_elapsed==IH_PWM_SOFTSTART_MS) dbg_pwm_softstart=0U;
}
void Pwm_Cancel(void) { active=0U; dbg_pwm_softstart=0U; }
