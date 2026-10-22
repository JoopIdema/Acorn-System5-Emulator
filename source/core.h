/* Acorn System 5 emulator core - 6502, 6522 x2, 6845 + teletext, 8271, 6581 SID */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "assets.h"

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int64_t i64;

/* ---------------------------------------------------------------- memory */
static u8 ram[0xE000];
static u8 rom_os[0x2000], rom_basic[0x4000];
/* ---- machine configuration (two models share the same chips) ---- */
enum { M_CUSTOM=0, M_STANDARD=1 };
static int cfg_model=M_CUSTOM;     /* custom System 5 (IMADOS, 40x25, SID)  or standard System 5 (AcornDOS) */
static int cfg_vdu=0;              /* standard model: 0 = 40x25 teletext, 1 = 80x25 mono */
static int cfg_basic=1;            /* standard model: System BASIC ROM at &C000-&CFFF */
static int cfg_fp=1;               /* standard model: floating point ROM at &D000-&DFFF */
static int cfg_mono=0;             /* 80x25 display colour: 0 white, 1 green, 2 amber */
static u8 rom_dos40[0x1000],rom_dos80[0x1000],rom_sbasic[0x1000],rom_sfp[0x1000],font80[0x800];
static i64 cyc;                 /* total CPU cycles since start */

/* ---------------------------------------------------------------- disks  */
static i64 motor_until;          /* drive motor / ready line: ready only while the motor is running */
typedef struct { u8 *data; long size; char path[512]; int mounted; } Disk;
static Disk disk[2];

static void disk_eject(int d){ if(disk[d].data) free(disk[d].data); memset(&disk[d],0,sizeof(Disk)); motor_until=0; }
static int disk_mount(int d,const char*path){
    FILE*f=fopen(path,"rb"); if(!f) return 0;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    u8*buf=(u8*)malloc(sz>0?sz:1); if(!buf){fclose(f);return 0;}
    if(fread(buf,1,sz,f)!=(size_t)sz){free(buf);fclose(f);return 0;}
    fclose(f);
    disk_eject(d);
    disk[d].data=buf; disk[d].size=sz; disk[d].mounted=1; motor_until=0;   /* disk change: ready line drops, so IMADOS re-reads the catalogue */
    strncpy(disk[d].path,path,sizeof(disk[d].path)-1);
    return 1;
}
static void disk_flush_sector(int d,long off){
    FILE*f=fopen(disk[d].path,"r+b"); if(!f) return;
    fseek(f,off,SEEK_SET); fwrite(disk[d].data+off,1,256,f); fclose(f);
}

/* ---------------------------------------------------------------- 6522   */
typedef struct {
    u8 orb,ora,ddrb,ddra,acr,pcr,ifr,ier,sr;
    u16 t1c,t1l,t2c; u8 t2ll; int t1armed,t1reload,t2armed;
} VIA;
static VIA via1,via2;
static FILE*printer_fp;

static void via_reset(VIA*v){ memset(v,0,sizeof(*v)); v->t1c=v->t1l=v->t2c=0xFFFF; }
static void via_tick(VIA*v,int n){
    while(n--){
        if(v->t1reload){ v->t1c=v->t1l; v->t1reload=0; }
        else{
            v->t1c--;
            if(v->t1c==0xFFFF){
                if(v->t1armed){ v->ifr|=0x40; if(!(v->acr&0x40)) v->t1armed=0; }
                if(v->acr&0x40) v->t1reload=1;
            }
        }
        if(!(v->acr&0x20)){
            v->t2c--;
            if(v->t2c==0xFFFF && v->t2armed){ v->ifr|=0x20; v->t2armed=0; }
        }
    }
}
static int via_irq(VIA*v){ return (v->ifr&v->ier&0x7F)!=0; }

/* keyboard */
static int kbq[8192],kbq_h,kbq_t; static int key_cur=0xFF; static int key_timer,key_state; /* 0 idle,1 hold,2 gap */
static int boot_hold,boot_rel;
static void kb_push(int c){ int n=(kbq_t+1)&8191; if(n!=kbq_h){ kbq[kbq_t]=c&0x7F; kbq_t=n; } }
static void kb_tick(int n){
    if(boot_rel>0){ boot_rel-=n; if(boot_rel<=0){ boot_rel=0; boot_hold=0; key_cur=0xFF; key_state=0; } return; }
    if(boot_hold) return;
    if(key_timer>0){ key_timer-=n; if(key_timer>0) return; }
    if(key_state==1){ key_cur=0xFF; key_state=2; key_timer=25000; return; }
    key_state=0;
    if(kbq_h!=kbq_t){ key_cur=kbq[kbq_h]; kbq_h=(kbq_h+1)&8191; key_state=1; key_timer=45000; }
}

static u8 via_read(VIA*v,int r,int isv1){
    switch(r&15){
    case 0: return (v->orb&v->ddrb)|(0xFF&~v->ddrb);
    case 1: case 15: {
        u8 in= isv1 ? (u8)key_cur : 0x00;
        if(isv1 && boot_hold && boot_rel==0) boot_rel=150000;   /* OS has sampled the boot key */
        return (v->ora&v->ddra)|(in&~v->ddra); }
    case 2: return v->ddrb; case 3: return v->ddra;
    case 4: v->ifr&=~0x40; return v->t1c&0xFF;
    case 5: return v->t1c>>8;
    case 6: return v->t1l&0xFF; case 7: return v->t1l>>8;
    case 8: v->ifr&=~0x20; return v->t2c&0xFF;
    case 9: return v->t2c>>8;
    case 10: return v->sr; case 11: return v->acr; case 12: return v->pcr;
    case 13: return v->ifr | ((v->ifr&v->ier&0x7F)?0x80:0);
    case 14: return v->ier|0x80;
    }
    return 0xFF;
}
static void via_write(VIA*v,int r,u8 d,int isv1){
    switch(r&15){
    case 0: v->orb=d; break;
    case 1: case 15: v->ora=d; break;
    case 2: v->ddrb=d; break; case 3: v->ddra=d; break;
    case 4: case 6: v->t1l=(v->t1l&0xFF00)|d; break;
    case 5: v->t1l=(v->t1l&0x00FF)|(d<<8); v->t1c=v->t1l; v->ifr&=~0x40; v->t1armed=1; v->t1reload=0; break;
    case 7: v->t1l=(v->t1l&0x00FF)|(d<<8); v->ifr&=~0x40; break;
    case 8: v->t2ll=d; break;
    case 9: v->t2c=(d<<8)|v->t2ll; v->ifr&=~0x20; v->t2armed=1; break;
    case 10: v->sr=d; break;
    case 11: v->acr=d; break;
    case 12:
        if(!isv1 && (d&0x0E)==0x0C && (v->pcr&0x0E)!=0x0C){   /* CA2 strobe low = printer character */
            if(!printer_fp) printer_fp=fopen("printer.txt","ab");
            if(printer_fp){ fputc(v->ora&0x7F,printer_fp); fflush(printer_fp); }
        }
        v->pcr=d; break;
    case 13: v->ifr&=~(d&0x7F); break;
    case 14: if(d&0x80) v->ier|=d&0x7F; else v->ier&=~(d&0x7F); break;
    }
}

/* ---------------------------------------------------------------- 6845   */
static u8 crtc_reg[32],crtc_addr;
static int crtc_cursor_phase;

/* ---------------------------------------------------------------- SID    */
/* 6581 emulation by reSID-fp (as used in b-em, GPL), see resid_wrap.cc */
#ifdef __cplusplus
extern "C" {
#endif
void s5sid_init(int rate); void s5sid_reset(void); void s5sid_write(int r,int v); int s5sid_read(int r);
int s5sid_clock(long cycles,short*buf,int maxn); void s5sid_advance(long cycles);
#ifdef __cplusplus
}
#endif
#ifdef NO_RESID
void s5sid_init(int rate){} void s5sid_reset(void){} void s5sid_write(int r,int v){} int s5sid_read(int r){return 0;}
int s5sid_clock(long c,short*b,int m){return 0;} void s5sid_advance(long c){}
#endif
static short sidbuf[8192]; static int sidn; static i64 sid_last; static int sid_nooutput;
static void sid_reset(void){ s5sid_reset(); sid_last=cyc; }
static void sid_run(i64 target){
    long d=(long)(target-sid_last); sid_last=target;
    if(d<=0) return;
    if(sid_nooutput) s5sid_advance(d);
    else sidn+=s5sid_clock(d,sidbuf+sidn,8192-sidn);
}
static void sid_write(int r,u8 d){ sid_run(cyc); s5sid_write(r&31,d); }
static u8 sid_read(int r){ sid_run(cyc); return (u8)s5sid_read(r&31); }

/* ---------------------------------------------------------------- 8271   */
enum { ST_IDLE,ST_SEEK,ST_XR,ST_XW,ST_SECDONE,ST_VER,ST_XF,ST_SEEKONLY,ST_FMTDONE };
typedef struct {
    u8 status,result,cmd,par[8],spec[64],sbuf[256],idbuf[4],data;
    int np,nreq,state,delay,drv,trk,sec,left,bp,fmt_n,fmt_ids[32],fmt_got,havecmd,activity;
} FDC;
static FDC fdc;
static int fdc_nmi_level(void){ return (fdc.status&0x0C)!=0; }
static void fdc_finish(int r){ fdc.result=r; fdc.status=0x18; fdc.state=ST_IDLE; fdc.delay=0; }
static long fdc_off(int d,int t,int s){ if(s<0||s>=10||t<0) return -1; long o=((long)t*10+s)*256; if(!disk[d].mounted||o+256>disk[d].size) return -1; return o; }
static void fdc_reset(void){ memset(&fdc,0,sizeof fdc); motor_until=0; }
static void fdc_load_sector(void){
    long o=fdc_off(fdc.drv,fdc.trk,fdc.sec);
    if(o<0){ fdc_finish(0x18); return; }
    memcpy(fdc.sbuf,disk[fdc.drv].data+o,256); fdc.bp=0; fdc.delay=300; fdc.state=ST_XR;
}
static void fdc_exec(void){
    FDC*f=&fdc; int c=f->cmd&0x3F; f->drv=(f->cmd&0x80)?1:0; int hasdrv=(f->cmd&0xC0)!=0;
    switch(c){
    case 0x0A: case 0x0B: case 0x12: case 0x13: case 0x16: case 0x17: case 0x1E: case 0x1F: {
        f->trk=f->par[0]; f->sec=f->par[1]; f->left=f->par[2]&0x1F; if(!f->left) f->left=1;
        if(!hasdrv||!disk[f->drv].mounted){ f->state=ST_SEEK; f->delay=2000; f->activity=-1; return; }
        int rg=f->drv?0x1A:0x12; int steps=abs(f->trk-f->spec[rg]); f->spec[rg]=f->trk;
        f->delay=1500+steps*2500; f->state=ST_SEEK; f->activity=f->drv+1; break; }
    case 0x23: f->trk=f->par[0]; f->fmt_n=f->par[2]&0x1F; f->fmt_got=0; f->bp=0;
        if(!hasdrv||!disk[f->drv].mounted){ f->state=ST_SEEK; f->delay=2000; f->activity=-1; return; }
        f->spec[f->drv?0x1A:0x12]=f->trk; f->delay=3000; f->state=ST_SEEK; f->activity=f->drv+1; break;
    case 0x29: { int rg=f->drv?0x1A:0x12; int steps=abs(f->par[0]-f->spec[rg]); f->spec[rg]=f->par[0];
        f->delay=1500+steps*2500; f->state=ST_SEEKONLY; break; }
    case 0x2C: {
        int run=(cyc<motor_until);
        u8 r=0x80; if(disk[0].mounted&&run) r|=0x04; if(disk[1].mounted&&run) r|=0x40;
        if(f->spec[f->drv?0x1A:0x12]==0) r|=0x08;
        f->result=r; f->status=0x10; f->state=ST_IDLE; break; }
    case 0x35: f->status=0; f->state=ST_IDLE; break;
    case 0x3A: f->spec[f->par[0]&63]=f->par[1]; f->status=0; f->state=ST_IDLE;
        if((f->par[0]&63)==0x23 && (f->par[1]&0xC8)) motor_until=cyc+3000000;   /* select/head load starts the motor (~3 s timeout) */
        break;
    case 0x3D: f->result=f->spec[f->par[0]&63]; f->status=0x10; f->state=ST_IDLE; break;
    default: fdc_finish(0x18); break;
    }
}
static int fdc_nparams(int cmd){
    switch(cmd&0x3F){
    case 0x0A: case 0x0B: case 0x12: case 0x13: case 0x16: case 0x17: case 0x1E: case 0x1F: case 0x1B: return 3;
    case 0x23: return 5; case 0x29: return 1; case 0x2C: return 0; case 0x35: return 4;
    case 0x3A: return 2; case 0x3D: return 1; case 0x00: case 0x04: return 5;
    }
    return 0;
}
static void fdc_event(void){
    FDC*f=&fdc;
    switch(f->state){
    case ST_SEEK: {
        int c=f->cmd&0x3F;
        if(f->activity<0){ fdc_finish(0x10); break; }
        if(c==0x23){ f->bp=0; f->state=ST_XF; f->delay=64; break; }
        if(c==0x1E||c==0x1F){ f->state=ST_VER; f->delay=1500; break; }
        if(c==0x0A||c==0x0B){ if(fdc_off(f->drv,f->trk,f->sec)<0){ fdc_finish(0x18); break; } f->bp=0; f->state=ST_XW; f->delay=64; break; }
        fdc_load_sector(); break; }
    case ST_SEEKONLY: fdc_finish(0); break;
    case ST_XR:
        if(f->status&0x04){ f->delay=16; break; }
        f->data=f->sbuf[f->bp++]; f->status=0x8C; break;
    case ST_XW:
        f->status=0x8C; break;
    case ST_SECDONE:
        f->left--; f->sec++;
        if(f->left<=0){ fdc_finish(0); break; }
        { int c=f->cmd&0x3F;
          if(c==0x0A||c==0x0B){ if(fdc_off(f->drv,f->trk,f->sec)<0){ fdc_finish(0x18); break; } f->bp=0; f->state=ST_XW; f->delay=300; }
          else fdc_load_sector(); }
        break;
    case ST_VER:
        if(fdc_off(f->drv,f->trk,f->sec)<0){ fdc_finish(0x18); break; }
        f->left--; f->sec++; if(f->left<=0) fdc_finish(0); else f->delay=1500; break;
    case ST_XF:
        f->status=0x8C; break;
    case ST_FMTDONE: fdc_finish(0); break;
    default: break;
    }
}
static void fdc_tick(int n){
    if(fdc.state!=ST_IDLE) motor_until=cyc+3000000;
    if(fdc.delay>0){ fdc.delay-=n; if(fdc.delay<=0){ fdc.delay=0; fdc_event(); } }
}
static u8 fdc_read(int r){
    FDC*f=&fdc;
    switch(r&7){
    case 0: return f->status;
    case 1: f->status&=~0x18; return f->result;
    case 4: {
        u8 d=f->data;
        if(f->state==ST_XR && (f->status&0x04)){
            f->status&=~0x0C;
            if(f->bp>=256){ f->state=ST_SECDONE; f->delay=100; } else f->delay=64;
        } else f->status&=~0x0C;
        return d; }
    }
    return 0;
}
static void fdc_write(int r,u8 d){
    FDC*f=&fdc;
    switch(r&7){
    case 0:
        if(f->status&0x80) break;
        f->cmd=d; f->np=0; f->nreq=fdc_nparams(d); f->status=0x80; f->havecmd=1;
        if(f->nreq==0) fdc_exec();
        break;
    case 1:
        if(f->havecmd && f->np<f->nreq){
            f->par[f->np++]=d;
            if(f->np==f->nreq){ f->havecmd=0; fdc_exec(); }
        }
        break;
    case 2: if(d&1){ memset(f->spec,0,sizeof f->spec); f->state=ST_IDLE; f->status=0; f->delay=0; f->havecmd=0; } break;
    case 4:
        if(f->state==ST_XW && (f->status&0x04)){
            f->sbuf[f->bp++]=d; f->status&=~0x0C;
            if(f->bp>=256){
                long o=fdc_off(f->drv,f->trk,f->sec);
                if(o>=0){ memcpy(disk[f->drv].data+o,f->sbuf,256); disk_flush_sector(f->drv,o); }
                f->state=ST_SECDONE; f->delay=100;
            } else f->delay=64;
        } else if(f->state==ST_XF && (f->status&0x04)){
            f->idbuf[f->bp++]=d; f->status&=~0x0C;
            if(f->bp>=4){
                f->fmt_ids[f->fmt_got&31]=f->idbuf[2]; f->fmt_got++; f->bp=0;
                if(f->fmt_got>=f->fmt_n){
                    for(int i=0;i<f->fmt_n;i++){
                        long o=fdc_off(f->drv,f->trk,f->fmt_ids[i]);
                        if(o>=0){ memset(disk[f->drv].data+o,0xE5,256); disk_flush_sector(f->drv,o); }
                    }
                    f->state=ST_FMTDONE; f->delay=60000;
                } else f->delay=64;
            } else f->delay=64;
        }
        break;
    }
}

static u8 crtc_read(int r){ return (r&1)? ((crtc_addr>=12&&crtc_addr<=17)?crtc_reg[crtc_addr]:0) : 0; }
static void crtc_write(int r,u8 d){ if(r&1){ if(crtc_addr<18) crtc_reg[crtc_addr]=d; } else crtc_addr=d&31; }

/* ---------------------------------------------------------------- bus    */
#ifdef FLATMEM
static u8 flat[65536];
#endif
/* standard Acorn System 5: RAM 0000-07FF, I/O 0800-0FFF, 80x25 video RAM 1000-17FF (CRTC at 1840),
   RAM 2000-BFFF, BASIC 4K at C000, FP 4K at D000 (RAM when the ROM is switched off), DOS ROM at F000 */
static u8 std_read(u16 a){
    if(a<0x0800) return ram[a];
    if(a<0x1000){
        if(a<0x0808) return cfg_vdu==0 ? crtc_read(a) : 0xFF;
        if(a>=0x0900&&a<0x0A00) return 0xFF;                   /* Econet not fitted */
        if(a>=0x0A00&&a<0x0B00) return fdc_read(a&7);
        if(a>=0x0C00&&a<0x0C20) return via_read(&via2,a&15,0);
        if(a>=0x0C20&&a<0x0C40) return (a&1)?0x00:0x02;        /* 6850 ACIA: transmitter empty */
        if(a>=0x0E00&&a<0x0F00) return via_read(&via1,a&15,1);
        return 0xFF;
    }
    if(a<0x1800) return ram[a];
    if(a>=0x1840&&a<0x1842) return cfg_vdu==1 ? crtc_read(a) : 0xFF;
    if(a<0x2000) return 0xFF;
    if(a<0xC000) return ram[a];
    if(a<0xD000) return cfg_basic? rom_sbasic[a-0xC000] : ram[a];
    if(a<0xE000) return cfg_fp? rom_sfp[a-0xD000] : ram[a];
    if(a<0xF000) return 0xFF;                                  /* NOS ROM not fitted */
    return (cfg_vdu? rom_dos80:rom_dos40)[a-0xF000];
}
static void std_write(u16 a,u8 d){
    if(a<0x0800){ ram[a]=d; return; }
    if(a<0x1000){
        if(a<0x0808){ if(cfg_vdu==0) crtc_write(a,d); return; }
        if(a>=0x0A00&&a<0x0B00){ fdc_write(a&7,d); return; }
        if(a>=0x0C00&&a<0x0C20){ via_write(&via2,a&15,d,0); return; }
        if(a>=0x0E00&&a<0x0F00){ via_write(&via1,a&15,d,1); return; }
        return;
    }
    if(a<0x1800){ ram[a]=d; return; }
    if(a>=0x1840&&a<0x1842){ if(cfg_vdu==1) crtc_write(a,d); return; }
    if(a<0x2000) return;
    if(a<0xC000){ ram[a]=d; return; }
    if(a<0xD000){ if(!cfg_basic) ram[a]=d; return; }
    if(a<0xE000){ if(!cfg_fp) ram[a]=d; return; }
}
static u8 mem_read(u16 a){
#ifdef FLATMEM
    return flat[a];
#endif
    if(cfg_model==M_STANDARD) return std_read(a);
    if(a<0x0800) return ram[a];
    if(a<0x1000){
        if(a<0x0808) return (a&1)? ((crtc_addr>=12&&crtc_addr<=17)?crtc_reg[crtc_addr]:0) : 0;
        if(a>=0x0A00&&a<0x0A08) return fdc_read(a);
        if(a>=0x0C00&&a<0x0C10) return via_read(&via2,a,0);
        if(a>=0x0C60&&a<0x0C80) return sid_read(a);
        if(a>=0x0E20&&a<0x0E30) return via_read(&via1,a,1);
        return ram[a];
    }
    if(a<0xA000) return ram[a];
    if(a<0xE000) return rom_basic[a-0xA000];
    return rom_os[a-0xE000];
}
static void mem_write(u16 a,u8 d){
#ifdef FLATMEM
    flat[a]=d; return;
#endif
    if(cfg_model==M_STANDARD){ std_write(a,d); return; }
    if(a<0x0800){ ram[a]=d; return; }
    if(a<0x1000){
        if(a<0x0808){ if(a&1){ if(crtc_addr<18) crtc_reg[crtc_addr]=d; } else crtc_addr=d&31; return; }
        if(a>=0x0A00&&a<0x0A08){ fdc_write(a,d); return; }
        if(a>=0x0C00&&a<0x0C10){ via_write(&via2,a,d,0); return; }
        if(a>=0x0C60&&a<0x0C80){ sid_write(a,d); return; }
        if(a>=0x0E20&&a<0x0E30){ via_write(&via1,a,d,1); return; }
        ram[a]=d; return;
    }
    if(a<0xA000) ram[a]=d;
}

static void load_default_roms(void){
    memcpy(rom_os,rom_os_def,sizeof rom_os); memcpy(rom_basic,rom_basic_def,sizeof rom_basic);
    memcpy(rom_dos40,dos40_def,sizeof rom_dos40); memcpy(rom_dos80,dos80_def,sizeof rom_dos80);
    memcpy(rom_sbasic,sbasic_def,sizeof rom_sbasic); memcpy(rom_sfp,sfp_def,sizeof rom_sfp);
    memcpy(font80,font80_def,sizeof font80);
}

/* ---------------------------------------------------------------- 6502   */
static u8 A,X,Y,S,P; static u16 PC;
static int nmi_prev,nmi_pend;
#define FC 0x01
#define FZ 0x02
#define FI 0x04
#define FD 0x08
#define FB 0x10
#define FU 0x20
#define FV 0x40
#define FN 0x80
static inline u8 rd(u16 a){ return mem_read(a); }
static inline void wr(u16 a,u8 d){ mem_write(a,d); }
static inline void push(u8 d){ wr(0x100+S,d); S--; }
static inline u8 pull(void){ S++; return rd(0x100+S); }
static inline void setnz(u8 v){ P&=~(FN|FZ); if(!v) P|=FZ; P|=v&0x80; }
static void cpu_reset(void){ A=X=Y=0; S=0xFD; P=FI|FU; PC=rd(0xFFFC)|(rd(0xFFFD)<<8); nmi_prev=0; nmi_pend=0; }

static void do_adc(u8 v){
    if(P&FD){
        int lo=(A&0xF)+(v&0xF)+(P&FC); int hi=(A&0xF0)+(v&0xF0);
        int bin=A+v+(P&FC);
        P&=~(FZ); if(!(bin&0xFF)) P|=FZ;
        if(lo>9) lo+=6;
        if(lo>0x0F) hi+=0x10;
        P&=~(FN|FV); P|=hi&0x80; if(~(A^v)&(A^hi)&0x80) P|=FV;
        if(hi>0x90) hi+=0x60;
        P&=~FC; if(hi>0xFF) P|=FC;
        A=(lo&0x0F)|(hi&0xF0);
    } else {
        int r=A+v+(P&FC);
        P&=~(FC|FV); if(r>0xFF) P|=FC;
        if(~(A^v)&(A^r)&0x80) P|=FV;
        A=r; setnz(A);
    }
}
static void do_sbc(u8 v){
    int bin=A-v-(1-(P&FC));
    if(P&FD){
        int lo=(A&0xF)-(v&0xF)-(1-(P&FC)); int hi=(A>>4)-(v>>4);
        if(lo<0){ lo-=6; hi--; }
        if(hi<0) hi-=6;
        P&=~(FC|FV|FZ|FN);
        if(bin>=0) P|=FC;
        if((A^v)&(A^bin)&0x80) P|=FV;
        if(!(bin&0xFF)) P|=FZ; P|=bin&0x80;
        A=((hi<<4)&0xF0)|(lo&0x0F);
    } else {
        P&=~(FC|FV); if(bin>=0) P|=FC;
        if((A^v)&(A^bin)&0x80) P|=FV;
        A=bin; setnz(A);
    }
}
static void do_cmp(u8 r,u8 v){ int t=r-v; P&=~FC; if(r>=v) P|=FC; setnz((u8)t); }
static u8 do_asl(u8 v){ P&=~FC; P|=v>>7; v<<=1; setnz(v); return v; }
static u8 do_lsr(u8 v){ P&=~FC; P|=v&1; v>>=1; setnz(v); return v; }
static u8 do_rol(u8 v){ u8 c=P&FC; P&=~FC; P|=v>>7; v=(v<<1)|c; setnz(v); return v; }
static u8 do_ror(u8 v){ u8 c=P&FC; P&=~FC; P|=v&1; v=(v>>1)|(c<<7); setnz(v); return v; }

static void do_interrupt(u16 vec,int brk){
    push(PC>>8); push(PC&0xFF); push((P&~FB)|FU|(brk?FB:0)); P|=FI; PC=rd(vec)|(rd(vec+1)<<8);
}
static int irq_line(void){ return via_irq(&via1)||via_irq(&via2); }

/* executes one instruction (or interrupt entry), returns cycles */
static int cpu_step(void){
    int nl=fdc_nmi_level(); if(nl&&!nmi_prev) nmi_pend=1; nmi_prev=nl;
    if(nmi_pend){ nmi_pend=0; do_interrupt(0xFFFA,0); return 7; }
    if(irq_line() && !(P&FI)){ do_interrupt(0xFFFE,0); return 7; }

    u8 op=rd(PC++); int cy=2; u16 ea=0; int cross=0; u8 v;
    int cc=op&3, bbb=(op>>2)&7, aaa=op>>5;

    #define FETCH8() rd(PC++)
    #define FETCH16() (PC+=2, rd(PC-2)|(rd(PC-1)<<8))
    if(op==0x00){ PC++; do_interrupt(0xFFFE,1); return 7; }
    if(op==0x20){ u16 t=FETCH16(); PC--; push(PC>>8); push(PC&0xFF); PC=t; return 6; }
    if(op==0x40){ P=(pull()&~FB)|FU; PC=pull(); PC|=pull()<<8; return 6; }
    if(op==0x60){ PC=pull(); PC|=pull()<<8; PC++; return 6; }
    if((op&0x1F)==0x10){   /* branches */
        int8_t off=(int8_t)FETCH8(); int cond;
        switch(aaa){ case 0: cond=!(P&FN); break; case 1: cond=P&FN; break; case 2: cond=!(P&FV); break; case 3: cond=P&FV;break;
                     case 4: cond=!(P&FC); break; case 5: cond=P&FC; break; case 6: cond=!(P&FZ); break; default: cond=P&FZ; }
        if(cond){ u16 t=PC+off; cy=3; if((t^PC)&0xFF00) cy=4; PC=t; }
        return cy;
    }
    switch(op){
    case 0x08: push(P|FB|FU); return 3;
    case 0x28: P=(pull()&~FB)|FU; return 4;
    case 0x48: push(A); return 3;
    case 0x68: A=pull(); setnz(A); return 4;
    case 0x88: Y--; setnz(Y); return 2;
    case 0xA8: Y=A; setnz(Y); return 2;
    case 0xC8: Y++; setnz(Y); return 2;
    case 0xE8: X++; setnz(X); return 2;
    case 0x18: P&=~FC; return 2; case 0x38: P|=FC; return 2;
    case 0x58: P&=~FI; return 2; case 0x78: P|=FI; return 2;
    case 0x98: A=Y; setnz(A); return 2; case 0xB8: P&=~FV; return 2;
    case 0xD8: P&=~FD; return 2; case 0xF8: P|=FD; return 2;
    case 0x8A: A=X; setnz(A); return 2; case 0x9A: S=X; return 2;
    case 0xAA: X=A; setnz(X); return 2; case 0xBA: X=S; setnz(X); return 2;
    case 0xCA: X--; setnz(X); return 2; case 0xEA: return 2;
    }
    int rmw=0,store=0,acc=0,imm=0;
    if(cc==1){
        store=(aaa==4);
        switch(bbb){
        case 0: { u8 z=(FETCH8()+X)&255; ea=rd(z)|(rd((z+1)&255)<<8); cy=6; break; }
        case 1: ea=FETCH8(); cy=3; break;
        case 2: imm=1; ea=PC++; cy=2; break;
        case 3: ea=FETCH16(); cy=4; break;
        case 4: { u8 z=FETCH8(); u16 b=rd(z)|(rd((z+1)&255)<<8); ea=b+Y; cross=((b^ea)&0xFF00)!=0; cy=5; if(store) cy=6, cross=0; break; }
        case 5: ea=(FETCH8()+X)&255; cy=4; break;
        case 6: { u16 b=FETCH16(); ea=b+Y; cross=((b^ea)&0xFF00)!=0; cy=4; if(store) cy=5,cross=0; break; }
        default:{ u16 b=FETCH16(); ea=b+X; cross=((b^ea)&0xFF00)!=0; cy=4; if(store) cy=5,cross=0; break; }
        }
        cy+=cross;
        if(store){ if(imm) return 2; wr(ea,A); return cy; }
        v=rd(ea);
        switch(aaa){
        case 0: A|=v; setnz(A); break; case 1: A&=v; setnz(A); break; case 2: A^=v; setnz(A); break;
        case 3: do_adc(v); break; case 5: A=v; setnz(A); break; case 6: do_cmp(A,v); break; case 7: do_sbc(v); break;
        }
        return cy;
    }
    if(cc==2){
        int yidx=(aaa==4||aaa==5);
        switch(bbb){
        case 0: imm=1; ea=PC++; cy=2; break;
        case 1: ea=FETCH8(); cy=5; break;
        case 2: acc=1; cy=2; break;
        case 3: ea=FETCH16(); cy=6; break;
        case 5: ea=(FETCH8()+(yidx?Y:X))&255; cy=6; break;
        case 7: { u16 b=FETCH16(); ea=b+(yidx?Y:X); cross=((b^ea)&0xFF00)!=0; cy=7; break; }
        default: return 2;   /* undefined */
        }
        if(aaa==4){ if(acc||imm) return 2; wr(ea,X); return bbb==1?3:4; }
        if(aaa==5){ v=rd(ea); X=v; setnz(X); return imm?2:(bbb==1?3:(bbb==3?4:(bbb==5?4:4+cross))); }
        if(acc){
            switch(aaa){ case 0: A=do_asl(A); break; case 1: A=do_rol(A); break; case 2: A=do_lsr(A); break; case 3: A=do_ror(A); break; case 6: return 2; case 7: return 2; }
            return 2;
        }
        if(imm) return 2;
        v=rd(ea); wr(ea,v);
        switch(aaa){ case 0: v=do_asl(v); break; case 1: v=do_rol(v); break; case 2: v=do_lsr(v); break; case 3: v=do_ror(v); break;
                     case 6: v--; setnz(v); break; case 7: v++; setnz(v); break; }
        wr(ea,v); return cy;
    }
    if(cc==0){
        switch(bbb){
        case 0: imm=1; ea=PC++; cy=2; break;
        case 1: ea=FETCH8(); cy=3; break;
        case 3: ea=FETCH16(); cy=4; break;
        case 5: ea=(FETCH8()+X)&255; cy=4; break;
        case 7: { u16 b=FETCH16(); ea=b+X; cross=((b^ea)&0xFF00)!=0; cy=4+cross; break; }
        default: return 2;
        }
        switch(aaa){
        case 1: if(imm||bbb>3) return cy; v=rd(ea); P&=~(FN|FV|FZ); P|=v&0xC0; if(!(A&v)) P|=FZ; return cy;
        case 2: if(bbb==3){ PC=ea; return 3; } return cy;
        case 3: if(bbb==3){ u16 lo=rd(ea); u16 hi=rd((ea&0xFF00)|((ea+1)&0xFF)); PC=lo|(hi<<8); return 5; } return cy;
        case 4: if(imm||bbb==7) return cy; wr(ea,Y); return cy;
        case 5: v=rd(ea); Y=v; setnz(Y); return cy;
        case 6: v=rd(ea); do_cmp(Y,v); return cy;
        case 7: v=rd(ea); do_cmp(X,v); return cy;
        }
        return cy;
    }
    return 2;
}

/* one instruction plus all peripherals */
static void machine_step(void){
    int n=cpu_step();
    cyc+=n;
    via_tick(&via1,n); via_tick(&via2,n); fdc_tick(n); kb_tick(n);
}
static void machine_reset(int boot){
    via_reset(&via1); via_reset(&via2); fdc_reset(); sid_reset();
    memset(crtc_reg,0,sizeof crtc_reg);
    kbq_h=kbq_t=0; key_cur=0xFF; key_state=0; key_timer=0; boot_hold=boot; boot_rel=0;
    if(boot) key_cur=0x20;
    cpu_reset();
}
static void machine_run(int cycles){
    i64 target=cyc+cycles;
    while(cyc<target) machine_step();
    sid_run(cyc);
}

/* ---------------------------------------------------------------- teletext display (SAA5050)
   Glyph tables and the diagonal "character rounding" rule come from b-em (m7makechars.c, GPL).
   Every character cell is rendered at 12x20 pixels. */
#define TT_W 480
#define TT_H 500
static u32 fb[TT_H][TT_W];
static const u32 ttcol[8]={0x000000,0xFF0000,0x00FF00,0xFFFF00,0x0000FF,0xFF00FF,0x00FFFF,0xFFFFFF};
static int frame_no;

/* rounding fill for dot (y,d): bit0 = left half, bit1 = right half */
static int tt_round(const u8*p,int y,int d){
    int stat=0;
    if(y>=9) return 0;
    if(p[y*6+d] && p[y*6+d+6]) stat|=3;
    if(d>0 && p[y*6+d] && p[y*6+d+5] && !p[y*6+d-1]) stat|=1;
    if(d>0 && p[y*6+d+6] && p[y*6+d-1] && !p[y*6+d+5]) stat|=1;
    if(d<5 && p[y*6+d] && p[y*6+d+7] && !p[y*6+d+1]) stat|=2;
    if(d<5 && p[y*6+d+6] && p[y*6+d+1] && !p[y*6+d+7]) stat|=2;
    return stat;
}
/* fill for the half-line between dot row y and y+1 (b-em rule): bit0 left half, bit1 right half */
static int tt_round_dn(const u8*p,int y,int d){ return tt_round(p,y,d); }
/* mirrored: fill for half-line between dot row y-1 and y */
static int tt_round_up(const u8*p,int y,int d){
    int stat=0;
    if(y<=0) return 0;
    const u8*a=p+(y-1)*6,*b=p+y*6;      /* a = row above, b = current */
    if(b[d]&&a[d]) stat|=3;
    if(d>0 && b[d]&&a[d-1]&&!b[d-1]) stat|=1;
    if(d>0 && a[d]&&b[d-1]&&!a[d-1]) stat|=1;
    if(d<5 && b[d]&&a[d+1]&&!b[d+1]) stat|=2;
    if(d<5 && a[d]&&b[d+1]&&!a[d+1]) stat|=2;
    return stat;
}
/* Each dot row is drawn as two half-lines: the upper one is rounded towards the row above,
   the lower one towards the row below (SAA5050 character rounding). */
/* mode: 0 normal height, 1 double-height top half, 2 double-height bottom half */
static void tt_line(u32*dst,const u8*g,int s,int mode,int rounding,u32 fgc,u32 bgc){
    int y,fup,fdn;
    if(mode==0){ y=s>>1; fup=!(s&1); fdn=s&1; }
    else { y=(s>>2)+(mode==2?5:0); int j=s&3; fup=(j==0); fdn=(j==3); }
    for(int d=0;d<6;d++){
        int base=g[y*6+d]; int l=base,r=base;
        if(rounding){
            int st=0; if(fup) st=tt_round_up(g,y,d); else if(fdn) st=tt_round_dn(g,y,d);
            l|=st&1; r|=(st>>1)&1;
        }
        dst[d*2]=l?fgc:bgc; dst[d*2+1]=r?fgc:bgc;
    }
}
static void tt_render_raw(void){
    int hd=crtc_reg[1]; if(hd>40) hd=40; if(hd<1) hd=40;
    int vd=crtc_reg[6]; if(vd>25) vd=25; if(vd<1) vd=25;
    int start=((crtc_reg[12]<<8)|crtc_reg[13])&0x3FFF;
    int cur=((crtc_reg[14]<<8)|crtc_reg[15])&0x3FFF;
    int cmode=(crtc_reg[10]>>5)&3;
    int cblink=(cmode==0)||(cmode==2&&((frame_no>>3)&1))||(cmode==3&&((frame_no>>4)&1));
    int flash_on=((frame_no%48)<32);
    memset(fb,0,sizeof fb);
    int prev_top=0;
    for(int row=0;row<vd;row++){
        int fg=7,bg=0,gfx=0,sep=0,hold=0,holdc=0x20,holdsep=0,flash=0,dh=0,conceal=0,row_dh=0;
        int second=prev_top;
        for(int col=0;col<hd;col++){
            int ma=(start+row*hd+col)&0x3FFF;
            int c=ram[0x400|(ma&0x3FF)]&0x7F;
            int is_ctl=c<0x20;
            if(is_ctl){
                switch(c){
                case 0x09: flash=0; break;
                case 0x0C: dh=0; break;
                case 0x19: sep=0; break;
                case 0x1A: sep=1; break;
                case 0x1C: bg=0; break;
                case 0x1D: bg=fg; break;
                case 0x1E: hold=1; break;
                }
            }
            const u8*g=NULL; int rounding=0,blank=0;
            if(is_ctl){
                if(hold&&gfx){ g=(holdsep?tt_sep:tt_gfx)+(holdc-0x20)*60; }
                else blank=1;
            } else if(gfx && (c&0x20)){
                g=(sep?tt_sep:tt_gfx)+(c-0x20)*60; holdc=c; holdsep=sep;
            } else { g=tt_chars+(c-0x20)*60; rounding=1; }
            if(conceal||(flash&&!flash_on)) blank=1;
            u32 fgc=ttcol[fg]|0xFF000000, bgc=ttcol[bg]|0xFF000000;
            int usedh=dh && !(is_ctl&&!(hold&&gfx));
            int mode=usedh?(second?2:1):0;
            for(int s=0;s<20;s++){
                u32*dst=&fb[row*20+s][col*12];
                if(blank||!g){ for(int px=0;px<12;px++) dst[px]=bgc; }
                else tt_line(dst,g,s,mode,rounding,fgc,bgc);
            }
            if(ma==cur && cblink && cmode!=1){
                for(int s=16;s<20;s++) for(int px=0;px<12;px++) fb[row*20+s][col*12+px]=0xFFFFFFFF;
            }
            if(is_ctl){
                switch(c){
                case 1: case 2: case 3: case 4: case 5: case 6: case 7: gfx=0; fg=c; conceal=0; break;
                case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: gfx=1; fg=c&7; conceal=0; break;
                case 0x08: flash=1; break;
                case 0x0D: dh=1; row_dh=1; break;
                case 0x18: conceal=1; break;
                case 0x1F: hold=0; break;
                }
            }
        }
        prev_top = row_dh && !second;
    }
    frame_no++;
}

/* ---- 80x25 monochrome VDU board (6845 + 2K video RAM at &1000 + 2K character ROM, 16 bytes per character) ---- */
#define V80_W 640
#define V80_H 500
static u32 fb80[V80_H][V80_W];
static const u32 monocol[3]={0xFFE8E8E8u,0xFF40FF40u,0xFFFFB000u};
static void vdu80_render_raw(void){
    int hd=crtc_reg[1]; if(hd>80||hd<1) hd=80;
    int vd=crtc_reg[6]; if(vd>25||vd<1) vd=25;
    int sl=(crtc_reg[9]&31)+1; if(sl>16) sl=16;
    int start=((crtc_reg[12]<<8)|crtc_reg[13])&0x3FFF;
    int cur=((crtc_reg[14]<<8)|crtc_reg[15])&0x3FFF;
    int cmode=(crtc_reg[10]>>5)&3;
    int cblink=(cmode==0)||(cmode==2&&((frame_no>>3)&1))||(cmode==3&&((frame_no>>4)&1));
    int cs=crtc_reg[10]&31, ce=crtc_reg[11]&31;
    u32 fg=monocol[cfg_mono%3], bg=0xFF000000u;
    for(int y=0;y<V80_H;y++) for(int x=0;x<V80_W;x++) fb80[y][x]=bg;
    for(int row=0;row<vd;row++) for(int col=0;col<hd;col++){
        int ma=(start+row*hd+col)&0x3FFF;
        int c=ram[0x1000|(ma&0x7FF)]&0x7F;
        const u8*g=font80+c*16;
        int iscur=(ma==cur && cblink && cmode!=1);
        for(int s=0;s<sl;s++){
            int y=(row*sl+s)*2; if(y+1>=V80_H) break;
            u8 bits=g[s];
            if(iscur && s>=cs && s<=ce) bits=0xFF;
            for(int px=0;px<8;px++){
                u32 v=((bits>>(7-px))&1)?fg:bg;
                fb80[y][col*8+px]=v; fb80[y+1][col*8+px]=v;
            }
        }
    }
    frame_no++;
}

/* cached frame builder: only re-renders when something visible changed */
static const u32 *disp=&fb[0][0];
static int disp_w=TT_W, disp_h=TT_H;
static int frame_changed=1;
static void tt_render(void){
    static u32 lasthash=0; static int first=1;
    int v80=(cfg_model==M_STANDARD && cfg_vdu==1);
    u32 h=2166136261u;
    const u8*vr= v80? ram+0x1000 : ram+0x400; int vn= v80? 2048 : 1024;
    for(int i=0;i<vn;i++){ h^=vr[i]; h*=16777619u; }
    for(int i=0;i<18;i++){ h^=crtc_reg[i]; h*=16777619u; }
    int cmode=(crtc_reg[10]>>5)&3;
    int cph=(cmode==0)?1:(cmode==2?((frame_no>>3)&1):(cmode==3?((frame_no>>4)&1):0));
    h^=(u32)(cph|(((frame_no%48)<32)<<1)|(v80<<2)|((cfg_mono&3)<<3)); h*=16777619u;
    if(first || h!=lasthash){ if(v80) vdu80_render_raw(); else tt_render_raw(); lasthash=h; frame_changed=1; first=0; }
    else frame_no++;
    if(v80){ disp=&fb80[0][0]; disp_w=V80_W; disp_h=V80_H; } else { disp=&fb[0][0]; disp_w=TT_W; disp_h=TT_H; }
}
