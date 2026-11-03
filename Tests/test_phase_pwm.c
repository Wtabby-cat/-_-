/* Phase-shift bridge pulses have long zero plateaus. Check asynchronous
 * sample positions across the full supported search range, not just a sine. */
#include "ih_phase.h"
#include "ih_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
  static const float freqs[]={11000,15000,20100,25000,30000,40000};
  static const float widths[]={45,60,90,120,170,179.9f,180};
  uint32_t pairs[256]; PhaseResult result;
  unsigned f,w,offset,i,invalid=0,total=0; float max_error=0;
  for(f=0;f<sizeof(freqs)/sizeof(freqs[0]);++f)
    for(w=0;w<sizeof(widths)/sizeof(widths[0]);++w)
      for(offset=0;offset<32;++offset) {
        float limit=cosf(widths[w]*0.01745329252f/2.0f);
        for(i=0;i<256;++i) {
          float theta=6.28318530718f*(freqs[f]*(float)i/IH_ADC_REGULAR_SAMPLE_HZ+(float)offset/32.0f);
          float v=sinf(theta+10.0f*0.01745329252f);
          int ri=(int)lroundf(1800+200*sinf(theta));
          int rv=2200+(v>limit ? 400 : v < -limit ? -400 : 0);
          pairs[i]=(uint32_t)ri|((uint32_t)rv<<16);
        }
        ++total;
        if(!Phase_Calculate(pairs,256,freqs[f],&result)) ++invalid;
        if(fabsf(result.degrees-10)>max_error) max_error=fabsf(result.degrees-10);
      }
  printf("PWM phase fit: max error %.3f degrees; invalid %u/%u\n",max_error,invalid,total);
  assert(invalid==0 && max_error<3.0f);
  return 0;
}
