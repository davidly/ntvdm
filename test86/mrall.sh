for optflag in 0 1 2 3 fast;
do
    mkdir bin"$optflag" 2>/dev/null
    mkdir clangbin"$optflag" 2>/dev/null

    _gnubuild="g++ -ggdb -fno-builtin -Wno-unused-result -fsigned-char -D NDEBUG -I .. -I . test86.cxx ../i8086.cxx -o bin"$optflag"/test86 -O"$optflag" -static"
    _clangbuild="clang-18 -x c++ test86.cxx ../i8086.cxx -I .. -I . -o clangbin"$optflag"/test86 -O"$optflag" -static -fno-builtin -D NDEBUG -fsigned-char -Wno-unused-result -Wno-format -Wno-format-security -std=c++14 -lm -lstdc++"

    if [ "$optflag" != "fast" ]; then
        $_clangbuild &
        $_gnubuild &
    else    
        $_clangbuild
        $_gnubuild
    fi
done

for optflag in 0 1 2 3 fast;
do
    cp bin"$optflag"/test86 /mnt/c/users/david/onedrive/ntvdm/test86/x64bin"$optflag"
    cp clangbin"$optflag"/test86 /mnt/c/users/david/onedrive/ntvdm/test86/x64clangbin"$optflag"
done
