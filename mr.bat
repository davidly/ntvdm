@echo off
del ntvdm.obj >nul 2>nul
del i8086.obj >nul 2>nul

rem cl /nologo ntvdm.cxx i8086.cxx /jumptablerdata /I. /EHsc /DNDEBUG /GS- /Ot /Ox /Ob2 /Oi /Qpar /Zi /Fa /FAsc /link /OPT:REF user32.lib

cl /W4 /wd4996 /nologo ntvdm.cxx i8086.cxx /jumptablerdata /I. /EHsc /DNDEBUG /GS- /Ot /Ox /Ob2 /Oi /Qpar /Zi /Fa /FAsc /link /OPT:REF user32.lib

