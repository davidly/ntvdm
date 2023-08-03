@echo off
setlocal

path=d:\mingw64\bin;%path%

del ntvdm.obj >nul 2>nul
del i8086.obj >nul 2>nul
g++ -Ofast -ggdb -D _MSC_VER -D _GNU_CPP ntvdm.cxx i8086.cxx -I ../djl -D NDEBUG -o ntvdmg.exe -static


