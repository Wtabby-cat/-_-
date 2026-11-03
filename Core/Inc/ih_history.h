#ifndef IH_HISTORY_H
#define IH_HISTORY_H
#include "ih_measurement.h"

/* One slot per second only between a successful start and stop/fault, including
 * invalid/gap slots. New start replaces the last run. HMI reboot preserves it. */
#define IH_HISTORY_SECONDS 7200U
typedef struct {
  int16_t target10, mean10, high10, low10;
  int16_t power_w, freq100, phase10;
  uint16_t flags;
} HistoryPoint;
void History_Reset(uint32_t now);
void History_Start(uint32_t now);
void History_Stop(void);
int History_HasData(void);
int History_Recording(void);
uint32_t History_Generation(void);
void History_Record(uint32_t now, const Measurement *m, int32_t target_c, uint32_t freq100);
uint32_t History_LastSecond(void);
const HistoryPoint *History_Get(uint32_t second);
/* Three 8-bit heights (0..176, 255 = invalid). Same geometry as HMI plots. */
uint32_t History_Heights(uint8_t page, uint32_t second, int32_t lo, int32_t hi);
extern volatile uint32_t dbg_history_seconds, dbg_history_full;
#endif
