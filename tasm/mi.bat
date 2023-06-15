ntvdm -c tasm /la %1.asm
ntvdm -c tlink  /m /l /s %1.obj
echo uint64_t %1_code[] = { >>asm.txt
hd %1.exe /q /o:512 /n >>asm.txt
echo }; >>asm.txt

