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
  static const float widths[]={45,60,90};
  uint32_t pairs[256]; PhaseResult result;
  unsigned f,w,offset,i,invalid=0; float max_error=0;
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
        if(!Phase_Calculate(pairs,256,freqs[f],&result)) ++invalid;
        if(fabsf(result.degrees-10)>max_error) max_error=fabsf(result.degrees-10);
      }
  printf("PWM phase fit: max error %.3f degrees; invalid %u/576\n",max_error,invalid);
  assert(invalid==0 && max_error<3.0f);
#if IH_FW_DIAG_VERSION != 110217U
  /* Keep rejection causes distinct; none of these may become a valid phase
   * merely to let the frequency controller continue. */
  for(w=0;w<4;++w) {
    static const uint32_t causes[]={PHASE_CURRENT_WEAK,PHASE_VOLTAGE_WEAK,PHASE_CURRENT_QUALITY,PHASE_VOLTAGE_QUALITY};
    for(i=0;i<256;++i) {
      float theta=6.28318530718f*20100.0f*(float)i/IH_ADC_REGULAR_SAMPLE_HZ;
      int ri=(int)lroundf(1800+(w==0 ? 2 : 200)*sinf(theta*(w==2 ? 2 : 1)));
      int rv=(int)lroundf(2200+(w==1 ? 2 : 400)*sinf(theta*(w==3 ? 2 : 1)));
      pairs[i]=(uint32_t)ri|((uint32_t)rv<<16);
    }
    assert(!Phase_Calculate(pairs,256,20100,&result) && result.reject==causes[w]);
    if(w==0) assert(result.rms_i_adc_v<0.01f && result.rms_v_adc_v>0.01f);
    if(w==1) assert(result.rms_v_adc_v<0.01f && result.rms_i_adc_v>0.01f);
  }
  pairs[0]=0;
  assert(!Phase_Calculate(pairs,256,20100,&result) && result.reject==PHASE_SNAPSHOT_CLIPPED);
  assert(!Phase_Calculate(pairs,256,NAN,&result) && result.reject==PHASE_BAD_CONFIG);
#endif
  return 0;
}
