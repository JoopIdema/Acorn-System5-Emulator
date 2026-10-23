\ =====================================================================
\  SNAKE  -  port of Acornsoft Snake (BBC Micro, 1982) to the Acorn System 5
\
\  Target : System 5, 6502, 40x25 teletext VDU (&0400, 6845 at &0800),
\           IMADOS / Acorn DOS, SID at &0C60
\  Build  : beebasm -i snake.asm            (produces SNAKE)
\  Run    : *RUN SNAKE   (load and exec address &2800)
\
\  The keyboard is read without blocking, through the same VIA port
\  as OSRDCH (SVIARA). The program gets that address at startup from
\  the OSRDCH code (via RDCHV at &020A), so platinclude.asm is not needed.
\ =====================================================================

CPU 0

\ ---------------------------------------------------------------------
\  CONFIGURATION
\ ---------------------------------------------------------------------
KEYVIA      = 0             \ 0 = look it up via RDCHV, otherwise the address of SVIARA
VDU_BASE    = &0400         \ SCREENBASE from IMADOS
SCRADDRREG  = &0800         \ 6845 address register
SCRDATAREG  = &0801         \ 6845 data register
SID         = &0C60         \ SID base address
SLOW_SLICES = 150           \ ticks per snake step, slow
FAST_SLICES = 50            \ same, fast (1 tick ~ 1.4 ms at 1 MHz)
SPS         = 715           \ ticks per second (measured: 1 tick ~ 1400 cycles at 1 MHz)
SNDSTEP     = 24            \ PauseN ticks per sound step (64 steps = 2 s at 1 MHz)

OSASCI      = &FFE9
OSWRCH      = &FFF4
RDCHV       = &020A

\ ---------------------------------------------------------------------
\  Fixed values
\ ---------------------------------------------------------------------
FIELD_X0    = 4             \ first column inside the border
FIELD_X1    = 37
FIELD_Y0    = 4
FIELD_Y1    = 22
FIELD_W     = FIELD_X1 - FIELD_X0 + 1       \ 34
FIELD_H     = FIELD_Y1 - FIELD_Y0 + 1       \ 19
SNAKECH     = &7F           \ full sixel block
CH_A        = &41
THIN_X0     = 8             \ thin mode: first sixel column inside the border
THIN_Y0     = 40            \ sixel row where the snake starts (middle of row 13)
THIN_LEN    = 4             \ initial length (in sixels) of the thin snake
SLOW_THIN   = 60            \ ticks per sixel step, thin snake, slow
FAST_THIN   = 22            \ same, fast
SADUPD      = 87            \ "oh what a pity": number of pitch steps (~0.6 s)
SADWAIT     = 5             \ ticks per pitch step
SADDROP     = &20           \ pitch drop (freq lo) per step; total ~&0AE0
MAXLETTER   = 9             \ letters A..I (value 1..9) appear
D_LEFT      = 0
D_RIGHT     = 1
D_DOWN      = 2
D_UP        = 3

\ Zero page (free user area; change if it clashes)
ptr         = &70           \ pointer into VDU memory
ptr2        = &72           \ pointer into shadow memory
sptr        = &74           \ pointer to text/page
bp          = &76           \ pointer to band table
kptr        = &78           \ pointer to keyboard port

\ ---------------------------------------------------------------------
\  Macros
\ ---------------------------------------------------------------------
MACRO SCREEN bands, pg
    LDA #LO(bands)
    STA bp
    LDA #HI(bands)
    STA bp+1
    LDA #LO(pg)
    STA sptr
    LDA #HI(pg)
    STA sptr+1
    JSR ShowScreen
ENDMACRO

MACRO SHOWPG pg
    LDA #LO(pg)
    STA sptr
    LDA #HI(pg)
    STA sptr+1
    JSR ShowPage
ENDMACRO

MACRO TXT r, s
    EQUB r, 0, &87 : EQUS s : EQUB 0
ENDMACRO

\ =====================================================================
ORG &2800
.start

.Start
{
    CLD
    JSR FindKeyVIA
    BCC found
    LDY #0
.m  LDA nokeymsg,Y
    BEQ done
    JSR OSASCI
    INY
    BNE m
.done
    RTS
.found
    LDA #12                     \ VDU 12: clear screen + CRTC to defaults
    JSR OSWRCH
    LDA #10                     \ 6845 R10: cursor off
    STA SCRADDRREG
    LDA #&20
    STA SCRDATAREG
    JSR SidInit
    LDA #&E1
    STA seed
    LDA #&AC
    STA seed+1
    LDA #0
    STA hiscore
    STA hiscore+1
    STA seedc

    SCREEN titlebands, titlepage
    JSR WaitSpace
    SCREEN nobands, instr1
    JSR WaitSpace
    SCREEN nobands, instr2
    JSR WaitSpace

.menu
    SCREEN speedbands, speedpage
    JSR WaitSpeed
    BCS exit
    SHOWPG modepage
    JSR WaitMode
    BCS exit
.again
    JSR PlayGame                \ C=0: SPACE = restart (same speed)
    BCC again
    JMP menu                    \ C=1: ESC = back to speed selection
.exit
    LDA #12                     \ restore screen and cursor for DOS
    JSR OSWRCH
    RTS                         \ back to DOS
}

.nokeymsg
    EQUS "Snake: keyboard port not found", 13, 0

\ =====================================================================
\  PLATFORM: keyboard
\ =====================================================================
\ FindKeyVIA: determines the address of SVIARA. C=1 if not found.
.FindKeyVIA
{
IF KEYVIA <> 0
    LDA #LO(KEYVIA)
    STA kptr
    LDA #HI(KEYVIA)
    STA kptr+1
    CLC
    RTS
ELSE
    LDA RDCHV                   \ OSRDCH starts with BIT SVIARA (&2C lo hi)
    STA sptr
    LDA RDCHV+1
    STA sptr+1
    LDY #0
    LDA (sptr),Y
    CMP #&2C
    BNE fail
    INY
    LDA (sptr),Y
    STA kptr
    INY
    LDA (sptr),Y
    STA kptr+1
    CLC
    RTS
.fail
    SEC
    RTS
ENDIF
}

\ Plat_ReadKey: non-blocking. Out: A = ASCII key or 0 (Z set if no key)
\ SVIARA bit 7 = 1: no key; otherwise bits 0-6 = ASCII.
.Plat_ReadKey
{
    LDY #0
    LDA (kptr),Y
    BMI none
    AND #&7F
    RTS
.none
    LDA #0
    RTS
}

\ =====================================================================
\  Keys
\ =====================================================================
.WaitKey                            \ wait for release, then for a press
{
.rel
    JSR Plat_ReadKey
    BNE rel
.prs
    INC seedc
    JSR Plat_ReadKey
    BEQ prs
    RTS
}

.WaitSpace
{
    JSR WaitKey
    CMP #&20
    BNE WaitSpace
    RTS
}

.WaitSpaceEsc                       \ C=0 on SPACE, C=1 on ESC
{
.l  JSR WaitKey
    CMP #&20
    BEQ space
    CMP #&1B
    BNE l
    SEC
    RTS
.space
    CLC
    RTS
}

.ToUpper
{
    CMP #&61
    BCC r
    CMP #&7B
    BCS r
    SEC
    SBC #&20
.r  RTS
}

.WaitSpeed                          \ C=1 on ESC
{
.l  JSR WaitKey
    CMP #&1B
    BEQ esc
    JSR ToUpper
    CMP #&53                        \ S
    BEQ slow
    CMP #&46                        \ F
    BNE l
    LDA #FAST_SLICES
    STA speedcnt
    CLC
    RTS
.slow
    LDA #SLOW_SLICES
    STA speedcnt
    CLC
    RTS
.esc
    SEC
    RTS
}

.WaitMode                           \ T = thin snake, N = normal; C=1 on ESC
{
.l  JSR WaitKey
    CMP #&1B
    BEQ esc
    JSR ToUpper
    CMP #&54                        \ T
    BEQ narrow
    CMP #&4E                        \ N
    BNE l
    LDA #0
    STA thin
    CLC
    RTS
.narrow
    LDA #1
    STA thin
    LDA speedcnt                    \ convert speed to sixel steps
    CMP #FAST_SLICES
    BEQ f
    LDA #SLOW_THIN
    BNE s
.f  LDA #FAST_THIN
.s  STA speedcnt
    CLC
    RTS
.esc
    SEC
    RTS
}

.HandleKey                          \ A = key -> newdir
{
    JSR ToUpper
    LDX #3
.l  CMP keys,X
    BEQ f
    DEX
    BPL l
    RTS
.f  STX newdir
    RTS
}

\ =====================================================================
\  Time
\ =====================================================================
.PauseN                             \ A ticks of ~1280 cycles
{
    STA pcnt
.o  LDX #0
.i  DEX
    BNE i
    DEC pcnt
    BNE o
    RTS
}

.Wait                               \ wait one snake step, reading keys
{
    LDA speedcnt
    STA wcnt
.s  JSR Plat_ReadKey
    BEQ nk
    JSR HandleKey
.nk LDA #1
    JSR PauseN
    JSR TickClock
    LDA secflag                     \ every full second: let the letter count down
    BEQ nos
    DEC secflag
    JSR LetterTick
.nos
    JSR SadStep                     \ let the sad tone keep gliding
    DEC wcnt
    BNE s
    RTS
}

\ ---- seconds clock based on ticks (SPS ticks = 1 second) ----
.ResetClock
{
    LDA #LO(SPS)
    STA secdown
    LDA #HI(SPS)
    STA secdown+1
    LDA #0
    STA secflag
    RTS
}

.TickClock                          \ call once per tick; sets secflag every second
{
    LDA secdown
    BNE lo
    DEC secdown+1
.lo DEC secdown
    LDA secdown
    ORA secdown+1
    BNE done
    LDA #LO(SPS)
    STA secdown
    LDA #HI(SPS)
    STA secdown+1
    INC secflag
.done
    RTS
}

.WaitSecs                           \ A = number of seconds to wait
{
    STA tmps
    JSR ResetClock
.l  JSR Plat_ReadKey                \ (only to match the timing of the game)
    LDA #1
    JSR PauseN
    JSR TickClock
    JSR SadStep                     \ (only to match the timing of the game)
    LDA secflag
    BEQ l
    LDA #0
    STA secflag
    DEC tmps
    BNE l
    RTS
}

\ =====================================================================
\  Random (16-bit Galois LFSR)
\ =====================================================================
.Rand
{
    LDX #8
.l  LSR seed+1
    ROR seed
    BCC n
    LDA seed+1
    EOR #&B4
    STA seed+1
.n  DEX
    BNE l
    LDA seed
    RTS
}

.RandMod                            \ A = n -> A = 0..n-1
{
    STA rmod
    JSR Rand
.l  CMP rmod
    BCC d
    SBC rmod
    JMP l
.d  RTS
}

\ =====================================================================
\  Screen
\ =====================================================================
.CalcPtrs                           \ Y = row -> ptr (VDU), ptr2 (shadow)
{
    LDA rowlo,Y
    CLC
    ADC #LO(VDU_BASE)
    STA ptr
    LDA rowhi,Y
    ADC #HI(VDU_BASE)
    STA ptr+1
    LDA rowlo,Y
    CLC
    ADC #LO(shadow)
    STA ptr2
    LDA rowhi,Y
    ADC #HI(shadow)
    STA ptr2+1
    RTS
}

.PutCell                            \ A = character, X = column, Y = row
{
    STA tmpc
    JSR CalcPtrs
    TXA
    TAY
    LDA tmpc
    STA (ptr),Y
    STA (ptr2),Y
    RTS
}

.GetCell                            \ X = column, Y = row -> A (from shadow)
{
    JSR CalcPtrs
    TXA
    TAY
    LDA (ptr2),Y
    RTS
}

.SegPut                             \ A = character, X = ring buffer index (X is preserved)
{
    STA tmpc2
    STX tmpi
    LDY bufy,X
    LDA bufx,X
    TAX
    LDA tmpc2
    JSR PutCell
    LDX tmpi
    RTS
}

.ClearScreen                        \ exactly 1000 bytes of spaces
{
    LDA #LO(VDU_BASE)
    STA ptr
    LDA #HI(VDU_BASE)
    STA ptr+1
    LDX #3
    LDA #&20
    LDY #0
.pg STA (ptr),Y
    INY
    BNE pg
    INC ptr+1
    DEX
    BNE pg
.rest
    STA (ptr),Y
    INY
    CPY #232
    BNE rest
    RTS
}

.FillRow                            \ Y = row, A = graphics colour code (&91..&97)
{
    PHA
    JSR CalcPtrs
    PLA
    LDY #0
    STA (ptr),Y
    INY
    LDA #&9A                        \ separated graphics (grid look)
    STA (ptr),Y
    INY
    LDA #&7F
.l  STA (ptr),Y
    INY
    CPY #40
    BNE l
    RTS
}

.DrawBands                          \ bp -> table of 25 colour codes (0 = skip)
{
    LDA #0
    STA fy
.r  LDY fy
    LDA (bp),Y
    BEQ n
    JSR FillRow
.n  INC fy
    LDA fy
    CMP #25
    BNE r
    RTS
}

.PrintAt                            \ tmpx/tmpy = position, sptr -> text (0-terminated)
{
    LDY tmpy
    JSR CalcPtrs
    LDA ptr
    CLC
    ADC tmpx
    STA ptr
    BCC a
    INC ptr+1
.a  LDY #0
.l  LDA (sptr),Y
    BEQ d
    STA (ptr),Y
    INY
    BNE l
.d  RTS                             \ Y = length
}

.ShowPage                           \ sptr -> row,column,text,0 ... &FF
{
.line
    LDY #0
    LDA (sptr),Y
    CMP #&FF
    BEQ end
    STA tmpy
    INY
    LDA (sptr),Y
    STA tmpx
    LDA sptr
    CLC
    ADC #2
    STA sptr
    BCC a
    INC sptr+1
.a  JSR PrintAt
    TYA
    SEC
    ADC sptr
    STA sptr
    BCC line
    INC sptr+1
    JMP line
.end
    RTS
}

.ShowScreen
    JSR ClearScreen
    JSR DrawBands
    JMP ShowPage

.PrintNum                           \ num = value, tmpx/tmpy = position; 5 digits
{
    LDA #0
    STA nz
    STA di
.next
    LDX di
    LDA #&30
    STA dch
.sub
    LDA num
    SEC
    SBC p10lo,X
    TAY
    LDA num+1
    SBC p10hi,X
    BCC out
    STA num+1
    STY num
    INC dch
    JMP sub
.out
    LDA dch
    CMP #&30
    BNE show
    LDA nz
    BNE show
    CPX #4
    BEQ show
    LDA #&20
    JMP put
.show
    LDA #1
    STA nz
    LDA dch
.put
    PHA
    LDY tmpy
    JSR CalcPtrs
    LDA tmpx
    CLC
    ADC di
    TAY
    PLA
    STA (ptr),Y
    INC di
    LDA di
    CMP #5
    BNE next
    RTS
}

.UpdateScores                       \ score on rows 1 and 2 (double height)
{
    LDA #1
    STA fx
.pass
    LDA score
    STA num
    LDA score+1
    STA num+1
    LDA fx
    STA tmpy
    LDA #12
    STA tmpx
    JSR PrintNum
    LDA hiscore
    STA num
    LDA hiscore+1
    STA num+1
    LDA #31
    STA tmpx
    JSR PrintNum
    INC fx
    LDA fx
    CMP #3
    BNE pass
    RTS
}

.DrawField                          \ rows 3..24: blue background, white border
{
    LDA #3
    STA fy
.row
    LDX #0
    LDA #&94                        \ blue graphics
    LDY fy
    JSR PutCell
    LDX #1
    LDA #&9D                        \ new background = blue
    LDY fy
    JSR PutCell
    LDX #2
    LDA #&97                        \ white graphics
    LDY fy
    JSR PutCell
    LDA #3
    STA fx
.col
    LDA #&20
    LDY fy
    CPY #24
    BEQ put
    LDX fx
    CPX #39                         \ column 39 stays empty (border runs up to 38)
    BEQ put
    CPY #3
    BEQ blk
    CPY #23
    BEQ blk
    LDX fx
    CPX #3
    BEQ blk
    CPX #38
    BNE put
.blk
    LDA #SNAKECH
.put
    LDX fx
    LDY fy
    JSR PutCell
    INC fx
    LDA fx
    CMP #40
    BNE col
    INC fy
    LDA fy
    CMP #25
    BNE row
    RTS
}

\ =====================================================================
\  Thin snake: moves per sixel (2 x 3 per character), thin border
\  Sixel (X,Y): character = (X/2, Y/3). Bits in the character code: &01 &02 / &04 &08 / &10 &40
\ =====================================================================
.ThinCalc                           \ X = sx, Y = sy -> tcol, trow, tmask
{
    TXA
    AND #1
    STA tmask
    LDA ydiv3,Y
    STA trow
    LDA ymod3x2,Y
    CLC
    ADC tmask
    TAY
    LDA masktab,Y
    STA tmask
    TXA
    LSR A
    STA tcol
    RTS
}

.ThinSet                            \ sixel on (X = sx, Y = sy)
{
    JSR ThinCalc
    LDX tcol
    LDY trow
    JSR GetCell
    CMP #&40
    BCC ok
    CMP #&60
    BCS ok
    LDA #&20                        \ there was a letter: it has now been eaten
.ok ORA tmask
    LDX tcol
    LDY trow
    JMP PutCell
}

.ThinClear                          \ sixel off (X = sx, Y = sy)
{
    JSR ThinCalc
    LDX tcol
    LDY trow
    JSR GetCell
    STA tcell
    LDA tmask
    EOR #&FF
    AND tcell
    LDX tcol
    LDY trow
    JMP PutCell
}

.ThinProbe                          \ X = sx, Y = sy -> A = &20 free, &7F occupied, otherwise a letter
{
    JSR ThinCalc
    LDX tcol
    LDY trow
    JSR GetCell
    CMP #&40
    BCC gfx
    CMP #&60
    BCS gfx
    RTS                             \ letter
.gfx
    AND tmask
    BEQ free
    LDA #SNAKECH
    RTS
.free
    LDA #&20
    RTS
}

.Probe                              \ what is at the new head position (nx,ny)?
{
    LDX nx
    LDY ny
    LDA thin
    BEQ wide
    JMP ThinProbe
.wide
    JMP GetCell
}

.DrawSeg                            \ X = ring buffer index: draw segment (X is preserved)
{
    LDA thin
    BNE t
    LDA #SNAKECH
    JMP SegPut
.t  STX segi
    LDY bufy,X
    LDA bufx,X
    TAX
    JSR ThinSet
    LDX segi
    RTS
}

.EraseSeg                           \ X = ring buffer index: erase segment (X is preserved)
{
    LDA thin
    BNE t
    LDA #&20
    JMP SegPut
.t  STX segi
    LDY bufy,X
    LDA bufx,X
    TAX
    JSR ThinClear
    LDX segi
    RTS
}

.ThinCode                           \ code for field cell (fx,fy) with thin border -> A
{
    LDA #&20
    LDY fy
    CPY #24
    BEQ done
    LDX fx
    CPX #39
    BEQ done
    CPY #3
    BEQ top
    CPY #23
    BEQ bot
    CPX #3
    BEQ left
    CPX #38
    BEQ right
.done
    RTS
.top
    CPX #3
    BEQ tl
    CPX #38
    BEQ tr
    LDA #&70                        \ bottom sixel row
    RTS
.tl LDA #&60                        \ corner: bottom-right sixel only
    RTS
.tr LDA #&30                        \ corner: bottom-left sixel only
    RTS
.bot
    CPX #3
    BEQ bl
    CPX #38
    BEQ br
    LDA #&23                        \ top sixel row
    RTS
.bl LDA #&22                        \ corner: top-right sixel only
    RTS
.br LDA #&21                        \ corner: top-left sixel only
    RTS
.left
    LDA #&6A                        \ right sixel column
    RTS
.right
    LDA #&35                        \ left sixel column
    RTS
}

.DrawFieldThin                      \ like DrawField, but with a thin border
{
    LDA #3
    STA fy
.row
    LDX #0
    LDA #&94
    LDY fy
    JSR PutCell
    LDX #1
    LDA #&9D
    LDY fy
    JSR PutCell
    LDX #2
    LDA #&97
    LDY fy
    JSR PutCell
    LDA #3
    STA fx
.col
    JSR ThinCode
    LDX fx
    LDY fy
    JSR PutCell
    INC fx
    LDA fx
    CMP #40
    BNE col
    INC fy
    LDA fy
    CMP #25
    BNE row
    RTS
}

\ =====================================================================
\  Sound (SID, 1 MHz clock)
\ =====================================================================
.SidInit
{
    LDX #24
    LDA #0
.l  STA SID,X
    DEX
    BPL l
    LDA #&09                        \ volume as in VDU7 in IMADOS
    STA SID+24
    RTS
}

.SidEat                             \ A = letter value 1..26: higher letter = higher tone
{
    ASL A
    CLC
    ADC #&20
    STA SID+1                       \ voice 1 freq hi
    LDA #0
    STA SID+0
    STA SID+2
    LDA #&08
    STA SID+3                       \ pulse width 50%
    LDA #&06                        \ attack 0, decay 6
    STA SID+5
    LDA #&00                        \ sustain 0
    STA SID+6
    LDA #&40                        \ pulse, gate off
    STA SID+4
    LDA #&41                        \ gate on
    STA SID+4
    RTS
}

.SidTick                            \ A = new letter value 1..26: short beep, lower for a lower letter
{
    ASL A
    CLC
    ADC #&14
    STA SID+8                       \ voice 2 freq hi
    LDA #0
    STA SID+7
    STA SID+9
    STA SID+13                      \ sustain 0
    LDA #&08
    STA SID+10                      \ pulse width 50%
    LDA #&03                        \ attack 0, decay 3 (short)
    STA SID+12
    LDA #&40
    STA SID+11
    LDA #&41                        \ gate on
    STA SID+11
    RTS
}

.StartSound                         \ ~2 seconds of rising tone on voice 1
{
    LDA #0
    STA SID+0
    STA SID+2
    LDA #&08
    STA SID+3
    LDA #&00
    STA SID+5
    LDA #&F0
    STA SID+6
    LDA #&41                        \ pulse + gate on
    STA SID+4
    LDY #0
.l  TYA
    CLC
    ADC #&10
    STA SID+1
    LDA #SNDSTEP
    JSR PauseN                      \ 64 steps of ~1/32 s
    INY
    CPY #64
    BNE l
    LDA #&40                        \ gate off
    STA SID+4
    RTS
}

.SadStart                          \ start gliding tone on voice 2, starting at the pitch of the A
{
    LDA #0
    STA sadlo
    STA SID+7
    LDA #&16                        \ same tone as the countdown beep at the A
    STA sadhi
    STA SID+8
    LDA #&00                        \ attack 0, decay 0
    STA SID+12
    LDA #&C5                        \ sustain C, release 5
    STA SID+13
    LDA #&10                        \ triangle, gate off
    STA SID+11
    LDA #&11                        \ gate on
    STA SID+11
    LDA #SADUPD
    STA sadupd
    LDA #SADWAIT
    STA sadwait
    RTS
}

.SadStep                            \ once per tick: tone a little lower each time, then gate off
{
    LDA sadupd
    BEQ done
    DEC sadwait
    BNE done
    LDA #SADWAIT
    STA sadwait
    SEC
    LDA sadlo
    SBC #SADDROP
    STA sadlo
    STA SID+7
    LDA sadhi
    SBC #0
    STA sadhi
    STA SID+8
    DEC sadupd
    BNE done
    LDA #&10                        \ done: gate off
    STA SID+11
.done
    RTS
}

.SidCrash                           \ falling noise on voice 3
{
    LDA #0
    STA SID+14
    STA SID+19                      \ attack/decay 0
    LDA #&F4
    STA SID+20                      \ sustain F, release 4
    LDA #&81                        \ noise, gate on
    STA SID+18
    LDY #&30
.l  STY SID+15
    LDA #6
    JSR PauseN
    DEY
    CPY #&06
    BNE l
    LDA #&80                        \ gate off
    STA SID+18
    RTS
}

\ =====================================================================
\  The game
\ =====================================================================
.PlaceLetter
{
.try
    LDA #FIELD_W
    JSR RandMod
    CLC
    ADC #FIELD_X0
    STA lx
    LDA #FIELD_H
    JSR RandMod
    CLC
    ADC #FIELD_Y0
    STA ly
    LDA lx                          \ not at the position of the previous (vanished) letter
    CMP ox
    BNE ok
    LDA ly
    CMP oy
    BEQ try
.ok LDX lx
    LDY ly
    JSR GetCell
    CMP #&20
    BNE try
    LDA #MAXLETTER
    JSR RandMod
    CLC
    ADC #1
    STA lval                        \ value 1..26
    LDA #0
    STA ltime
    STA atime
    JSR ResetClock                  \ countdown runs in sync with the new letter
    JMP DrawLetter
}

.DrawLetter                         \ show letter lval at (lx,ly)
{
    LDA lval
    CLC
    ADC #CH_A - 1
    LDX lx
    LDY ly
    JMP PutCell
}

.LetterTick                         \ every second: wait 5 s, then 1 letter per second down;
{                                   \ if the A has been there 2 s without being hit: new letter elsewhere
    LDA ltime
    CMP #4
    BCS count
    INC ltime
    RTS
.count
    BEQ first                       \ ltime = 4: first countdown second
    LDA lval
    CMP #2
    BCS dec
    INC atime                       \ the A is showing: count the 2 seconds
    LDA atime
    CMP #2
    BCC done
    JMP Expire
.first
    INC ltime
    LDA lval
    CMP #2
    BCC done                        \ started as an A: wait for the next seconds
.dec
    SEC
    SBC #1
    STA lval
    JSR DrawLetter
    LDA lval
    JSR SidTick
.done
    RTS
}

.Expire                             \ letter vanishes, new letter in a different place
{
    LDA lx
    STA ox
    LDA ly
    STA oy
    LDA #&20
    LDX lx
    LDY ly
    JSR PutCell                     \ erase old letter
    JSR SadStart                    \ "oh what a pity": falling tone
    JMP PlaceLetter
}

.PlayGame
{
    JSR ClearScreen
    SHOWPG hdrpage
    LDA thin
    BNE fthin
    JSR DrawField
    JMP fdone
.fthin
    JSR DrawFieldThin
.fdone
    LDA #0
    STA sadupd                      \ don't let a tone from the previous game carry on
    LDA #&10
    STA SID+11
    LDA #0
    STA score
    STA score+1
    STA growth
    STA eaten
    STA tailidx
    LDA seedc                       \ mix the seed with the player's waiting time
    EOR seed
    STA seed
    ORA seed+1
    BNE seedok
    LDA #1
    STA seed
.seedok
    LDA thin                        \ start position and length per mode
    BNE ti
    LDA #FIELD_X0
    STA startx
    LDA #13
    STA starty
    LDA #3
    STA initlen
    JMP ci
.ti LDA #THIN_X0
    STA startx
    LDA #THIN_Y0
    STA starty
    LDA #THIN_LEN
    STA initlen
.ci LDA initlen
    SEC
    SBC #1
    STA headidx
    LDX #0
.init
    TXA
    CLC
    ADC startx
    STA bufx,X
    LDA starty
    STA bufy,X
    JSR DrawSeg
    INX
    CPX initlen
    BNE init
    LDA startx
    CLC
    ADC initlen
    SEC
    SBC #1
    STA headx
    LDA starty
    STA heady
    LDA #D_RIGHT
    STA dir
    STA newdir
    JSR PlaceLetter
    JSR UpdateScores
    LDA #2
    JSR WaitSecs                    \ 2 seconds pause
    JSR StartSound                  \ 2 seconds of sound
    JSR ResetClock                  \ only then does the game (and the countdown) start

.loop
    JSR Wait
    LDA newdir
    STA dir
    TAX
    LDA headx
    CLC
    ADC dxtab,X
    STA nx
    LDA heady
    CLC
    ADC dytab,X
    STA ny
    JSR Probe
    CMP #&20
    BEQ move
    CMP #SNAKECH
    BNE letter
    JMP crash
.letter
    SEC
    SBC #CH_A - 1                   \ value 1..26
    STA eatval
    CLC
    ADC score
    STA score
    BCC s1
    INC score+1
.s1 LDA eatval
    LDX thin
    BEQ gw
    ASL A                           \ thin snake: growth x3 (sixels are smaller)
    CLC
    ADC eatval
.gw CLC
    ADC growth
    BCC g1
    LDA #255
.g1 STA growth
    LDA eatval
    JSR SidEat
    LDA #1
    STA eaten
.move
    INC headidx
    LDX headidx
    LDA nx
    STA bufx,X
    STA headx
    LDA ny
    STA bufy,X
    STA heady
    JSR DrawSeg
    LDA headidx                     \ maximum length ~200 (ring buffer is 256)
    SEC
    SBC tailidx
    CMP #200
    BCC lenok
    LDA #0
    STA growth
.lenok
    LDA growth
    BEQ tail
    DEC growth
    JMP after
.tail
    LDX tailidx
    JSR EraseSeg
    INC tailidx
.after
    LDA eaten
    BNE ate
    JMP loop
.ate
    LDA #0
    STA eaten
    JSR PlaceLetter
    JSR UpdateScores
    JMP loop

.crash
    JSR SidCrash
    LDA score                       \ new record?
    CMP hiscore
    LDA score+1
    SBC hiscore+1
    BCC nohi
    LDA score
    STA hiscore
    LDA score+1
    STA hiscore+1
.nohi
    JSR UpdateScores
    SHOWPG overpage
    JMP WaitSpaceEsc                \ C=0 space (restart), C=1 ESC
}

\ =====================================================================
\  Data
\ =====================================================================
.keys
    EQUB &5A, &58, &2F, &27         \ Z X / '   (left right down up)
.dxtab
    EQUB &FF, 1, 0, 0
.dytab
    EQUB 0, 0, 1, &FF

.p10lo
    EQUB LO(10000), LO(1000), LO(100), LO(10), LO(1)
.p10hi
    EQUB HI(10000), HI(1000), HI(100), HI(10), HI(1)

.ydiv3
FOR i, 0, 79
    EQUB i DIV 3
NEXT
.ymod3x2
FOR i, 0, 79
    EQUB (i MOD 3) * 2
NEXT
.masktab
    EQUB &01, &02, &04, &08, &10, &40
.rowlo
FOR i, 0, 24
    EQUB LO(i * 40)
NEXT
.rowhi
FOR i, 0, 24
    EQUB HI(i * 40)
NEXT

\ --- tables of coloured bands (0 = none) ---
.titlebands
    EQUB &91,&91,&91,&91,&91        \ red
    EQUB &92,&92,&92,&92            \ green
    EQUB 0,0                        \ (Snake in double height)
    EQUB &93,&93,&93,&93,&93        \ yellow
    EQUB &94,&94,&94,&94,&94        \ blue
    EQUB 0,0,0,0
.nobands
FOR i, 1, 25
    EQUB 0
NEXT
.speedbands
    EQUB &96
FOR i, 1, 23
    EQUB 0
NEXT
    EQUB &96

\ --- pages: row, column, text, 0 ... &FF
\ teletext: &81 red &82 green &83 yellow &84 blue &86 cyan &87 white (alpha)
\           &9D new background  &8D double height  &97 white (graphics)
.titlepage
    EQUB 9,15,&8D,&87 : EQUS "Snake" : EQUB 0
    EQUB 10,15,&8D,&87 : EQUS "Snake" : EQUB 0
    EQUB 22,6,&87 : EQUS "Copyright (C) Acornsoft 1982" : EQUB 0
    EQUB 24,8,&86 : EQUS "Press SPACE to start" : EQUB 0
    EQUB &FF

.instr1
    EQUB 0,0,&81,&9D,&83 : EQUS "         Acornsoft Snake" : EQUB 0
    EQUB 1,0,&86 : EQUS "             Instructions" : EQUB 0
    TXT  3, "This game gets progressively more"
    TXT  4, "exciting the longer each 'go'"
    TXT  5, "continues. You are in control of a"
    TXT  6, "snake which you can move around the"
    TXT  7, "screen. It can be made to move at one"
    TXT  8, "of two speeds and you can choose the"
    TXT  9, "speed before you start. When the game"
    TXT 10, "starts the snake is very short. The"
    TXT 11, "aim is to grow the snake by guiding"
    TXT 12, "it towards letters which appear at"
    TXT 13, "unpredictable places on the screen."
    TXT 14, "If the snake 'eats' the letter it"
    TXT 15, "grows in length by the 'value' of the"
    TXT 16, "letter (A = 1, B = 2, C = 3 etc) and"
    TXT 17, "your score also increases by the"
    TXT 18, "same value."
    EQUB 23,3,&86 : EQUS "Press SPACE to continue." : EQUB 0
    EQUB &FF

.instr2
    EQUB 0,0,&81,&9D,&83 : EQUS "         Acornsoft Snake" : EQUB 0
    EQUB 1,0,&86 : EQUS "             Instructions" : EQUB 0
    TXT  3, "The longer the snake gets the higher"
    TXT  4, "the score. But don't let the snake"
    TXT  5, "collide with the side of its cage and"
    TXT  6, "don't let it collide with itself."
    TXT  7, "This will happen if the snake's"
    TXT  8, "direction is reversed or if its head"
    TXT  9, "hits some part of its body as it"
    TXT 10, "moves around the screen - and the"
    TXT 11, "longer the snake gets the more likely"
    TXT 12, "this is."
    EQUB 23,3,&86 : EQUS "Press SPACE to continue." : EQUB 0
    EQUB &FF

.speedpage
    EQUB 2,16,&86 : EQUS "Snake" : EQUB 0
    EQUB 4,4,&87 : EQUS "Guide the snake onto the letters" : EQUB 0
    EQUB 5,6 : EQUS "but don't run into yourself" : EQUB 0
    EQUB 6,10 : EQUS "or the boundary !" : EQUB 0
    EQUB 9,6 : EQUS "LEFT  - Z" : EQUB 0
    EQUB 9,22 : EQUS "RIGHT - X" : EQUB 0
    EQUB 11,6 : EQUS "DOWN  - /" : EQUB 0
    EQUB 11,22 : EQUS "UP    - '" : EQUB 0
    EQUB 13,6 : EQUS "RESTART - SPACE BAR" : EQUB 0
    EQUB 16,6,&86 : EQUS "Fast or slow game (F/S) ?" : EQUB 0
    EQUB 22,6,&87 : EQUS "ESC = back to DOS" : EQUB 0
    EQUB &FF

.modepage
    EQUB 18,6,&86 : EQUS "Thin or normal snake (T/N) ?" : EQUB 0
    EQUB &FF

.hdrpage                            \ rows 1+2 = double height, identical content
    EQUB 0,0,&81,&9D,0
    EQUB 1,0,&81,&9D,&8D,&87 : EQUS "  Score" : EQUB 0
    EQUB 1,20 : EQUS "High Score" : EQUB 0
    EQUB 2,0,&81,&9D,&8D,&87 : EQUS "  Score" : EQUB 0
    EQUB 2,20 : EQUS "High Score" : EQUB 0
    EQUB &FF

.overpage
    EQUB 12,12,&87 : EQUS "GAME OVER" : EQUB &97, 0
    EQUB 14,5,&86,&88 : EQUS "Press SPACE BAR to restart" : EQUB &89,&97, 0   \ &88 = flash
    EQUB &FF

.codeend

\ =====================================================================
\  Variables and buffers (NOT included in the saved file)
\ =====================================================================
.tmpx     SKIP 1
.tmpy     SKIP 1
.tmpc     SKIP 1
.tmpc2    SKIP 1
.tmpi     SKIP 1
.fx       SKIP 1
.fy       SKIP 1
.num      SKIP 2
.dch      SKIP 1
.nz       SKIP 1
.di       SKIP 1
.seed     SKIP 2
.seedc    SKIP 1
.rmod     SKIP 1
.pcnt     SKIP 1
.wcnt     SKIP 1
.speedcnt SKIP 1
.headx    SKIP 1
.heady    SKIP 1
.nx       SKIP 1
.ny       SKIP 1
.dir      SKIP 1
.newdir   SKIP 1
.headidx  SKIP 1
.tailidx  SKIP 1
.growth   SKIP 1
.eatval   SKIP 1
.eaten    SKIP 1
.secdown  SKIP 2
.secflag  SKIP 1
.ltime    SKIP 1
.lval     SKIP 1
.tmps     SKIP 1
.thin     SKIP 1
.startx   SKIP 1
.starty   SKIP 1
.initlen  SKIP 1
.segi     SKIP 1
.tcol     SKIP 1
.trow     SKIP 1
.tmask    SKIP 1
.tcell    SKIP 1
.sadupd   SKIP 1
.sadwait  SKIP 1
.sadlo    SKIP 1
.sadhi    SKIP 1
.atime    SKIP 1
.ox       SKIP 1
.oy       SKIP 1
.lx       SKIP 1
.ly       SKIP 1
.score    SKIP 2
.hiscore  SKIP 2
.bufx     SKIP 256                  \ ring buffer of the snake's x positions
.bufy     SKIP 256                  \ ring buffer of the snake's y positions
.shadow   SKIP 1000                 \ copy of the screen (never read back from the VDU)
.memend

PRINT "Code:", ~start, "-", ~codeend, " length", codeend - start
PRINT "Work memory up to:", ~memend

SAVE "SNAKE", start, codeend, start
