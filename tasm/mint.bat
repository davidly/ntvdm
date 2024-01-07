@echo off
setlocal
rem builds c++ arrays with 8086 machine code to handle various interrupts in ntvdm

echo // machine code for various interrupts. generated with mint.bat. >asm.txt

set _intlist="int16_0" "int21_1" "int21_8" "int21_a" "int21_3f"

( for %%t in (%_intlist%) do ( call :buildit %%t ) )

echo // end of machine code >>asm.txt
echo. >>asm.txt

goto :eof

:buildit

ntvdm tasm /t /la %~1.asm
ntvdm tlink /m /l /s %~1.obj >nul
echo uint64_t %~1_code[] = { >>asm.txt
hd %~1.exe /q /o:512 /n >>asm.txt
echo }; >>asm.txt

exit /b 0

:eof
