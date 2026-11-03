#ifndef IH_PWM_H
#define IH_PWM_H
#include "main.h"
/* Init/Prepare/PrepareAt require gate disabled and all counters stopped.
 * Running frequency/phase writes transfer together at the master boundary.
 * Prepare restores the original fixed setup; PrepareAt starts a search. */
int Pwm_Init(void);
int Pwm_Prepare(void);
int Pwm_PrepareAt(uint32_t frequency100, uint32_t target_phase10);
int Pwm_SetFrequency100(uint32_t frequency100);
uint32_t Pwm_Frequency100(void);
/* 2..180 degrees (0.1-degree units), cancels ramp. B resets on CMP2 or master
 * period, including exact 180. No live source switch or software update. */
int Pwm_SetPhase10(uint32_t phase10);
void Pwm_RampTo(uint32_t phase10, uint32_t now);
void Pwm_RampStart(uint32_t now);
void Pwm_Task(uint32_t now, uint8_t allow_increase);
void Pwm_Cancel(void);
extern volatile uint32_t dbg_pwm_phase10, dbg_pwm_softstart;
#endif
