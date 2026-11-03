#include "ih_history.h"
#include <string.h>
static HistoryPoint points[IH_HISTORY_SECONDS + 1U];
static uint32_t origin, last;
static uint8_t seen, recording;
static uint32_t generation;
volatile uint32_t dbg_history_seconds, dbg_history_full;

static int16_t Round16(float v)
{
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
}
void History_Reset(uint32_t now)
{
  origin = now; last = 0U; seen = recording = 0U;
  dbg_history_seconds = dbg_history_full = 0U;
  /* Old slots are inaccessible until overwritten or explicitly cleared as gaps. */
}
void History_Start(uint32_t now)
{ History_Reset(now); recording = 1U; ++generation; }
void History_Stop(void) { recording = 0U; }
int History_HasData(void) { return seen; }
int History_Recording(void) { return recording; }
uint32_t History_Generation(void) { return generation; }
void History_Record(uint32_t now, const Measurement *m, int32_t target_c, uint32_t freq100)
{
  HistoryPoint *p;
  uint32_t second = (uint32_t)(now - origin) / 1000U, gap;
  if (!recording) return;
  if (second > IH_HISTORY_SECONDS) { dbg_history_full = 1U; return; }
  if (seen && second <= last) return;
  gap = seen ? last + 1U : 0U;
  if (second > gap) memset(&points[gap], 0, (second - gap) * sizeof(points[0]));
  p = &points[second];
  p->target10 = Round16((float)target_c * 10.0f);
  p->mean10 = Round16(m->workpiece.mean_c * 10.0f);
  p->high10 = Round16(m->workpiece.high_c * 10.0f);
  p->low10 = Round16(m->workpiece.low_c * 10.0f);
  p->power_w = Round16(m->active_power_w);
  p->freq100 = (int16_t)freq100; p->phase10 = Round16(m->phase_deg * 10.0f);
  p->flags = 2U | 32U | (m->power_valid ? 4U : 0U) |
             (m->phase_valid ? 64U : 0U) |
             (m->workpiece.region_valid ? 128U : 0U);
  seen = 1U; last = second; dbg_history_seconds = second;
}
uint32_t History_LastSecond(void) { return last; }
const HistoryPoint *History_Get(uint32_t second)
{ return seen && second <= last ? &points[second] : (const HistoryPoint *)0; }
static uint32_t Height(int32_t value, int32_t lo, int32_t hi, int valid)
{
  if (!valid) return 255U;
  if (value < lo) value = lo;
  if (value > hi) value = hi;
  return (uint32_t)((value - lo) * 176L / (hi - lo));
}
uint32_t History_Heights(uint8_t page, uint32_t second, int32_t lo, int32_t hi)
{
  const HistoryPoint *p = History_Get(second);
  uint32_t a = 255U, b = 255U, c = 255U;
  if (p == 0 || hi <= lo) return 0xFFFFFFU;
  switch (page) {
  case 2:
    a = Height(p->target10, lo, hi, p->flags & 2U);
    b = Height(p->mean10, lo, hi, p->flags & 128U);
    c = Height(p->high10, lo, hi, p->flags & 128U); break;
  case 3: a = Height(p->power_w, lo, hi, p->flags & 4U); break;
  case 4: a = Height(p->freq100, lo, hi, p->flags & 32U); break;
  case 6:
    /* Keep the reference +/-30 degree view; an out-of-range phase breaks the
     * trace instead of drawing a false flat line on the boundary. */
    a = Height(p->phase10, lo, hi, (p->flags & 64U) && p->phase10 >= lo && p->phase10 <= hi); break;
  default: break;
  }
  return a | (b << 8) | (c << 16);
}
