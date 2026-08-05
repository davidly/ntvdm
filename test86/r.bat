wk /o /s:*.json /p:tests /f test86 {p} >out.txt 2>&1
diff baseline_out.txt out.txt


