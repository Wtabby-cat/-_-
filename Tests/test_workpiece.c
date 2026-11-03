#include "ih_workpiece.h"
#include "ih_hmi.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
UART_HandleTypeDef huart1, huart2;
static uint32_t tick;
static uint8_t *rx_dest;
static const uint8_t *tx_data;
static uint16_t tx_len;
static unsigned abort_rx, abort_tx, hmi_rx, hmi_tx, hmi_errors;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *data,uint16_t n)
{ assert(u==&huart2 && n==1); rx_dest=data; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *data,uint16_t n)
{ assert(u==&huart2); tx_data=data; tx_len=n; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ assert(u==&huart2); ++abort_rx; rx_dest=NULL; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ assert(u==&huart2); ++abort_tx; return HAL_OK; }
void Hmi_RxCompleteCallback(UART_HandleTypeDef *u) { if(u==&huart1) ++hmi_rx; }
void Hmi_TxCompleteCallback(UART_HandleTypeDef *u) { if(u==&huart1) ++hmi_tx; }
void Hmi_UartErrorCallback(UART_HandleTypeDef *u) { if(u==&huart1) ++hmi_errors; }
#if IH_WORKPIECE_UART_ENABLED
static void receive(uint8_t b)
{ assert(rx_dest); *rx_dest=b; HAL_UART_RxCpltCallback(&huart2); }
#endif
int main(void)
{
  WorkpieceTemperature t;
  uint8_t request[3]={1,2,3};
  Workpiece_Get(&t,0); assert(!t.valid);
#if !IH_WORKPIECE_UART_ENABLED
  Workpiece_Init(); Workpiece_Task();
  assert(abort_rx==0 && abort_tx==0 && rx_dest==NULL);
  assert(!Workpiece_Transmit(request,sizeof(request)) && !Workpiece_Submit(42.0f,0));
  HAL_UART_RxCpltCallback(&huart2); HAL_UART_TxCpltCallback(&huart2);
  HAL_UART_ErrorCallback(&huart2); Workpiece_Task(); Workpiece_Get(&t,0);
  assert(!t.valid && dbg_workpiece_rx_bytes==0 && dbg_workpiece_uart_errors==0 && tx_len==0);
  HAL_UART_RxCpltCallback(&huart1); assert(hmi_rx==1);
  puts("PASS: reserved workpiece port performs no receive, transmit, decoding or UART recovery by default");
#else
  unsigned i;
  Workpiece_Init(); assert(abort_rx==1 && rx_dest);
  /* No invented default parser: even plausible ASCII temperature stays invalid. */
  for(i=0;i<7;++i) receive((uint8_t)"123.4\r\n"[i]);
  Workpiece_Task(); Workpiece_Get(&t,tick);
  assert(!t.valid && dbg_workpiece_rx_bytes==7 && hmi_rx==0);
  tick=100; assert(Workpiece_Submit(123.4f,tick));
  Workpiece_Get(&t,tick); assert(t.valid && t.celsius==123.4f);
  Workpiece_Get(&t,tick+IH_WORKPIECE_STALE_MS-1); assert(t.valid);
  Workpiece_Get(&t,tick+IH_WORKPIECE_STALE_MS); assert(!t.valid);
  assert(Workpiece_SubmitRegion(80.0f,90.0f,70.0f,tick));
  Workpiece_Get(&t,tick); assert(t.valid && t.region_valid && t.celsius==80.0f && t.high_c==90.0f && t.low_c==70.0f);
  Workpiece_Get(&t,tick+IH_WORKPIECE_STALE_MS); assert(!t.valid && !t.region_valid);
  assert(!Workpiece_SubmitRegion(80.0f,70.0f,60.0f,tick));
  assert(!Workpiece_SubmitRegion(80.0f,NAN,60.0f,tick));
  assert(Workpiece_Submit(50.0f,tick)); Workpiece_Get(&t,tick); assert(t.valid && !t.region_valid);
  assert(!Workpiece_Submit(NAN,tick)); Workpiece_Get(&t,tick); assert(!t.valid);
  assert(!Workpiece_Submit(INFINITY,tick)); assert(!Workpiece_Submit(-274.0f,tick));
  tick=0xFFFFFFF0U; assert(Workpiece_Submit(50.0f,tick));
  Workpiece_Get(&t,20); assert(t.valid); /* clock wrap */
  assert(Workpiece_Transmit(request,sizeof(request)));
  request[0]=99; assert(tx_len==3 && tx_data[0]==1); /* owned TX buffer */
  assert(!Workpiece_Transmit(request,sizeof(request)));
  HAL_UART_TxCpltCallback(&huart1); assert(hmi_tx==1);
  assert(!Workpiece_Transmit(request,sizeof(request))); /* HMI completion is unrelated */
  HAL_UART_TxCpltCallback(&huart2); assert(Workpiece_Transmit(request,sizeof(request)));
  HAL_UART_TxCpltCallback(&huart2);
  assert(!Workpiece_Transmit(NULL,1) && !Workpiece_Transmit(request,0));
  HAL_UART_ErrorCallback(&huart1); assert(hmi_errors==1);
  Workpiece_Get(&t,20); assert(t.valid); /* HMI errors cannot change this source */
  HAL_UART_ErrorCallback(&huart2); Workpiece_Get(&t,20); assert(!t.valid);
  assert(hmi_errors==1 && dbg_workpiece_uart_errors==1);
  tick=5000; Workpiece_Task(); assert(abort_rx==2 && abort_tx==1 && rx_dest);
  Workpiece_Get(&t,tick); assert(!t.valid); /* recovery never revives an old sample */
  assert(Workpiece_Submit(66.0f,tick));
  for(i=0;i<300;++i) receive(0xAA);
  Workpiece_Get(&t,tick); assert(!t.valid && dbg_workpiece_rx_overflow>0);
  tick+=100; Workpiece_Task(); Workpiece_Get(&t,tick); assert(!t.valid);
  assert(Workpiece_Submit(67.0f,tick)); Workpiece_Get(&t,tick); assert(t.valid);
  Workpiece_Invalidate(); Workpiece_Get(&t,tick); assert(!t.valid);
  HAL_UART_RxCpltCallback(&huart1); assert(hmi_rx==1);
  puts("PASS: USART2 transport, no guessed decoding, sample freshness, owned TX, overflow/recovery and UART isolation");
#endif
  return 0;
}
