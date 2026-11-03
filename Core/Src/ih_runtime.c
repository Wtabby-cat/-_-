#include "ih_app.h"

/* The firmware has no host console or command line. In particular, a fatal
 * C-library exit must disable the gate instead of issuing BKPT 0xAB and
 * depending on whether a debugger is attached. */
#if defined(__CC_ARM)
#include <rt_sys.h>
#pragma import(__use_no_semihosting)

volatile uint32_t dbg_runtime_exited;
volatile int dbg_runtime_exit_code;

void _sys_exit(int code)
{
  dbg_runtime_exit_code = code;
  dbg_runtime_exited = 1U;
  App_EmergencyOff();
  __disable_irq();
  for (;;) { }
}

void _ttywrch(int ch)
{
  (void)ch; /* USART1 is reserved for the HMI protocol. */
}

char *_sys_command_string(char *cmd, int len)
{
  (void)cmd; (void)len;
  return (char *)0;
}
#endif
