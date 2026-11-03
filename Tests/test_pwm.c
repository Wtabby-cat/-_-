/* Real PWM code with a mock repetition-boundary transfer of compare preloads. */
#include "ih_pwm.h"
#include "ih_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static HRTIM_TypeDef fake_hrtim;
HRTIM_HandleTypeDef hhrtim1;
static uint32_t active1, active2, updates;
static void repetition(void)
{
  if (!(fake_hrtim.sCommonRegs.CR1&HRTIM_CR1_TBUDIS)) {
    active1=fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP1xR;
    active2=fake_hrtim.sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].CMP2xR;
    ++updates;
  }
}
static void update_barrier(void)
{
  uint32_t before=updates;
  assert(fake_hrtim.sCommonRegs.CR1&HRTIM_CR1_TBUDIS);
  repetition(); assert(updates==before); /* cannot transfer a partial pair */
}
#define __DSB() update_barrier()
#include "../Core/Src/ih_pwm.c"
HAL_StatusTypeDef HAL_HRTIM_SoftwareUpdate(HRTIM_HandleTypeDef *h,uint32_t timers)
{ assert(h==&hhrtim1 && timers==HRTIM_TIMERUPDATE_B); repetition(); return HAL_OK; }
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
  fake_hrtim.sCommonRegs.CR1=HRTIM_CR1_TAUDIS;
  assert(Pwm_Init() && active1==5638 && active2==22553);
  assert(fake_hrtim.sTimerxRegs[1].TIMxCR&HRTIM_TIMCR_PREEN);
  assert(fake_hrtim.sTimerxRegs[1].TIMxCR&HRTIM_TIMCR_TREPU);
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
  assert(fake_hrtim.sTimerxRegs[1].FLTxR==0x3 && fake_hrtim.sCommonRegs.CR1==HRTIM_CR1_TAUDIS);
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
  puts("PASS: PWM boundary updates, 2-to-60 degree ramp, UV hold, stop/restart and tick wrap");
  return 0;
}
