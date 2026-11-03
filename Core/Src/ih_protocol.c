#include "ih_protocol.h"
#include <string.h>

void HmiParser_Reset(HmiParser *p) { p->used = 0U; }

static uint8_t FrameLength(const HmiParser *p)
{
  switch (p->data[0]) {
  case 0xA5:
    if (p->used < 2U) return 12U;
    if (p->data[1] != 0x5A) return 0U;
    if (p->used < 3U) return 12U;
    if (p->data[2] == 1U) return 8U;
    if (p->data[2] == 2U) return 12U;
    return 0U;
  case 0x65: return 7U;
  case 0x66: return 5U;
  case 0x71: return 8U;
  case 0x70: return 0U; /* no string queries are issued */
  case 0x88: case 0x00: case 0x01: case 0x02: case 0x03:
  case 0x04: case 0x05: case 0x11: case 0x12: case 0x1A:
  case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
  case 0x20: case 0x23: case 0x24: return 4U;
  default: return 0U;
  }
}

int HmiParser_Feed(HmiParser *p, uint8_t byte, HmiEvent *e)
{
  uint8_t n;
  uint8_t *d = p->data;
  uint32_t value;
  if (p->used >= sizeof(p->data)) p->used = 0U;
  d[p->used++] = byte;
  while (p->used != 0U) {
    n = FrameLength(p);
    if (n != 0U && p->used < n) return 0;
    if (n != 0U && d[n-1U] == 0xFFU && d[n-2U] == 0xFFU && d[n-3U] == 0xFFU) {
      memset(e, 0, sizeof(*e));
      e->type = d[0];
      if (d[0] == 0xA5) {
        e->type = d[2]; e->page = d[3]; e->id = d[4];
      } else if (d[0] == 0x65 || d[0] == 0x66) {
        e->page = d[1]; e->id = d[2];
        if (d[0] == 0x65) e->value = d[3];
      }
      if (e->type == 2U || e->type == 0x71U) {
        uint8_t offset = (e->type == 2U) ? 5U : 1U;
        value = (uint32_t)d[offset] | ((uint32_t)d[offset+1U] << 8)
              | ((uint32_t)d[offset+2U] << 16) | ((uint32_t)d[offset+3U] << 24);
        memcpy(&e->value, &value, sizeof(value));
      }
      p->used = 0U;
      return 1;
    }
    --p->used;
    memmove(d, d+1, p->used);
  }
  return 0;
}
