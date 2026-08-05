wk /o /s:*.json /p:tests\undocumented /f test86 {p} >outu.txt 2>&1
diff baseline_outu.txt outu.txt

