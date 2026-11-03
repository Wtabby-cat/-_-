#ifndef IH_PWM_H
#define IH_PWM_H
#include "main.h"
/* Call Init/Prepare with gate disabled and counters stopped. Target phase is
 * captured once from MX_HRTIM1_Init; STOP/restart never adopts a partial ramp. */
int Pwm_Init(void);
int Pwm_Prepare(void);
void Pwm_RampStart(uint32_t now);
void Pwm_Task(uint32_t now, uint8_t allow_increase);
void Pwm_Cancel(void);
extern volatile uint32_t dbg_pwm_phase10, dbg_pwm_softstart;
#endif
