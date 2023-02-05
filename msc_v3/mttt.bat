@echo off

del ttt16.exe
del ttt16.obj

rem use "ntvdm cl /help" to get command-line arguments
ntvdm -t cl /Ox /AS /Gs /Ze -I inc -L lib ttt16.c
if %ERRORLEVEL% NEQ 0 goto alldone

ntvdm -t link ttt16,,ttt16,lib\slibfp+lib\slibc+lib\em
if %ERRORLEVEL% NEQ 0 goto alldone

ntvdm -p ttt16

:alldone

