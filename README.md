# ntvdm
NT Virtual DOS Machine. Not the real one, but this one runs on 64-bit Windows (x64 and ARM64)

This code emulates the i8086 and DOS well enough to run some command-line and text-mode apps. I wrote it
so I could test my BA BASIC compiler (in the TTT repo). I've tested it with:

    Turbo Pascal 1.00A and apps it creates
    Turbo Pascal 3.02A and apps it creates
    Masm V1.10
    Link V2.00 and apps it creates
    QuickBasic 7.1 and apps it creates
    WordStar Professional Release 4 for DOS
    GWBasic in both command-line and full-screen text mode
    Brief 3.1. -k must be passed on the Brief command line to enable "compatible" keyboard handling
    
For all of the above apps, attempts to run nested apps like command.com and the QuickBasic compiler fail.
However, running Turbo Pascal apps created within those apps works.

This code implements no graphics, sound, mouse, or anything else not needed for simple command-line apps.

i8086 emulation performance is similar to other C/C++ emulators.

I validated that ntvdm works on Arm64 with both native and x64 binaries.

I can't vouch for 100% i8086 emulation because I can't find any apps that perform such validation, unlike
what's out there for the 6502, 8080, Z80, and other earlier CPUs. But I have done a fair bit of testing 
with many apps and compared instruction + register traces with other emulators.

djl8086d.hxx is an 8086 disassembler that's used when tracing instructions. It's useful when debugging why
apps don't work properly.

Cycle counts are conditionally computed based on a #define in i8086.hxx. Using this, the emulator can
simulate running at a given clock rate. Cycle counts vary widely between various spec docs I found online,
and the code doesn't check for misaligned memory access, get details of mult/div correct, or otherwise
get any closer than about 25% of what would be accurate. It's in the ballpark.

    usage: ntvdm [arguments] <DOS executable> [arg1] [arg2]
      notes:
                -c     don't auto-detect apps that want 80x25 then set window to that size
                -C     always set window to 80x25
                -i     trace instructions as they are executed (this is verbose!)
                -p     show performance information
                -s:X   speed in Hz. Default is 0, which is as fast as possible.
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
