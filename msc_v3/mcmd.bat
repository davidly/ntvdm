@echo off

del command.com
del command.exe
del command.obj

ntvdm -t cl /Os /Fa /Fm /Ze /AL -I inc -I inc\sys command.c /link lib\
if %ERRORLEVEL% NEQ 0 goto alldone

rem wow this is a hack, but it works
ren command.exe command.com
ntvdm -t command

:alldone


