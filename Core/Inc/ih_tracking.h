#ifndef IH_TRACKING_H
#define IH_TRACKING_H
#include "ih_measurement.h"
typedef enum { TRACK_OFF, TRACK_RAMP, TRACK_SEARCH, TRACK_ACQUIRE, TRACK_LOCKED, TRACK_FAILED } TrackState;
typedef enum { TRACK_OK, TRACK_SIGNAL, TRACK_RANGE, TRACK_TIMEOUT, TRACK_PHASE, TRACK_PWM } TrackError;
void Tracking_Init(void);
int Tracking_SetBounds(uint32_t lo100,uint32_t hi100);
uint32_t Tracking_Lower100(void);
uint32_t Tracking_Upper100(void);
void Tracking_Start(uint32_t now);
void Tracking_Stop(void);
void Tracking_Task(uint32_t now,const Measurement *m);
TrackState Tracking_State(void);
TrackError Tracking_Error(void);
int Tracking_Locked(void);
/* Only the startup ramp is permitted; tracking never commands power phase. */
int Tracking_AllowRamp(void);
extern volatile uint32_t dbg_track_state,dbg_track_error,dbg_track_freq100;
extern volatile float dbg_track_phase_deg,dbg_track_error_deg;
#endif
