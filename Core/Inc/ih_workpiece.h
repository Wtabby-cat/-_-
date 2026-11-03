#ifndef IH_WORKPIECE_H
#define IH_WORKPIECE_H
#include "main.h"
typedef struct {
  float celsius;
  float mean_c, high_c, low_c;
  uint32_t sample_tick;
  uint8_t valid, region_valid;
} WorkpieceTemperature;
/* USART2 transport only: no sensor protocol has been selected. Transport is
 * disabled by default (IH_WORKPIECE_UART_ENABLED=0). Enable only after device,
 * electrical interface, baud and parser are configured. Call Submit after a complete,
 * validated device frame has been converted to Celsius, in the foreground. */
void Workpiece_Init(void);
void Workpiece_Task(void);
int Workpiece_Submit(float celsius, uint32_t sample_tick);
/* Optional complete region statistics from a verified device frame. A single
 * point temperature must not be fabricated into region mean/max/min. */
int Workpiece_SubmitRegion(float mean_c, float high_c, float low_c, uint32_t sample_tick);
void Workpiece_Invalidate(void);
void Workpiece_Get(WorkpieceTemperature *out, uint32_t now);
/* Copies to owned storage; nonblocking, returns 0 while busy/on failure. */
int Workpiece_Transmit(const uint8_t *bytes, uint16_t length);
/* Weak no-op hooks for the future device-specific foreground parser. */
void Workpiece_ProtocolByte(uint8_t byte, uint32_t now);
void Workpiece_ProtocolReset(void);
void Workpiece_RxCompleteCallback(UART_HandleTypeDef *uart);
void Workpiece_TxCompleteCallback(UART_HandleTypeDef *uart);
void Workpiece_UartErrorCallback(UART_HandleTypeDef *uart);
extern volatile uint32_t dbg_workpiece_rx_bytes, dbg_workpiece_rx_overflow;
extern volatile uint32_t dbg_workpiece_uart_errors, dbg_workpiece_tx_errors;
extern volatile uint32_t dbg_workpiece_last_byte, dbg_workpiece_valid, dbg_workpiece_age_ms;
extern volatile float dbg_workpiece_c;
#endif
