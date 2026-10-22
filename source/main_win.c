/* Acorn System 5 emulator - Windows front end (pure Win32, no external DLLs) */
#include "core.h"
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <ctype.h>

#define ID_D0     101
#define ID_D1     102
#define ID_E0     103
#define ID_E1     104
#define ID_EXIT   105
#define ID_BREAK  110
#define ID_BOOT   111
#define ID_PASTE  113
#define ID_CAPS   114
#define ID_SMOOTH 140
#define ID_MODEL_C 150
#define ID_MODEL_S 151
#define ID_VDU40   152
#define ID_VDU80   153
#define ID_BASICROM 154
#define ID_FPROM   155
#define ID_STARTBASIC 156
#define ID_MONO    160    /* 160..162: white, green, amber */
#define S5_VERSION "1.1.0"
#define BORDER_X_PM 65     /* black border left/right, in 1/1000 of the 4:3 frame width  */
#define BORDER_Y_PX 25     /* black border top/bottom: pixels at a 800x600 window, scales with window size */
#define ID_ABOUT  120
#define ID_SIZE1  130
#define ID_SIZE2  131
#define ID_SIZE3  132

static HWND hwnd; static int running=1, caps_on=1;   /* emulator Caps Lock, on at start */
static char exedir[MAX_PATH];
static BITMAPINFO bmi;
static int smooth=1;                  /* bilinear smoothing when scaling, like BeebEm's DirectX smoothing */
static u32 *sbuf; static int sbw,sbh; static int sdirty=1;
static HMENU hm_machine,hm_model,hm_std,hm_mono,hm_view;
static int d_user[2];                 /* drive chosen by the user (saved in the ini file) */
static char ini_path[MAX_PATH+16], *ini_disk[2];

/* ---- audio ---- */
#define NBUF 6
static HWAVEOUT hwo; static WAVEHDR wh[NBUF]; static short wbuf[NBUF][1200]; static int audio_ok;
static void audio_init(void){
    WAVEFORMATEX wf; memset(&wf,0,sizeof wf);
    wf.wFormatTag=WAVE_FORMAT_PCM; wf.nChannels=1; wf.nSamplesPerSec=44100; wf.wBitsPerSample=16;
    wf.nBlockAlign=2; wf.nAvgBytesPerSec=88200;
    if(waveOutOpen(&hwo,WAVE_MAPPER,&wf,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) return;
    for(int i=0;i<NBUF;i++){ memset(&wh[i],0,sizeof wh[i]); wh[i].lpData=(LPSTR)wbuf[i]; wh[i].dwBufferLength=882*2;
        waveOutPrepareHeader(hwo,&wh[i],sizeof wh[i]); }
    audio_ok=1;
}
static void audio_push(void){
    if(!audio_ok || cfg_model!=M_CUSTOM) return;      /* the standard System 5 has no SID */
    for(int i=0;i<NBUF;i++){
        if(wh[i].dwFlags&WHDR_INQUEUE) continue;
        int n=sidn>1100?1100:sidn;
        memcpy(wbuf[i],sidbuf,n*2);
        for(int k=n;k<882;k++) wbuf[i][k]= n? wbuf[i][n-1]:0;
        wh[i].dwBufferLength=(n<882?882:n)*2;
        waveOutWrite(hwo,&wh[i],sizeof wh[i]);
        return;
    }
}

/* ---- helpers ---- */
static const char*base_name(const char*p){ const char*s=strrchr(p,'\\'); if(!s) s=strrchr(p,'/'); return s?s+1:p; }
static void update_title(void){
    char t[600];
    const char*mn= cfg_model==M_CUSTOM ? "Custom (IMADOS)" : (cfg_vdu ? "Standard 80x25" : "Standard 40x25");
    snprintf(t,sizeof t,"Acorn System 5 v" S5_VERSION " - %s - D0: %s   D1: %s%s",mn,
        disk[0].mounted?base_name(disk[0].path):"(empty)", disk[1].mounted?base_name(disk[1].path):"(empty)",
        caps_on?"   [CAPS]":"");
    SetWindowTextA(hwnd,t);
}
/* ---- settings (system5.ini next to the exe) ---- */
static void cfg_save(void){
    FILE*f=fopen(ini_path,"w"); if(!f) return;
    fprintf(f,"model=%d\nvdu=%d\nbasic=%d\nfp=%d\nmono=%d\nsmooth=%d\n",cfg_model,cfg_vdu,cfg_basic,cfg_fp,cfg_mono,smooth);
    for(int i=0;i<2;i++) if(d_user[i]) fprintf(f,"d%d=%s\n",i,disk[i].mounted?disk[i].path:"");
    fclose(f);
}
static void cfg_load(void){
    FILE*f=fopen(ini_path,"r"); if(!f) return; char ln[800];
    while(fgets(ln,sizeof ln,f)){
        char*e=strchr(ln,'='); if(!e) continue; *e=0; char*v=e+1; v[strcspn(v,"\r\n")]=0;
        if(!strcmp(ln,"model")) cfg_model=atoi(v)?M_STANDARD:M_CUSTOM;
        else if(!strcmp(ln,"vdu")) cfg_vdu=atoi(v)?1:0;
        else if(!strcmp(ln,"basic")) cfg_basic=atoi(v)?1:0;
        else if(!strcmp(ln,"fp")) cfg_fp=atoi(v)?1:0;
        else if(!strcmp(ln,"mono")){ cfg_mono=atoi(v); if(cfg_mono<0||cfg_mono>2) cfg_mono=0; }
        else if(!strcmp(ln,"smooth")) smooth=atoi(v)?1:0;
        else if(ln[0]=='d'&&(ln[1]=='0'||ln[1]=='1')&&!ln[2]){ int d=ln[1]-'0'; d_user[d]=1; free(ini_disk[d]); ini_disk[d]=*v?_strdup(v):NULL; }
    }
    fclose(f);
}
static void mount_default(int d){
    const char*nm= d ? "disk1.ssd" : (cfg_model==M_STANDARD ? "DOS_Utilities.ssd" : "disk0.ssd");
    char p[MAX_PATH+64]; snprintf(p,sizeof p,"%s%s",exedir,nm); disk_mount(d,p);
}
static void refresh_menu(void){
    CheckMenuRadioItem(hm_model,ID_MODEL_C,ID_MODEL_S,ID_MODEL_C+cfg_model,MF_BYCOMMAND);
    CheckMenuRadioItem(hm_std,ID_VDU40,ID_VDU80,ID_VDU40+cfg_vdu,MF_BYCOMMAND);
    CheckMenuItem(hm_std,ID_BASICROM,MF_BYCOMMAND|(cfg_basic?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(hm_std,ID_FPROM,MF_BYCOMMAND|(cfg_fp?MF_CHECKED:MF_UNCHECKED));
    UINT en=(cfg_model==M_STANDARD)?MF_ENABLED:MF_GRAYED;
    EnableMenuItem(hm_std,ID_VDU40,MF_BYCOMMAND|en); EnableMenuItem(hm_std,ID_VDU80,MF_BYCOMMAND|en);
    EnableMenuItem(hm_std,ID_BASICROM,MF_BYCOMMAND|en); EnableMenuItem(hm_std,ID_FPROM,MF_BYCOMMAND|en);
    CheckMenuRadioItem(hm_mono,ID_MONO,ID_MONO+2,ID_MONO+cfg_mono,MF_BYCOMMAND);
    UINT em=(cfg_model==M_STANDARD&&cfg_vdu==1)?MF_ENABLED:MF_GRAYED;
    for(int i=0;i<3;i++) EnableMenuItem(hm_mono,ID_MONO+i,MF_BYCOMMAND|em);
    EnableMenuItem(hm_machine,ID_STARTBASIC,MF_BYCOMMAND|((cfg_model==M_CUSTOM||cfg_basic)?MF_ENABLED:MF_GRAYED));
    CheckMenuItem(hm_view,ID_SMOOTH,MF_BYCOMMAND|(smooth?MF_CHECKED:MF_UNCHECKED));
}
/* after any machine change: default disks for drives the user did not choose, reset, save */
static void apply_config(void){
    for(int d=0;d<2;d++) if(!d_user[d]){ disk_eject(d); mount_default(d); }
    machine_reset(0); sdirty=1; refresh_menu(); update_title(); cfg_save();
}
static void user_mount(int d,const char*path){
    if(disk_mount(d,path)){ d_user[d]=1; cfg_save(); }
    else MessageBoxA(hwnd,"Could not read that disk image.","Acorn System 5",MB_ICONWARNING);
    update_title();
}
static int load_file(const char*name,u8*dst,int n){
    char p[MAX_PATH+64]; snprintf(p,sizeof p,"%s%s",exedir,name);
    FILE*f=fopen(p,"rb"); if(!f) return 0;
    int r=(int)fread(dst,1,n,f); fclose(f); return r==n;
}
static void pick_disk(int d){
    char file[MAX_PATH]=""; OPENFILENAMEA o; memset(&o,0,sizeof o);
    o.lStructSize=sizeof o; o.hwndOwner=hwnd; o.lpstrFile=file; o.nMaxFile=MAX_PATH;
    o.lpstrFilter="Disk images (*.ssd;*.img)\0*.ssd;*.img\0All files\0*.*\0";
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    o.lpstrTitle= d? "Mount disk in drive 1":"Mount disk in drive 0";
    if(GetOpenFileNameA(&o)) user_mount(d,file);
}
static void do_paste(void){
    if(!OpenClipboard(hwnd)) return;
    HANDLE h=GetClipboardData(CF_TEXT);
    if(h){ const char*s=(const char*)GlobalLock(h);
        if(s){ int prev=0; for(;*s;s++){ unsigned char c=*s;
                if(c=='\r'){ kb_push(13); prev=1; continue; }
                if(c=='\n'){ if(!prev) kb_push(13); prev=0; continue; }
                prev=0; if(c<128&&(c>=32||c==9)) kb_push(c); }
            GlobalUnlock(h); } }
    CloseClipboard();
}
static void set_client_size(int cw,int ch){
    RECT r={0,0,cw,ch};
    AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,TRUE);
    SetWindowPos(hwnd,NULL,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
}

static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE: DragAcceptFiles(h,TRUE); return 0;
    case WM_DESTROY: running=0; PostQuitMessage(0); return 0;
    case WM_SIZE: {
        int cw=LOWORD(l),ch=HIWORD(l),id=0; HMENU mn=GetMenu(h);
        if(cw==640&&ch==480) id=ID_SIZE1; else if(cw==800&&ch==600) id=ID_SIZE2; else if(cw==1280&&ch==960) id=ID_SIZE3;
        if(mn) for(int i=0;i<3;i++) CheckMenuItem(mn,ID_SIZE1+i,MF_BYCOMMAND|((ID_SIZE1+i==id)?MF_CHECKED:MF_UNCHECKED));
        sdirty=1; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT rc; GetClientRect(h,&rc);
        int cw=rc.right,ch=rc.bottom; if(cw<1)cw=1; if(ch<1)ch=1;
        /* 4:3 monitor frame, centred in the window */
        int fw=cw,fh=cw*3/4; if(fh>ch){ fh=ch; fw=ch*4/3; }
        int fx=(cw-fw)/2,fy=(ch-fh)/2;
        /* teletext area inside the frame (BeebEm geometry): 6.5% black border left and right, full height */
        int by=fh*BORDER_Y_PX/600;
        int dx=fx+fw*BORDER_X_PM/1000, dy=fy+by;
        int dw=fw-2*(fw*BORDER_X_PM/1000), dh=fh-2*by;
        if(dw<1)dw=1; if(dh<1)dh=1;
        HBRUSH bk=(HBRUSH)GetStockObject(BLACK_BRUSH);
        { RECT a={0,0,cw,dy},b={0,dy+dh,cw,ch},l={0,dy,dx,dy+dh},r={dx+dw,dy,cw,dy+dh};
          if(a.bottom>a.top) FillRect(dc,&a,bk); if(b.bottom>b.top) FillRect(dc,&b,bk);
          if(l.right>l.left) FillRect(dc,&l,bk); if(r.right>r.left) FillRect(dc,&r,bk); }
        if(smooth && dw>0 && dh>0){
            if(sdirty || dw!=sbw || dh!=sbh){
                if(dw!=sbw || dh!=sbh){ free(sbuf); sbuf=(u32*)malloc((size_t)dw*dh*4); sbw=dw; sbh=dh; }
                if(sbuf){
                    int *x0=(int*)malloc(dw*sizeof(int)*2),*fx=x0+dw;
                    for(int x=0;x<dw;x++){
                        int p=(int)(((x*2+1)*(i64)disp_w*128)/dw)-128;      /* 8.8 fixed point source position */
                        if(p<0)p=0; if(p>(disp_w-1)*256)p=(disp_w-1)*256;
                        x0[x]=p>>8; fx[x]=p&255; }
                    for(int y=0;y<dh;y++){
                        int p=(int)(((y*2+1)*(i64)disp_h*128)/dh)-128;
                        if(p<0)p=0; if(p>(disp_h-1)*256)p=(disp_h-1)*256;
                        int y0=p>>8,fy=p&255,y1=y0<disp_h-1?y0+1:y0;
                        const u32*r0=disp+y0*disp_w,*r1=disp+y1*disp_w; u32*o=sbuf+(size_t)y*dw;
                        for(int x=0;x<dw;x++){
                            int xa=x0[x],xb=xa<disp_w-1?xa+1:xa,wx=fx[x];
                            u32 a=r0[xa],b=r0[xb],c=r1[xa],d=r1[xb];
                            u32 rb0=((a&0xFF00FF)*(256-wx)+(b&0xFF00FF)*wx)>>8, g0=(((a>>8)&255)*(256-wx)+((b>>8)&255)*wx)>>8;
                            u32 rb1=((c&0xFF00FF)*(256-wx)+(d&0xFF00FF)*wx)>>8, g1=(((c>>8)&255)*(256-wx)+((d>>8)&255)*wx)>>8;
                            u32 rb=(((rb0&0xFF00FF)*(256-fy)+(rb1&0xFF00FF)*fy)>>8)&0xFF00FF;
                            u32 g=(g0*(256-fy)+g1*fy)>>8;
                            o[x]=0xFF000000u|rb|(g<<8);
                        }
                    }
                    free(x0);
                }
                sdirty=0;
            }
            BITMAPINFO sb=bmi; sb.bmiHeader.biWidth=dw; sb.bmiHeader.biHeight=-dh;
            if(sbuf) StretchDIBits(dc,dx,dy,dw,dh,0,0,dw,dh,sbuf,&sb,DIB_RGB_COLORS,SRCCOPY);
        } else {
            SetStretchBltMode(dc,COLORONCOLOR);
            { BITMAPINFO nb=bmi; nb.bmiHeader.biWidth=disp_w; nb.bmiHeader.biHeight=-disp_h;
              StretchDIBits(dc,dx,dy,dw,dh,0,0,disp_w,disp_h,disp,&nb,DIB_RGB_COLORS,SRCCOPY); }
        }
        EndPaint(h,&ps); return 0; }
    case WM_DROPFILES: {
        char f[MAX_PATH]; DragQueryFileA((HDROP)w,0,f,MAX_PATH); DragFinish((HDROP)w);
        user_mount((GetKeyState(VK_SHIFT)&0x8000)?1:0,f); return 0; }
    case WM_COMMAND:
        switch(LOWORD(w)){
        case ID_D0: pick_disk(0); break; case ID_D1: pick_disk(1); break;
        case ID_E0: disk_eject(0); d_user[0]=1; cfg_save(); update_title(); break; case ID_E1: disk_eject(1); d_user[1]=1; cfg_save(); update_title(); break;
        case ID_EXIT: DestroyWindow(h); break;
        case ID_BREAK: machine_reset(0); break;
        case ID_BOOT: machine_reset(1); break;
        case ID_PASTE: do_paste(); break;
        case ID_CAPS: caps_on=!caps_on; update_title(); break;
        case ID_SMOOTH: smooth=!smooth; sdirty=1; refresh_menu(); cfg_save(); InvalidateRect(h,NULL,FALSE); break;
        case ID_MODEL_C: cfg_model=M_CUSTOM; apply_config(); break;
        case ID_MODEL_S: cfg_model=M_STANDARD; apply_config(); break;
        case ID_VDU40: cfg_vdu=0; apply_config(); break;
        case ID_VDU80: cfg_vdu=1; apply_config(); break;
        case ID_BASICROM: cfg_basic=!cfg_basic; apply_config(); break;
        case ID_FPROM: cfg_fp=!cfg_fp; apply_config(); break;
        case ID_MONO: case ID_MONO+1: case ID_MONO+2: cfg_mono=LOWORD(w)-ID_MONO; refresh_menu(); cfg_save(); break;
        case ID_STARTBASIC: { const char*c= cfg_model==M_CUSTOM ? "BASIC\r" : "GO C2B2\r"; for(;*c;c++) kb_push(*c); break; }
        case ID_SIZE1: set_client_size(640,480); break; case ID_SIZE2: set_client_size(800,600); break; case ID_SIZE3: set_client_size(1280,960); break;
        case ID_ABOUT: { char t[1900];
            snprintf(t,sizeof t,
            "Acorn System 5 Emulator  -  version " S5_VERSION "\n(build %s)\n\n"
            "Two machine models (Machine > Model):\n"
            " - Custom System 5: IMADOS, 40x25 teletext, SID, BASIC &A000-&DFFF\n"
            " - Standard System 5: AcornDOS, 40x25 teletext or 80x25 mono VDU,\n"
            "   System BASIC &C000 with optional floating point ROM &D000, no SID\n\n"
            "6502 @ 1 MHz, 2x 6522 VIA, 6845/HD46505, SAA5050 teletext, 8271 FDC\n\n"
            "F12 = Break     F11 = Space-Break (boot disk)     F8 = smoothing on/off\n"
            "Machine > Start BASIC types BASIC (custom) or GO C2B2 (standard)\n"
            "Caps Lock = uppercase on/off (on at start; Shift inverts)\n"
            "Ctrl+V = paste text as typing\n"
            "Cursor keys = Ctrl-A/S/W/Z, End = copy (Ctrl-Q)\n"
            "Drop a .ssd file on the window to mount in drive 0 (hold Shift for drive 1)\n\n"
            "Teletext font/rounding and SID (reSID-fp) taken from b-em (GPL).\n"
            "For private and hobby use.", __DATE__);
            MessageBoxA(h,t,"About Acorn System 5 Emulator",MB_OK|MB_ICONINFORMATION); break; }

        }
        return 0;
    case WM_KEYDOWN:
        switch(w){
        case VK_F12: machine_reset(0); return 0;
        case VK_F11: machine_reset(1); return 0;
        case VK_LEFT: kb_push(0x01); return 0;
        case VK_RIGHT: kb_push(0x13); return 0;
        case VK_UP: kb_push(0x17); return 0;
        case VK_DOWN: kb_push(0x1A); return 0;
        case VK_END: kb_push(0x11); return 0;
        case VK_F8: SendMessage(h,WM_COMMAND,ID_SMOOTH,0); return 0;
        case VK_CAPITAL: caps_on=!caps_on; update_title(); return 0;
        case VK_DELETE: kb_push(0x7F); return 0;
        case 'V': if(GetKeyState(VK_CONTROL)&0x8000){ do_paste(); return 0; } break;
        }
        break;
    case WM_CHAR:
        if(w==0x16) return 0;                 /* Ctrl+V handled above */
        if(w==8) kb_push(0x7F);               /* Backspace -> DELETE */
        else if(w<128){
            int c=(int)w;
            if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){      /* emulator Caps Lock; Shift inverts, like a real keyboard */
                int shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
                c=((caps_on^shift)?toupper(c):tolower(c));
            }
            kb_push(c);
        }
        return 0;
    }
    return DefWindowProcA(h,m,w,l);
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR cmd,int show){
    GetModuleFileNameA(NULL,exedir,MAX_PATH);
    char*sl=strrchr(exedir,'\\'); if(sl) sl[1]=0; else exedir[0]=0;
    SetCurrentDirectoryA(exedir);

    load_default_roms();
    load_file("imados.rom",rom_os,0x2000); load_file("basic.rom",rom_basic,0x4000);
    load_file("dos40a.bin",rom_dos40,0x1000); load_file("dos80a.bin",rom_dos80,0x1000);
    load_file("systembasic.bin",rom_sbasic,0x1000); load_file("systembasic-fp.bin",rom_sfp,0x1000);
    load_file("font80.rom",font80,0x800);

    snprintf(ini_path,sizeof ini_path,"%ssystem5.ini",exedir);
    cfg_load();
    { int nd=0; for(int i=1;i<__argc&&nd<2;i++) if(__argv[i][0]!='-'){ if(disk_mount(nd,__argv[i])){ d_user[nd]=1; } nd++; } }
    for(int d=0;d<2;d++){
        if(disk[d].mounted) continue;
        if(d_user[d]){ if(ini_disk[d]&&!disk_mount(d,ini_disk[d])) d_user[d]=0; }
        if(!d_user[d]) mount_default(d);
    }

    memset(&bmi,0,sizeof bmi); bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=TT_W; bmi.bmiHeader.biHeight=-TT_H; bmi.bmiHeader.biPlanes=1;
    bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;

    WNDCLASSA wc; memset(&wc,0,sizeof wc);
    wc.lpfnWndProc=wndproc; wc.hInstance=hi; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.lpszClassName="S5EMU";
    wc.hIcon=LoadIcon(hi,MAKEINTRESOURCE(1));
    if(!wc.hIcon) wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassA(&wc);

    HMENU mb=CreateMenu(),mf=CreatePopupMenu(),mm=CreatePopupMenu(),mv=CreatePopupMenu(),mh=CreatePopupMenu();
    AppendMenuA(mf,MF_STRING,ID_D0,"Mount disk in drive &0...");
    AppendMenuA(mf,MF_STRING,ID_D1,"Mount disk in drive &1...");
    AppendMenuA(mf,MF_STRING,ID_E0,"Eject drive 0");
    AppendMenuA(mf,MF_STRING,ID_E1,"Eject drive 1");
    AppendMenuA(mf,MF_SEPARATOR,0,NULL); AppendMenuA(mf,MF_STRING,ID_EXIT,"E&xit");
    hm_machine=mm; hm_view=mv; hm_model=CreatePopupMenu(); hm_std=CreatePopupMenu(); hm_mono=CreatePopupMenu();
    AppendMenuA(mm,MF_STRING,ID_BREAK,"&Break\tF12");
    AppendMenuA(mm,MF_STRING,ID_BOOT,"Space-Break (&boot disk)\tF11");
    AppendMenuA(mm,MF_STRING,ID_STARTBASIC,"Start BAS&IC");
    AppendMenuA(mm,MF_SEPARATOR,0,NULL);
    AppendMenuA(hm_model,MF_STRING,ID_MODEL_C,"Custom System 5 (IMADOS, 40x25, SID)");
    AppendMenuA(hm_model,MF_STRING,ID_MODEL_S,"Standard System 5 (AcornDOS, no SID)");
    AppendMenuA(mm,MF_POPUP,(UINT_PTR)hm_model,"&Model");
    AppendMenuA(hm_std,MF_STRING,ID_VDU40,"Display: 40x25 teletext VDU");
    AppendMenuA(hm_std,MF_STRING,ID_VDU80,"Display: 80x25 monochrome VDU");
    AppendMenuA(hm_std,MF_SEPARATOR,0,NULL);
    AppendMenuA(hm_std,MF_STRING,ID_BASICROM,"System BASIC ROM at &C000");
    AppendMenuA(hm_std,MF_STRING,ID_FPROM,"Floating point ROM at &D000");
    AppendMenuA(mm,MF_POPUP,(UINT_PTR)hm_std,"&Standard System 5 options");
    AppendMenuA(mm,MF_SEPARATOR,0,NULL);
    AppendMenuA(mm,MF_STRING,ID_PASTE,"&Paste text\tCtrl+V");
    AppendMenuA(mm,MF_STRING,ID_CAPS,"Caps &Lock (uppercase)\tCaps Lock");
    AppendMenuA(mv,MF_STRING|MF_CHECKED,ID_SMOOTH,"Smoothing (bilinear)\tF8");
    AppendMenuA(hm_mono,MF_STRING,ID_MONO,"White");
    AppendMenuA(hm_mono,MF_STRING,ID_MONO+1,"Green");
    AppendMenuA(hm_mono,MF_STRING,ID_MONO+2,"Amber");
    AppendMenuA(mv,MF_POPUP,(UINT_PTR)hm_mono,"80x25 display &colour");
    AppendMenuA(mv,MF_SEPARATOR,0,NULL);
    AppendMenuA(mv,MF_STRING,ID_SIZE1,"Small (640x480)");
    AppendMenuA(mv,MF_STRING,ID_SIZE2,"Normal (800x600)");
    AppendMenuA(mv,MF_STRING,ID_SIZE3,"Large (1280x960)");
    AppendMenuA(mh,MF_STRING,ID_ABOUT,"&About / keys (v" S5_VERSION ")");
    AppendMenuA(mb,MF_POPUP,(UINT_PTR)mf,"&File"); AppendMenuA(mb,MF_POPUP,(UINT_PTR)mm,"&Machine");
    AppendMenuA(mb,MF_POPUP,(UINT_PTR)mv,"&View"); AppendMenuA(mb,MF_POPUP,(UINT_PTR)mh,"&Help");

    RECT r={0,0,800,600}; AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,TRUE);
    hwnd=CreateWindowA("S5EMU","Acorn System 5",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,mb,hi,NULL);
    refresh_menu();
    ShowWindow(hwnd,show); update_title();

    audio_init();
    s5sid_init(44100);
    machine_reset(0);

    timeBeginPeriod(1);
    LARGE_INTEGER qf,qn; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn);
    i64 next=qn.QuadPart; i64 step=qf.QuadPart/50;
    MSG msg;
    while(running){
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) running=0; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if(!running) break;
        QueryPerformanceCounter(&qn);
        if(qn.QuadPart>=next){
            machine_run(20000);
            audio_push(); sidn=0;
            tt_render();
            if(frame_changed){ frame_changed=0; sdirty=1; InvalidateRect(hwnd,NULL,FALSE); }
            next+=step; if(qn.QuadPart-next>step*5) next=qn.QuadPart;
        } else Sleep(1);
    }
    if(audio_ok){ waveOutReset(hwo); for(int i=0;i<NBUF;i++) waveOutUnprepareHeader(hwo,&wh[i],sizeof wh[i]); waveOutClose(hwo); }
    timeEndPeriod(1);
    return 0;
}
