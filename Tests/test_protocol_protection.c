#include "ih_protocol.h"
#include "ih_protection.h"
#include <assert.h>
#include <stdio.h>

static void protocol(void)
{
  HmiParser p = {{0},0}; HmiEvent e;
  const uint8_t stream[] = {
    0x12,0x34,0xFF, /* noise */
    0x65,0,38,0,0xFF,0xFF,0xFF,
    0xA5,0x5A,1,0,1,0xFF,0xFF,0xFF,
    0x71,0xF1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, /* -15 */
    0xA5,0x5A,2,5,36,28,0,0,0,0xFF,0xFF,0xFF,
    0xA5,0x5A,1,5,2,0xFF,0xFF,0xFF,
    0x66,7,0xFF,0xFF,0xFF,
    0x01,0xFF,0xFF,0xFF,
    0x88,0xFF,0xFF,0xFF
  };
  unsigned i, seen=0;
  for(i=0;i<sizeof(stream);++i) if(HmiParser_Feed(&p,stream[i],&e)) {
    switch(seen++) {
    case 0: assert(e.type==0x65 && e.page==0 && e.id==38); break;
    case 1: assert(e.type==1 && e.page==0 && e.id==1); break;
    case 2: assert(e.type==0x71 && e.value==-15); break;
    case 3: assert(e.type==2 && e.page==5 && e.id==36 && e.value==28); break;
    case 4: assert(e.type==1 && e.page==5 && e.id==2); break;
    case 5: assert(e.type==0x66 && e.page==7); break;
    case 6: assert(e.type==1 && e.page==0 && e.id==0); break;
    case 7: assert(e.type==0x88); break;
    default: assert(0);
    }
  }
  assert(seen==8);
  /* Broken trailer followed by a complete stop request must recover. */
  {
    const uint8_t corrupt[] = {0xA5,0x5A,1,0,1,0,0,0,
                               0xA5,0x5A,1,0,2,0xFF,0xFF,0xFF};
    int stops=0;
    HmiParser_Reset(&p);
    for(i=0;i<sizeof(corrupt);++i)
      if(HmiParser_Feed(&p,corrupt[i],&e) && e.type==1 && e.id==2) ++stops;
    assert(stops==1);
  }
}
static void protection(void)
{
  ProtectionParams p={28,18,10,85,90,150};
  ProtectionInputs in={24.0f,2.0f,1,1,0,0,1};
  assert(Protection_ParamsValid(&p));
  assert(Protection_Evaluate(&p,&in)==0);
  in.bus_v=28.0f; assert(Protection_Evaluate(&p,&in)==IH_FAULT_OV);
  in.bus_v=18.0f; assert(Protection_Evaluate(&p,&in)==IH_FAULT_UV);
  in.running=0; assert(Protection_Evaluate(&p,&in)==0);
  in.running=1; in.bus_v=24.0f; in.current_peak_a=10.0f;
  assert(Protection_Evaluate(&p,&in)==IH_FAULT_OC);
  in.bus_valid=0; in.adc_valid=0;
  assert(Protection_Evaluate(&p,&in)==(IH_FAULT_BUS|IH_FAULT_ADC));
  in.hw_ov=1; in.hw_oc=1;
  assert(Protection_Evaluate(&p,&in)==(IH_FAULT_BUS|IH_FAULT_ADC|IH_FAULT_OV|IH_FAULT_OC));
  in.running=0; assert(Protection_Evaluate(&p,&in)==(IH_FAULT_OV|IH_FAULT_OC));
  p.uv=p.ov; assert(!Protection_ParamsValid(&p));
  p.uv=18; p.oc=-1; assert(!Protection_ParamsValid(&p));
  p.oc=10; p.ot1=1000; assert(!Protection_ParamsValid(&p));
  /* Hardware trips remain effective even with no saved software settings. */
  { ProtectionParams empty={0}; assert(!Protection_ParamsValid(&empty));
    assert(Protection_Evaluate(&empty,&in)==(IH_FAULT_OV|IH_FAULT_OC)); }
}
int main(void)
{
  protocol(); protection();
  puts("PASS: V8 framing/resynchronization and protection boundary tests");
  return 0;
}
