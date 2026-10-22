// C interface to reSID-fp (taken from b-em, GPL) for the System 5 emulator
#include "resid-fp/sid.h"
static SIDFP *psid;
extern "C" {
void s5sid_init(int sample_rate){
    psid=new SIDFP;
    psid->set_chip_model(MOS6581FP);
    psid->set_voice_nonlinearity(1.0f);
    psid->get_filter().set_distortion_properties(0.f,0.f,0.f);
    /* 6581 R4AR 3789 filter curve (b-em default) */
    psid->get_filter().set_type3_properties(1.40e6f,1.47e8f,1.0059f,1.55e4f);
    psid->enable_filter(true);
    psid->enable_external_filter(true);
    psid->reset();
    for(int c=0;c<32;c++) psid->write(c,0);
    psid->set_sampling_parameters(1000000.0f,SAMPLE_RESAMPLE_INTERPOLATE,(float)sample_rate,0.9f*(float)sample_rate/2.0f);
}
void s5sid_reset(void){ psid->reset(); for(int c=0;c<32;c++) psid->write(c,0); }
void s5sid_write(int r,int v){ psid->write(r&31,(unsigned char)v); }
int  s5sid_read(int r){ return psid->read(r&31); }
int  s5sid_clock(long cycles,short*buf,int maxn){
    cycle_count d=(cycle_count)cycles; int n=0;
    while(d>0){
        if(n>=maxn){ static short junk[1024]; while(d>0) psid->clock(d,junk,1024,1); break; }
        int r=psid->clock(d,buf+n,maxn-n,1); n+=r;
    }
    return n;
}
void s5sid_advance(long cycles){ static short junk[1024]; cycle_count d=(cycle_count)cycles; while(d>0) psid->clock(d,junk,1024,1); }
}
