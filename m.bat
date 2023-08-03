@echo off
del ntvdm.obj >nul 2>nul
del i8086.obj >nul 2>nul
cl /nologo ntvdm.cxx i8086.cxx /I. /EHsc /DDEBUG /O2 /Oi /Fa /Qpar /Zi /jumptablerdata /link /OPT:REF user32.lib


