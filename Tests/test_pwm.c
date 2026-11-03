/* Real PWM code with a mock repetition-boundary transfer of compare preloads. */
#include "ih_pwm.h"
#include "ih_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static HRTIM_TypeDef fake_hrtim;
HRTIM_HandleTypeDef hhrtim1;
static uint32_t active1, active2, updates;
static uint32_t active_period, active_half;
static void repetition(void)
{
  if (!(fake_hrtim.sCommonRegs.CR1&(HRTIM_CR1_MUDIS|HRTIM_CR1_TAUDIS|HRTIM_CR1_TBUDIS))) {
    assert(fake_hrtim.sMasterRegs.MPER==fake_hrtim.sTimerxRegs[0].PERxR);
    assert(fake_hrtim.sTimerxRegs[0].PERxR==fake_hrtim.sTimerxRegs[1].PERxR);
    active_period=fake_hrtim.sMasterRegs.MPER;
    active_half=fake_hrtim.sTimerxRegs[0].CMP1xR;
    active1=fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP1xR;
    active2=fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP2xR;
    ++updates;
  }
}
static void update_barrier(void)
{
  uint32_t before=updates;
  assert((fake_hrtim.sCommonRegs.CR1&(HRTIM_CR1_MUDIS|HRTIM_CR1_TAUDIS|HRTIM_CR1_TBUDIS))==
         (HRTIM_CR1_MUDIS|HRTIM_CR1_TAUDIS|HRTIM_CR1_TBUDIS));
  repetition(); assert(updates==before); /* cannot transfer a partial pair */
}
#define __DSB() update_barrier()
#include "../Core/Src/ih_pwm.c"
uint32_t HAL_RCC_GetSysClockFreq(void) { return 170000000U; }
HAL_StatusTypeDef HAL_HRTIM_SoftwareUpdate(HRTIM_HandleTypeDef *h,uint32_t timers)
{ assert(h==&hhrtim1 && timers==UPDATE_ALL); repetition(); return HAL_OK; }
int main(void)
{
  uint32_t tick=0, previous, count;
  hhrtim1.Instance=&fake_hrtim;
  fake_hrtim.sTimerxRegs[0].PERxR=33830;
  fake_hrtim.sTimerxRegs[0].CMP1xR=16915;
  fake_hrtim.sTimerxRegs[1].PERxR=33830;
  fake_hrtim.sTimerxRegs[1].CMP1xR=5638;
  fake_hrtim.sTimerxRegs[1].CMP2xR=22553;
  fake_hrtim.sTimerxRegs[1].DTxR=0x1234;
  fake_hrtim.sTimerxRegs[1].FLTxR=0x3;
  fake_hrtim.sCommonRegs.CR1=HRTIM_CR1_TCUDIS;
  assert(Pwm_Init() && active1==5638 && active2==22553);
  assert(fake_hrtim.sTimerxRegs[1].TIMxCR&HRTIM_TIMCR_PREEN);
  assert((fake_hrtim.sTimerxRegs[1].TIMxCR&HRTIM_TIMCR_MSTU) && !(fake_hrtim.sTimerxRegs[1].TIMxCR&HRTIM_TIMCR_TREPU));
  assert(fake_hrtim.sMasterRegs.MCR&HRTIM_MCR_MREPU);
  assert(fake_hrtim.sTimerxRegs[0].SETx1R&HRTIM_SET1R_MSTPER);
  assert(fake_hrtim.sTimerxRegs[1].RSTxR&HRTIM_RSTR_MSTPER);
  assert(Pwm_Prepare() && active1==188 && active2-active1==16915);
  Pwm_RampStart(tick); previous=active1;
  while(++tick<=IH_PWM_SOFTSTART_MS) {
    Pwm_Task(tick,1);
    assert(active1==previous); /* writes only preload; wait for repetition */
    repetition();
    assert(active1>=previous && active1-previous<=11U && active2-active1==16915);
    previous=active1;
  }
  assert(active1==5638 && active2==22553 && !dbg_pwm_softstart && dbg_pwm_phase10==600);
  assert(fake_hrtim.sTimerxRegs[1].PERxR==33830 && fake_hrtim.sTimerxRegs[1].DTxR==0x1234);
  assert(fake_hrtim.sTimerxRegs[1].FLTxR==0x3 && fake_hrtim.sCommonRegs.CR1==HRTIM_CR1_TCUDIS);
  Pwm_Cancel(); assert(Pwm_Prepare() && active1==188);
  Pwm_RampStart(1000);
  Pwm_Task(1100,0); repetition(); assert(active1==188); /* UV pending: no rise */
  Pwm_Task(1101,1); repetition(); assert(active1==198); /* no catch-up jump */
  previous=active1;
  Pwm_Task(2101,1); repetition(); assert(active1-previous<=55); /* foreground stall capped */
  Pwm_Cancel(); count=updates; previous=active1;
  Pwm_Task(2200,1); assert(updates==count && active1==previous && !dbg_pwm_softstart);
  assert(Pwm_Prepare() && active1==188); /* restart uses original target */
  Pwm_RampStart(0xFFFFFFF0U);
  Pwm_Task(0xFFFFFFF1U,1); repetition(); previous=active1;
  Pwm_Task(2,1); repetition(); assert(active1>=previous && active1-previous<=55);
  assert(!Pwm_SetFrequency100(1000) && !Pwm_SetFrequency100(4001));
  assert(Pwm_PrepareAt(2500,450)); Pwm_RampStart(0);
  for(tick=1;tick<=500;++tick) { Pwm_Task(tick,1); repetition(); }
  assert(dbg_pwm_phase10==450 && Pwm_Frequency100()==2500);
  { const uint32_t values[]={1500,1100,4000,2300,2500}; unsigned i;
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i) {
      uint32_t old_period=active_period;
      assert(Pwm_SetFrequency100(values[i])); assert(active_period==old_period);
      repetition(); assert((active_period&1)==0 && active_half==active_period/2);
      assert(active2-active1==active_half && dbg_pwm_phase10>=449 && dbg_pwm_phase10<=451);
      assert(Pwm_Frequency100()>=values[i]-1 && Pwm_Frequency100()<=values[i]+1);
      assert(fake_hrtim.sTimerxRegs[1].DTxR==0x1234 && fake_hrtim.sTimerxRegs[1].FLTxR==3);
    }
  }
  /* Repeated PLL frequency corrections cannot drift a fixed power angle. */
  for(tick=0;tick<1000;++tick) {
    assert(Pwm_SetFrequency100(2000U+tick%37U)); repetition();
    assert(dbg_pwm_phase10==450U);
  }
  Pwm_RampTo(600,0); for(tick=1;tick<=500;++tick) { Pwm_Task(tick,1); repetition(); }
  assert(dbg_pwm_phase10==600);
  for(tick=0;tick<1000;++tick) {
    assert(Pwm_SetFrequency100(2000U+tick%37U)); repetition();
    assert(dbg_pwm_phase10==600U);
  }
  puts("PASS: PWM boundary updates, 2-to-60 degree ramp, UV hold, stop/restart and tick wrap");
  puts("PASS: common master period updates, 11-40 kHz limits, even periods, 50-percent duty, phase preservation and unchanged dead-time/faults");
  return 0;
}
