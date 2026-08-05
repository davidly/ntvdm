@echo off
del test86.obj >nul 2>nul
cl /nologo test86.cxx ..\i8086.cxx /I.. /I. /EHsc /DDEBUG /O2 /Oi /Fa /Qpar /Zi /jumptablerdata /link /OPT:REF user32.lib 



