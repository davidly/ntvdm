@echo off
cl /nologo ntvdm.cxx i8086.cxx /I. /EHsc /DDEBUG /O2 /Oi /Fa /Qpar /Zi /link /OPT:REF user32.lib


