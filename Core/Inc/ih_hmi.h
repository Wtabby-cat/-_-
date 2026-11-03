#ifndef IH_HMI_H
#define IH_HMI_H
#include <stdint.h>
#include "main.h"
void Hmi_Init(void);
void Hmi_Task(void);
int Hmi_LinkHealthy(void);
void Hmi_RxCompleteCallback(UART_HandleTypeDef *uart);
void Hmi_TxCompleteCallback(UART_HandleTypeDef *uart);
void Hmi_UartErrorCallback(UART_HandleTypeDef *uart);
extern volatile uint32_t dbg_hmi_rx_overflow, dbg_hmi_uart_errors, dbg_hmi_tx_errors;
extern volatile uint32_t dbg_hmi_requests, dbg_hmi_query_timeouts;
extern volatile uint32_t dbg_hmi_rx_bytes, dbg_hmi_rx_frames, dbg_hmi_tx_bytes;
extern volatile uint32_t dbg_hmi_start_requests, dbg_hmi_page, dbg_hmi_link;
extern volatile uint32_t dbg_hmi_resyncs, dbg_hmi_last_error, dbg_hmi_error_bits;
#endif
