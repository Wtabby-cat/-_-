/* Regression: execute MX_ADC2_Init and the vendor's injected configuration
 * against host registers. ScanConvMode must not discard injected rank 2. */
#include "main.h"
#include "ih_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ADC_TypeDef fake_adc2;
static ADC_TypeDef fake_adc1;
static GPIO_TypeDef fake_gpioa;
static GPIO_TypeDef fake_gpiob;
static RCC_TypeDef fake_rcc;
#undef GPIOA
#undef RCC
#define GPIOA (&fake_gpioa)
#undef GPIOB
#define GPIOB (&fake_gpiob)
#define RCC (&fake_rcc)
#undef ADC2
#define ADC2 (&fake_adc2)
ADC_HandleTypeDef hadc2;
ADC_HandleTypeDef hadc1;
static uint32_t tick, started, read_count, temperature_count;
static unsigned uart2_irqs;
#include "../tmp/adc_config_under_test.c"

uint32_t SystemCoreClock=170000000U;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *adc)
{
  assert(adc==(started%2U==0U ? &hadc2 : &hadc1));
  ++started; adc->Instance->ISR=0U;
  return HAL_OK;
}
uint32_t HAL_ADCEx_InjectedGetValue(const ADC_HandleTypeDef *adc,uint32_t rank)
{
  assert(adc==&hadc1 || adc==&hadc2); ++read_count;
  return rank==ADC_INJECTED_RANK_1 ? adc->Instance->JDR1 : adc->Instance->JDR2;
}
HAL_StatusTypeDef HAL_ADCEx_InjectedPollForConversion(ADC_HandleTypeDef *adc,uint32_t timeout)
{
  assert(timeout==0U && (adc->Instance->ISR&ADC_FLAG_JEOS));
  adc->Instance->ISR=0U; return HAL_OK;
}
void Temperature_Submit(uint16_t pa2,uint16_t pa3,uint32_t now)
{ assert(pa2==101U && pa3==202U && now==tick); ++temperature_count; }
void Error_Handler(void) { abort(); }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(const RCC_PeriphCLKInitTypeDef *config)
{ assert(config->PeriphClockSelection==RCC_PERIPHCLK_USART2); return HAL_OK; }
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t priority,uint32_t sub)
{ assert(irq==USART2_IRQn && priority==2U && sub==0U); }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { assert(irq==USART2_IRQn); ++uart2_irqs; }
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *adc)
{
  assert(adc==&hadc2);
  assert(adc->Init.NbrOfConversion==1U);
  adc->State=HAL_ADC_STATE_READY;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc,const ADC_ChannelConfTypeDef *config)
{
  assert(adc==&hadc2 && config->Channel==ADC_CHANNEL_2 && config->Rank==ADC_REGULAR_RANK_1);
  return HAL_OK;
}
int main(void)
{
  uint16_t raw=0U;
  uint32_t i;
  COMP_HandleTypeDef comp={0};
  GPIO_InitTypeDef pin={0};
  UART_HandleTypeDef uart={0};
  /* Execute the actual GPIO HAL: analog mode does not automatically clear
   * the G4 pull-down. The actual COMP2 MSP must explicitly clear PA7 PUPDR. */
  pin.Pin=GPIO_PIN_7; pin.Mode=GPIO_MODE_ANALOG; pin.Pull=GPIO_PULLDOWN;
  fake_gpioa.PUPDR=GPIO_PULLUP<<(6U*2U);
  HAL_GPIO_Init(GPIOA,&pin);
  assert(((fake_gpioa.PUPDR>>(7U*2U))&3U)==GPIO_PULLDOWN);
  comp.Instance=COMP2; HAL_COMP_MspInit(&comp);
  assert(((fake_gpioa.MODER>>(7U*2U))&3U)==MODE_ANALOG);
  assert(((fake_gpioa.PUPDR>>(7U*2U))&3U)==GPIO_NOPULL);
  assert(((fake_gpioa.PUPDR>>(6U*2U))&3U)==GPIO_PULLUP);
  assert(fake_rcc.AHB2ENR&RCC_AHB2ENR_GPIOAEN);
  uart.Instance=USART2; HAL_UART_MspInit(&uart);
  assert(((fake_gpioa.MODER>>(7U*2U))&3U)==MODE_ANALOG);
  assert(((fake_gpioa.PUPDR>>(7U*2U))&3U)==GPIO_NOPULL);
  assert(((fake_gpioa.AFR[1]>>(7U*4U))&15U)==GPIO_AF7_USART2);
  assert(((fake_gpiob.AFR[0]>>(3U*4U))&15U)==GPIO_AF7_USART2);
  assert(uart2_irqs==0U && ((fake_gpioa.PUPDR>>(15U*2U))&3U)==GPIO_NOPULL);
  puts("PASS: reserved USART2 MSP leaves PA7 analog/no-pull and does not enable RX pull-up or IRQ");
  puts("PASS: actual COMP2 MSP and GPIO HAL leave PA7 analog with pull-down disabled");
  hadc1.Instance=&fake_adc1;
  Test_MX_ADC2_Init();
  Measurement_BoardBusInit();
  assert(hadc2.Init.ScanConvMode==ADC_SCAN_ENABLE);
  assert((fake_adc2.JSQR&ADC_JSQR_JL)==1U); /* two injected ranks */
  assert(((fake_adc2.JSQR&ADC_JSQR_JSQ1)>>ADC_JSQR_JSQ1_Pos)==4U);
  assert(((fake_adc2.JSQR&ADC_JSQR_JSQ2)>>ADC_JSQR_JSQ2_Pos)==4U);
  /* Prove why the old configuration failed with this actual HAL version. */
  memset(&fake_adc2,0,sizeof(fake_adc2));
  memset(&hadc2.InjectionConfig,0,sizeof(hadc2.InjectionConfig));
  hadc2.Init.ScanConvMode=ADC_SCAN_DISABLE;
  Measurement_BoardBusInit();
  assert((fake_adc2.JSQR&ADC_JSQR_JL)==0U);
  assert((fake_adc2.JSQR&ADC_JSQR_JSQ2)==0U);
  fake_adc1.JDR1=101; fake_adc1.JDR2=202;
  fake_adc2.JDR1=0; fake_adc2.JDR2=0;
  tick=25;
  assert(!Measurement_BoardBusRead(&raw) && started==2U && read_count==0U);
  tick=35; fake_adc1.ISR=ADC_FLAG_JEOS;
  assert(!Measurement_BoardBusRead(&raw) && read_count==0U && temperature_count==0U);
  for (i=0U; i<IH_BUS_BURST_SCANS; ++i) {
    fake_adc1.ISR=fake_adc2.ISR=ADC_FLAG_JEOS;
    if (i!=0U) { fake_adc2.JDR1=920; fake_adc2.JDR2=925; }
    ++tick;
    if (i+1U<IH_BUS_BURST_SCANS) {
      assert(!Measurement_BoardBusRead(&raw) && temperature_count==0U);
      assert(started==2U*(i+2U));
    } else assert(Measurement_BoardBusRead(&raw) && temperature_count==1U);
  }
  assert(raw==(925U*(IH_BUS_BURST_SCANS-1U)+IH_BUS_BURST_SCANS/2U)/IH_BUS_BURST_SCANS);
  assert(bus_result_tick==25U && bus_result_tick!=tick);
  assert(dbg_bus_rank2_raw==raw && dbg_bus_min_raw==0U && dbg_bus_max_raw==925U);
  assert(raw*31U/4095U>1U); /* one zero cannot turn a 7 V DC mean into UV=1 */
  assert(!Measurement_BoardBusRead(&raw) && started==2U*(IH_BUS_BURST_SCANS+1U));
  for (i=0U; i<IH_BUS_BURST_SCANS; ++i) {
    fake_adc1.ISR=fake_adc2.ISR=ADC_FLAG_JEOS;
    fake_adc2.JDR1=fake_adc2.JDR2=0U;
    ++tick;
    if (i+1U<IH_BUS_BURST_SCANS) assert(!Measurement_BoardBusRead(&raw));
    else assert(Measurement_BoardBusRead(&raw));
  }
  assert(raw==0U && dbg_bus_max_raw==0U && temperature_count==2U); /* sustained zero is not hidden */
  puts("PASS: actual MX_ADC2 initialization and vendor HAL preserve both bus injected ranks");
  puts("PASS: both JEOS flags required, PA7/temperature ranks isolated, acquisition timestamp retained");
  puts("PASS: 32-scan mean rejects isolated zero, preserves peak and true sustained zero");
  return 0;
}
