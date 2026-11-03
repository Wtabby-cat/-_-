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
/* Model set/reset events, deliberately NOT generating CMP2 when it equals
 * PER: the counter resets there. The separate master event must terminate B.
 * Integrate ideal upper-switch levels (dead time is checked separately). */
static void check_waveform(void)
{
  uint32_t edge[]={0,active1,active_half,active2,active_period};
  uint32_t a=0,b=1,high_a=0,high_b=0,bridge=0,i;
  uint32_t set_a=fake_hrtim.sTimerxRegs[0].SETx1R;
  uint32_t rst_a=fake_hrtim.sTimerxRegs[0].RSTx1R;
  uint32_t set_b=fake_hrtim.sTimerxRegs[1].SETx1R;
  uint32_t rst_b=fake_hrtim.sTimerxRegs[1].RSTx1R;
  assert(set_a==HRTIM_SET1R_MSTPER && rst_a==HRTIM_RST1R_CMP1);
  assert(set_b==HRTIM_SET1R_CMP1 && rst_b==(HRTIM_RST1R_CMP2|HRTIM_RST1R_MSTPER));
  assert(active1>=96 && active1<=active_half && active2==active1+active_half);
  assert(fake_hrtim.sTimerxRegs[1].DTxR==0x1234 && fake_hrtim.sTimerxRegs[1].FLTxR==3);
  for(i=0;i<5;++i) {
    uint32_t t=edge[i],master=(t==0 || t==active_period);
    uint32_t sa=master && (set_a&HRTIM_SET1R_MSTPER);
    uint32_t ra=t==active_half && (rst_a&HRTIM_RST1R_CMP1);
    uint32_t sb=t==active1 && (set_b&HRTIM_SET1R_CMP1);
    uint32_t rb=(master && (rst_b&HRTIM_RST1R_MSTPER)) ||
                (t==active2 && t<active_period && (rst_b&HRTIM_RST1R_CMP2));
    assert(!(sa && ra) && !(sb && rb));
    if(sa) a=1;
    if(ra) a=0;
    if(sb) b=1;
    if(rb) b=0;
    if(master) assert(a==1 && b==0);
    if(i<4) {
      uint32_t duration=edge[i+1]-t;
      high_a+=a*duration; high_b+=b*duration; bridge+=(a!=b)*duration;
    }
  }
  assert(high_a==active_half && high_b==active_half && bridge==2*active1);
}
int main(void)
{
  uint32_t tick=0, previous, count;
  hhrtim1.Instance=&fake_hrtim;
  fake_hrtim.sTimerxRegs[0].PERxR=33830;
  fake_hrtim.sTimerxRegs[0].CMP1xR=16915;
  fake_hrtim.sTimerxRegs[1].PERxR=33830;
  fake_hrtim.sTimerxRegs[1].CMP1xR=5638;
  fake_hrtim.sTimerxRegs[1].CMP2xR=22553;
  fake_hrtim.sTimerxRegs[0].SETx1R=HRTIM_SET1R_PER;
  fake_hrtim.sTimerxRegs[0].RSTx1R=HRTIM_RST1R_CMP1;
  fake_hrtim.sTimerxRegs[1].SETx1R=HRTIM_SET1R_CMP1;
  fake_hrtim.sTimerxRegs[1].RSTx1R=HRTIM_RST1R_CMP2;
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
  previous=active1;
  assert(Pwm_SetPhase10(200) && active1==previous && !dbg_pwm_softstart);
  repetition(); assert(dbg_pwm_phase10==200 && active2-active1==active_period/2);
  assert(!Pwm_SetPhase10(19) && !Pwm_SetPhase10(1801));
  assert(Pwm_SetPhase10(20)); repetition();
  assert(Pwm_SetFrequency100(4000)); repetition(); assert(active1>=96);
  assert(Pwm_SetPhase10(1200)); repetition();
  assert(dbg_pwm_phase10==1200 && active2-active1==active_period/2);
  assert(fake_hrtim.sTimerxRegs[1].DTxR==0x1234 && fake_hrtim.sTimerxRegs[1].FLTxR==3);
  puts("PASS: common master period updates, 11-40 kHz limits, even periods, 50-percent duty, phase preservation and unchanged dead-time/faults");
  /* Every 0.1 degree across 11..40 kHz. In particular 179.9 -> 180 -> 179.9
   * keeps routing fixed and has no dead band or latched high right bridge. */
  for(count=1100;count<=4000;count+=100) {
    assert(Pwm_SetPhase10(1800)); repetition(); check_waveform();
    assert(Pwm_SetFrequency100(count)); repetition(); check_waveform();
    assert(active1==active_half && active2==active_period && dbg_pwm_phase10==1800);
    for(tick=20;tick<=1800;++tick) {
      assert(Pwm_SetPhase10(tick)); repetition(); check_waveform();
    }
    for(tick=1800;tick>=1700;--tick) {
      assert(Pwm_SetPhase10(tick)); repetition(); check_waveform();
      assert(dbg_pwm_phase10==tick);
    }
  }
  assert(!Pwm_PrepareAt(2500,1801));
  assert(Pwm_PrepareAt(2500,1800)); Pwm_RampStart(0); previous=active1;
  for(tick=1;tick<=IH_PWM_SOFTSTART_MS;++tick) {
    Pwm_Task(tick,1); repetition(); check_waveform();
    assert(active1>=previous); previous=active1;
  }
  assert(dbg_pwm_phase10==1800 && !dbg_pwm_softstart);
  Pwm_RampTo(20,1000); previous=active1;
  for(tick=1001;tick<=1000+IH_PWM_SOFTSTART_MS;++tick) {
    Pwm_Task(tick,0); repetition(); check_waveform();
    assert(active1<=previous); previous=active1;
  }
  assert(dbg_pwm_phase10==20 && !dbg_pwm_softstart);
  assert(Pwm_SetFrequency100(4000)); repetition();
  Pwm_RampTo(1800,2000);
  for(tick=2001;tick<=2000+IH_PWM_SOFTSTART_MS;++tick) {
    Pwm_Task(tick,1); repetition(); check_waveform();
  }
  Pwm_RampTo(20,3000);
  for(tick=3001;tick<=3000+IH_PWM_SOFTSTART_MS;++tick) {
    Pwm_Task(tick,0); repetition(); check_waveform();
  }
  assert(active1==96 && !dbg_pwm_softstart);
  puts("PASS: 2..180 degree event waveforms, exact 180 without boundary CMP2, fixed event routes, PLL frequency changes and bidirectional ramps");
  return 0;
}
