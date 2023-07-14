@echo off
cl /nologo ntvdm.cxx i8086.cxx /I. /EHsc /DNDEBUG /GS- /Ot /Ox /Ob2 /Oi /Fa /Qpar /Zi /Fa /FAsc /link /OPT:REF user32.lib

