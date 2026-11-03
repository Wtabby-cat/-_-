#include "ih_hmi.h"
#include "ih_workpiece.h"
/* One HAL callback owner, with independent USART1 and USART2 services. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{ Hmi_RxCompleteCallback(uart); Workpiece_RxCompleteCallback(uart); }
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{ Hmi_TxCompleteCallback(uart); Workpiece_TxCompleteCallback(uart); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{ Hmi_UartErrorCallback(uart); Workpiece_UartErrorCallback(uart); }
