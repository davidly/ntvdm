del command.com
del command.exe
del command.obj

ntvdm -t cl /Ze /AL -I inc -L lib command.c
if %ERRORLEVEL% NEQ 0 goto alldone

rem ntvdm -c -t link command,,command,lib\slibfp+lib\slibc+lib\em
ntvdm -c -t link command,,command,lib\llibfp+lib\llibc+lib\em
if %ERRORLEVEL% NEQ 0 goto alldone

ren command.exe command.com
ntvdm -t command

:alldone


