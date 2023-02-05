del tp.exe
ntvdm -c bc tp.bas tp.obj tp.lst /O
ntvdm -c link tp,,tp,.\,nul.def
ntvdm -c tp

