ntvdm -c tasm /la int21_8.asm
ntvdm -c tlink  /m /l /s int21_8.obj
hd int21_8.exe /q /o:512 /n

