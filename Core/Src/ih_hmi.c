#include "ih_hmi.h"
#include "ih_app.h"
#include "ih_config.h"
#include "ih_protocol.h"
#include "ih_hmi_messages.h"
#include "ih_program.h"
#include "ih_pwm.h"
#include "ih_history.h"
#include "ih_tracking.h"
#include "ih_power.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
#define RX_SIZE 512U
#define TX_SIZE 4096U
#define BATCH_SIZE 1536U
static uint8_t rx_byte, rx_ring[RX_SIZE], tx_ring[TX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static uint16_t tx_head, tx_tail, tx_inflight;
static volatile uint8_t tx_done, rx_bad, tx_bad;
static volatile uint8_t rx_started;
static HmiParser parser;
static char batch[BATCH_SIZE];
static uint16_t batch_len;
static uint8_t batch_bad;
static uint8_t page = 0xFFU, booted, save_active, query_waiting, query_index, quarantine;
static uint8_t telemetry_pending;
static uint32_t boot_tick, refresh_tick, query_tick, page_tick, rx_frame_tick;
static uint32_t resync_tick;
static uint32_t sent_faults;
static struct {
  uint32_t lo, hi, cursor, sent_second, sent_heights;
  int32_t cutoff, ylo, yhi;
  uint32_t generation;
  uint16_t epoch;
  uint8_t page, enabled, reset, emitted;
} plot;
static int32_t plot_meta[6];
static uint8_t plot_meta_page, plot_meta_mask;
static ProtectionParams candidate;
static HeatProgram program_candidate;
static uint8_t query_kind, query_count;
static int32_t search_candidate[2];
static const char *const names[6] = {"ov", "uv", "oc", "ot1", "ot2", "ot3"};
static const char *const program_names[27] = {"nseg", "mode", "plim",
  "tg1", "hd1", "tg2", "hd2", "tg3", "hd3", "tg4", "hd4",
  "tg5", "hd5", "tg6", "hd6", "tg7", "hd7", "tg8", "hd8",
  "tg9", "hd9", "tg10", "hd10", "tg11", "hd11", "tg12", "hd12"};
volatile uint32_t dbg_hmi_rx_overflow, dbg_hmi_uart_errors, dbg_hmi_tx_errors;
volatile uint32_t dbg_hmi_requests, dbg_hmi_query_timeouts;
volatile uint32_t dbg_hmi_rx_bytes, dbg_hmi_rx_frames, dbg_hmi_tx_bytes;
volatile uint32_t dbg_hmi_start_requests, dbg_hmi_page = 0xFFU, dbg_hmi_link;
volatile uint32_t dbg_hmi_resyncs, dbg_hmi_last_error, dbg_hmi_error_bits;
volatile uint32_t dbg_hmi_fault_source;

static int Active(void) { return App_State() == IH_RUNNING || App_State() == IH_PRECHARGE; }
static void CommunicationTrip(uint32_t source)
{ dbg_hmi_fault_source = source; App_CommunicationFault(); }
static void Begin(void) { batch_len = 0U; batch_bad = 0U; }
static void Command(const char *format, ...)
{
  va_list args;
  int n;
  if (batch_bad) return;
  va_start(args, format);
  n = vsnprintf(batch + batch_len, BATCH_SIZE - batch_len, format, args);
  va_end(args);
  if (n < 0 || (uint32_t)n + batch_len + 3U >= BATCH_SIZE) { batch_bad = 1U; return; }
  batch_len += (uint16_t)n;
  batch[batch_len++] = (char)0xFF; batch[batch_len++] = (char)0xFF; batch[batch_len++] = (char)0xFF;
}
static int Commit(void)
{
  uint16_t i;
  uint16_t free_bytes = (uint16_t)((tx_tail + TX_SIZE - tx_head - 1U) % TX_SIZE);
  if (batch_bad || batch_len > free_bytes) return 0;
  for (i = 0; i < batch_len; ++i) {
    tx_ring[tx_head] = (uint8_t)batch[i]; tx_head = (tx_head + 1U) % TX_SIZE;
  }
  return 1;
}
static void TxService(void)
{
  uint16_t len;
  if (tx_done) {
    dbg_hmi_tx_bytes += tx_inflight;
    tx_done = 0U; tx_tail = (tx_tail + tx_inflight) % TX_SIZE; tx_inflight = 0U;
  }
  if (tx_inflight != 0U || tx_tail == tx_head) return;
  len = tx_head > tx_tail ? (uint16_t)(tx_head - tx_tail) : (uint16_t)(TX_SIZE - tx_tail);
  /* Keep the active transfer short; storage and safety logic never wait on UART. */
  if (len > 128U) len = 128U;
  tx_inflight = len;
  if (HAL_UART_Transmit_IT(&huart1, &tx_ring[tx_tail], len) != HAL_OK) {
    tx_inflight = 0U; tx_bad = 1U; ++dbg_hmi_tx_errors;
  }
}
static void Message(HmiMessage code, int32_t a, int32_t b)
{
  Command("msgCode=%u", (unsigned)code);
  Command("msgA=%ld", (long)a); Command("msgB=%ld", (long)b);
  Command("msgPush=1"); Command("click bInput,0");
}
static void Footer(HmiMessage code)
{
  if (page > 6U) return;
  Begin(); Message(code, 0, 0); Commit();
}
static void StartBlocked(void)
{
  uint32_t reasons = dbg_start_block;
  if (reasons & IH_START_PARAMS) Footer(HMI_MSG_START_PARAMS);
  else if (reasons & IH_START_ADC) Footer(HMI_MSG_START_ADC);
  else if (reasons & IH_START_BUS) Footer(HMI_MSG_START_BUS);
  else if (reasons & IH_START_UV) Footer(HMI_MSG_START_UV);
  else if (reasons & IH_START_OV) Footer(HMI_MSG_START_OV);
  else if (reasons & IH_START_OC) Footer(HMI_MSG_START_OC);
  else if (reasons & IH_START_HW) Footer(HMI_MSG_START_HW);
  else if (reasons & IH_START_LATCHED) {
    if (App_Faults() & IH_FAULT_UV) {
      Begin(); Message(HMI_MSG_UV_HISTORY, (int32_t)(dbg_trip_bus_v * 10.0f + 0.5f),
                       (int32_t)dbg_trip_uv_v); Commit();
    }
    else Footer(HMI_MSG_START_LATCHED);
  }
  else if (reasons & IH_START_LINK) Footer(HMI_MSG_START_LINK);
  else Footer(HMI_MSG_START_STATE);
}
static void SetCandidate(uint8_t index, int32_t value)
{
  if (query_kind==3U) { search_candidate[index]=value; return; }
  if (query_kind != 0U) {
    if (query_kind == 2U) program_candidate.power_limit_w = value;
    else if (index == 0U) program_candidate.count = value;
    else if (index == 1U) program_candidate.mode = value;
    else if (index == 2U) program_candidate.power_limit_w = value;
    else if ((index & 1U) != 0U) program_candidate.target_c[(index - 3U) / 2U] = value;
    else program_candidate.hold_s[(index - 4U) / 2U] = value;
    return;
  }
  switch (index) {
  case 0: candidate.ov = value; break; case 1: candidate.uv = value; break;
  case 2: candidate.oc = value; break; case 3: candidate.ot1 = value; break;
  case 4: candidate.ot2 = value; break; case 5: candidate.ot3 = value; break;
  default: break;
  }
}
static void PublishParams(void)
{
  const ProtectionParams *p = App_Protection();
  Begin();
  Command("ov=%ld", (long)p->ov); Command("uv=%ld", (long)p->uv); Command("oc=%ld", (long)p->oc);
  Command("ot1=%ld", (long)p->ot1); Command("ot2=%ld", (long)p->ot2); Command("ot3=%ld", (long)p->ot3);
  Command("dirty=1"); Commit();
}
static void AbortSave(HmiMessage message)
{
  save_active = 0U; query_waiting = 0U; quarantine = 1U;
  /* sendme is an ordering barrier: discard late untagged 71 replies until 66. */
  Begin(); Command("sendme"); Commit();
  Footer(message);
}
static void Request(const HmiEvent *e)
{
  if (e->page > 6U) return;
  ++dbg_hmi_requests;
  switch (e->id) {
  case 2:
    App_Stop();
    if (save_active) AbortSave(HMI_MSG_STOPPED);
    else Footer(HMI_MSG_STOPPED);
    break;
  case 1:
    if (e->page != 0U) break;
    ++dbg_hmi_start_requests;
    if (save_active || quarantine)
      Footer(HMI_MSG_WAIT_SYNC);
    else if (Active()) Footer(HMI_MSG_START_STATE);
    else { query_kind=3U; query_count=2U; query_index=0U; save_active=1U; Footer(HMI_MSG_SEARCH_READING); }
    break;
  case 4:
    if (e->page != 5U) { Footer(HMI_MSG_PROGRAM_UNAVAILABLE); break; }
    if (Active() || save_active || quarantine) { Footer(HMI_MSG_SAVE_BUSY); break; }
    candidate = *App_Protection(); query_index = 0U; save_active = 1U;
    query_kind = 0U; query_count = 6U;
    Begin(); Command("saved=0"); Commit();
    Footer(HMI_MSG_READING_PARAMS);
    break;
  case 6:
    if (e->page == 5U) {
      Measurement m;
      Measurement_Get(&m);
      if (App_ResetFault()) Footer(HMI_MSG_RESET_OK);
      else if (m.bus_valid && m.bus_v <= (float)App_Protection()->uv)
        Footer(HMI_MSG_RESET_UV);
      else Footer(HMI_MSG_RESET_BLOCKED);
    }
    break;
  case 7:
    if (e->page == 5U) {
      Begin(); Command("ack=1"); Command("dirty=1"); Commit();
      Footer(HMI_MSG_ACKNOWLEDGED);
    }
    break;
  case 3:
    if (e->page != 1U && e->page != 3U) break;
    if (Active() || save_active || quarantine) { Footer(HMI_MSG_SAVE_BUSY); break; }
    program_candidate = *Program_Get();
    query_kind = e->page == 1U ? 1U : 2U;
    query_count = e->page == 1U ? 27U : 1U;
    query_index = 0U; save_active = 1U;
    Footer(HMI_MSG_READING_PROGRAM);
    break;
  case 5:
    if (e->page != 4U) break;
    if (Active() || save_active || quarantine) Footer(HMI_MSG_SAVE_BUSY);
    else { query_kind=3U; query_count=2U; query_index=0U; save_active=1U; Footer(HMI_MSG_SEARCH_READING); }
    break;
  default: break;
  }
  telemetry_pending = 1U;
}
static void Event(const HmiEvent *e)
{
  if (e->type == 0x88U) {
    if (Active()) CommunicationTrip(IH_HMI_FAULT_REBOOT);
    booted = 0U; boot_tick = HAL_GetTick(); page = 0xFFU;
    save_active = 0U; query_waiting = 0U; quarantine = 1U;
    HAL_UART_AbortTransmit(&huart1);
    tx_head = tx_tail = tx_inflight = 0U; tx_done = 0U;
    sent_faults = 0U; plot.enabled = 0U; plot_meta_mask = 0U;
    return;
  }
  if (!booted) return;
  if (e->type == 1U) Request(e);
  else if (e->type == 2U) {
    /* View metadata, committed by its final epoch frame. It cannot start, stop,
     * save or apply anything. The epoch rejects queued packets after navigation. */
    if ((e->page == 2U || e->page == 3U || e->page == 4U || e->page == 6U) &&
        e->id >= 42U && e->id <= 48U) {
      if (e->id == 42U) { plot_meta_mask = 0U; plot_meta_page = e->page; }
      if (e->page != plot_meta_page) return;
      if (e->id != 45U) {
        uint8_t index = e->id <= 44U ? e->id - 42U : e->id - 43U;
        plot_meta[index] = e->value;
        plot_meta_mask |= (uint8_t)(1U << index);
      } else {
        uint8_t axis_ok =
          (e->page == 2U && plot_meta[3] == 0 && plot_meta[4] > 0 && plot_meta[4] <= 10290) ||
          (e->page == 3U && plot_meta[3] == 0 && plot_meta[4] > 0 && plot_meta[4] <= 9999) ||
          (e->page == 4U && plot_meta[3] >= 100 && plot_meta[4] <= 9900 && plot_meta[3] < plot_meta[4]) ||
          (e->page == 6U && plot_meta[3] == -300 && plot_meta[4] == 300);
        if (plot_meta_mask == 63U && axis_ok && (uint32_t)plot_meta[5] == History_Generation() &&
            e->value > 0 && e->value <= 65535 &&
            plot_meta[0] >= 0 && plot_meta[1] <= 86400 && plot_meta[0] < plot_meta[1] &&
            plot_meta[2] >= -1 && plot_meta[2] <= (int32_t)IH_HISTORY_SECONDS) {
          plot.lo = (uint32_t)plot_meta[0]; plot.hi = (uint32_t)plot_meta[1];
          plot.cutoff = plot_meta[2]; plot.cursor = plot.lo; plot.sent_second = plot.lo;
          plot.ylo = plot_meta[3]; plot.yhi = plot_meta[4]; plot.generation = History_Generation();
          plot.epoch = (uint16_t)e->value; plot.page = e->page;
          plot.enabled = plot.reset = 1U; plot.emitted = 0U;
          page = e->page; page_tick = HAL_GetTick(); telemetry_pending = 1U;
        }
        plot_meta_mask = 0U;
      }
      return;
    }
    /* Numeric confirmation is an edit, never an apply/save operation. */
    if (e->page == 5U && e->id >= 36U && e->id <= 41U) {
      if (save_active) AbortSave(HMI_MSG_SAVE_CHANGED);
    }
    if (save_active && (query_kind==1U || query_kind==2U) &&
        ((e->page == 1U && e->id <= 25U) || (e->page == 3U && e->id == 1U)))
      AbortSave(HMI_MSG_SAVE_CHANGED);
    if (save_active && query_kind==3U && e->page==4U && (e->id==34U || e->id==35U))
      AbortSave(HMI_MSG_SAVE_CHANGED);
  } else if (e->type == 0x66U && e->page <= 7U) {
    uint8_t was_unknown = page == 0xFFU;
    if (page != e->page) telemetry_pending = 1U;
    page = e->page; page_tick = HAL_GetTick(); quarantine = 0U;
    if (was_unknown) {
      Footer(Protection_ParamsValid(App_Protection()) ? HMI_MSG_LINK_OK : HMI_MSG_LINK_NEEDS_PARAMS);
      telemetry_pending = 1U;
    }
  } else if (e->type == 0x71U && save_active && query_waiting && !quarantine) {
    SetCandidate(query_index, e->value); query_waiting = 0U; ++query_index;
    if (query_index == query_count) {
      save_active = 0U;
      if (query_kind==3U) {
        if (search_candidate[0]<11 || search_candidate[1]>40 || search_candidate[0]>=search_candidate[1])
          Footer(HMI_MSG_SEARCH_INVALID);
        else if (!App_StartSearch((uint32_t)search_candidate[0]*100U,(uint32_t)search_candidate[1]*100U)) StartBlocked();
        else {
          plot.enabled=0U;
          Begin(); Command("clearPlot=1"); Commit(); Footer(HMI_MSG_STARTED);
        }
      } else if (query_kind != 0U) {
        Footer(!Active() && Program_Apply(&program_candidate) ? HMI_MSG_PROGRAM_APPLIED : HMI_MSG_PROGRAM_INVALID);
      } else if (App_SaveProtection(&candidate)) {
        Begin(); Command("saved=1"); Commit();
        Footer(HMI_MSG_SAVED);
      } else Footer(HMI_MSG_SAVE_FAILED);
    }
  } else if (e->type == 0x65U && e->value == 0) {
    /* Touch is informational; business frames alone execute START/STOP. */
    Begin(); Command("sendme"); Commit();
  }
}
static int32_t Scaled(float v, float scale)
{
  float value = v * scale;
  if (value > 2147483000.0f) return 2147483000L;
  if (value < -2147483000.0f) return -2147483000L;
  return (int32_t)(value + (value < 0.0f ? -0.5f : 0.5f));
}
static void Telemetry(void)
{
  Measurement m;
  if (page == 7U) return; /* keypad must retain its current editing state */
  if (page > 7U) {
    /* The HMI renders Chinese locally, even with its return wire disconnected. */
    Begin(); Message(HMI_MSG_NO_REPLY, 0, 0);
    if (Commit()) telemetry_pending = 0U;
    return;
  }
  Measurement_Get(&m);
  Begin();
  Command("dirty=0");
  Command("run=%d", Active() ? 1 : 0);
  Command("locked=%d", Active() && Tracking_Locked() ? 1 : 0);
  Command("trackState=%u",(unsigned)Tracking_State());
  Command("phaseState=%d", !Active() ? 0 : Tracking_State()==TRACK_SEARCH ? 5 :
          Tracking_State()==TRACK_RAMP ? 6 : Tracking_Locked() ? 8 : 7);
  if (page == 0U) {
    Command("totalSec=%lu", (unsigned long)App_ElapsedSeconds());
    Command("bus10=%ld", (long)Scaled(m.bus_v, 10.0f));
    Command("amp10=%ld", (long)Scaled(m.current_rms_a, 10.0f));
    Command("temp10=%ld", (long)Scaled(m.workpiece.celsius, 10.0f));
  }
  if (page == 0U || page == 4U) Command("freq100=%lu", (unsigned long)App_Frequency100());
  if (page == 5U) {
    Command("temp110=%ld", (long)Scaled(m.components.celsius[0], 10.0f));
    Command("temp210=%ld", (long)Scaled(m.components.celsius[1], 10.0f));
    Command("temp1mv=%ld", (long)Scaled(m.components.adc_v[0], 1000.0f));
    Command("temp2mv=%ld", (long)Scaled(m.components.adc_v[1], 1000.0f));
  }
  if (page == 2U) {
    Command("mean10=%ld", (long)Scaled(m.workpiece.mean_c, 10.0f));
    Command("high10=%ld", (long)Scaled(m.workpiece.high_c, 10.0f));
    Command("low10=%ld", (long)Scaled(m.workpiece.low_c, 10.0f));
  }
  /* Send the fields on the visible page, leaving bandwidth for history replay. */
  Command("target10=%ld", (long)Program_Get()->target_c[0] * 10L);
  if (page == 0U || page == 3U) {
    Command("power100=%ld", (long)(Active() ? Scaled(m.active_power_w, 100.0f) : 0));
    Command("adcClipped=%u", m.adc_clipped ? 1U : 0U);
  }
  if (page == 3U) {
    Command("angle10=%lu", (unsigned long)dbg_pwm_phase10);
    Command("powerSet100=%ld",(long)Scaled(Power_TargetW(),100.0f));
  }
  if (page == 6U) Command("phase10=%ld", (long)Scaled(m.phase_deg, 10.0f));
  if (page == 0U || page == 5U) {
    Command("fwVersion=%lu", (unsigned long)IH_FW_DIAG_VERSION);
    /* Frozen trip values, independent of the current (possibly recovered) bus. */
    Command("tripCount=%lu", (unsigned long)dbg_trip_count);
    Command("tripMask=%lu", (unsigned long)dbg_trip_mask);
    Command("tripGate=%lu", (unsigned long)dbg_trip_pwm_en);
    Command("tripMs=%lu", (unsigned long)(dbg_trip_elapsed_ms > 99999U ? 99999U : dbg_trip_elapsed_ms));
    Command("tripRaw1=%lu", (unsigned long)dbg_trip_bus_rank1_raw);
    Command("tripRaw2=%lu", (unsigned long)dbg_trip_bus_rank2_raw);
  }
  Command("internalFault=%lu", (unsigned long)(App_Faults() & (IH_FAULT_ADC | IH_FAULT_BUS | IH_FAULT_COM | IH_FAULT_TRACK | IH_FAULT_POWER)));
  /* The HMI owns all field labels and feedback text. */
  Command("dataFlags=%u", (Active() ? 32U : 0U) | 2U | 256U | (m.bus_valid ? 8U : 0U) | (m.current_valid ? 16U : 0U) |
          (m.power_valid ? 4U : 0U) | (m.workpiece.valid ? 1U : 0U) |
          ((Active() && m.phase_valid) ? 64U : 0U) |
          (m.workpiece.region_valid ? 128U : 0U) |
          ((m.components.valid_mask & 1U) ? 512U : 0U) |
          ((m.components.valid_mask & 2U) ? 2048U : 0U) | (m.components.adc_valid ? 1024U : 0U));
  Command("valid=1");
  Command("lastSec=%lu", (unsigned long)History_LastSecond());
  Command("runId=%lu", (unsigned long)History_Generation());
  Command("dirty=1");
  Command("click bInput,0");
  /* HMI owns tState and tPhase; writing these labels directly races its timer.
   * run drives the heating state; phaseState=4 labels fixed-frequency output
   * without claiming that temperature stages or closed-loop control exist. */
  if (Commit()) telemetry_pending = 0U;
}
static void PlotService(void)
{
  uint32_t end, cursor, sent, heights, next, packet[16], base;
  uint16_t queued = (uint16_t)((tx_head + TX_SIZE - tx_tail) % TX_SIZE);
  uint8_t count = 0U, budget = 64U, emitted;
  if (!plot.enabled || page != plot.page || save_active || quarantine ||
      !Hmi_LinkHealthy() || queued > 768U) return;
  if (plot.generation != History_Generation()) { plot.enabled = 0U; return; }
  end = History_LastSecond();
  if (end > plot.hi) end = plot.hi;
  if (plot.cutoff >= 0 && end > (uint32_t)plot.cutoff) end = (uint32_t)plot.cutoff;
  cursor = plot.cursor; sent = plot.sent_second; base = sent; emitted = plot.emitted;
  heights = plot.sent_heights;
  while (History_HasData() && cursor <= end && count < 16U && budget--) {
    uint32_t current = History_Heights(page, cursor, plot.ylo, plot.yhi);
    next = cursor < end ? History_Heights(page, cursor + 1U, plot.ylo, plot.yhi) : current;
    /* Omit only redundant points in an exactly horizontal pixel run. Preserve
     * the point before a change, validity gaps, final point and <=127 s deltas.
     * The archive itself always retains every one-second sample. */
    if (!emitted || current != heights || current != next || cursor == end || cursor - sent >= 127U) {
      packet[count++] = ((cursor - sent) << 24) | current;
      sent = cursor; heights = current; emitted = 1U;
    }
    ++cursor;
  }
  if (count == 0U && !plot.reset) { plot.cursor = cursor; return; }
  Begin();
  Command("plotPage=%u", plot.page); Command("plotEpoch=%u", plot.epoch);
  Command("plotRun=%lu", (unsigned long)plot.generation);
  Command("plotYLo=%ld", (long)plot.ylo); Command("plotYHi=%ld", (long)plot.yhi);
  Command("plotReset=%u", plot.reset); Command("plotStart=%lu", (unsigned long)base);
  Command("plotCount=%u", count);
  {
    uint8_t i;
    for (i = 0U; i < count; ++i) Command("cv%u=%lu", i, (unsigned long)packet[i]);
  }
  Command("click bCurve,0");
  if (Commit()) {
    plot.cursor = cursor; plot.sent_second = sent; plot.sent_heights = heights;
    plot.emitted = emitted; plot.reset = 0U;
  }
}
static void Alarms(void)
{
  uint32_t faults = App_Faults(), delta = faults ^ sent_faults;
  uint8_t code;
  if (page > 6U || delta == 0U) return;
  Begin();
  for (code = 1U; code <= 8U; ++code) {
    if ((delta & (1UL << code)) == 0U) continue;
    Command("alarmIn=%u", code); Command("alarmOn=%u", (faults & (1UL << code)) ? 1U : 0U);
    Command("alarmSec=%lu", (unsigned long)App_ElapsedSeconds());
    Command("alarmPush=1"); Command("click bInput,0");
  }
  if (delta & faults & IH_FAULT_UV)
    Message(HMI_MSG_UV_LATCHED, (int32_t)(dbg_trip_bus_v * 10.0f + 0.5f), (int32_t)dbg_trip_uv_v);
  if (delta & faults & IH_FAULT_ADC) Message(HMI_MSG_ADC_FAILED,0,0);
  if (delta & faults & IH_FAULT_BUS) Message(HMI_MSG_BUS_FAILED,0,0);
  if (delta & faults & IH_FAULT_COM) Message(HMI_MSG_COM_FAILED,0,0);
  if (delta & faults & IH_FAULT_POWER) Message(HMI_MSG_POWER_FAILED,(int32_t)Power_Error(),0);
  if (delta & faults & IH_FAULT_TRACK) {
    HmiMessage message=HMI_MSG_TRACK_SIGNAL;
    switch (Tracking_Error()) {
    case TRACK_RANGE: message=HMI_MSG_TRACK_RANGE; break;
    case TRACK_TIMEOUT: message=HMI_MSG_TRACK_TIMEOUT; break;
    case TRACK_PHASE: message=HMI_MSG_TRACK_PHASE; break;
    case TRACK_PWM: message=HMI_MSG_TRACK_PWM; break;
    default: break;
    }
    Message(message,0,0);
  }
  if (Commit()) sent_faults = faults;
}
int Hmi_LinkHealthy(void)
{
  return booted && page <= 7U && !rx_bad && !tx_bad &&
         (uint32_t)(HAL_GetTick() - page_tick) < 3500U;
}
void Hmi_Init(void)
{
  HmiParser_Reset(&parser);
  boot_tick = HAL_GetTick(); resync_tick = boot_tick;
  /* Discard line noise collected while the HMI TX pin was powering up. */
  HAL_UART_AbortReceive(&huart1);
  rx_started = HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) == HAL_OK;
  if (!rx_started) { rx_bad = 1U; ++dbg_hmi_uart_errors; }
}
void Hmi_Task(void)
{
  HmiEvent event;
  Measurement history_measurement;
  uint32_t now = HAL_GetTick();
  uint16_t limit = RX_SIZE;
  Measurement_Get(&history_measurement);
  History_Record(now, &history_measurement, Program_Get()->target_c[0], App_Frequency100());
  if (rx_bad || tx_bad) {
    if (Active()) CommunicationTrip((rx_bad ? IH_HMI_FAULT_RX : 0U) |
                                    (tx_bad ? IH_HMI_FAULT_TX : 0U));
    save_active = 0U; query_waiting = 0U; quarantine = 1U;
    page = 0xFFU;
    rx_tail = rx_head; HmiParser_Reset(&parser); rx_bad = 0U;
    if (tx_bad) {
      HAL_UART_AbortTransmit(&huart1);
      tx_head = tx_tail = tx_inflight = 0U; tx_done = tx_bad = 0U;
      Begin(); Command(""); Command("sendme"); Commit();
    }
  }
  if (!rx_started) rx_started = HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) == HAL_OK;
  if (parser.used && (uint32_t)(now - rx_frame_tick) > 100U) HmiParser_Reset(&parser);
  while (limit-- && rx_tail != rx_head) {
    uint8_t b = rx_ring[rx_tail]; rx_tail = (rx_tail + 1U) % RX_SIZE;
    rx_frame_tick = now;
    if (HmiParser_Feed(&parser, b, &event)) { ++dbg_hmi_rx_frames; Event(&event); }
  }
  if (!booted && (uint32_t)(now - boot_tick) >= IH_HMI_BOOT_MS) {
    booted = 1U;
    Begin(); Command(""); Command("bkcmd=0"); Command("sendme"); Command("clearPlot=1"); Commit();
    PublishParams(); refresh_tick = now; telemetry_pending = 1U;
  }
  if (booted && (uint32_t)(now - refresh_tick) >= IH_HMI_REFRESH_MS) {
    refresh_tick = now; telemetry_pending = 1U;
    Begin(); Command("sendme"); Commit();
  }
  if (booted && Active() && !Hmi_LinkHealthy()) CommunicationTrip(IH_HMI_FAULT_LINK);
  if (booted && !Active() && !Hmi_LinkHealthy() && !save_active && !query_waiting &&
      (uint32_t)(now - resync_tick) >= IH_HMI_RESYNC_MS) {
    /* Foreground, RX-only recovery; HAL clears PE/FE/NE/ORE and flushes RDR.
     * Never resets power control or clears a latched heating fault. */
    resync_tick = now; ++dbg_hmi_resyncs;
    HAL_UART_AbortReceive(&huart1); rx_started = 0U;
    rx_tail = rx_head; HmiParser_Reset(&parser);
    page = 0xFFU; quarantine = 1U;
    Begin(); Command(""); Command("bkcmd=0"); Command("sendme"); Commit();
    rx_started = HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) == HAL_OK;
    if (!rx_started) { rx_bad = 1U; ++dbg_hmi_uart_errors; }
    telemetry_pending = 1U;
  }
  if (query_waiting && (uint32_t)(now - query_tick) >= IH_HMI_QUERY_TIMEOUT_MS) {
    ++dbg_hmi_query_timeouts; AbortSave(HMI_MSG_SAVE_TIMEOUT);
  }
  if (save_active && !query_waiting && !quarantine && tx_head == tx_tail && tx_inflight == 0U) {
    Begin(); Command("get %s", query_kind==3U ? (query_index==0U ? "flo" : "fhi") : query_kind == 0U ? names[query_index] :
                   query_kind == 2U ? "plim" : program_names[query_index]);
    if (Commit()) { query_waiting = 1U; query_tick = now; }
  }
  if (booted) {
    Alarms();
    if (telemetry_pending && !save_active) Telemetry();
    PlotService();
  }
  TxService();
  dbg_hmi_page = page; dbg_hmi_link = Hmi_LinkHealthy() ? 1U : 0U;
}
void Hmi_RxCompleteCallback(UART_HandleTypeDef *uart)
{
  uint16_t next;
  if (uart != &huart1) return;
  ++dbg_hmi_rx_bytes;
  next = (rx_head + 1U) % RX_SIZE;
  if (next == rx_tail) { ++dbg_hmi_rx_overflow; rx_bad = 1U; }
  else { rx_ring[rx_head] = rx_byte; rx_head = next; }
  rx_started = HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) == HAL_OK;
  if (!rx_started) { rx_bad = 1U; ++dbg_hmi_uart_errors; }
}
void Hmi_TxCompleteCallback(UART_HandleTypeDef *uart)
{
  if (uart == &huart1) tx_done = 1U;
}
void Hmi_UartErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart != &huart1) return;
  dbg_hmi_last_error = uart->ErrorCode; dbg_hmi_error_bits |= uart->ErrorCode;
  ++dbg_hmi_uart_errors; rx_bad = 1U;
  HAL_UART_AbortReceive(&huart1);
  rx_started = 0U;
}
