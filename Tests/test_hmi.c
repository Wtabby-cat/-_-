/* Host simulation of the real HMI service, including 9600-baud queue timing. */
#include "ih_hmi.h"
#include "ih_app.h"
#include "ih_config.h"
#include "ih_hmi_messages.h"
#include "ih_program.h"
#include "ih_history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
static uint32_t tick, due, faults;
static uint8_t *rx_dest, transfer[128], screen_page;
static uint16_t transfer_len;
static int transfer_pending, reply_pages=1, reply_get=1, start_calls, stop_calls, saves;
static int get_calls, plot_clicks, alarm_events;
static IhState state;
static ProtectionParams params={28,18,10,85,90,150};
static const int32_t edited[6]={27,17,9,80,85,140};
static int last_message, message_a, message_b;
static unsigned label_writes, rx_aborts;
static int rx_poisoned;
volatile uint32_t dbg_start_block;
volatile float dbg_trip_bus_v, dbg_trip_uv_v;
volatile uint32_t dbg_trip_count, dbg_trip_pwm_en, dbg_trip_elapsed_ms;
volatile uint32_t dbg_trip_bus_rank1_raw, dbg_trip_bus_rank2_raw;
static unsigned screen_fw, screen_trip_count, screen_trip_gate, screen_trip_ms;
static unsigned screen_raw1, screen_raw2;
static int start_ok=1;
volatile uint32_t dbg_pwm_phase10=600;
static int program_reply, program_invalid, program_gets;
static int screen_target, screen_power, screen_flags, screen_temp_mv, screen_angle;
static int screen_workpiece, screen_component1, screen_component2;
static int screen_phase, phase_connected=1;
static int current_connected=1;
static int curve_mode, curve_epoch, curve_reset, curve_start, curve_count;
static uint32_t curve_values[16], curve_last_second, curve_packets, freeze_max;
static FILE *curve_output;
static unsigned curve_samples;
static uint32_t curve_origin;
static int curve_ylo,curve_yhi,curve_run;

uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *b,uint16_t n)
{ (void)u; assert(n==1); if(rx_poisoned) return HAL_BUSY; rx_dest=b; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *b,uint16_t n)
{
  (void)u; assert(!transfer_pending && n<=sizeof(transfer));
  memcpy(transfer,b,n); transfer_len=n; transfer_pending=1;
  due=tick+(n*10000U+IH_HMI_BAUD-1U)/IH_HMI_BAUD;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ (void)u; transfer_pending=0; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ u->ErrorCode=HAL_UART_ERROR_NONE; rx_dest=NULL; rx_poisoned=0; ++rx_aborts; return HAL_OK; }
static void receive(const uint8_t *b,size_t n)
{
  size_t i;
  for(i=0;i<n;++i) { assert(rx_dest); *rx_dest=b[i]; HAL_UART_RxCpltCallback(&huart1); }
}
static void numeric(int32_t value)
{
  uint32_t v=(uint32_t)value;
  uint8_t f[]={0x71,(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24),255,255,255};
  receive(f,sizeof(f));
}
static void screen_command(const char *cmd)
{
  static const char *const keys[]={"ov","uv","oc","ot1","ot2","ot3"};
  unsigned i;
  if(!strcmp(cmd,"sendme") && reply_pages && rx_dest!=NULL) {
    uint8_t f[]={0x66,screen_page,255,255,255}; receive(f,sizeof(f));
  }
  if(!strncmp(cmd,"get ",4)) {
    ++get_calls;
    if(reply_get) {
      if(program_reply) {
        ++program_gets;
        if(!strcmp(cmd+4,"nseg")) numeric(program_invalid ? 13 : 2);
        else if(!strcmp(cmd+4,"mode")) numeric(1);
        else if(!strcmp(cmd+4,"plim")) numeric(900);
        else if(!strncmp(cmd+4,"tg",2)) numeric(80);
        else if(!strncmp(cmd+4,"hd",2)) numeric(300);
        else assert(0);
      } else for(i=0;i<6;++i) if(!strcmp(cmd+4,keys[i])) numeric(edited[i]);
    }
  }
  if(!strncmp(cmd,"msgCode=",8)) last_message=atoi(cmd+8);
  if(!strncmp(cmd,"msgA=",5)) message_a=atoi(cmd+5);
  if(!strncmp(cmd,"msgB=",5)) message_b=atoi(cmd+5);
  if(!strncmp(cmd,"fwVersion=",10)) screen_fw=(unsigned)atoi(cmd+10);
  if(!strncmp(cmd,"tripCount=",10)) screen_trip_count=(unsigned)atoi(cmd+10);
  if(!strncmp(cmd,"tripGate=",9)) screen_trip_gate=(unsigned)atoi(cmd+9);
  if(!strncmp(cmd,"tripMs=",7)) screen_trip_ms=(unsigned)atoi(cmd+7);
  if(!strncmp(cmd,"tripRaw1=",9)) screen_raw1=(unsigned)atoi(cmd+9);
  if(!strncmp(cmd,"tripRaw2=",9)) screen_raw2=(unsigned)atoi(cmd+9);
  if(!strncmp(cmd,"target10=",9)) screen_target=atoi(cmd+9);
  if(!strncmp(cmd,"power100=",9)) screen_power=atoi(cmd+9);
  if(!strncmp(cmd,"dataFlags=",10)) screen_flags=atoi(cmd+10);
  if(!strncmp(cmd,"temp1mv=",8)) screen_temp_mv=atoi(cmd+8);
  if(!strncmp(cmd,"angle10=",8)) screen_angle=atoi(cmd+8);
  if(!strncmp(cmd,"phase10=",8)) screen_phase=atoi(cmd+8);
  if(!strncmp(cmd,"temp10=",7)) screen_workpiece=atoi(cmd+7);
  if(!strncmp(cmd,"temp110=",8)) screen_component1=atoi(cmd+8);
  if(!strncmp(cmd,"temp210=",8)) screen_component2=atoi(cmd+8);
  assert(strstr(cmd,".txt=")==NULL); /* Chinese text is rendered only by HMI */
  if(!strcmp(cmd,"click bInput,0")) ++plot_clicks;
  if(!strncmp(cmd,"plotEpoch=",10)) curve_epoch=atoi(cmd+10);
  if(!strncmp(cmd,"plotReset=",10)) curve_reset=atoi(cmd+10);
  if(!strncmp(cmd,"plotStart=",10)) curve_start=atoi(cmd+10);
  if(!strncmp(cmd,"plotCount=",10)) curve_count=atoi(cmd+10);
  if(!strncmp(cmd,"plotYLo=",8)) curve_ylo=atoi(cmd+8);
  if(!strncmp(cmd,"plotYHi=",8)) curve_yhi=atoi(cmd+8);
  if(!strncmp(cmd,"plotRun=",8)) curve_run=atoi(cmd+8);
  { unsigned index, value;
    if(sscanf(cmd,"cv%u=%u",&index,&value)==2) { assert(index<16); curve_values[index]=value; }
  }
  if(!strcmp(cmd,"click bCurve,0")) {
    uint32_t second=(uint32_t)curve_start;
    assert(curve_count>=0 && curve_count<=16);
    ++curve_packets;
    curve_samples+=(unsigned)curve_count;
    if(curve_epoch==2 && curve_output) {
      fprintf(curve_output,"plotPage=3\nplotEpoch=2\nplotRun=%d\nplotYLo=%d\nplotYHi=%d\nplotReset=%d\nplotStart=%d\nplotCount=%d\n",curve_run,curve_ylo,curve_yhi,curve_reset,curve_start,curve_count);
    }
    for(i=0;i<(unsigned)curve_count;++i) {
      uint32_t delta=curve_values[i]>>24;
      assert(delta<=127); second+=delta;
      if(curve_epoch==2 && curve_output) fprintf(curve_output,"cv%u=%u\n",i,(unsigned)curve_values[i]);
    }
    if(curve_epoch==2 && curve_output) fprintf(curve_output,"click bCurve,0\n");
    if(curve_epoch==3 && second>freeze_max) freeze_max=second;
    curve_last_second=second;
  }
  if(!strncmp(cmd,"alarmIn=",8)) ++alarm_events;
  if(!strncmp(cmd,"tPhase.txt=",11) || !strncmp(cmd,"tState.txt=",11)) ++label_writes;
  assert(strncmp(cmd,"tActual.txt=",12)!=0 && strncmp(cmd,"tTarget.txt=",12)!=0 &&
         strncmp(cmd,"tPower.txt=",11)!=0); /* no MCU/timer ownership race */
}
static void step(unsigned ms)
{
  static char command[256];
  static unsigned used, ff;
  unsigned j;
  while(ms--) {
    ++tick;
    if(transfer_pending && tick>=due) {
      transfer_pending=0;
      for(j=0;j<transfer_len;++j) {
        if(transfer[j]==255) {
          if(++ff==3) { command[used]=0; screen_command(command); used=ff=0; }
        } else {
          assert(ff==0 && used+1<sizeof(command)); command[used++]=(char)transfer[j];
        }
      }
      HAL_UART_TxCpltCallback(&huart1);
    }
    Hmi_Task();
  }
}
static void request(uint8_t pg,uint8_t id)
{
  uint8_t f[]={0xA5,0x5A,1,pg,id,255,255,255}; receive(f,sizeof(f));
}
void Measurement_Get(Measurement *m)
{ memset(m,0,sizeof(*m)); m->bus_v=24; m->current_rms_a=2; m->adc_valid=m->bus_valid=1; m->current_valid=(uint8_t)current_connected;
  m->active_power_w=curve_mode ? (float)((tick-curve_origin)/1000U) : -12.4f; m->power_valid=(uint8_t)current_connected; m->components.adc_valid=1; m->components.adc_v[0]=1.234f;
  m->phase_valid=(uint8_t)(phase_connected && current_connected); m->phase_deg=-12.3f;
  m->components.valid_mask=3; m->components.celsius[0]=32.0f; m->components.celsius[1]=41.0f; }
IhState App_State(void) { return state; }
uint32_t App_Faults(void) { return faults; }
uint32_t App_ElapsedSeconds(void) { return tick/1000; }
uint32_t App_Frequency100(void) { return 2010; }
const ProtectionParams *App_Protection(void) { return &params; }
void App_Stop(void) { ++stop_calls; state=faults ? IH_FAULT : IH_IDLE; History_Stop(); }
int App_Start(void) { ++start_calls; if(!start_ok) return 0; state=IH_RUNNING; curve_origin=tick; History_Start(tick); return 1; }
int App_ResetFault(void) { faults=0; state=IH_IDLE; return 1; }
void App_CommunicationFault(void) { faults|=IH_FAULT_COM; state=IH_FAULT; History_Stop(); }
int App_SaveProtection(const ProtectionParams *p)
{ assert(Protection_ParamsValid(p)); params=*p; ++saves; return 1; }

static void view(int32_t lo,int32_t hi,int32_t cutoff,int32_t epoch)
{
  int32_t values[]={lo,hi,cutoff,0,1500,(int32_t)History_Generation(),epoch};
  const uint8_t fields[]={42,43,44,46,47,48,45};
  unsigned i;
  for(i=0;i<7;++i) {
    uint32_t v=(uint32_t)values[i];
    uint8_t f[]={0xA5,0x5A,2,3,fields[i],(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24),255,255,255};
    receive(f,sizeof(f));
  }
}
static int curves(void)
{
  unsigned before;
  curve_mode=1; Program_Init(); Hmi_Init(); step(10000);
  assert(History_Get(0)==NULL && !(screen_flags&32));
  screen_page=3; view(0,600,-1,1); step(2000);
  assert(curve_samples==0 && History_Get(0)==NULL);
  screen_page=0; request(0,1); step(2000);
  screen_page=3; view(0,600,-1,1); step(65000);
  assert(Hmi_LinkHealthy() && curve_packets>30 && History_Get(0)->power_w==0);
  screen_page=0; step(85000);
  assert(History_Get(10)->power_w==10 && History_LastSecond()>=149);
  curve_output=fopen("tmp/test_hmi_curve_packets.txt","w"); assert(curve_output);
  before=curve_packets; screen_page=3; view(0,600,-1,2); step(5000);
  assert(curve_packets>before && curve_last_second>=150 && Hmi_LinkHealthy());
  fclose(curve_output); curve_output=NULL;
  /* Replay does not block START/STOP dispatch behind the UART queue. */
  state=IH_RUNNING; before=(unsigned)stop_calls;
  request(3,2); step(1); assert((unsigned)stop_calls==before+1 && state==IH_IDLE);
  before=History_LastSecond(); step(2000); assert(History_LastSecond()==before);
  view(0,600,30,3); step(4000); assert(freeze_max==30 && Hmi_LinkHealthy());
  before=curve_packets; step(3000); assert(curve_packets==before);
  before=History_Generation(); screen_page=0; request(0,1); step(20);
  assert(History_Generation()==before+1 && History_LastSecond()==0 && History_Get(10)==NULL);
  puts("PASS: real history replay after navigation, frozen cutoff, compressed deltas and immediate STOP at 9600 baud");
  return 0;
}
int main(int argc,char **argv)
{
  uint8_t touch[]={0x65,0,38,0,255,255,255};
  uint8_t edit[]={0xA5,0x5A,2,5,36,27,0,0,0,255,255,255};
  unsigned before, wait;
  if(argc>1 && !strcmp(argv[1],"curves")) return curves();
  /* MCU boots first, HMI TX is not ready, and an old RX state needs clearing. */
  reply_pages=0; rx_poisoned=1;
  Program_Init();
  Hmi_Init(); step(8000);
  assert(rx_aborts>=2 && dbg_hmi_resyncs>=2 && start_calls==0 && !Hmi_LinkHealthy());
  reply_pages=1; step(3500);
  assert(Hmi_LinkHealthy() && start_calls==0);
  assert(dbg_hmi_rx_bytes>0 && dbg_hmi_rx_frames>0 && dbg_hmi_tx_bytes>0);
  assert(dbg_hmi_page==0 && dbg_hmi_link==1);
  assert(screen_fw==IH_FW_DIAG_VERSION);
  assert(screen_target==600 && screen_power==0);
  assert(!(screen_flags&32));
  assert((screen_flags&(2|4|256|512|1024|2048))==(2|4|256|512|1024|2048) && !(screen_flags&(1|64)));
  assert(screen_workpiece==0);
  current_connected=0; step(1500);
  assert(!(screen_flags&(4|16|32)) && (screen_flags&8));
  current_connected=1; step(1500);
  assert(label_writes==0); /* V8 timer alone owns the state / temperature phase labels */
  receive(touch,sizeof(touch)); request(0,1); step(100);
  assert(start_calls==1); /* touch does not duplicate business execution */
  step(1500); assert(screen_power==-1240 && (screen_flags&32));
  assert(dbg_hmi_start_requests==1);
  request(4,2); step(100); assert(stop_calls==1 && state==IH_IDLE);
  /* Profile reads form one atomic transaction; invalid data cannot change the
   * applied target and a mid-read edit cancels rather than mixing two profiles. */
  program_reply=1; screen_page=1; step(1500);
  request(1,3); step(6500);
  assert(program_gets==27 && Program_Get()->count==2 && Program_Get()->target_c[0]==80);
  assert(last_message==HMI_MSG_PROGRAM_APPLIED && screen_target==800);
  program_invalid=1; request(1,3); step(6500);
  assert(Program_Get()->count==2 && last_message==HMI_MSG_PROGRAM_INVALID);
  program_invalid=0;
  before=(unsigned)program_gets; request(1,3);
  for(wait=0;wait<3000 && (unsigned)program_gets==before;++wait) step(1);
  { uint8_t changed[]={0xA5,0x5A,2,1,2,81,0,0,0,255,255,255}; receive(changed,sizeof(changed)); }
  step(2000); assert(last_message==HMI_MSG_SAVE_CHANGED && Program_Get()->target_c[0]==80);
  screen_page=3; step(1500); before=(unsigned)program_gets;
  request(3,3); step(2000); assert((unsigned)program_gets==before+1 && Program_Get()->power_limit_w==900);
  program_reply=0; get_calls=0;
  receive(edit,sizeof(edit)); step(100); assert(saves==0);
  screen_page=5; step(1500);
  assert(screen_component1==320 && screen_component2==410 && screen_temp_mv==1234);
  request(5,4); step(3000);
  assert(saves==1 && get_calls==6 && params.ov==27 && params.ot3==140);
  /* Late untagged query replies after timeout must not corrupt a later save. */
  reply_get=0; request(5,4);
  for(wait=0;wait<4000 && dbg_hmi_query_timeouts==0;++wait) step(1);
  assert(dbg_hmi_query_timeouts==1 && saves==1);
  numeric(9999); step(1000);
  reply_get=1; request(5,4); step(3000);
  assert(saves==2 && params.ov==27 && params.ot3==140);
  /* Editing while a snapshot is being read cancels the entire transaction. */
  before=(unsigned)get_calls; request(5,4);
  for(wait=0;wait<3000 && (unsigned)get_calls==before;++wait) step(1);
  assert((unsigned)get_calls>before);
  receive(edit,sizeof(edit)); step(2000); assert(saves==2);
  /* Latched hardware alarms are posted once, and recovery is posted once. */
  faults=IH_FAULT_OV|IH_FAULT_OC; state=IH_FAULT; step(1500);
  assert(alarm_events==2); step(1500); assert(alarm_events==2);
  request(5,6); step(1500); assert(alarm_events==4);
  /* Numeric keypad receives heartbeat but no click/plot refresh. */
  screen_page=7; step(2000); before=(unsigned)plot_clicks;
  step(2000); assert((unsigned)plot_clicks==before && Hmi_LinkHealthy());
  screen_page=0; step(2000);
  request(0,1); step(100); assert(state==IH_RUNNING);
  screen_page=6; step(2000);
  assert(screen_phase==-123 && (screen_flags&64));
  phase_connected=0; step(2000); assert(!(screen_flags&64));
  phase_connected=1; screen_page=0; step(2000);
  reply_pages=0; step(4500); assert(state==IH_FAULT && (faults&IH_FAULT_COM));
  /* A disconnected return wire must still leave a useful one-way TX message. */
  { uint8_t reboot[]={0x88,255,255,255}; receive(reboot,sizeof(reboot)); }
  step(3000);
  assert(dbg_hmi_page==255 && dbg_hmi_link==0);
  assert(last_message==HMI_MSG_NO_REPLY);
  reply_pages=1; step(2000); assert(dbg_hmi_page==0 && dbg_hmi_link==1);
  assert(History_Get(0)!=NULL); /* HMI reboot preserves the completed run. */
  request(5,6); step(1500);
  start_ok=0; dbg_start_block=IH_START_BUS;
  before=History_Generation();
  request(0,1); step(1500);
  assert(last_message==HMI_MSG_START_BUS && state==IH_IDLE);
  assert(History_Generation()==before && History_Get(0)!=NULL);
  faults=IH_FAULT_UV; state=IH_FAULT; dbg_trip_bus_v=17.0f; dbg_trip_uv_v=18.0f;
  dbg_trip_count=1; dbg_trip_pwm_en=1; dbg_trip_elapsed_ms=60;
  dbg_trip_bus_rank1_raw=81; dbg_trip_bus_rank2_raw=79;
  step(1500);
  assert(last_message==HMI_MSG_UV_LATCHED && message_a==170 && message_b==18);
  assert(screen_trip_count==1 && screen_trip_gate==1 && screen_trip_ms==60);
  assert(screen_raw1==81 && screen_raw2==79);
  dbg_start_block=IH_START_LATCHED|IH_START_STATE;
  request(0,1); step(1500);
  assert(last_message==HMI_MSG_UV_HISTORY && message_a==170 && message_b==18);
  request(5,6); step(1500); assert(state==IH_IDLE);
  assert(screen_trip_count==1 && screen_trip_ms==60 && screen_raw2==79);
  /* Recover a lost receive arm without an IRQ callback or debugger reset. */
  before=dbg_hmi_resyncs; rx_poisoned=1; rx_dest=NULL;
  step(7000);
  assert(dbg_hmi_resyncs>before && Hmi_LinkHealthy() && rx_dest!=NULL);
  huart1.ErrorCode=HAL_UART_ERROR_FE|HAL_UART_ERROR_ORE;
  HAL_UART_ErrorCallback(&huart1); step(2000);
  assert(Hmi_LinkHealthy());
  assert(dbg_hmi_last_error==(HAL_UART_ERROR_FE|HAL_UART_ERROR_ORE));
  assert((dbg_hmi_error_bits&HAL_UART_ERROR_FE)!=0 && (dbg_hmi_error_bits&HAL_UART_ERROR_ORE)!=0);
  assert(label_writes==0 && state==IH_IDLE);
  puts("PASS: HMI queue, single execution, readback/save, timeout recovery, alarms and link-loss stop");
  return 0;
}
