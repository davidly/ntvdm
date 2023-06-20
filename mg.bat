@echo off
setlocal

path=d:\mingw64\bin;%path%

g++ -Ofast -ggdb -D _MSC_VER -D _GNU_CPP ntvdm.cxx i8086.cxx -I ../djl -D DEBUG -o ntvdmg.exe -static


