/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ========================================================================
 * 信号链参数（修改硬件后只需改这里，下面计算值自动派生）
 * ======================================================================== */

/* ---- 谐振电流采样 ---- */
/* 1/1000 CT → 10Ω 采样电阻 → 5.6x 运放 → +1.65V 偏置 */
#define I_CURR_CT_RATIO           1000.0f   /* 电流互感器变比 (1:N) */
#define I_CURR_SENSE_R            10.0f     /* 采样电阻 (Ω) */
#define I_CURR_OPAMP_GAIN         5.6f      /* 运放增益 (V/V) */
#define I_CURR_BIAS_V             1.59f     /* 直流偏置电压 (V)，实测 PA0 空闲时 ADC 读数 */
#define I_CURR_SCALE              (I_CURR_SENSE_R * I_CURR_OPAMP_GAIN / I_CURR_CT_RATIO)
/* I_CURR_SCALE = 10 * 5.6 / 1000 = 0.056 V/A */

/* ---- 谐振电压采样 ---- */
/* 1/31 分压 → 0.4x 隔离运放 → 3.3x 运放 → +1.65V 偏置 */
#define V_RES_DIV_RATIO           31.0f     /* 分压比 (1:N) */
#define V_RES_ISO_GAIN            0.4f      /* 隔离运放增益 (V/V) */
#define V_RES_OPAMP_GAIN          3.3f      /* 运放增益 (V/V) */
#define V_RES_BIAS_V              1.59f     /* 直流偏置电压 (V)，实测 PA1 空闲时 ADC 读数（1.57~1.61V 取中值） */
#define V_RES_SCALE               (V_RES_ISO_GAIN * V_RES_OPAMP_GAIN / V_RES_DIV_RATIO)
/* V_RES_SCALE = 0.4 * 3.3 / 31 ≈ 0.04258 V/V */

/* ---- 保护阈值（比较器 DAC 基准电压） ---- */
#define ZC_THRESHOLD_V            1.59f     /* 过零检测阈值 (V)，跟随 ADC 偏置实测值 */
#define RES_OCP_THRESHOLD_V       3.10f     /* 谐振过流保护阈值 (V) */
#define BUS_OVP_THRESHOLD_V       3.20f     /* 母线过压保护阈值 (V) */

/* ---- 系统常量 ---- */
#define ADC_VREF_VOLTAGE           3.300f    /* REF3033 / VDDA 基准电压 */
#define ADC_BUF_SIZE               256U      /* DMA 缓冲区大小 */
#define ADC_SAMPLE_MASK            0xFFFU    /* 12-bit 掩码 */
#define RELAY_DELAY_MS             500U      /* 软启动继电器延时 (ms) */

/* ---- DAC code（由阈值电压自动计算） ---- */
#define ZEROCROSS_CURR_DAC_CODE           ((uint32_t)(ZC_THRESHOLD_V      / ADC_VREF_VOLTAGE * 4095.0f))
#define ZEROCROSS_VOLT_DAC_CODE           ((uint32_t)(ZC_THRESHOLD_V      / ADC_VREF_VOLTAGE * 4095.0f))
#define BUS_OVERVOLT_DAC_CODE             ((uint32_t)(BUS_OVP_THRESHOLD_V / ADC_VREF_VOLTAGE * 4095.0f))
#define RESONANT_OVERCURRENT_DAC_CODE     ((uint32_t)(RES_OCP_THRESHOLD_V / ADC_VREF_VOLTAGE * 4095.0f))

/* ---- HRTIM 输出/定时器掩码 ---- */
#define POWER_STAGE_HRTIM_OUTPUTS        (HRTIM_OUTPUT_TA1 | HRTIM_OUTPUT_TA2 | HRTIM_OUTPUT_TB1 | HRTIM_OUTPUT_TB2)
#define POWER_STAGE_HRTIM_TIMERS         (HRTIM_TIMERID_TIMER_A | HRTIM_TIMERID_TIMER_B)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;

COMP_HandleTypeDef hcomp2;
COMP_HandleTypeDef hcomp3;
COMP_HandleTypeDef hcomp5;
COMP_HandleTypeDef hcomp6;

DAC_HandleTypeDef hdac1;
DAC_HandleTypeDef hdac4;

HRTIM_HandleTypeDef hhrtim1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint32_t pc13_blink_tick = 0U;

/* ========================================================================
 * Keil Watch 调试变量（12 个，按功能分组）
 * ======================================================================== */

/* 谐振电流 */
volatile float    dbg_i_adc_v = 0.0f;       /* ADC 采样电压 (V) */
volatile float    dbg_i_a     = 0.0f;       /* 谐振电流 (A) */

/* 谐振电压 */
volatile float    dbg_v_adc_v = 0.0f;       /* ADC 采样电压 (V) */
volatile float    dbg_v_res_v = 0.0f;       /* 谐振电压 (V) */

/* 继电器 / PWM 使能 */
volatile uint32_t dbg_relay  = 0U;          /* 继电器 0=关 1=开 */
volatile uint32_t dbg_pwm_en = 0U;          /* PWM 使能 0=禁止 1=使能 */

/* 保护状态 */
volatile uint32_t dbg_ovp       = 0U;       /* COMP2 过压实时 */
volatile uint32_t dbg_ocp       = 0U;       /* COMP6 过流实时 */
volatile uint32_t dbg_ovp_fault = 0U;       /* FAULT1 锁存 */
volatile uint32_t dbg_ocp_fault = 0U;       /* FAULT3 锁存 */

/* 过零状态 */
volatile uint32_t dbg_zc_i = 0U;            /* COMP3 电流过零 */
volatile uint32_t dbg_zc_v = 0U;            /* COMP5 电压过零 */

static uint32_t relay_tick = 0U;
static uint8_t  relay_on   = 0U;

/* 双路 ADC DMA 缓冲区 */
static uint32_t adc_dma_buf[ADC_BUF_SIZE] = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_HRTIM1_Init(void);
static void MX_DAC1_Init(void);
static void MX_COMP2_Init(void);
static void MX_COMP3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_COMP5_Init(void);
static void MX_COMP6_Init(void);
static void MX_DAC4_Init(void);
/* USER CODE BEGIN PFP */
static void StartPowerStagePwm(void);
static void DebugCaptureAll(void);
static void StartDualAdcDma(void);
static void ProcessAdcDmaBlock(uint32_t start_index, uint32_t length);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* STM32G474 comparator positive inputs are fixed to dedicated pins.
 * PA0 can feed COMP3 only, and PA1 cannot be used by COMP5/COMP6 directly.
 */

static void StartPowerStagePwm(void)
{
  if (HAL_HRTIM_WaveformOutputStart(&hhrtim1, POWER_STAGE_HRTIM_OUTPUTS) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_HRTIM_WaveformCountStart(&hhrtim1, POWER_STAGE_HRTIM_TIMERS) != HAL_OK)
  {
    Error_Handler();
  }
}

static void DebugCaptureAll(void)
{
  uint32_t isr;

  /* 保护状态 */
  dbg_ovp = HAL_COMP_GetOutputLevel(&hcomp2);
  dbg_ocp = HAL_COMP_GetOutputLevel(&hcomp6);

  isr = hhrtim1.Instance->sCommonRegs.ISR;
  dbg_ovp_fault = ((isr & HRTIM_ISR_FLT1) != 0U) ? 1U : 0U;
  dbg_ocp_fault = ((isr & HRTIM_ISR_FLT3) != 0U) ? 1U : 0U;

  /* 过零状态 */
  dbg_zc_i = HAL_COMP_GetOutputLevel(&hcomp3);
  dbg_zc_v = HAL_COMP_GetOutputLevel(&hcomp5);

  /* 继电器 / PWM 使能 */
  dbg_relay = relay_on;
  /* PWM_EN_N 低电平有效 → 取反后 1=使能 */
  dbg_pwm_en = (HAL_GPIO_ReadPin(PWM_EN_N_GPIO_Port, PWM_EN_N_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void StartDualAdcDma(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  /* In dual regular simultaneous mode, ADC2 is the slave and ADC1 is the master.
   * DMA must be started with HAL_ADCEx_MultiModeStart_DMA(), not HAL_ADC_Start_DMA().
   */
  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_BUF_SIZE) != HAL_OK)
  {
    Error_Handler();
  }
}

static void ProcessAdcDmaBlock(uint32_t start_index, uint32_t length)
{
  uint32_t raw;
  uint16_t curr;
  uint16_t volt;
  float    vadc;

  if ((length == 0U) || ((start_index + length) > ADC_BUF_SIZE))
  {
    return;
  }

  /* 取半缓冲块末尾最后一个采样点 */
  raw = adc_dma_buf[start_index + length - 1U];
  curr = (uint16_t)(raw & ADC_SAMPLE_MASK);
  volt = (uint16_t)((raw >> 16) & ADC_SAMPLE_MASK);

  /* 谐振电流 */
  vadc = ((float)curr) * ADC_VREF_VOLTAGE / 4095.0f;
  dbg_i_adc_v = vadc;
  dbg_i_a     = (vadc - I_CURR_BIAS_V) / I_CURR_SCALE;

  /* 谐振电压 */
  vadc = ((float)volt) * ADC_VREF_VOLTAGE / 4095.0f;
  dbg_v_adc_v = vadc;
  dbg_v_res_v = (vadc - V_RES_BIAS_V) / V_RES_SCALE;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_HRTIM1_Init();
  MX_DAC1_Init();
  MX_COMP2_Init();
  MX_COMP3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_COMP5_Init();
  MX_COMP6_Init();
  MX_DAC4_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(PWM_EN_N_GPIO_Port, PWM_EN_N_Pin, GPIO_PIN_SET);

  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, BUS_OVERVOLT_DAC_CODE);
  HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, RESONANT_OVERCURRENT_DAC_CODE);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, ZEROCROSS_CURR_DAC_CODE);
  HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, ZEROCROSS_VOLT_DAC_CODE);

  HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
  HAL_DAC_Start(&hdac4, DAC_CHANNEL_2);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_Start(&hdac4, DAC_CHANNEL_1);

  HAL_Delay(1);

  HAL_COMP_Start(&hcomp2);
  HAL_COMP_Start(&hcomp3);
  HAL_COMP_Start(&hcomp5);
  HAL_COMP_Start(&hcomp6);

  HAL_Delay(1);

  /* Clear stale fault flags once before arming protection. */
  hhrtim1.Instance->sCommonRegs.ICR = HRTIM_ICR_FLT1C | HRTIM_ICR_FLT3C;

  /* Arm both faults. After a fault trip, do not clear it in software.
   * Recovery requires MCU reset.
   */
  HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_1, HRTIM_FAULTMODECTL_ENABLED);
  HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_3, HRTIM_FAULTMODECTL_ENABLED);

  StartPowerStagePwm();
  /* Enable the external gate drivers after PWM and protection are ready. */
  HAL_GPIO_WritePin(PWM_EN_N_GPIO_Port, PWM_EN_N_Pin, GPIO_PIN_RESET);
  pc13_blink_tick = HAL_GetTick();
  relay_tick      = HAL_GetTick();  /* soft-start timer begins now */

  /* Start final ADC acquisition: ADC1(PA0) + ADC2(PA1) dual simultaneous DMA. */
  StartDualAdcDma();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    DebugCaptureAll();

    /* Soft-start relay: after RELAY_DELAY_MS, bypass thermistor */
    if (!relay_on && (HAL_GetTick() - relay_tick) >= RELAY_DELAY_MS)
    {
      HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port, RELAY_CTRL_Pin, GPIO_PIN_SET);
      relay_on = 1U;
    }

    if ((HAL_GetTick() - pc13_blink_tick) >= 500U)
    {
      pc13_blink_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};
  ADC_InjectionConfTypeDef sConfigInjected = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_DUALMODE_REGSIMULT;
  multimode.DMAAccessMode = ADC_DMAACCESSMODE_12_10_BITS;
  multimode.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_1CYCLE;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_12CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Injected Channel
  */
  sConfigInjected.InjectedChannel = ADC_CHANNEL_3;
  sConfigInjected.InjectedRank = ADC_INJECTED_RANK_1;
  sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  sConfigInjected.InjectedSingleDiff = ADC_SINGLE_ENDED;
  sConfigInjected.InjectedOffsetNumber = ADC_OFFSET_NONE;
  sConfigInjected.InjectedOffset = 0;
  sConfigInjected.InjectedNbrOfConversion = 2;
  sConfigInjected.InjectedDiscontinuousConvMode = DISABLE;
  sConfigInjected.AutoInjectedConv = DISABLE;
  sConfigInjected.QueueInjectedContext = DISABLE;
  sConfigInjected.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START;
  sConfigInjected.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_NONE;
  sConfigInjected.InjecOversamplingMode = DISABLE;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Injected Channel
  */
  sConfigInjected.InjectedChannel = ADC_CHANNEL_4;
  sConfigInjected.InjectedRank = ADC_INJECTED_RANK_2;
  if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &sConfigInjected) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.GainCompensation = 0;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = ENABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc2.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_12CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief COMP2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_COMP2_Init(void)
{

  /* USER CODE BEGIN COMP2_Init 0 */

  /* USER CODE END COMP2_Init 0 */

  /* USER CODE BEGIN COMP2_Init 1 */

  /* USER CODE END COMP2_Init 1 */
  hcomp2.Instance = COMP2;
  hcomp2.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp2.Init.InputMinus = COMP_INPUT_MINUS_DAC1_CH2;
  hcomp2.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp2.Init.Hysteresis = COMP_HYSTERESIS_40MV;
  hcomp2.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp2.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP2_Init 2 */

  /* USER CODE END COMP2_Init 2 */

}

/**
  * @brief COMP3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_COMP3_Init(void)
{

  /* USER CODE BEGIN COMP3_Init 0 */

  /* USER CODE END COMP3_Init 0 */

  /* USER CODE BEGIN COMP3_Init 1 */

  /* USER CODE END COMP3_Init 1 */
  hcomp3.Instance = COMP3;
  hcomp3.Init.InputPlus = COMP_INPUT_PLUS_IO2;
  hcomp3.Init.InputMinus = COMP_INPUT_MINUS_DAC1_CH1;
  hcomp3.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp3.Init.Hysteresis = COMP_HYSTERESIS_NONE;
  hcomp3.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp3.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP3_Init 2 */

  /* USER CODE END COMP3_Init 2 */

}

/**
  * @brief COMP5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_COMP5_Init(void)
{

  /* USER CODE BEGIN COMP5_Init 0 */

  /* USER CODE END COMP5_Init 0 */

  /* USER CODE BEGIN COMP5_Init 1 */

  /* USER CODE END COMP5_Init 1 */
  hcomp5.Instance = COMP5;
  hcomp5.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp5.Init.InputMinus = COMP_INPUT_MINUS_DAC4_CH1;
  hcomp5.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp5.Init.Hysteresis = COMP_HYSTERESIS_NONE;
  hcomp5.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp5.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP5_Init 2 */

  /* USER CODE END COMP5_Init 2 */

}

/**
  * @brief COMP6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_COMP6_Init(void)
{

  /* USER CODE BEGIN COMP6_Init 0 */

  /* USER CODE END COMP6_Init 0 */

  /* USER CODE BEGIN COMP6_Init 1 */

  /* USER CODE END COMP6_Init 1 */
  hcomp6.Instance = COMP6;
  hcomp6.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp6.Init.InputMinus = COMP_INPUT_MINUS_DAC4_CH2;
  hcomp6.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp6.Init.Hysteresis = COMP_HYSTERESIS_40MV;
  hcomp6.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp6.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP6_Init 2 */

  /* USER CODE END COMP6_Init 2 */

}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
  sConfig.DAC_DMADoubleDataMode = DISABLE;
  sConfig.DAC_SignedFormat = DISABLE;
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_Trigger2 = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_DISABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief DAC4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC4_Init(void)
{

  /* USER CODE BEGIN DAC4_Init 0 */

  /* USER CODE END DAC4_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC4_Init 1 */

  /* USER CODE END DAC4_Init 1 */

  /** DAC Initialization
  */
  hdac4.Instance = DAC4;
  if (HAL_DAC_Init(&hdac4) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
  sConfig.DAC_DMADoubleDataMode = DISABLE;
  sConfig.DAC_SignedFormat = DISABLE;
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_Trigger2 = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_DISABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  if (HAL_DAC_ConfigChannel(&hdac4, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC4_Init 2 */

  /* USER CODE END DAC4_Init 2 */

}

/**
  * @brief HRTIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_HRTIM1_Init(void)
{

  /* USER CODE BEGIN HRTIM1_Init 0 */

  /* USER CODE END HRTIM1_Init 0 */

  HRTIM_TimeBaseCfgTypeDef pTimeBaseCfg = {0};
  HRTIM_TimerCfgTypeDef pTimerCfg = {0};
  HRTIM_TimerCtlTypeDef pTimerCtl = {0};
  HRTIM_CompareCfgTypeDef pCompareCfg = {0};
  HRTIM_DeadTimeCfgTypeDef pDeadTimeCfg = {0};
  HRTIM_OutputCfgTypeDef pOutputCfg = {0};

  /* USER CODE BEGIN HRTIM1_Init 1 */

  /* USER CODE END HRTIM1_Init 1 */
  hhrtim1.Instance = HRTIM1;
  hhrtim1.Init.HRTIMInterruptResquests = HRTIM_IT_NONE;
  hhrtim1.Init.SyncOptions = HRTIM_SYNCOPTION_NONE;
  if (HAL_HRTIM_Init(&hhrtim1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_HRTIM_DLLCalibrationStart(&hhrtim1, HRTIM_CALIBRATIONRATE_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_HRTIM_PollForDLLCalibration(&hhrtim1, 10) != HAL_OK)
  {
    Error_Handler();
  }
  pTimeBaseCfg.Period = 0xFFDF;
  pTimeBaseCfg.RepetitionCounter = 0x00;
  pTimeBaseCfg.PrescalerRatio = HRTIM_PRESCALERRATIO_MUL4;
  pTimeBaseCfg.Mode = HRTIM_MODE_CONTINUOUS;
  if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, &pTimeBaseCfg) != HAL_OK)
  {
    Error_Handler();
  }
  pTimerCfg.InterruptRequests = HRTIM_MASTER_IT_NONE;
  pTimerCfg.DMARequests = HRTIM_MASTER_DMA_NONE;
  pTimerCfg.DMASrcAddress = 0x0000;
  pTimerCfg.DMADstAddress = 0x0000;
  pTimerCfg.DMASize = 0x1;
  pTimerCfg.HalfModeEnable = HRTIM_HALFMODE_DISABLED;
  pTimerCfg.InterleavedMode = HRTIM_INTERLEAVED_MODE_DISABLED;
  pTimerCfg.StartOnSync = HRTIM_SYNCSTART_DISABLED;
  pTimerCfg.ResetOnSync = HRTIM_SYNCRESET_DISABLED;
  pTimerCfg.DACSynchro = HRTIM_DACSYNC_NONE;
  pTimerCfg.PreloadEnable = HRTIM_PRELOAD_DISABLED;
  pTimerCfg.UpdateGating = HRTIM_UPDATEGATING_INDEPENDENT;
  pTimerCfg.BurstMode = HRTIM_TIMERBURSTMODE_MAINTAINCLOCK;
  pTimerCfg.RepetitionUpdate = HRTIM_UPDATEONREPETITION_DISABLED;
  pTimerCfg.ReSyncUpdate = HRTIM_TIMERESYNC_UPDATE_UNCONDITIONAL;
  if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_MASTER, &pTimerCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pTimeBaseCfg.Period = 33830;
  pTimeBaseCfg.PrescalerRatio = HRTIM_PRESCALERRATIO_MUL4;
  if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pTimeBaseCfg) != HAL_OK)
  {
    Error_Handler();
  }
  pTimerCtl.UpDownMode = HRTIM_TIMERUPDOWNMODE_UP;
  pTimerCtl.GreaterCMP1 = HRTIM_TIMERGTCMP1_EQUAL;
  pTimerCtl.DualChannelDacEnable = HRTIM_TIMER_DCDE_DISABLED;
  if (HAL_HRTIM_WaveformTimerControl(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pTimerCtl) != HAL_OK)
  {
    Error_Handler();
  }
  pTimerCfg.InterruptRequests = HRTIM_TIM_IT_NONE;
  pTimerCfg.DMARequests = HRTIM_TIM_DMA_NONE;
  pTimerCfg.PreloadEnable = HRTIM_PRELOAD_DISABLED;
  pTimerCfg.PushPull = HRTIM_TIMPUSHPULLMODE_DISABLED;
  pTimerCfg.FaultEnable = HRTIM_TIMFAULTENABLE_FAULT1 | HRTIM_TIMFAULTENABLE_FAULT3;
  pTimerCfg.FaultLock = HRTIM_TIMFAULTLOCK_READWRITE;
  pTimerCfg.DeadTimeInsertion = HRTIM_TIMDEADTIMEINSERTION_ENABLED;
  pTimerCfg.DelayedProtectionMode = HRTIM_TIMER_A_B_C_DELAYEDPROTECTION_DISABLED;
  pTimerCfg.UpdateTrigger = HRTIM_TIMUPDATETRIGGER_NONE;
  pTimerCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_NONE;
  pTimerCfg.ResetUpdate = HRTIM_TIMUPDATEONRESET_DISABLED;
  if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pTimerCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pCompareCfg.CompareValue = 16915;
  if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
  {
    Error_Handler();
  }

  /* fHRTIM = 170 MHz: DIV1 gives 85 / 170 MHz = 500 ns on both edges.
   * This deadtime stays fixed when the PWM period is changed for a sweep.
   */
  pDeadTimeCfg.Prescaler = HRTIM_TIMDEADTIME_PRESCALERRATIO_DIV1;
  pDeadTimeCfg.RisingValue = 85;
  pDeadTimeCfg.RisingSign = HRTIM_TIMDEADTIME_RISINGSIGN_POSITIVE;
  pDeadTimeCfg.RisingLock = HRTIM_TIMDEADTIME_RISINGLOCK_WRITE;
  pDeadTimeCfg.RisingSignLock = HRTIM_TIMDEADTIME_RISINGSIGNLOCK_WRITE;
  pDeadTimeCfg.FallingValue = 85;
  pDeadTimeCfg.FallingSign = HRTIM_TIMDEADTIME_FALLINGSIGN_POSITIVE;
  pDeadTimeCfg.FallingLock = HRTIM_TIMDEADTIME_FALLINGLOCK_WRITE;
  pDeadTimeCfg.FallingSignLock = HRTIM_TIMDEADTIME_FALLINGSIGNLOCK_WRITE;
  if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &pDeadTimeCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pOutputCfg.Polarity = HRTIM_OUTPUTPOLARITY_HIGH;
  pOutputCfg.SetSource = HRTIM_OUTPUTSET_TIMPER;
  pOutputCfg.ResetSource = HRTIM_OUTPUTRESET_TIMCMP1;
  pOutputCfg.IdleMode = HRTIM_OUTPUTIDLEMODE_NONE;
  pOutputCfg.IdleLevel = HRTIM_OUTPUTIDLELEVEL_INACTIVE;
  pOutputCfg.FaultLevel = HRTIM_OUTPUTFAULTLEVEL_INACTIVE;
  pOutputCfg.ChopperModeEnable = HRTIM_OUTPUTCHOPPERMODE_DISABLED;
  pOutputCfg.BurstModeEntryDelayed = HRTIM_OUTPUTBURSTMODEENTRY_REGULAR;
  if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA1, &pOutputCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pOutputCfg.SetSource = HRTIM_OUTPUTSET_NONE;
  pOutputCfg.ResetSource = HRTIM_OUTPUTRESET_NONE;
  if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, HRTIM_OUTPUT_TA2, &pOutputCfg) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pTimeBaseCfg) != HAL_OK)
  {
    Error_Handler();
  }
  pTimerCtl.TrigHalf = HRTIM_TIMERTRIGHALF_DISABLED;
  if (HAL_HRTIM_WaveformTimerControl(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pTimerCtl) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pTimerCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pCompareCfg.CompareValue = 5638;   /* Timer B lags Timer A by 60 degrees (33830/6). */
  if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_1, &pCompareCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pCompareCfg.CompareValue = 22553;  /* CMP1 + half a period: 50% duty. */
  if (HAL_HRTIM_WaveformCompareConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_COMPAREUNIT_2, &pCompareCfg) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_HRTIM_DeadTimeConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &pDeadTimeCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pOutputCfg.SetSource = HRTIM_OUTPUTSET_TIMCMP1;   /* 60-degree phase delay. */
  pOutputCfg.ResetSource = HRTIM_OUTPUTRESET_TIMCMP2;
  if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB1, &pOutputCfg) != HAL_OK)
  {
    Error_Handler();
  }

  pOutputCfg.SetSource = HRTIM_OUTPUTSET_NONE;
  pOutputCfg.ResetSource = HRTIM_OUTPUTRESET_NONE;
  if (HAL_HRTIM_WaveformOutputConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, HRTIM_OUTPUT_TB2, &pOutputCfg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN HRTIM1_Init 2 */
  {
    HRTIM_FaultCfgTypeDef pFaultCfg = {0};

    if (HAL_HRTIM_FaultPrescalerConfig(&hhrtim1, HRTIM_FAULTPRESCALER_DIV1) != HAL_OK)
    {
      Error_Handler();
    }

    /* FAULT1: over-voltage protection (COMP2, internal source, filtered) */
    pFaultCfg.Source = HRTIM_FAULTSOURCE_INTERNAL;
    pFaultCfg.Polarity = HRTIM_FAULTPOLARITY_HIGH;
    pFaultCfg.Filter = HRTIM_FAULTFILTER_15;
    pFaultCfg.Lock = HRTIM_FAULTLOCK_READWRITE;
    if (HAL_HRTIM_FaultConfig(&hhrtim1, HRTIM_FAULT_1, &pFaultCfg) != HAL_OK)
    {
      Error_Handler();
    }

    /* FAULT3: over-current protection (COMP6, internal source, filtered) */
    pFaultCfg.Source = HRTIM_FAULTSOURCE_INTERNAL;
    pFaultCfg.Polarity = HRTIM_FAULTPOLARITY_HIGH;
    pFaultCfg.Filter = HRTIM_FAULTFILTER_8;
    pFaultCfg.Lock = HRTIM_FAULTLOCK_READWRITE;
    if (HAL_HRTIM_FaultConfig(&hhrtim1, HRTIM_FAULT_3, &pFaultCfg) != HAL_OK)
    {
      Error_Handler();
    }

    /* Keep faults disabled during initialization. They are enabled in main()
     * after DAC and COMP are started.
     */
    HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_1, HRTIM_FAULTMODECTL_DISABLED);
    HAL_HRTIM_FaultModeCtl(&hhrtim1, HRTIM_FAULT_3, HRTIM_FAULTMODECTL_DISABLED);
  }

  /* USER CODE END HRTIM1_Init 2 */
  HAL_HRTIM_MspPostInit(&hhrtim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port, RELAY_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  /* PWM enable is active low, keep it disabled after reset. */
  HAL_GPIO_WritePin(PWM_EN_N_GPIO_Port, PWM_EN_N_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : RELAY_CTRL_Pin PWM_EN_N_Pin */
  GPIO_InitStruct.Pin = RELAY_CTRL_Pin|PWM_EN_N_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief  ADC half-buffer callback.
  *         Dual simultaneous mode: each 32-bit word = ADC2[31:16] | ADC1[15:0].
  */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1)
  {
    ProcessAdcDmaBlock(0U, ADC_BUF_SIZE / 2U);
  }
}

/**
  * @brief  ADC full-buffer callback.
  *         Dual simultaneous mode: each 32-bit word = ADC2[31:16] | ADC1[15:0].
  */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1)
  {
    ProcessAdcDmaBlock(ADC_BUF_SIZE / 2U, ADC_BUF_SIZE / 2U);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
