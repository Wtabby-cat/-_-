#include "ih_history.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
  Measurement m;
  unsigned i;
  memset(&m, 0, sizeof(m));
  m.power_valid = 1; m.active_power_w = 123.4f;
  /* Calibrated component ADC inputs must never become region temperatures. */
  m.components.valid_mask = 3; m.components.celsius[0] = 32; m.components.celsius[1] = 41;
  History_Reset(100);
  History_Record(100, &m, 60, 2010);
  assert(!History_Recording() && History_Get(0)==NULL);
  History_Start(100);
  History_Record(100, &m, 60, 2010);
  assert(sizeof(HistoryPoint) == 16 && History_LastSecond() == 0);
  assert(History_Get(0)->power_w == 123 && !(History_Get(0)->flags & 128));
  assert((History_Heights(2, 0, 0, 1500) >> 8) == 65535 && History_Heights(6, 0, -300, 300) == 0xFFFFFF);
  assert((History_Heights(3,0,0,1500)&255)==14);
  assert((History_Heights(3,0,0,300)&255)==72);
  assert((History_Heights(4,0,1700,2300)&255)==90);
  assert((History_Heights(2,0,0,1200)&255)==88);
  assert(History_Heights(3,0,0,0)==0xFFFFFF);
  History_Record(999, &m, 90, 2010); assert(History_Get(0)->target10 == 600);
  m.workpiece.region_valid = 1; m.workpiece.mean_c = 80;
  m.workpiece.high_c = 90; m.workpiece.low_c = 70;
  m.phase_valid=1; m.phase_deg=-12.3f;
  History_Record(3100, &m, 90, 2010);
  assert(History_Get(1)->flags == 0 && History_Get(2)->flags == 0);
  assert(History_Get(3)->mean10 == 800 && History_Get(3)->high10 == 900);
  assert(History_Get(3)->phase10==-123 && (History_Get(3)->flags&64));
  assert((History_Heights(6,3,-300,300)&255)==51);
  m.phase_deg=150; History_Record(4100,&m,90,2010);
  assert(History_Heights(6,4,-300,300)==0xFFFFFF); /* no false +/-30 boundary line */
  m.phase_valid=0; History_Record(5100,&m,90,2010);
  assert(History_Heights(6,5,-300,300)==0xFFFFFF);
  History_Stop(); History_Record(4100,&m,120,2010);
  assert(History_LastSecond()==5 && History_Get(6)==NULL);
  History_Start(100); History_Record(3100,&m,90,2010);
  for (i = 4; i <= IH_HISTORY_SECONDS; ++i) History_Record(100 + i * 1000, &m, 90, 2010);
  assert(History_Get(0)->flags == 0 && History_Get(3)->mean10 == 800);
  History_Record(100 + (IH_HISTORY_SECONDS + 1) * 1000, &m, 120, 2010);
  assert(dbg_history_full && History_LastSecond() == IH_HISTORY_SECONDS);
  assert(History_Get(0)->flags == 0 && History_Get(IH_HISTORY_SECONDS + 1) == NULL);
  /* Starting a new record cannot reveal slots from the previous run. */
  History_Start(0xFFFFFFF0U);
  assert(History_Get(0) == NULL && History_Get(50) == NULL);
  History_Record(984U, &m, 120, 2010);
  assert(History_Get(0)->flags == 0 && History_Get(1)->target10 == 1200);
  assert(History_Get(3) == NULL);
  puts("PASS: one-second history, two-hour retention without overwrites, gaps, reset, wrap and serial-only region temperatures");
  return 0;
}
