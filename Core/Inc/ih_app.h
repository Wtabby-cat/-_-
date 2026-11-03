#ifndef IH_APP_H
#define IH_APP_H
#include "ih_measurement.h"
#include "ih_protection.h"
typedef enum { IH_IDLE, IH_PRECHARGE, IH_RUNNING, IH_FAULT } IhState;
#define IH_START_PARAMS   (1UL << 0)
#define IH_START_ADC      (1UL << 1)
#define IH_START_BUS      (1UL << 2)
#define IH_START_UV       (1UL << 3)
#define IH_START_OV       (1UL << 4)
#define IH_START_OC       (1UL << 5)
#define IH_START_HW       (1UL << 6)
#define IH_START_LATCHED  (1UL << 7)
#define IH_START_LINK     (1UL << 8)
#define IH_START_STATE    (1UL << 9)
void App_Init(void);
void App_Task(void);
void App_Stop(void);
int App_Start(void);
int App_StartSearch(uint32_t lo100,uint32_t hi100);
uint32_t App_StartBlockReasons(void);
int App_ResetFault(void);
void App_CommunicationFault(void);
int App_SaveProtection(const ProtectionParams *params);
const ProtectionParams *App_Protection(void);
uint32_t App_Faults(void);
IhState App_State(void);
uint32_t App_ElapsedSeconds(void);
uint32_t App_Frequency100(void);
/* All-off for fatal CPU exceptions / initialization, including the relay.
 * Heating alarms and STOP must use the PWM stop path and preserve the relay. */
void App_EmergencyOff(void);
extern volatile uint32_t dbg_relay, dbg_pwm_en, dbg_ovp, dbg_ocp;
extern volatile uint32_t dbg_ovp_fault, dbg_ocp_fault, dbg_zc_i, dbg_zc_v;
extern volatile uint32_t dbg_state, dbg_fault_mask;
extern volatile uint32_t dbg_app_init_stage, dbg_start_block;
extern volatile uint32_t dbg_trip_mask, dbg_trip_bus_raw, dbg_trip_elapsed_ms, dbg_trip_bus_tick;
extern volatile float dbg_trip_bus_v, dbg_trip_uv_v;
extern volatile uint32_t dbg_start_count, dbg_trip_count, dbg_trip_pwm_en;
extern volatile uint32_t dbg_pwm_start_count;
extern volatile float dbg_start_bus_v;
extern volatile uint32_t dbg_trip_bus_rank1_raw, dbg_trip_bus_rank2_raw;
extern volatile uint32_t dbg_trip_state, dbg_trip_phase10, dbg_trip_sample_age_ms;
extern volatile uint32_t dbg_trip_bus_min_raw, dbg_trip_bus_max_raw;
#endif
