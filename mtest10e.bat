d:\mingw64\bin\nasm -f bin -o test10e.com test10e.s

if %ERRORLEVEL% NEQ 0 goto eof

ntvdm /t /i /c test10e.com

:eof




