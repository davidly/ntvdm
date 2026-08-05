wk /o /s:*.json /p:tests /f ..\..\armos\armos -m:200 ..\test86 {p} >out.txt 2>&1
diff baseline_out.txt out.txt


