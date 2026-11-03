#ifndef IH_CONFIG_H
#define IH_CONFIG_H

/* Board calibration. ADC voltage = physical quantity * SCALE + BIAS. */
#define IH_ADC_VREF_V             3.300f
#define IH_CURRENT_SCALE          (10.0f * 5.6f / 1000.0f)
#define IH_CURRENT_BIAS_V         1.63f
/* User confirmed the CT at JP7 and LC_C_A -> PA0 are connected. This is an
 * explicit board configuration, NOT automatic unplug detection. Set to 0
 * when intentionally testing without the CT. Gates DISPLAY validity only;
 * ADC acquisition and fixed-reference peak overcurrent checks stay active. */
#ifndef IH_CURRENT_SENSOR_CONNECTED
#define IH_CURRENT_SENSOR_CONNECTED 1
#endif
#define IH_RESONANT_SCALE         (0.4f * 3.3f / 31.0f)
#define IH_RESONANT_BIAS_V        1.63f
/* Field-verified for this CT: red -> JP7-1, black -> JP7-2.
 * Swapping the previous connection changed ~+163 deg / negative watts
 * to ~-17 deg / positive watts. Retain +1 and preserve signed power.
 * The remaining analogue-path phase delay has not been calibrated. */
#define IH_POWER_POLARITY         1.0f
#define IH_ADC_CLIP_MARGIN        8U
/* 170 MHz / 4 / (12.5 sampling + 12.5 conversion). Phase capture rejects
 * blocks interrupted by the injected PA7/temperature conversions. */
#define IH_ADC_REGULAR_SAMPLE_HZ  1700000.0f
#define IH_PHASE_SAMPLES          256U
#define IH_PHASE_MIN_RMS_ADC_V    0.010f
#define IH_PHASE_MIN_CURRENT_QUALITY 0.70f
/* Phase-shifted bridge voltage is pulsed: its fundamental need not contain
 * most of its total energy. Current is expected to be close to sinusoidal. */
#define IH_PHASE_MIN_VOLTAGE_QUALITY 0.35f
#define IH_ZC_THRESHOLD_V         1.63f
#define IH_HW_OVP_V               3.20f
#define IH_HW_OCP_V               3.03f
#define IH_DAC_CODE(v)            ((uint32_t)((v) / IH_ADC_VREF_V * 4095.0f))

/* PA7 feeds COMP2 and ADC2_IN4. Nominal board: Vadc = Vbus / 31 * 3.3.
 * Software calibration below does not change divider/device voltage ratings
 * or the existing 3.20 V comparator DAC threshold. */
#define IH_BUS_SCALE              (3.3f / 31.0f)
#define IH_BUS_BIAS_V             0.0f
/* User's two points (old display -> measured bus): 10.6 -> 11, 15.3 -> 16.
 * Apply the same correction to mean and peak protection values; clamp at 0. */
#define IH_BUS_CAL_GAIN           (5.0f / 4.7f)
#define IH_BUS_CAL_OFFSET_V       (-0.276595745f)
/* Retain the matching HMI keypad bounds. This 110217 comparison build uses
 * the original 110214 MCU parameter validator and a fixed 3.03 V COMP6 DAC;
 * saving OC changes the software peak threshold only. */
#define IH_LIMIT_OV_MAX_V         31
#define IH_LIMIT_UV_MAX_V         30
#define IH_LIMIT_OC_PEAK_MAX_A    26
#define IH_LIMIT_OT_MAX_C         999
#define IH_BUS_SAMPLE_MS          10U
#define IH_BUS_BURST_PERIOD_MS    20U
#define IH_BUS_BURST_SCANS        32U
#define IH_ADC_STALE_MS           100U
#define IH_BUS_STALE_MS           100U
/* Continuous low samples, not a blind delay after START. Start/reset still
 * require bus > UV immediately; invalid data and other trips are immediate. */
#define IH_UV_CONFIRM_MS          40U
/* PA2/PA3 COMPONENT temperature front ends are not populated yet. Do not interpret
 * floating ADC inputs as temperatures. Set both connected and gain only
 * after the circuit/calibration is known. Celsius = Vadc * gain + offset. */
#ifndef IH_TEMP1_CONNECTED
#define IH_TEMP1_CONNECTED        0
#define IH_TEMP1_GAIN_C_PER_V     0.0f
#define IH_TEMP1_OFFSET_C         0.0f
#endif
#ifndef IH_TEMP2_CONNECTED
#define IH_TEMP2_CONNECTED        0
#define IH_TEMP2_GAIN_C_PER_V     0.0f
#define IH_TEMP2_OFFSET_C         0.0f
#endif
#define IH_TEMP_FILTER_ALPHA      0.25f
/* USART2: PA15 RX, PB3 TX. Provisional baud/age until sensor is selected;
 * default protocol hooks do NOT decode data or transmit requests. */
#ifndef IH_WORKPIECE_UART_ENABLED
#define IH_WORKPIECE_UART_ENABLED  0
#endif
#define IH_WORKPIECE_UART_BAUD     115200U
#define IH_WORKPIECE_STALE_MS      2000U
#define IH_RELAY_DELAY_MS         500U
#define IH_RELAY_SETTLE_MS        20U
#define IH_PWM_PRIME_MS           2U
#define IH_PWM_SOFTSTART_MS       500U
#define IH_PWM_START_MDEG         2000U
#define IH_PWM_RAMP_MAX_STEP_MS   5U
/* Frequency is kHz * 100. Phase compensation is intentionally zero for now. */
#define IH_TRACK_MIN_FREQ100     1100U
#define IH_TRACK_MAX_FREQ100     4000U
#define IH_TRACK_DEFAULT_LO100   1500U
#define IH_TRACK_DEFAULT_HI100   2500U
/* Temporary open-loop power setting until a power controller owns this.
 * The frequency/phase PLL must never raise this angle after lock. */
#define IH_PWM_FIXED_PHASE10     600U
#define IH_SEARCH_STEP100        10U
#define IH_TRACK_SETTLE_MS       50U
#define IH_TRACK_SIGNAL_MS       250U
#define IH_SEARCH_TIMEOUT_MS     15000U
#define IH_PLL_TIMEOUT_MS        5000U
#define IH_PLL_TARGET_DEG        10.0f
#define IH_PLL_BAND_DEG          3.0f
#define IH_PLL_MIN_PHASE_DEG     3.0f
#define IH_PLL_LOCK_MS           300U
#define IH_PLL_KP_HZ_PER_DEG     8.0f
#define IH_PLL_KI_HZ_PER_DEG_S    40.0f
#define IH_PLL_MAX_STEP100       2.0f
#define IH_RESET_STABLE_MS        200U
#define IH_ADC_BUF_SIZE           256U
#define IH_HMI_BAUD               9600U
#define IH_HMI_REFRESH_MS         1000U
#define IH_HMI_BOOT_MS            1500U
#define IH_HMI_QUERY_TIMEOUT_MS   600U
#define IH_HMI_RESYNC_MS          3000U
/* Visible build identifier for field diagnostics. */
#define IH_FW_DIAG_VERSION        110217U

/* No guessed physical protection thresholds: configure/save on page 5. */
#define IH_DEFAULT_OV_V           0
#define IH_DEFAULT_UV_V           0
#define IH_DEFAULT_OC_A           0
#define IH_DEFAULT_OT1_C          0
#define IH_DEFAULT_OT2_C          0
#define IH_DEFAULT_OT3_C          0

#define IH_POWER_OUTPUTS (HRTIM_OUTPUT_TA1 | HRTIM_OUTPUT_TA2 | HRTIM_OUTPUT_TB1 | HRTIM_OUTPUT_TB2)
#define IH_POWER_TIMERS  (HRTIM_TIMERID_MASTER | HRTIM_TIMERID_TIMER_A | HRTIM_TIMERID_TIMER_B)
#endif
