#include "ih_program.h"
static HeatProgram applied;
void Program_Init(void)
{
  uint32_t i;
  applied.count = 3; applied.mode = 0; applied.power_limit_w = 1500;
  for (i = 0; i < IH_PROGRAM_STAGES; ++i) {
    applied.target_c[i] = i == 0U ? 60 : i == 1U ? 90 : 120;
    applied.hold_s[i] = i == 0U ? 600 : i == 1U ? 1800 : i == 2U ? 3600 : 300;
  }
}
int Program_Apply(const HeatProgram *p)
{
  uint32_t i;
  if (p->count < 1 || p->count > (int32_t)IH_PROGRAM_STAGES ||
      p->mode < 0 || p->mode > 2 || p->power_limit_w < 1 || p->power_limit_w > 9999) return 0;
  for (i = 0; i < (uint32_t)p->count; ++i)
    if (p->target_c[i] < 1 || p->target_c[i] > 999 || p->hold_s[i] < 1 || p->hold_s[i] > 9999) return 0;
  applied = *p;
  return 1;
}
const HeatProgram *Program_Get(void) { return &applied; }
