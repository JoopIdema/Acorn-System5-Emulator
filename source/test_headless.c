#include "core.h"
static void dump_text(void){
    int hd=crtc_reg[1]?crtc_reg[1]:40, vd=crtc_reg[6]?crtc_reg[6]:25;
    int start=((crtc_reg[12]<<8)|crtc_reg[13])&0x3FFF;
    for(int r=0;r<vd;r++){ char l[64]; int n=0; for(int c=0;c<hd;c++){ int ch=ram[0x400|((start+r*hd+c)&0x3FF)]&0x7F; l[n++]=(ch>=32&&ch<127)?ch:' '; } while(n>0&&l[n-1]==' ')n--; l[n]=0; printf("|%s\n",l);} }
static void write_ppm(const char*fn){
    FILE*f=fopen(fn,"wb"); fprintf(f,"P6\n%d %d\n255\n",TT_W*3/2,TT_H*3/2);
    for(int y=0;y<TT_H*3/2;y++)for(int x=0;x<TT_W*3/2;x++){u32 p=disp[(y*2/3)*TT_W+x*2/3]; fputc((p>>16)&255,f);fputc((p>>8)&255,f);fputc(p&255,f);} fclose(f);}
static void load(const char*fn,u8*dst,int n){FILE*f=fopen(fn,"rb");fread(dst,1,n,f);fclose(f);}
int main(int argc,char**argv){
    load("imados.rom",rom_os,0x2000); load("basic.rom",rom_basic,0x4000);
    disk_mount(0,"disk0.ssd");
    int boot=argc>2&&argv[2][0]=='B';
    s5sid_init(44100); machine_reset(boot);
    const char*keys=argc>1?argv[1]:"";
    int frames=argc>3?atoi(argv[3]):300;
    for(int fr=0;fr<frames;fr++){
        if(fr==100){ for(const char*k=keys;*k;k++){ int c=*k; if(c=='~') c=13; kb_push(c);} }
        machine_run(20000); sidn=0;
    }
    tt_render(); dump_text(); write_ppm("out.ppm");
    printf("PC=%04X cyc=%lld\n",PC,(long long)cyc);
    return 0;
}
