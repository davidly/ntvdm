ntvdm -c tasm /la int21_3f.asm
ntvdm -c tlink /m /l /s int21_3f.obj
hd int21_3f.exe /q /o:512 /n

