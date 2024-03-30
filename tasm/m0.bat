ntvdm -c tasm /la int16_0.asm
ntvdm -c tlink /m /l /s int16_0.obj
hd int16_0.exe /q /o:512 /n

