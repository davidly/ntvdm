d:\mingw64\bin\nasm -f bin -o bs.bin bs.s

if %ERRORLEVEL% NEQ 0 goto eof

ntvdm /t /i /b /c bs.bin

:eof



