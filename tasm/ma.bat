ntvdm -c tasm /la int21_a.asm
ntvdm -c tlink  /m /l /s int21_a.obj
hd int21_a.exe /q /o:512 /n

