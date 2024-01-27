@echo off
setlocal

del ntvdm.obj >nul 2>nul
del i8086.obj >nul 2>nul

cl /W4 /wd4996 /nologo /jumptablerdata /I. /EHsc /DNDEBUG /GS- /Ot /Ox /Ob3 /Oi /Qpar /Zi /Fa /FAsc ntvdm.cxx i8086.cxx /link /OPT:REF user32.lib

