del sieve.exe
ntvdm -t cl -I inc -L lib sieve.c
ntvdm -c -t link sieve,,sieve,lib\slibfp.lib+lib\slibc+lib\em.lib
ntvdm -p sieve

