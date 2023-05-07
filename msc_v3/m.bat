@echo off

del %1.exe
del %1.obj

rem use "ntvdm cl /help" to get command-line arguments
ntvdm -t cl /Ox /AS /Gs /Ze -I inc -L lib %1.c
if %ERRORLEVEL% NEQ 0 goto alldone

ntvdm -t link %1,,%1,lib\slibfp+lib\slibc+lib\em
if %ERRORLEVEL% NEQ 0 goto alldone

ntvdm -p %1

:alldone

