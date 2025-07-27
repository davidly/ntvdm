# this compiler: g++ (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
# generates bad code using -O3 and -Ofast. I've yet to get to the bottom of this compiler bug.

g++ -flto -ggdb -O2 -fno-builtin -D NDEBUG -I . ntvdm.cxx i8086.cxx -o ntvdm -fopenmp -static
