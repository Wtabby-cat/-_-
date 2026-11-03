#ifndef IH_PROTOCOL_H
#define IH_PROTOCOL_H
#include <stdint.h>

typedef struct {
  uint8_t type; /* 1=request, 2=parameter, 0x65=touch, 0x66=page, 0x71=value */
  uint8_t page;
  uint8_t id;
  int32_t value;
} HmiEvent;
typedef struct {
  uint8_t data[12];
  uint8_t used;
} HmiParser;
void HmiParser_Reset(HmiParser *parser);
/* Fixed lengths preserve embedded FF FF FF in signed numeric payloads. */
int HmiParser_Feed(HmiParser *parser, uint8_t byte, HmiEvent *event);
#endif
