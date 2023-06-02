# ntvdm
NT Virtual DOS Machine. Not the real one, but this one runs on 64-bit Windows (x64 and ARM64)

This code emulates the i8086 and DOS well enough to run some command-line and text-mode apps. I wrote it
so I could test my BA BASIC compiler (in the TTT repo). I've tested it with:

    Turbo Pascal 1.00A and apps it creates
    Turbo Pascal 3.02A and apps it creates
    Masm V1.10
    Link V2.00 and apps it creates
    QuickBasic (qbx.exe) 7.1 and apps the compiler creates
    QBasic (qbasic.exe) 7.1
    WordStar Professional Release 4 for DOS
    GWBasic in both command-line and full-screen text mode
    Brief 3.1. -k must be passed on the Brief command line to enable "compatible" keyboard handling
    ExeHr.exe: Microsoft (R) EXE File Header Utility  Version 2.01  
    BC.exe: Microsoft Basic compiler 7.10, part of Quick Basic.
    Link.exe: Microsoft (R) Segmented-Executable Linker  Version 5.10 
    Mips.com Version 1.20 from Chips and Technologies.
    Microsoft 8086 Object Linker Version 3.01 (C) Copyright Microsoft Corp 1983, 1984, 1985
    Microsoft C Compiler V1.04 & Microsoft Object Linker V1.10 (C) Copyright 1981 by Microsoft Inc.
    Microsoft C Compiler V2.03 & Microsoft 8086 Object Linker Version 2.40  (C) Copyright Microsoft Corp 1983
    Microsoft C Compiler Version 3.00 (C) Copyright Microsoft Corp 1984 1985  
    
This code implements no graphics, sound, mouse, or anything else not needed for simple command-line apps.

i8086 emulation performance is similar to other C/C++ emulators.

I validated that ntvdm works on Arm64 with both native and x64 binaries.

Ntvdm works with apps that hook interrupts via the DOS mechanism or by directly writing to memory. This
includes support for int 9 and int 0x1c in apps like Quick Basic and Brief.

I can't vouch for 100% i8086 emulation because I can't find any apps that perform such validation, unlike
what's out there for the 6502, 8080, Z80, and other earlier CPUs. But I have done a fair bit of testing 
with many apps and compared instruction + register traces with other emulators.

djl8086d.hxx is an 8086 disassembler that's used when tracing instructions. It's useful when debugging why
apps don't work properly.

Build with m.bat or mr.bat for debug and release on Windows. Or use g++ instead using mg.bat or mgr.bat.
G++ versions are 20% faster than Microsoft C++ versions.

Cycle counts are conditionally computed based on a #define in i8086.hxx. Using this, the emulator can
simulate running at a given clock rate. Cycle counts vary widely between various spec docs I found online,
and the code doesn't check for misaligned memory access, get details of mult/div correct, or otherwise
get any closer than about 25% of what would be accurate. It's in the ballpark. I tested against a physical
8088 running at 4.77Mhz. That CPU takes extra cycles for memory access because of the narrower bus. It runs
about 32% slower than this simulated 8086 at 4.77Mhz, which seems reasonably close.

Using mips.com Version 1.20, a benchmark app from 1986 written by Chips and Technologies, if the clock
is set to /s:3900000 (3.9 Mhz) the 8086 emulator runs at about the same speed as a 4.77Mhz 8088. Given the
wide variability online regarding the performance differences between the 8088 and 8086 (5%-50%) this 
seems close. I validated the mips.com results on an actual 8088 running at 4.77Mhz.

The msc_v3 folder contains command.c, a greatly simplified replacement for command.com that can be built
with that compiler. It's handy for when apps like WordStar and QBX shell out to command.com.

Folders with test apps:

    gwbasic -- gwbasic 3.22
    msc_v3 -- Microsoft C Compiler Version 3.00
    turbodos -- Turbo Pascal 1.00A
    turbo3dos -- Turbo Pascal 3.02A
    qbx -- Quick Basic 7.1
    wordstar -- WordStar Professional Release 4

Usage information:

    usage: ntvdm [arguments] <DOS executable> [arg1] [arg2]
      notes:
                -c     don't auto-detect apps that want 80x25 then set window to that size;
                       stay in teletype mode.
                -C     always set window to 80x25; don't use teletype mode.
                -d     don't clear the display when in 80x25 mode on app exit
                -i     trace instructions as they are executed to ntvdm.log (this is verbose!)
                -p     show performance information
                -s:X   emulated speed in Hz. Default is to run as fast as possible.
                       for 4.77Mhz, use -s:4770000
                -t     enable debug tracing to ntvdm.log
     [arg1] [arg2]     arguments after the .COM/.EXE file are passed to that command
      examples:
          ntvdm -c -t app.com foo bar
          ntvdm turbo.com
          ntvdm s:\github\MS-DOS\v2.0\bin\masm small,,,small
          ntvdm s:\github\MS-DOS\v2.0\bin\link small,,,small
          ntvdm -t b -k myfile.asm
          
sample usage:

    C:\>ntvdm -c -p ttt8086.com
    5.7 seconds
    moves: 6493
    iterations: 1000
    8086 cycles:         7,612,099,336
    clock rate:              unbounded
    approx ms at 4.77Mhz:    1,595,827  == 0 days, 0 hours, 26 minutes, 35 seconds, 827 milliseconds
    kernel CPU ms:                   0
    user CPU ms:                 5,687
    total CPU ms:                5,687
    elapsed ms:                  5,728
