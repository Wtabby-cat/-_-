#ifndef IH_POWER_H
#define IH_POWER_H
#include "ih_measurement.h"
typedef enum { POWER_OFF, POWER_WAIT_LOCK, POWER_RAMP, POWER_REGULATE, POWER_FAILED, POWER_STOP_REQUEST } PowerState;
typedef enum { POWER_OK, POWER_FEEDBACK, POWER_REVERSE, POWER_PWM, POWER_EXCESS, POWER_CONFIG } PowerError;
void Power_Init(void);
int Power_Start(uint32_t now,float target_w,float limit_w);
void Power_Stop(void);
/* Reserved for the future temperature outer loop. Zero requests normal STOP. */
int Power_SetTargetW(float watts);
int Power_SetLimitW(float watts);
float Power_TargetW(void);
void Power_Task(uint32_t now,const Measurement *m,int phase_locked);
int Power_AllowStartupRise(uint32_t now);
PowerState Power_State(void);
PowerError Power_Error(void);
extern volatile uint32_t dbg_power_state,dbg_power_error,dbg_power_saturated;
extern volatile float dbg_power_target_w,dbg_power_reference_w,dbg_power_filtered_w;
extern volatile float dbg_power_error_w,dbg_power_phase10,dbg_power_integral10;
extern volatile float dbg_power_trip_w,dbg_power_trip_phase10;
#endif
