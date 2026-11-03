#ifndef IH_PROGRAM_H
#define IH_PROGRAM_H
#include <stdint.h>
#define IH_PROGRAM_STAGES 12U
typedef struct {
  int32_t count, mode, power_limit_w;
  int32_t target_c[IH_PROGRAM_STAGES], hold_s[IH_PROGRAM_STAGES];
} HeatProgram;
/* Target configuration / display only. No uncalibrated temperature feedback,
 * stage advancement, PID, power limiting or automatic restart is implied. */
void Program_Init(void);
int Program_Apply(const HeatProgram *program);
const HeatProgram *Program_Get(void);
#endif
