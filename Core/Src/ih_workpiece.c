#include "ih_workpiece.h"
#include "ih_config.h"
#include <string.h>
extern UART_HandleTypeDef huart2;
#define WORK_RX_SIZE 256U
#define WORK_TX_SIZE 64U
static uint8_t tx_buffer[WORK_TX_SIZE];
static volatile uint8_t rx_started, uart_bad, tx_busy;
#if IH_WORKPIECE_UART_ENABLED
static uint8_t rx_byte, rx_ring[WORK_RX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static uint32_t retry_tick;
#endif
static WorkpieceTemperature sample;
static uint8_t seen;
volatile uint32_t dbg_workpiece_rx_bytes, dbg_workpiece_rx_overflow;
volatile uint32_t dbg_workpiece_uart_errors, dbg_workpiece_tx_errors;
volatile uint32_t dbg_workpiece_last_byte, dbg_workpiece_valid, dbg_workpiece_age_ms;
volatile float dbg_workpiece_c;

__weak void Workpiece_ProtocolByte(uint8_t byte, uint32_t now) { (void)byte; (void)now; }
__weak void Workpiece_ProtocolReset(void) { }

void Workpiece_Invalidate(void) { seen = 0U; }
int Workpiece_Submit(float celsius, uint32_t sample_tick)
{
  /* Reject non-finite and physically impossible values; the real protocol
   * parser must also check its sensor's range, framing, units and checksum. */
  if (!IH_WORKPIECE_UART_ENABLED || !(celsius >= -273.15f && celsius <= 9999.0f) || uart_bad || !rx_started) {
    Workpiece_Invalidate(); return 0;
  }
  sample.celsius = celsius; sample.sample_tick = sample_tick; seen = 1U;
  sample.region_valid = 0U;
  return 1;
}
int Workpiece_SubmitRegion(float mean_c, float high_c, float low_c, uint32_t sample_tick)
{
  if (!(low_c >= -273.15f && high_c <= 9999.0f && low_c <= mean_c && mean_c <= high_c) ||
      !Workpiece_Submit(mean_c, sample_tick)) {
    Workpiece_Invalidate(); return 0;
  }
  sample.mean_c = mean_c; sample.high_c = high_c; sample.low_c = low_c;
  sample.region_valid = 1U;
  return 1;
}
void Workpiece_Get(WorkpieceTemperature *out, uint32_t now)
{
  *out = sample;
  out->valid = IH_WORKPIECE_UART_ENABLED && seen && !uart_bad && rx_started &&
               (uint32_t)(now - sample.sample_tick) < IH_WORKPIECE_STALE_MS;
  out->region_valid = out->valid && sample.region_valid;
  dbg_workpiece_c = sample.celsius;
  dbg_workpiece_valid = out->valid;
  dbg_workpiece_age_ms = seen ? now - sample.sample_tick : 0xFFFFFFFFU;
}
void Workpiece_Init(void)
{
  seen = 0U; uart_bad = tx_busy = 0U;
  rx_started = 0U;
#if IH_WORKPIECE_UART_ENABLED
  rx_head = rx_tail = 0U;
  retry_tick = HAL_GetTick();
  Workpiece_ProtocolReset();
  HAL_UART_AbortReceive(&huart2);
  rx_started = HAL_UART_Receive_IT(&huart2, &rx_byte, 1U) == HAL_OK;
  if (!rx_started) { uart_bad = 1U; ++dbg_workpiece_uart_errors; }
#endif
}
void Workpiece_Task(void)
{
#if IH_WORKPIECE_UART_ENABLED
  uint32_t now;
  uint16_t budget = 64U;
  now = HAL_GetTick();
  if (uart_bad || !rx_started) {
    Workpiece_Invalidate();
    if ((uint32_t)(now - retry_tick) >= 100U) {
      retry_tick = now;
      HAL_UART_AbortReceive(&huart2); HAL_UART_AbortTransmit(&huart2);
      tx_busy = 0U; rx_tail = rx_head; uart_bad = 0U;
      Workpiece_ProtocolReset();
      rx_started = HAL_UART_Receive_IT(&huart2, &rx_byte, 1U) == HAL_OK;
      if (!rx_started) { uart_bad = 1U; ++dbg_workpiece_uart_errors; }
    }
    return;
  }
  while (budget-- && rx_tail != rx_head && !uart_bad) {
    uint8_t byte = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1U) % WORK_RX_SIZE;
    Workpiece_ProtocolByte(byte, now);
  }
#endif
}
int Workpiece_Transmit(const uint8_t *bytes, uint16_t length)
{
  if (!IH_WORKPIECE_UART_ENABLED || bytes == NULL || length == 0U || length > WORK_TX_SIZE || tx_busy || uart_bad) return 0;
  memcpy(tx_buffer, bytes, length); tx_busy = 1U;
  if (HAL_UART_Transmit_IT(&huart2, tx_buffer, length) != HAL_OK) {
    tx_busy = 0U; ++dbg_workpiece_tx_errors; return 0;
  }
  return 1;
}
void Workpiece_RxCompleteCallback(UART_HandleTypeDef *uart)
{
#if IH_WORKPIECE_UART_ENABLED
  uint16_t next;
  if (uart != &huart2) return;
  ++dbg_workpiece_rx_bytes; dbg_workpiece_last_byte = rx_byte;
  next = (rx_head + 1U) % WORK_RX_SIZE;
  if (next == rx_tail) { ++dbg_workpiece_rx_overflow; uart_bad = 1U; }
  else { rx_ring[rx_head] = rx_byte; rx_head = next; }
  rx_started = HAL_UART_Receive_IT(&huart2, &rx_byte, 1U) == HAL_OK;
  if (!rx_started) { uart_bad = 1U; ++dbg_workpiece_uart_errors; }
#else
  (void)uart;
#endif
}
void Workpiece_TxCompleteCallback(UART_HandleTypeDef *uart)
{ if (IH_WORKPIECE_UART_ENABLED && uart == &huart2) tx_busy = 0U; }
void Workpiece_UartErrorCallback(UART_HandleTypeDef *uart)
{
  if (!IH_WORKPIECE_UART_ENABLED || uart != &huart2) return;
  ++dbg_workpiece_uart_errors; uart_bad = 1U; rx_started = 0U;
}
