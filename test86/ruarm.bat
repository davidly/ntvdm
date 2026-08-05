wk /o /s:*.json /p:tests\undocumented /f ..\..\armos\armos -m:200 ..\..\test86 {p} >outu.txt 2>&1
diff baseline_outu.txt outu.txt


