[README.md](https://github.com/user-attachments/files/33814207/README.md)
# Acorn System 5 Emulator (Windows)

An emulator for the Acorn System 5 (6502 based). It runs on Windows as a single program (`system5.exe`): no installation and no extra DLLs needed.

For private and hobby use. The teletext font and character rounding, and the SID emulation (reSID-fp), are taken from [b-em](https://github.com/stardot/b-em) (GPL); see `source/b-em-COPYING.txt`.

## Two machine models

Choose the model in **Machine > Model**. A Break follows automatically when you switch.

### 1. Custom System 5 — IMADOS 1.41, 40x25 teletext, SID

| Item | Address / description |
|------|-----------------------|
| RAM | `&0000-&9FFF` |
| BASIC | `&A000-&DFFF` (BBC BASIC II) |
| IMADOS | `&E000-&FFFF` |
| VIA1 | `&0E20` (keyboard at `&0E21`, 100 Hz timer) |
| VIA2 | `&0C00` (printer port; printed characters are written to `printer.txt`) |
| VDU | RAM `&0400-&07FF` (teletext 40x25), 6845/HD46505 CRTC at `&0800` |
| FDC | 8271 at `&0A00` |
| SID | 6581 at `&0C60` |
## About IMADOS 1.41

**IMADOS is built from Acorn DOS with the following additions:**

* BBC like Boot sound using SID
* All Acorn DOS Floppy based utilities built-in
* HELP command, shows all built-in commands
* BASIC command, starts BBC Basic


**Supports the following BBC BASIC II functions:**

* VDU7 (ctrl-G) beep using SID
* TIME (like the BBC using VIA T1 as a 100Hz IRQ routine)

[IMADOS.md](https://github.com/user-attachments/files/33819301/IMADOS.md)

### 2. Standard System 5 — AcornDOS, no SID

| Item | Address / description |
|------|-----------------------|
| RAM | `&0000-&07FF` and `&2000-&BFFF` |
| AcornDOS | `&F000-&FFFF` |
| VDU (choose one) | **40x25 teletext**: video RAM `&0400-&07FF`, CRTC at `&0800` <br> **80x25 mono**: video RAM `&1000-&17FF`, CRTC at `&1840`, character ROM `font80.rom` |
| System BASIC | `&C000-&CFFF` (can be switched on/off) |
| Floating point ROM | `&D000-&DFFF` (can be switched on/off) |

Notes for the standard model:

- The VDU choice also selects the matching AcornDOS ROM (DOS40A or DOS80A).
- The 80x25 colour (white / green / amber) is set in the **View** menu.
- When a ROM is switched off, its area is plain RAM, so BASIC / FP can also be loaded from disk.
- **Start BASIC:** use **Machine > Start BASIC** (it types `GO C2B2`), or type `GO C2B2` yourself.
- **Floating point:** use `FPRINT` instead of `PRINT`, for example `FPRINT PI` or `FPRINT 22/7`. With the FP ROM switched off, `FPRINT` gives an error.
- When the standard model is selected, `DOS_Utilities.ssd` is mounted in drive 0 automatically.

## Keys and menus

| Key | Action |
|-----|--------|
| `F12` | Break |
| `F11` | Space-Break (boots the disk) |
| `F8` | Smoothing (bilinear scaling) on/off |
| `Caps Lock` | Uppercase on/off (switched on at start; Shift inverts, like a real keyboard) |
| `Ctrl+V` | Paste text from the clipboard as typing |
| Cursor keys | `Ctrl-A` / `Ctrl-S` / `Ctrl-W` / `Ctrl-Z` (cursor movement) |
| `End` | `Ctrl-Q` (copy) |
| `Backspace` | DELETE |

Drop a `.ssd` file on the window to mount it in drive 0 (hold **Shift** for drive 1).

| Menu | Contents |
|------|----------|
| **File** | Mount / eject disks in drive 0 and 1, exit |
| **Machine** | Break, Space-Break, Start BASIC, model, standard System 5 options, paste, Caps Lock |
| **View** | Smoothing, 80x25 display colour, window size (Small 640x480, Normal 800x600, Large 1280x960) |
| **Help** | About / keys, version |

The picture is shown in a 4:3 frame with a black border (6.5% left/right, 25 pixels top/bottom at 800x600).

## Files next to the exe

All optional: copies of the ROMs are built into the exe. If you put a ROM file next to the exe, it replaces the built-in copy.

Disks are `.ssd` images (single sided, 10 sectors of 256 bytes per track, 40 or 80 tracks). Writes go straight to the `.ssd` file, so keep a copy of any disk you want to protect.

## What is emulated

- 6502 at 1 MHz (passes the Klaus Dormann functional test, including decimal mode)
- 2 x 6522 VIA, 6845/HD46505 CRTC, SAA5050 teletext, 8271 floppy controller, 6581 SID (custom model only)
- Drive motor and ready line: after about 3 seconds without disk activity the drive reports "not ready", and after a disk change the OS re-reads the catalogue.

**Not emulated:** double sided disks (`.dsd`), the 6850 serial port and 8255 parallel port (present but inert), Econet.

## Change log

### Version 1.1.0
- New: second machine model "Standard System 5" with AcornDOS (40x25 teletext or 80x25 mono VDU), System BASIC at `&C000` and optional Floating Point ROM at `&D000`, without SID
- New: 80x25 mono VDU with character ROM, cursor and scrolling through the 6845; white/green/amber
- New: Machine > Start BASIC; settings are remembered in `system5.ini`
- `DOS_Utilities.ssd` included


## Building from source

The source is in the `source` folder (C, plus the C++ reSID-fp files). It needs MinGW-w64 (`gcc`, `g++`, `windres`). Run `source\build.bat` from inside the `source` folder.
