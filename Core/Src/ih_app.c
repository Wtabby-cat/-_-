#include "ih_app.h"
#include "ih_config.h"
#include "ih_hmi.h"
#include "ih_settings.h"
#include "ih_pwm.h"
#include "ih_program.h"
#include "ih_workpiece.h"
#include "ih_history.h"

extern HRTIM_HandleTypeDef hhrtim1;
extern COMP_HandleTypeDef hcomp2, hcomp3, hcomp5, hcomp6;
extern DAC_HandleTypeDef hdac1, hdac4;
static ProtectionParams protection = {IH_DEFAULT_OV_V, IH_DEFAULT_UV_V, IH_DEFAULT_OC_A,
                                     IH_DEFAULT_OT1_C, IH_DEFAULT_OT2_C, IH_DEFAULT_OT3_C};
static IhState state;
static uint32_t fault_mask, start_tick, elapsed_ms, measure_tick, led_tick;
static uint32_t clear_since;
static uint32_t relay_tick, relay_power_tick;
static uint8_t clear_tracking;
static uint8_t uv_tracking;
static uint32_t uv_since;
static uint32_t pwm_prime_tick;
static uint8_t pwm_priming;
volatile uint32_t dbg_relay, dbg_pwm_en, dbg_ovp, dbg_ocp;
volatile uint32_t dbg_ovp_fault, dbg_ocp_fault, dbg_zc_i, dbg_zc_v, dbg_state, dbg_fault_mask;
volatile uint32_t dbg_app_init_stage, dbg_start_block;
volatile uint32_t dbg_trip_mask, dbg_trip_bus_raw, dbg_trip_elapsed_ms, dbg_trip_bus_tick;
volatile float dbg_trip_bus_v, dbg_trip_uv_v;
volatile uint32_t dbg_start_count, dbg_trip_count, dbg_trip_pwm_en;
volatile uint32_t dbg_pwm_start_count;
volatile float dbg_start_bus_v;
volatile uint32_t dbg_trip_bus_rank1_raw, dbg_trip_bus_rank2_raw;
volatile uint32_t dbg_trip_state, dbg_trip_phase10, dbg_trip_sample_age_ms;
volatile uint32_t dbg_trip_bus_min_raw, dbg_trip_bus_max_raw;

static void DisableGate(void)
{
  /* Direct register writes are usable even with interrupts/HAL unavailable. */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIOB->BSRR = PWM_EN_N_Pin;
  MODIFY_REG(GPIOB->MODER, GPIO_MODER_MODE1, GPIO_MODER_MODE1_0);
  if ((RCC->APB2ENR & RCC_APB2ENR_HRTIM1EN) != 0U)
    HRTIM1->sCommonRegs.ODISR = IH_POWER_OUTPUTS;
}
void App_EmergencyOff(void)
{
  /* Initialization / fatal CPU errors only. Heating alarms use StopOutputs()
   * and must not switch the independent NTC bypass relay. */
  DisableGate();
  GPIOB->BSRR = (uint32_t)RELAY_CTRL_Pin << 16U;
  MODIFY_REG(GPIOB->MODER, GPIO_MODER_MODE0, GPIO_MODER_MODE0_0);
  dbg_relay = 0U; dbg_pwm_en = 0U;
}
static void StopOutputs(void)
{
  History_Stop();
  /* Both normal STOP and heating faults preserve the NTC bypass. */
  DisableGate();
  Pwm_Cancel(); pwm_priming = 0U;
  HAL_HRTIM_WaveformOutputStop(&hhrtim1, IH_POWER_OUTPUTS);
  HAL_HRTIM_WaveformCountStop(&hhrtim1, IH_POWER_TIMERS);
  dbg_pwm_en = 0U;
}
void App_Stop(void)
{
  if (state == IH_RUNNING || state == IH_PRECHARGE) elapsed_ms = HAL_GetTick() - start_tick;
  StopOutputs();
  uv_tracking = 0U;
  state = fault_mask ? IH_FAULT : IH_IDLE;
}
static void Trip(uint32_t faults)
{
  Measurement m;
  if ((faults & ~fault_mask) == 0U) return;
  Measurement_Get(&m);
  /* Keep the initiating fault frozen. Later hardware/link faults while stopped
   * still latch, but must not overwrite the evidence from the original trip. */
  if (fault_mask == 0U) {
    ++dbg_trip_count; dbg_trip_pwm_en = dbg_pwm_en;
    dbg_trip_state = (uint32_t)state; dbg_trip_phase10 = dbg_pwm_phase10;
    dbg_trip_sample_age_ms = HAL_GetTick() - m.bus_sample_tick;
    dbg_trip_mask = faults;
    dbg_trip_bus_v = m.bus_v; dbg_trip_uv_v = (float)protection.uv;
    dbg_trip_bus_raw = (uint32_t)(m.bus_adc_v * 4095.0f / IH_ADC_VREF_V + 0.5f);
    dbg_trip_bus_rank1_raw = m.bus_rank1_raw; dbg_trip_bus_rank2_raw = m.bus_rank2_raw;
    dbg_trip_bus_min_raw = m.bus_min_raw; dbg_trip_bus_max_raw = m.bus_max_raw;
    dbg_trip_bus_tick = m.bus_sample_tick;
    dbg_trip_elapsed_ms = (state == IH_RUNNING || state == IH_PRECHARGE) ? HAL_GetTick() - start_tick : 0U;
  }
  fault_mask |= faults;
  App_Stop(); clear_tracking = 0U;
}
void App_CommunicationFault(void) { Trip(IH_FAULT_COM); }

static uint32_t PresentFaults(uint8_t check_running)
{
  Measurement m;
  ProtectionInputs in;
  uint32_t flags = hhrtim1.Instance->sCommonRegs.ISR;
  Measurement_Get(&m);
  dbg_ovp = HAL_COMP_GetOutputLevel(&hcomp2);
  dbg_ocp = HAL_COMP_GetOutputLevel(&hcomp6);
  dbg_ovp_fault = (flags & HRTIM_ISR_FLT1) != 0U;
  dbg_ocp_fault = (flags & HRTIM_ISR_FLT3) != 0U;
  dbg_zc_i = HAL_COMP_GetOutputLevel(&hcomp3);
  dbg_zc_v = HAL_COMP_GetOutputLevel(&hcomp5);
  in.bus_v = m.bus_v; in.current_peak_a = m.current_peak_a;
  in.bus_valid = m.bus_valid; in.adc_valid = m.adc_valid;
  in.hw_ov = (uint8_t)dbg_ovp; in.hw_oc = (uint8_t)dbg_ocp;
  in.running = check_running;
  flags = Protection_Evaluate(&protection, &in);
  /* UV uses the DC mean; preserve software OV against any sampled peak. */
  if (m.bus_valid && protection.ov > 0 && m.bus_peak_v >= (float)protection.ov) flags |= IH_FAULT_OV;
  return flags;
}
static int InputsReady(void)
{
  Measurement m;
  Measurement_Get(&m);
  return Protection_ParamsValid(&protection) && m.adc_valid && m.bus_valid &&
         m.bus_v > (float)protection.uv && m.bus_v < (float)protection.ov &&
         PresentFaults(1U) == 0U;
}
static uint32_t QualifyRunningUV(uint32_t faults)
{
  Measurement m;
  if (state != IH_RUNNING || !(faults & IH_FAULT_UV)) {
    uv_tracking = 0U;
    return faults;
  }
  Measurement_Get(&m);
  if (!uv_tracking) { uv_tracking = 1U; uv_since = m.bus_sample_tick; }
  /* Compare acquisition timestamps, not the number of foreground calls:
   * one cached low result cannot satisfy the persistence check. */
  if ((uint32_t)(m.bus_sample_tick - uv_since) < IH_UV_CONFIRM_MS)
    faults &= ~IH_FAULT_UV;
  return faults;
}
uint32_t App_StartBlockReasons(void)
{
  Measurement m;
  uint32_t blocked = 0U;
  Measurement_Get(&m);
  if (!Protection_ParamsValid(&protection)) blocked |= IH_START_PARAMS;
  if (!m.adc_valid) blocked |= IH_START_ADC;
  if (!m.bus_valid) blocked |= IH_START_BUS;
  if (m.bus_valid && protection.uv > 0 && m.bus_v <= (float)protection.uv) blocked |= IH_START_UV;
  if (m.bus_valid && protection.ov > 0 &&
      (m.bus_v >= (float)protection.ov || m.bus_peak_v >= (float)protection.ov)) blocked |= IH_START_OV;
  if (m.adc_valid && protection.oc > 0 && m.current_peak_a >= (float)protection.oc) blocked |= IH_START_OC;
  if (HAL_COMP_GetOutputLevel(&hcomp2) != 0U || HAL_COMP_GetOutputLevel(&hcomp6) != 0U ||
      (hhrtim1.Instance->sCommonRegs.ISR & (HRTIM_ISR_FLT1 | HRTIM_ISR_FLT3)) != 0U)
    blocked |= IH_START_HW;
  if (fault_mask != 0U) blocked |= IH_START_LATCHED;
  if (!Hmi_LinkHealthy()) blocked |= IH_START_LINK;
  if (state != IH_IDLE) blocked |= IH_START_STATE;
  return blocked;
}
int App_Start(void)
{
  Measurement m;
  dbg_start_block = App_StartBlockReasons();
  if (dbg_start_block != 0U) return 0;
  Measurement_Get(&m);
  dbg_start_bus_v = m.bus_v; ++dbg_start_count;
  StopOutputs();
  start_tick = HAL_GetTick(); elapsed_ms = 0U;
  state = IH_PRECHARGE;
  History_Start(HAL_GetTick());
  return 1;
}
int App_ResetFault(void)
{
  if (state == IH_RUNNING || state == IH_PRECHARGE) return 0;
  if (!InputsReady() || !Hmi_LinkHealthy() || !clear_tracking ||
      (uint32_t)(HAL_GetTick() - clear_since) < IH_RESET_STABLE_MS) return 0;
  StopOutputs();
  /* Fault inputs stay armed. Only clear flags after the sources are inactive. */
  hhrtim1.Instance->sCommonRegs.ICR = HRTIM_ICR_FLT1C | HRTIM_ICR_FLT3C;
  __DSB();
  if (PresentFaults(1U) != 0U ||
      (hhrtim1.Instance->sCommonRegs.ISR & (HRTIM_ISR_FLT1 | HRTIM_ISR_FLT3)) != 0U) return 0;
  fault_mask = 0U; state = IH_IDLE;
  return 1; /* restart always needs a separate START request */
}
int App_SaveProtection(const ProtectionParams *params)
{
  if (state == IH_RUNNING || state == IH_PRECHARGE || !Protection_ParamsValid(params)) return 0;
  StopOutputs();
  if (!Settings_Save(params)) return 0;
  protection = *params;
  clear_tracking = 0U;
  return 1;
}
void App_Init(void)
{
  dbg_app_init_stage = 1U;
  Program_Init();
  History_Reset(HAL_GetTick());
  App_EmergencyOff();
  StopOutputs();
  if (!Pwm_Init()) Error_Handler();
  relay_power_tick = HAL_GetTick();
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, IH_DAC_CODE(IH_HW_OVP_V));
  HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, IH_DAC_CODE(IH_HW_OCP_V));
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, IH_DAC_CODE(IH_ZC_THRESHOLD_V));
  HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, IH_DAC_CODE(IH_ZC_THRESHOLD_V));
  if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK || HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK ||
      HAL_DAC_Start(&hdac4, DAC_CHANNEL_1) != HAL_OK || HAL_DAC_Start(&hdac4, DAC_CHANNEL_2) != HAL_OK)
    Error_Handler();
  HAL_Delay(1U);
  dbg_app_init_stage = 2U;
  if (HAL_COMP_Start(&hcomp2) != HAL_OK || HAL_COMP_Start(&hcomp3) != HAL_OK ||
      HAL_COMP_Start(&hcomp5) != HAL_OK || HAL_COMP_Start(&hcomp6) != HAL_OK)
    Error_Handler();
  HAL_Delay(1U);
  hhrtim1.Instance->sCommonRegs.ICR = HRTIM_ICR_FLT1C | HRTIM_ICR_FLT3C;
  HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_1, HRTIM_FAULTMODECTL_ENABLED);
  HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_3, HRTIM_FAULTMODECTL_ENABLED);
  dbg_app_init_stage = 3U;
  Settings_Load(&protection);
  dbg_app_init_stage = 4U;
  Measurement_Init();
  dbg_app_init_stage = 5U;
  Hmi_Init();
  Workpiece_Init();
  dbg_app_init_stage = 6U;
}
void App_Task(void)
{
  uint32_t now = HAL_GetTick(), faults;
  Workpiece_Task();
  Measurement_BusTask();
  if ((uint32_t)(now - measure_tick) >= IH_BUS_SAMPLE_MS) {
    measure_tick = now;
    Measurement_SetFrequency(App_Frequency100());
    Measurement_Task();
  }
  faults = PresentFaults((state == IH_RUNNING || state == IH_PRECHARGE) ? 1U : 0U);
  /* A short comparator pulse must also remain visible via the HRTIM flags. */
  if (dbg_ovp_fault) faults |= IH_FAULT_OV;
  if (dbg_ocp_fault) faults |= IH_FAULT_OC;
  Trip(QualifyRunningUV(faults));
  /* Restore the original power-up NTC bypass: it does not wait for START,
   * valid protection settings, HMI synchronization or a nonzero bus reading.
   * Heating faults latch the PWM OFF; they do not control this relay. */
  if (!dbg_relay &&
      (uint32_t)(now - relay_power_tick) >= IH_RELAY_DELAY_MS) {
    HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port, RELAY_CTRL_Pin, GPIO_PIN_SET);
    dbg_relay = 1U; relay_tick = now;
  }
  if (InputsReady() && Hmi_LinkHealthy()) {
    if (!clear_tracking) { clear_tracking = 1U; clear_since = now; }
  } else clear_tracking = 0U;
  if (state == IH_PRECHARGE) {
    /* Preserve the actual cause rather than relabelling OV/OC/UV as BUS. */
    if (!InputsReady()) {
      faults = PresentFaults(1U);
      Trip(faults ? faults : IH_FAULT_BUS);
    }
    else if (dbg_relay && (uint32_t)(now - relay_tick) >= IH_RELAY_SETTLE_MS) {
      if (!pwm_priming) {
        if (!Pwm_Prepare()) Error_Handler();
        __HAL_HRTIM_SETCOUNTER(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, 0U);
        __HAL_HRTIM_SETCOUNTER(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, 0U);
        if (HAL_HRTIM_WaveformOutputStart(&hhrtim1, IH_POWER_OUTPUTS) != HAL_OK ||
            HAL_HRTIM_WaveformCountStart(&hhrtim1, IH_POWER_TIMERS) != HAL_OK) Error_Handler();
        pwm_priming = 1U; pwm_prime_tick = HAL_GetTick();
      } else if ((uint32_t)(now - pwm_prime_tick) >= IH_PWM_PRIME_MS) {
        faults = PresentFaults(1U);
        if (dbg_ovp_fault) faults |= IH_FAULT_OV;
        if (dbg_ocp_fault) faults |= IH_FAULT_OC;
        if (faults != 0U) Trip(faults);
        else {
          HAL_GPIO_WritePin(PWM_EN_N_GPIO_Port, PWM_EN_N_Pin, GPIO_PIN_RESET);
          ++dbg_pwm_start_count;
          dbg_pwm_en = 1U; state = IH_RUNNING; pwm_priming = 0U;
          Pwm_RampStart(HAL_GetTick());
        }
      }
    }
  }
  if (state == IH_RUNNING) {
    Measurement m;
    Measurement_Get(&m);
    Pwm_Task(HAL_GetTick(), m.bus_valid && m.bus_v > (float)protection.uv);
  }
  Hmi_Task();
  if ((uint32_t)(now - led_tick) >= 500U) {
    led_tick = now; HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  }
  dbg_state = (uint32_t)state; dbg_fault_mask = fault_mask;
}
const ProtectionParams *App_Protection(void) { return &protection; }
uint32_t App_Faults(void) { return fault_mask; }
IhState App_State(void) { return state; }
uint32_t App_ElapsedSeconds(void)
{
  return ((state == IH_RUNNING || state == IH_PRECHARGE) ? HAL_GetTick() - start_tick : elapsed_ms) / 1000U;
}
uint32_t App_Frequency100(void)
{
  uint32_t period = __HAL_HRTIM_GETPERIOD(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A);
  /* The existing A/B setup uses HRTIM_PRESCALERRATIO_MUL4. kHz * 100. */
  return period ? (HAL_RCC_GetSysClockFreq() / 10U * 4U + period / 2U) / period : 0U;
}
