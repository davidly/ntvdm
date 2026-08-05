@echo off
setlocal

set _optlist=bin0 bin1 bin2 bin3 binfast clangbin0 clangbin1 clangbin2 clangbin3 clangbinfast

( for %%a in (%_optlist%) do ( call :appRun %%a ) )

goto :eof

:appRun

wk /o /s:*.json /p:tests\undocumented /f ..\..\armos\armos -m:200 ..\..\%~1\test86 {p} >outu_%~1.txt 2>&1
diff baseline_outu.txt outu_%~1.txt

exit /b 0

:eof

