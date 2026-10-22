@echo off
rem Needs MinGW-w64 (gcc, g++, windres) in PATH. Run from this folder.
if not exist obj mkdir obj
for %%f in (resid-fp\*.cc resid_wrap.cc) do g++ -O2 -w -c %%f -o obj\%%~nf.o
windres app.rc -O coff -o obj\app_res.o
gcc -O2 -w -c main_win.c -o obj\main_win.o
g++ -mwindows -static -o ..\system5.exe obj\*.o -lgdi32 -lwinmm -lcomdlg32 -lshell32 -lm
