@echo off
cl /nologo ntvdm.cxx i8086.cxx /I. /EHsc /DNDEBUG /Ot /Ox /Ob2 /Oi /Fa /Qpar /Zi /link /OPT:REF user32.lib

