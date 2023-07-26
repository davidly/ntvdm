// NT Virtual DOS Machine. Not the real one, but one that works on 64-bit Windows.
// Written by David Lee in late 2022
// This only simulates a small subset of DOS and BIOS behavior.
// I only implemented BIOS/DOS calls used by tested apps, so there are some big gaps.
// Only CGA text modes 2 and 3 (80x25 greyscale and color) are supported.
// No graphics, sound, mouse, or anything else not needed for simple command-line apps.
// tested apps:
//    Turbo Pascal 1.00A, 3.02A, 5.5, and 7.0, both the apps and the programs they generate.
//    masm.exe V1.10 and link.exe V2.00 from MS-DOS v2.0
//    ba BASIC compiler in my ttt repo
//    Wordstar Release 4
//    GWBASIC.COM.
//    QBASIC 7.1.
//    Apps the QBASIC compiler creates.
//    Brief 3.1. Use b.exe's -k flag for compatible keyboard handling. (automatically set in code below)
//    ExeHdr: Microsoft (R) EXE File Header Utility  Version 2.01
//    Link.exe: Microsoft (R) Segmented-Executable Linker  Version 5.10
//    BC.exe: Microsoft Basic compiler 7.10 (part of Quick Basic 7.1)
//    Microsoft 8086 Object Linker Version 3.01 (C) Copyright Microsoft Corp 1983, 1984, 1985
//    Microsoft C v1, v2, and v3
//    Turbo C 1.0 and 2.0
//    QuickC 1.0 and 2.x. ilink.exe in 2.x doesn't work because it relies on undocumented MCB behavior.
//    Quick Pascal 1.0
//    Lotus 1-2-3 v1.0a
//    Word for DOS 6.0.
//    Multiplan v2. Setup for later versions fail, but they fail in dosbox too.
//    Microsoft Works v3.0.
//    Turbo Assembler tasm.
//
// I went from 8085/6800/Z80 machines to Amiga to IBM 360/370 to VAX/VMS to Unix to
// Windows to OS/2 to NT to Mac to Linux, and mostly skipped DOS programming. Hence this fills a gap
// in my knowledge.
//
// Useful: http://www2.ift.ulaval.ca/~marchand/ift17583/dosints.pdf
//         https://en.wikipedia.org/wiki/Program_Segment_Prefix
//         https://stanislavs.org/helppc/bios_data_area.html
//         https://stanislavs.org/helppc/scan_codes.html
//         http://www.ctyme.com/intr/int.htm
//         https://grandidierite.github.io/dos-interrupts/
//
// Memory map:
//     0x00000 -- 0x003ff   interrupt vectors; only claimed first x40 of slots for bios/DOS
//     0x00400 -- 0x0057f   bios data
//     0x00580 -- 0x005ff   "list of lists" is 0x5b0 and extends in both directions
//     0x00600 -- 0x00bff   assembly code for interrupt routines that can't be accomplished in C
//     0x00c00 -- 0x00fff   C code for interrupt routines (here, not in BIOS space because it fits)
//     0x01000 -- 0xb7fff   apps are loaded here. On real hardware you can only go to 0x9ffff.
//     0xb8000 -- 0xeffff   reserved for hardware (CGA in particular)
//     0xf0000 -- 0xfbfff   system monitor (0 for now)
//     0xfc000 -- 0xfffff   bios code and hard-coded bios data (mostly 0 for now)

#include <djl_os.hxx>
#include <sys/timeb.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <assert.h>
#include <vector>

#include <djltrace.hxx>
#include <djl_con.hxx>
#include <djl_cycle.hxx>
#include <djl_durat.hxx>
#include <djl_thrd.hxx>
#include <djl8086d.hxx>
#include "i8086.hxx"

uint16_t AllocateEnvironment( uint16_t segStartingEnv, const char * pathToExecute, const char * pcmdLineEnv );
uint16_t LoadBinary( const char * app, const char * acAppArgs, uint16_t segment, bool setupRegs,
                     uint16_t * reg_ss, uint16_t * reg_sp, uint16_t * reg_cs, uint16_t * reg_ip );
uint16_t LoadOverlay( const char * app, uint16_t segLoadAddress, uint16_t segmentRelocationFactor );

uint16_t round_up_to( uint16_t x, uint16_t multiple )
{
    if ( 0 == ( x % multiple ) )
        return x;

    return x + ( multiple - ( x % multiple ) );
} //round_up_to

// machine code for various interrupts. generated with mint.bat. 
uint64_t int16_0_code[] = { 
0x01b4c08e0040b806, 0x068326fafa741669, 0x001a3e832602001a, 0x001a06c726077c3e, 0x000000cb07fb001e, 
}; 
uint64_t int21_8_code[] = { 
0x00cb08b416cd00b4, 
}; 
uint64_t int21_a_code[] = { 
0x000144c6f28b5653, 0xcd01b43674003c80, 0x3c16cd00b4fa7416, 0x7400017c800c7508, 0x339010eb014cfeec, 0x3c024088015c8adb, 0xd08a0144fe10740d, 0x3a01448a21cd02b4, 
0x00cb5b5ef8ca7504, 
}; 
uint64_t int21_3f_code[] = { 
0x00f1bf5657525153, 0x0000eb06c72ef28b, 0xc5e9037500f98300, 0x50b9037e50f98300, 0xa12e00e90e892e00, 0x7c00ef063b2e00ed, 0xb4fa7416cd01b46d, 0x00e93e832e16cd00, 
0x2e1075083c147401, 0x2ee2740000ef3e83, 0x2e901eeb00ef0eff, 0x2e01882e00ef1e8b, 0x22740d3c00ef06ff, 0xe93e832e0875083c, 0x02b4d08a06740100, 0x3b2e00efa12e21cd, 
0x004f3d197400e906, 0x2e00ef1e8b2ea775, 0x00ef06ff2e0a01c6, 0x8b2e21cd02b40ab2, 0x8b2e018b2e00ed1e, 0x06ff2e008900eb1e, 0x2e00ed06ff2e00eb, 0x00ef063b2e00eda1, 
0x0000ed06c72e1175, 0x000000ef06c72e00, 0x2e00e9a12e900ceb, 0xa12ec07500eb063b, 0x5b595a5f5ef800eb, 0x00000000000000cb, 0x0000000000000000, 0x0000000000000000, 
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 
0x0000000000000000, 
}; 
// end of machine code 

uint8_t * GetMem( uint16_t seg, uint16_t offset )
{
    return memory + ( ( ( (uint32_t) seg ) << 4 ) + offset );
} //GetMem

uint16_t GetSegment( uint8_t * p )
{
    uint64_t ptr = (uint64_t) p;
    uint64_t mem = (uint64_t) memory;
    ptr -= mem;
    return (uint16_t) ( ptr >> 4 );
} //GetSegment

struct FileEntry
{
    char path[ MAX_PATH ];
    FILE * fp;
    uint16_t handle; // DOS handle, not host OS
    bool writeable;
    uint16_t seg_process; // process that opened the file
    uint16_t refcount;

    void Trace()
    {
        tracer.Trace( "      handle %04x, path %s, owning process %04x\n", handle, path, seg_process );
    }
};

struct AppExecuteMode3
{
    uint16_t segLoadAddress;
    uint16_t segmentRelocationFactor;

    void Trace()
    {
        tracer.Trace( "  AppExecuteMode3:\n" );
        tracer.Trace( "    segLoadAddress: %#x\n", segLoadAddress );
        tracer.Trace( "    segmentRelocationFactor: %#x\n", segmentRelocationFactor );
    } //Trace
};

struct AppExecute
{
    uint16_t segEnvironment;            // for mode 3, this is segment load address
    uint16_t offsetCommandTail;         // for mode 3, this is segment relocation factor
    uint16_t segCommandTail;
    uint16_t offsetFirstFCB;
    uint16_t segFirstFCB;
    uint16_t offsetSecondFCB;
    uint16_t segSecondFCB;
    uint16_t func1SP; // these 4 are return values if al = 1
    uint16_t func1SS;
    uint16_t func1IP;
    uint16_t func1CS;

    void Trace()
    {
        tracer.Trace( "  app execute block: \n" );
        tracer.Trace( "    segEnvironment:    %04x\n", segEnvironment );
        tracer.Trace( "    offsetCommandTail: %04x\n", offsetCommandTail );
        tracer.Trace( "    segCommandTail:    %04x\n", segCommandTail );
    }
};

struct ExeHeader
{
    uint16_t signature;
    uint16_t bytes_in_last_block;
    uint16_t blocks_in_file;
    uint16_t num_relocs;
    uint16_t header_paragraphs;
    uint16_t min_extra_paragraphs;
    uint16_t max_extra_paragraphs;
    uint16_t ss;
    uint16_t sp;
    uint16_t checksum;
    uint16_t ip;
    uint16_t cs;
    uint16_t reloc_table_offset;
    uint16_t overlay_number;
};

struct ExeRelocation
{
    uint16_t offset;
    uint16_t segment;
};

struct DosAllocation
{
    uint16_t segment;          // segment handed out to the app, 1 past the MCB
    uint16_t para_length;      // length in paragraphs including MCB
    uint16_t seg_process;      // the process PSP that allocated the memory or 0 prior to the app running
};

struct IntCalled
{
    uint8_t i;      // interrupt #
    uint16_t c;     // ah command (0xffff if n/a)
    uint32_t calls; // # of times invoked
};

const uint8_t DefaultVideoAttribute = 7;                          // light grey text
const uint32_t ScreenColumns = 80;                                // this is the only mode supported
const uint32_t ScreenRows = 25;                                   // this is the only mode supported
const uint32_t ScreenColumnsM1 = ScreenColumns - 1;               // columns minus 1
const uint32_t ScreenRowsM1 = ScreenRows - 1;                     // rows minus 1
const uint32_t ScreenBufferSize = 2 * ScreenColumns * ScreenRows; // char + attribute
const uint32_t ScreenBufferSegment = 0xb800;                      // location in i8086 physical RAM of CGA display. 16k, 4k per page.
const uint32_t MachineCodeSegment = 0x0060;                       // machine code for keyboard/etc. starts here
const uint16_t SegmentHardware = ScreenBufferSegment;             // where hardware starts (unlike real machines, which start at 0xa000)
const uint16_t AppSegment = 0x1000 / 16;                          // base address for apps in the vm. 4k. DOS uses 0x1920 == 6.4k
const uint32_t DOS_FILENAME_SIZE = 13;                            // 8 + 3 + '.' + 0-termination
const uint16_t InterruptRoutineSegment = 0x00c0;                  // interrupt routines start here.
const uint32_t firstAppTerminateAddress = 0xf000dead;             // exit ntvdm when this is the parent return address
const uint16_t SegmentListOfLists = 0x50;
const uint16_t OffsetListOfLists = 0xb0;
#if 0
const uint16_t OffsetSystemFileTable = 0xf0;
#endif

CDJLTrace tracer;

static uint16_t blankLine[ScreenColumns] = {0};    // an optimization for filling lines with blanks
static std::mutex g_mtxEverything;                 // one mutex for all shared state
static ConsoleConfiguration g_consoleConfig;       // to get into and out of 80x25 mode
static HANDLE g_hFindFirst = INVALID_HANDLE_VALUE; // used for find first / find next
static HANDLE g_hConsoleOutput = 0;                // the Windows console output handle
static HANDLE g_hConsoleInput = 0;                 // the Windows console input handle
static bool g_haltExecution = false;               // true when the app is shutting down
static uint16_t g_diskTransferSegment = 0;         // segment of current disk transfer area
static uint16_t g_diskTransferOffset = 0;          // offset of current disk transfer area
static vector<FileEntry> g_fileEntries;            // vector of currently open files
static vector<DosAllocation> g_allocEntries;       // vector of blocks allocated to DOS apps
static uint8_t g_videoMode = 3;                    // 2=80x25 16 grey, 3=80x25 16 colors
static uint16_t g_currentPSP = 0;                  // psp of the currently running process
static bool g_use80x25 = false;                    // true to force 80x25 with cursor positioning
static bool g_forceConsole = false;                // true to force teletype mode, with no cursor positioning
static bool g_int16_1_loop = false;                // true if an app is looping to get keyboard input. don't busy loop.
static bool g_KbdIntWaitingForRead = false;        // true when a kbd int happens and no read has happened since
static bool g_KbdPeekAvailable = false;            // true when peek on the keyboard sees keystrokes
static bool g_injectControlC = false;              // true when ^c is hit and it must be put in the keyboard buffer
static int g_appTerminationReturnCode = 0;         // when int 21 function 4c is invoked to terminate an app, this is the app return code
static char g_acApp[ MAX_PATH ];                   // the DOS .com or .exe being run
static char g_thisApp[ MAX_PATH ];                 // name of this exe (argv[0]), likely NTVDM
static char g_lastLoadedApp[ MAX_PATH ] = {0};     // path of most recenly loaded program (though it may have terminated)
static bool g_PackedFileCorruptWorkaround = false; // if true, allocate memory starting at 64k, not AppSegment
static uint16_t g_int21_3f_seg = 0;                // segment where this code resides with ip = 0
static uint16_t g_int21_a_seg = 0;                 // "
static uint16_t g_int21_8_seg = 0;                 // "
static uint16_t g_int16_0_seg = 0;                 // "
static char cwd[ MAX_PATH ] = {0};                 // used as a temporary in several locations
static vector<IntCalled> g_InterruptsCalled;       // track interrupt usage
static uint64_t g_startSystemTime;                 // system time at app start

uint8_t * GetDiskTransferAddress() { return GetMem( g_diskTransferSegment, g_diskTransferOffset ); }

#pragma pack( push, 1 )
struct DosFindFile
{
    uint8_t undocumentedA[ 0xc ];  // no attempt to mock this because I haven't found apps that use it
    uint8_t search_attributes;     // at offset 0x0c
    uint8_t undocumentedB[ 0x8  ]; // no attempt to mock this because I haven't found apps that use it

    uint8_t file_attributes;       // at offset 0x15
    uint16_t file_time;            //           0x16
    uint16_t file_date;            //           0x18
    uint32_t file_size;            //           0x1a
    char file_name[ DOS_FILENAME_SIZE ];     // 0x1e      8.3, blanks stripped, null-terminated
};
#pragma pack(pop)

static void usage( char const * perr )
{
    g_consoleConfig.RestoreConsole( false );

    if ( perr )
        printf( "error: %s\n", perr );

    printf( "NT Virtual DOS Machine: emulates an 8086 MS-DOS 3.00 runtime environment enough to run COM/EXE apps\n" );
    printf( "usage: %s [arguments] <DOS executable> [arg1] [arg2]\n", g_thisApp );
    printf( "  notes:\n" );
    printf( "            -c     don't auto-detect apps that want 80x25 then set window to that size;\n" );
    printf( "                   stay in teletype/console mode.\n" );
    printf( "            -C     always set window to 80x25; don't use teletype mode.\n" );
    printf( "            -d     don't clear the display on app exit when in 80x25 mode\n" );
    printf( "            -e     comma-separated list of environment variables. e.g. -e:include=..\\include,lib=..\\lib\n" );
    printf( "            -h     workaround for Packed File Corrupt error: load apps High, above 64k\n" );
    printf( "            -i     trace instructions as they are executed to %s.log (this is verbose!)\n", g_thisApp );
    printf( "            -p     show performance information\n" ); 
#ifdef I8086_TRACK_CYCLES
    printf( "            -s:X   speed in Hz. Default is to run as fast as possible.\n" );
    printf( "                   for 4.77Mhz, use -s:4770000\n" );
    printf( "                   to roughly match a 4.77Mhz 8088, use -s:4500000\n" );
#endif
    printf( "            -t     enable debug tracing to %s.log\n", g_thisApp );
    printf( "            -z:X   applies X as a hex mask to SetProcessAffinityMask, e.g.:\n" );
    printf( "                     /z:11    2 performance cores on an i7-1280P\n" );
    printf( "                     /z:3000  2 efficiency cores on an i7-1280P\n" );
    printf( "                     /z:11    2 random good cores on a 5950x\n" );
    printf( " [arg1] [arg2]     arguments after the .COM/.EXE file are passed to that command\n" );
    printf( "  examples:\n" );
    printf( "      %s -c -t app.com foo bar\n", g_thisApp );
    printf( "      %s turbo.com\n", g_thisApp );
    printf( "      %s s:\\github\\MS-DOS\\v2.0\\bin\\masm small,,,small\n", g_thisApp );
    printf( "      %s s:\\github\\MS-DOS\\v2.0\\bin\\link small,,,small\n", g_thisApp );
    printf( "      %s -t b -k myfile.asm\n", g_thisApp );
    printf( "  %s\n", build_string() );
    exit( 1 );
} //usage

void SleepAndScheduleInterruptCheck()
{
    SleepEx( 1, TRUE );
    cpu.exit_emulate_early(); // fall out of the instruction loop early to check for a timer interrupt
} //SleepAndScheduleInterruptCheck

int ends_with( const char * str, const char * end )
{
    size_t len = strlen( str );
    size_t lenend = strlen( end );

    if ( len < lenend )
        return false;

    return !_stricmp( str + len - lenend, end );
} //ends_with

bool isFilenameChar( char c )
{
    char l = (char) tolower( c );
    return ( ( l >= 'a' && l <= 'z' ) || ( l >= '0' && l <= '9' ) || '_' == c || '^' == c || '$' == c || '~' == c || '!' == c || '*' == c );
} //isFilenameChar

static void trace_all_open_files()
{
    size_t cEntries = g_fileEntries.size();
    tracer.Trace( "  all files, count %d:\n", cEntries );
    for ( size_t i = 0; i < cEntries; i++ )
    {
        FileEntry & fe = g_fileEntries[ i ];
        tracer.Trace( "    file entry %d, fp %p, handle %u, writable %d, process %u, refcount %u, path %s\n", i,
                      fe.fp, fe.handle, fe.writeable, fe.seg_process, fe.refcount, fe.path );
    }
} //trace_all_open_files

static bool tc_build_file_open()
{
    // check if it looks like Turbo C is building something so we won't sleep in dos idle loop, which it calls
    // all the time regardless of whether it's building something.

    static const char * build_ext[] = { ".obj", ".c", ".h", ".exe", ".lib", };
    size_t cEntries = g_fileEntries.size();
    for ( size_t i = 0; i < cEntries; i++ )
    {
        FileEntry & fe = g_fileEntries[ i ];

        for ( size_t e = 0; e < _countof( build_ext ); e++ )
          if ( ends_with( fe.path, build_ext[ e ] ) )
              return true;
    }

    return false;
} //tc_build_file_open

uint8_t get_current_drive()
{
    // 0 == A...

    GetCurrentDirectoryA( sizeof( cwd ), cwd );
    return (uint8_t) toupper( cwd[0] ) - 'A';
} //get_current_drive

static int compare_int_entries( const void * a, const void * b )
{
    // sort by int then ah

    IntCalled const * pa = (IntCalled const *) a;
    IntCalled const * pb = (IntCalled const *) b;

    if ( pa->i > pb->i )
        return 1;

    if ( pa->i == pb->i )
    {
        if ( pa->c > pb->c )
            return 1;

        if ( pa->c == pb->c )
            return 0;
    }

    return -1;
} //compare_int_entries

static int compare_file_entries( const void * a, const void * b )
{
    // sort by file handle, low to high

    FileEntry const * pa = (FileEntry const *) a;
    FileEntry const * pb = (FileEntry const *) b;

    if ( pa->handle > pb->handle )
        return 1;

    if ( pa->handle == pb->handle )
        return 0;

    return -1;
} //compare_file_entries

FILE * RemoveFileEntry( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            FILE * fp = g_fileEntries[ i ].fp;
            tracer.Trace( "  removing file entry %s: %d\n", g_fileEntries[ i ].path, i );
            g_fileEntries.erase( g_fileEntries.begin() + i );
            return fp;
        }
    }

    tracer.Trace( "  ERROR: could not remove file entry for handle %04x\n", handle );
    return 0;
} //RemoveFileEntry

FILE * FindFileEntry( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            tracer.Trace( "  found file entry '%s': %d\n", g_fileEntries[ i ].path, i );
            return g_fileEntries[ i ].fp;
        }
    }

    tracer.Trace( "  ERROR: could not find file entry for handle %04x\n", handle );
    return 0;
} //FindFileEntry

size_t FindFileEntryIndex( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            tracer.Trace( "  found file entry '%s': %d\n", g_fileEntries[ i ].path, i );
            return i;
        }
    }

    tracer.Trace( "  ERROR: could not find file entry for handle %04x\n", handle );
    return -1;
} //FindFileEntryIndex

size_t FindFileEntryIndexByProcess( uint16_t seg )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( seg == g_fileEntries[ i ].seg_process )
            return i;
    }
    return -1;
} //FindFileEntryIndexByProcess

const char * FindFileEntryPath( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            tracer.Trace( "  found file entry '%s': %d\n", g_fileEntries[ i ].path, i );
            return g_fileEntries[ i ].path;
        }
    }

    tracer.Trace( "  ERROR: could not find file entry for handle %04x\n", handle );
    return 0;
} //FindFileEntryPath

void TraceOpenFiles()
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        tracer.Trace( "    file index %d\n", i );
        g_fileEntries[ i ].Trace();
    }
} //TraceOpenFiles

size_t FindFileEntryFromPath( const char * pfile )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( !_stricmp( pfile, g_fileEntries[ i ].path ) )
        {
            tracer.Trace( "  found file entry '%s': %d\n", g_fileEntries[ i ].path, i );
            return i;
        }
    }

    tracer.Trace( "  NOTICE: could not find file entry for path %s\n", pfile );
    return -1;
} //FindFileEntryFromPath

uint16_t FindFirstFreeFileHandle()
{
    // Apps like the QuickBasic compiler (bc.exe) depend on the side effect that after a file
    // is closed and a new file is opened the lowest possible free handle value is used for the
    // newly opened file. It's a bug in the app, but it's not getting fixed.

    qsort( g_fileEntries.data(), g_fileEntries.size(), sizeof( FileEntry ), compare_file_entries );
    uint16_t freehandle = 5; // DOS uses this, since 0-4 are for built-in handles

    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( g_fileEntries[ i ].handle != freehandle )
            return freehandle;
        else
            freehandle++;
    }

    return freehandle;
} //FindFirstFreeFileHandle

#pragma pack( push, 1 )
// Note: this app doesn't use the MCB for anything other than making it available for apps
// that (unfortunately) assume it's there to do their thing (QuickC 2.x are prime examples).
struct DOSMemoryControlBlock
{
    uint8_t header;        // 'M' for member or 'Z' for last entry in the chain
    uint16_t psp;          // PSP of owning process or 0 if free or 8 if allocated by DOS
    uint16_t paras;        // # of paragraphs for the allocation excluding the MCB
    uint8_t reserved[3];
    uint8_t appname[8];    // ascii name of app, null-terminated if < 8 chars long
    // beyond here is the segment handed to the app for the allocation
};
#pragma pack(pop)

static void trace_all_allocations()
{
    size_t cEntries = g_allocEntries.size();
    tracer.Trace( "  all allocations, count %d:\n", cEntries );
    for ( size_t i = 0; i < cEntries; i++ )
    {
        DosAllocation & da = g_allocEntries[i];
        DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) GetMem( da.segment - 1, 0 );

        tracer.Trace( "      alloc entry %d, process %04x, uses segment %04x, para size %04x, (MCB %04x - %04x)  header %c, psp %04x, paras %04x\n", i,
                      da.seg_process, da.segment, da.para_length, da.segment - 1, da.segment - 1 + da.para_length - 1,
                      pmcb->header, pmcb->psp, pmcb->paras );
    }
} //trace_all_allocations

size_t FindAllocationEntry( uint16_t segment )
{
    for ( size_t i = 0; i < g_allocEntries.size(); i++ )
    {
        if ( segment == g_allocEntries[ i ].segment )
        {
            tracer.Trace( "  found allocation entry segment %04x para size %04x\n", g_allocEntries[ i ].segment, g_allocEntries[ i ].para_length );
            return i;
        }
    }

    tracer.Trace( "  ERROR: could not find alloc entry for segment %04x\n", segment );
    trace_all_allocations();
    return -1;
} //FindAllocationEntry

size_t FindAllocationEntryByProcess( uint16_t segment )
{
    for ( size_t i = 0; i < g_allocEntries.size(); i++ )
    {
        if ( segment == g_allocEntries[ i ].seg_process )
            return i;
    }

    return -1;
} //FindAllocationEntry

void reset_mcb_tags()
{
    // 1) update header with M for non-last and Z for last entries
    // 2) update paras to point to the next MCB, even if that means lying about the actually allocated size
    // 3) ... unless it's the last (Z) MCB, in which case update paras to reflect reality

    for ( size_t i = 0; i < g_allocEntries.size(); i++ )
    {
        DosAllocation & da = g_allocEntries[i];
        DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) GetMem( da.segment - 1, 0 );
        
        if ( i == ( g_allocEntries.size() - 1 ) )
        {
            pmcb->header = 'Z';
            pmcb->paras = da.para_length - 1; // mcb has user-known-size DosAllocation has actual size
        }
        else
        {
            pmcb->header = 'M';
            pmcb->paras = g_allocEntries[ i + 1 ].segment - da.segment - 1; // -1 to exclude the MCB
        }
    }
} //reset_mcb_tags

void initialize_mcb( uint16_t segMCB, uint16_t paragraphs )
{
    DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) GetMem( segMCB, 0 );

    pmcb->header = 'M'; // mark as not the last in the chain
    pmcb->psp = ( 0 == g_currentPSP ) ? 8 : g_currentPSP; // this is what's expected
    pmcb->paras = paragraphs;
    memset( pmcb->appname, 0, sizeof( pmcb->appname ) );
} //initialize_mcb

void update_mcb_length( uint16_t segMCB, uint16_t paragraphs )
{
    DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) GetMem( segMCB, 0 );
    pmcb->paras = paragraphs;
} //update_mcb_length

uint16_t AllocateMemory( uint16_t request_paragraphs, uint16_t & largest_block )
{
    size_t cEntries = g_allocEntries.size();

    // DOS V2 sort.exe asks for 0 paragraphs

    if ( 0 == request_paragraphs )
        request_paragraphs = 1;

    // save room for the MCB prior to the allocation if the call isn't just to see how much RAM is available

    if ( 0xffff != request_paragraphs )
        request_paragraphs++;

    tracer.Trace( "  request to allocate %04x paragraphs (including MCB)\n", request_paragraphs );
    trace_all_allocations();

    uint16_t allocatedSeg = 0;
    size_t insertLocation = 0;

    // sometimes allocate an extra spaceBetween paragraphs between blocks for overly-optimistic resize requests.
    // I'm looking at you link.exe v5.10 from 1990. QBX and cl.exe, run link.exe as a child process, so look for that too.
    // debug.com from DOS v2 is even worse about this.

    uint16_t spaceBetween = ( ends_with( g_acApp, "LINK.EXE" ) || ends_with( g_lastLoadedApp, "LINK.EXE" ) ) ? 0x40 : 0;
    spaceBetween = ( ends_with( g_acApp, "DEBUG.COM" ) || ends_with( g_lastLoadedApp, "DEBUG.COM" ) ) ? 0x60 : spaceBetween;
    if ( ends_with( g_acApp, "ILINK.EXE" ) )
        spaceBetween = 0;

    if ( 0 == cEntries )
    {
        // Microsoft (R) Overlay Linker  Version 3.61 with Quick C v 1.0 is a packed EXE that requires /h to be in high memory

        const uint16_t baseSeg = g_PackedFileCorruptWorkaround ? ( 65536 / 16 ) : AppSegment;
        const uint16_t ParagraphsAvailable = SegmentHardware - baseSeg - 1;  // hardware starts at 0xb800, apps load at baseSeg, -1 for MCB

        if ( request_paragraphs > ParagraphsAvailable )
        {
            largest_block = ParagraphsAvailable;
            tracer.Trace( "allocating first bock, and reporting %04x paragraphs available\n", largest_block );
            trace_all_allocations();
            return 0;
        }

        allocatedSeg = baseSeg; // default if nothing is allocated yet
        tracer.Trace( "    allocating first block at segment %04x\n", allocatedSeg );

        // update the entry in the "list of lists" of the first memory control block

        uint16_t *pFirstBlockInListOfLists = (uint16_t *) GetMem( SegmentListOfLists, OffsetListOfLists - 2 );
        *pFirstBlockInListOfLists = allocatedSeg;
        tracer.Trace( "wrote segment of first allocation %04x to list of lists - 2 at %04x:%04x\n", allocatedSeg, SegmentListOfLists, OffsetListOfLists - 2 );
    }
    else
    {
        uint16_t largestGap = 0;

        for ( size_t i = 0; i < cEntries; i++ )
        {
            uint16_t after = g_allocEntries[ i ].segment - 1 + g_allocEntries[ i ].para_length;
            if ( i < ( cEntries - 1 ) )
            {
                uint16_t freePara = g_allocEntries[ i + 1 ].segment - 1 - after;
                if ( freePara > largestGap )
                    largestGap = freePara;

                if ( freePara >= ( request_paragraphs + spaceBetween) )
                {
                    tracer.Trace( "  using gap from previously freed memory: %02x\n", after );
                    allocatedSeg = after;
                    insertLocation = i + 1;
                    break;
                }
            }
            else if ( ( after + request_paragraphs + spaceBetween ) <= SegmentHardware )
            {
                tracer.Trace( "  using gap after allocated memory: %02x\n", after );
                allocatedSeg = after + spaceBetween;
                insertLocation = i + 1;
                break;
            }
        }

        // if allocation failed, try again without spaceBetween

        if ( 0 != spaceBetween && 0 == allocatedSeg )
        {
            for ( size_t i = 0; i < cEntries; i++ )
            {
                uint16_t after = g_allocEntries[ i ].segment - 1 + g_allocEntries[ i ].para_length;
                if ( i < ( cEntries - 1 ) )
                {
                    uint16_t freePara = g_allocEntries[ i + 1 ].segment - 1 - after;
                    if ( freePara > largestGap )
                        largestGap = freePara;
    
                    if ( freePara >= request_paragraphs )
                    {
                        tracer.Trace( "  using gap from previously freed memory: %02x\n", after );
                        allocatedSeg = after;
                        insertLocation = i + 1;
                        break;
                    }
                }
                else if ( ( after + request_paragraphs ) <= SegmentHardware )
                {
                    tracer.Trace( "  using gap after allocated memory: %02x\n", after );
                    allocatedSeg = after;
                    insertLocation = i + 1;
                    break;
                }
            }
        }
    
        if ( 0 == allocatedSeg )
        {
            DosAllocation & last = g_allocEntries[ cEntries - 1 ];
            uint16_t firstFreeSeg = last.segment - 1 + last.para_length;
            assert( firstFreeSeg <= SegmentHardware );
            largest_block = SegmentHardware - firstFreeSeg;
            if ( largest_block > spaceBetween )
                largest_block -= spaceBetween;

            if ( largestGap > largest_block )
                largest_block = largestGap;

            largest_block--; // don't include the MCB
    
            tracer.Trace( "  ERROR: unable to allocate %02x paragraphs. returning that %02x paragraphs are free\n", request_paragraphs, largest_block );
            return 0;
        }
    }

    allocatedSeg++; // move past the MCB

    DosAllocation da;
    da.segment = allocatedSeg;
    da.para_length = request_paragraphs;
    da.seg_process = g_currentPSP;
    g_allocEntries.insert( insertLocation + g_allocEntries.begin(), da );
    largest_block = SegmentHardware - allocatedSeg;
    initialize_mcb( allocatedSeg - 1, request_paragraphs - 1 );
    reset_mcb_tags();
    trace_all_allocations();
    return allocatedSeg;
} //AllocateMemory

bool FreeMemory( uint16_t segment )
{
    size_t entry = FindAllocationEntry( segment );
    if ( -1 == entry )
    {
        // The Microsoft Basic compiler BC.EXE 7.10 attempts to free segment 0x80, which it doesn't own.
        // Turbo Pascal v5.5 exits a process never create except via int21 0x55, which frees that PSP,
        // which isn't allocated, and the environment blocked it contains (0).

        tracer.Trace( "  ERROR: memory corruption possible; can't find freed segment %04x\n", segment );
        return false;
    }

    tracer.Trace( "  freeing memory with segment %04x entry %d\n", segment, entry );
    g_allocEntries.erase( g_allocEntries.begin() + entry );
    reset_mcb_tags();

    trace_all_allocations();
    return true;
} //FreeMemory

bool isPressed( int vkey )
{
    SHORT s = GetAsyncKeyState( vkey );
    return 0 != ( 0x8000 & s );
} //isPressed

bool keyState( int vkey )
{
    SHORT s = GetAsyncKeyState( vkey );
    return 0 != ( 0x1000 & s );
} //keyState

uint8_t GetActiveDisplayPage()
{
    uint8_t activePage = * GetMem( 0x40, 0x62 );
    assert( activePage <= 3 );
    return activePage;
} //GetActiveDisplayPage

void SetActiveDisplayPage( uint8_t page )
{
    assert( page <= 3 );
    * GetMem( 0x40, 0x62 ) = page;
} //SetActiveDisplayPage

uint8_t * GetVideoMem()
{
    return GetMem( ScreenBufferSegment, 0x1000 * GetActiveDisplayPage() );
} //GetVideoMem

void GetCursorPosition( uint8_t & row, uint8_t & col )
{
    uint8_t * cursordata = GetMem( 0x40, 0x50 ) + ( GetActiveDisplayPage() * 2 );

    col = cursordata[ 0 ];
    row = cursordata[ 1 ];
} //GetCursorPosition

void SetCursorPosition( uint8_t row, uint8_t col )
{
    uint8_t * cursordata = GetMem( 0x40, 0x50 ) + ( GetActiveDisplayPage() * 2 );

    cursordata[ 0 ] = col;
    cursordata[ 1 ] = row;
} //SetCursorPosition

char printable( uint8_t x )
{
    if ( x < ' ' || x >= 127 )
        return ' ';
    return x;
} //printable

void init_blankline( uint8_t attribute )
{
    uint8_t * pbyte = (uint8_t *) blankLine;

    for ( int x = 0; x < _countof( blankLine ); x++ )
    {
        pbyte[ 2 * x ] = ' ';
        pbyte[ 2 * x + 1 ] = attribute;
    }
} //init_blankline

uint8_t get_keyboard_flags_depressed()
{
    uint8_t val = 0;
    val |= ( 0 != isPressed( VK_RSHIFT ) );
    val |= ( isPressed( VK_LSHIFT ) << 1 );
    val |= ( ( isPressed( VK_LCONTROL ) || isPressed( VK_RCONTROL ) ) << 2 );
    val |= ( ( isPressed( VK_LMENU ) || isPressed( VK_RMENU ) ) << 3 ); // alt
    val |= ( keyState( VK_SCROLL ) << 4 );
    val |= ( keyState( VK_NUMLOCK ) << 5 );
    val |= ( keyState( VK_CAPITAL ) << 6 );
    val |= ( keyState( VK_INSERT ) << 7 );

    return val;
} //get_keyboard_flags_depressed

#pragma pack( push, 1 )
struct DOSPSP
{
    uint16_t int20Code;              // machine code cd 20 to end app
    uint16_t topOfMemory;            // in segment (paragraph) form. one byte beyond what's allocated
    uint8_t  reserved;               // generally 0
    uint8_t  dispatcherCPM;          // obsolete cp/m-style code to call dispatcher
    uint16_t comAvailable;           // .com programs bytes available in segment (cp/m holdover)
    uint16_t farJumpCPM;
    uint32_t int22TerminateAddress;  // When a child process ends, there is where execution resumes in the parent.
    uint32_t int23ControlBreak;
    uint32_t int24CriticalError;
    uint16_t segParent;              // parent process segment address of PSP
    uint8_t  fileHandles[20];        // unused by emulator
    uint16_t segEnvironment;
    uint32_t ssspEntry;              // ss:sp on entry to last int21 function. undocumented
    uint16_t handleArraySize;        // undocumented
    uint32_t handleArrayPointer;     // undocumented
    uint32_t previousPSP;            // undocumented. default 0xffff:ffff
    uint8_t  reserved2[20];
    uint8_t  dispatcher[3];          // undocumented
    uint8_t  reserved3[9];
    uint8_t  firstFCB[16];           // later parts of a real fcb shared with secondFCB
    uint8_t  secondFCB[16];          // only the first part is used
    uint16_t parentSS;               // not DOS standard -- I use it to restore SS on child app exit
    uint16_t parentSP;               // not DOS standard -- I use it to restore SP on child app exit
    uint8_t  countCommandTail;       // # of characters in command tail. This byte and beyond later used as Disk Transfer Address
    uint8_t  commandTail[127];       // command line characters after executable, newline terminated

    void Trace()
    {
        assert( 0x16 == offsetof( DOSPSP, segParent ) );
        assert( 0x5c == offsetof( DOSPSP, firstFCB ) );
        assert( 0x6c == offsetof( DOSPSP, secondFCB ) );
        assert( 0x80 == offsetof( DOSPSP, countCommandTail ) );

        tracer.Trace( "  PSP: %04x\n", GetSegment( (uint8_t *) this ) );
        tracer.TraceBinaryData( (uint8_t *) this, sizeof( DOSPSP ), 4 );
        tracer.Trace( "  topOfMemory: %04x\n", topOfMemory );
        tracer.Trace( "  segParent: %04x\n", segParent );
        tracer.Trace( "  return address: %04x\n", int22TerminateAddress );
        tracer.Trace( "  command tail: len %u, '%.*s'\n", countCommandTail, countCommandTail, commandTail );

        tracer.Trace( "  segEnvironment: %04x\n", segEnvironment );
        if ( 0 != segEnvironment )
        {
            const char * penv = (char *) GetMem( segEnvironment, 0 );
            tracer.TraceBinaryData( (uint8_t *) penv, 0x80, 4 );
        }
    }
};
#pragma pack(pop)

#pragma pack( push, 1 )
struct DOSFCB
{
    uint8_t drive;
    char name[8];
    char ext[3];
    uint16_t curBlock;
    uint16_t recSize;
    uint32_t fileSize;
    uint16_t date;
    uint16_t time;
    uint8_t reserved[8];
    uint8_t curRecord;       // where sequential i/o starts
    uint32_t recNumber;      // where random i/o starts

    void SetFP( FILE * fp ) { * ( (FILE **) & ( this->reserved ) ) = fp; }
    FILE * GetFP() { return * ( (FILE **) &this->reserved ); }

    void Trace()
    {
        assert( 0x0 == offsetof( DOSFCB, drive ) );
        assert( 0x1 == offsetof( DOSFCB, name ) );
        assert( 0x9 == offsetof( DOSFCB, ext ) );
        assert( 0xc == offsetof( DOSFCB, curBlock ) );
        assert( 0xe == offsetof( DOSFCB, recSize ) );
        assert( 0x10 == offsetof( DOSFCB, fileSize ) );
        assert( 0x14 == offsetof( DOSFCB, date ) );
        assert( 0x16 == offsetof( DOSFCB, time ) );
        assert( 0x18 == offsetof( DOSFCB, reserved ) );
        assert( 0x20 == offsetof( DOSFCB, curRecord ) );
        assert( 0x21 == offsetof( DOSFCB, recNumber ) );

        tracer.Trace( "  fcb: %p\n", this );
        tracer.Trace( "    drive        %u\n", this->drive );
        tracer.Trace( "    filename     '%c%c%c%c%c%c%c%c'\n",
                      this->name[0],this->name[1],this->name[2],this->name[3],
                      this->name[4],this->name[5],this->name[6],this->name[7] );
        tracer.Trace( "    ext          '%c%c%c'\n", this->ext[0],this->ext[1],this->ext[2] );
        tracer.Trace( "    curBlock:    %u\n", this->curBlock );
        tracer.Trace( "    recSize:     %u\n", this->recSize );
        tracer.Trace( "    fileSize:    %u\n", this->fileSize );
        tracer.Trace( "    reserved/fp: %p\n", GetFP() );
        tracer.Trace( "    curRecord:   %u\n", this->curRecord );
        tracer.Trace( "    recNumber:   %u\n", this->recNumber );
    }

    void TraceFirst16()
    {
        assert( 0x0 == offsetof( DOSFCB, drive ) );
        assert( 0x1 == offsetof( DOSFCB, name ) );
        assert( 0x9 == offsetof( DOSFCB, ext ) );
        assert( 0xc == offsetof( DOSFCB, curBlock ) );
        assert( 0xe == offsetof( DOSFCB, recSize ) );

        tracer.Trace( "  fcb first 16: %p\n", this );
        tracer.Trace( "    drive        %u\n", this->drive );
        tracer.Trace( "    filename     '%c%c%c%c%c%c%c%c'\n",
                      this->name[0],this->name[1],this->name[2],this->name[3],
                      this->name[4],this->name[5],this->name[6],this->name[7] );
        tracer.Trace( "    filename     %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n",
                      this->name[0],this->name[1],this->name[2],this->name[3],
                      this->name[4],this->name[5],this->name[6],this->name[7] );
        tracer.Trace( "    ext          '%c%c%c'\n", this->ext[0],this->ext[1],this->ext[2] );
        tracer.Trace( "    ext          %02x, %02x, %02x\n", this->ext[0],this->ext[1],this->ext[2] );
        tracer.Trace( "    curBlock:    %u\n", this->curBlock );
        tracer.Trace( "    recSize:     %u\n", this->recSize );
    }
};

struct DOSEXFCB
{
    uint8_t extendedFlag; // 0xff to indicate it's an extended FCB
    uint8_t reserved[ 5 ];
    uint8_t attr;
    DOSFCB fcb;
};
#pragma pack(pop)

const char * GetCurrentAppPath()
{
    DOSPSP * psp = (DOSPSP *) GetMem( g_currentPSP, 0 );
    uint16_t segEnv = psp->segEnvironment;
    const char * penv = (char *) GetMem( segEnv, 0 );

    size_t len = strlen( penv );
    while ( 0 != len )
    {
        penv += ( 1 + len );
        len = strlen( penv );
    }

    penv += 3; // get past final 0 and count of extra strings.
    return penv;
} //GetCurrentAppPath

bool GetDOSFilename( DOSFCB &fcb, char * filename )
{
    char * orig = filename;

    for ( int i = 0; i < 8; i++ )
    {
        if ( ' ' == fcb.name[i] || 0 == fcb.name[i] )
            break;

        *filename++ = fcb.name[i];
    }

    *filename++ = '.';

    for ( int i = 0; i < 3; i++ )
    {
        if ( ' ' == fcb.ext[i] || 0 == fcb.ext[i] )
            break;

        *filename++ = fcb.ext[i];
    }

    *filename = 0;

    return ( 0 != *orig && '.' != *orig );
} //GetDOSFilename

void UpdateWindowsCursorPosition()
{
    assert( g_use80x25 );
    uint8_t row, col;
    GetCursorPosition( row, col );
    COORD pos = { col, row };
    SetConsoleCursorPosition( g_hConsoleOutput, pos );
} //UpdateWindowsCursorPosition

static uint8_t bufferLastUpdate[ ScreenBufferSize ] = {0}; // used to check for changes in video memory

void ClearLastUpdateBuffer()
{
    memset( bufferLastUpdate, 0, sizeof( bufferLastUpdate ) );
} //ClearLastUpdateBuffer

void traceDisplayBuffers()
{
    for ( int i = 0; i < 2; i++ )
    {
        tracer.Trace( "cga memory buffer %d\n", i );
        uint8_t * pbuf = GetMem( ScreenBufferSegment, (uint16_t) ( 0x1000 * i ) );
        for ( size_t y = 0; y < ScreenRows; y++ )
        {
            size_t yoffset = y * ScreenColumns * 2;
            tracer.Trace( "    row %02u: '", y );
            for ( size_t x = 0; x < ScreenColumns; x++ )
            {
                size_t offset = yoffset + x * 2;
                tracer.Trace( "%c", printable( pbuf[ offset ] ) );
            }
            tracer.Trace( "'\n" );
        }
    }
} //traceDisplayBuffers

void traceDisplayBufferAsHex()
{
    tracer.Trace( "cga memory buffer %d\n" );
    uint8_t * pbuf = GetVideoMem();
    for ( size_t y = 0; y < ScreenRows; y++ )
    {
        size_t yoffset = y * ScreenColumns * 2;
        tracer.Trace( "    row %02u: '", y );
        for ( size_t x = 0; x < ScreenColumns; x++ )
        {
            size_t offset = yoffset + x * 2;
            tracer.Trace( "%c=%02x ", printable( pbuf[ offset ] ), pbuf[ offset ] );
        }
        tracer.Trace( "'\n" );
    }
} //traceDisplayBufferAsHex

// Unicode equivalents of DOS characters < 32. Codepage 437 doesn't automatically make these work in v2 of console.

static WCHAR awcLowDOSChars[ 32 ] =
{ 
    0,      0x263a, 0x2638, 0x2665, 0x2666, 0x2663, 0x2600, 0x2022, 0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
    0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x2582, 0x25bc,
};

bool UpdateDisplay()
{
    assert( g_use80x25 );
    uint8_t * pbuf = GetVideoMem();

    if ( memcmp( bufferLastUpdate, pbuf, sizeof( bufferLastUpdate ) ) )
    {
        //tracer.Trace( "UpdateDisplay with changes\n" );
        #if false
            CONSOLE_SCREEN_BUFFER_INFOEX csbi = { 0 };
            csbi.cbSize = sizeof( csbi );
            GetConsoleScreenBufferInfoEx( g_hConsoleOutput, &csbi );

            tracer.Trace( "  UpdateDisplay: pbuf %p, csbi size %d %d, window %d %d %d %d\n",
                          pbuf, csbi.dwSize.X, csbi.dwSize.Y,
                          csbi.srWindow.Left, csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom );
        #endif

        for ( uint32_t y = 0; y < ScreenRows; y++ )
        {
            uint32_t yoffset = y * ScreenColumns * 2;

            if ( memcmp( bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 ) )
            {
                memcpy( bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 );
                WORD aAttribs[ScreenColumns];
                char ac[ScreenColumns];
                for ( size_t x = 0; x < ScreenColumns; x++ )
                {
                    size_t offset = yoffset + x * 2;
                    ac[ x ] = pbuf[ offset ];
                    if ( 0 == ac[ x ] )
                        ac[ x ] = ' '; // brief alternately writes 0 then ':' for the clock
                    aAttribs[ x ] = pbuf[ 1 + offset ]; 
                }

                WCHAR awcLine[ScreenColumns];
                MultiByteToWideChar( 437, 0, ac, ScreenColumns, awcLine, ScreenColumns );

                // CP 437 doesn't handle characters 0 to 31 or 0x7f correctly. 

                for ( size_t x = 0; x < ScreenColumns; x++ )
                {
                    if (  awcLine[ x ] < 32 )
                        awcLine[ x ] = awcLowDOSChars[ awcLine[ x ] ];
                    else if ( 0x7f == awcLine[ x ] )
                        awcLine[ x ] = 0x2302;
                }

                #if false
                    //tracer.Trace( "    row %02u: '%.80s'\n", y, ac );
                    tracer.Trace( "    row %02u: '", y );
                    for ( size_t c = 0; c < 80; c++ )
                        tracer.Trace( "%c", printable( ac[ c ] ) );
                    tracer.Trace( "'\n" );
                #endif
    
                COORD pos = { 0, (SHORT) y };
                SetConsoleCursorPosition( g_hConsoleOutput, pos );
    
                BOOL ok = WriteConsoleW( g_hConsoleOutput, awcLine, ScreenColumns, 0, 0 );
                if ( !ok )
                    tracer.Trace( "writeconsolea failed with error %d\n", GetLastError() );
    
                DWORD dwWritten;
                ok = WriteConsoleOutputAttribute( g_hConsoleOutput, aAttribs, ScreenColumns, pos, &dwWritten );
                if ( !ok )
                    tracer.Trace( "writeconsoleoutputattribute failed with error %d\n", GetLastError() );
            }
        }

        //traceDisplayBufferAsHex();
        //traceDisplayBuffers();
        //cpu.trace_instructions( true );
        UpdateWindowsCursorPosition();
        return true;
    }

    //tracer.Trace( "UpdateDisplay not updated; no changes\n" );
    return false;
} //UpdateDisplay

bool throttled_UpdateDisplay( int64_t delay = 50 )
{
    static CDuration _duration;

    if ( _duration.HasTimeElapsedMS( delay ) )
        return UpdateDisplay();

    return false;
} //throttled_UpdateDisplay();

void ClearDisplay()
{
    assert( g_use80x25 );
    uint8_t * pbuf = GetVideoMem();

    for ( size_t y = 0; y < ScreenRows; y++ )
        memcpy( pbuf + ( y * 2 * ScreenColumns ), blankLine, sizeof( blankLine ) );
} //ClearDisplay

BOOL WINAPI ControlHandler( DWORD fdwCtrlType )
{
    // this happens in a third thread, which is implicitly created

    if ( CTRL_C_EVENT == fdwCtrlType )
    {
        // for 80x25 apps, ^c is often a valid character for page down, etc.
        // for command-line apps, terminate execution.

        if ( !g_use80x25 )
        {
            g_haltExecution = true;
            cpu.end_emulation();
        }
        else
            g_injectControlC = true;

        return TRUE;
    }

    return FALSE;
} //ControlHandler

struct IntInfo
{
    uint8_t i;  // interrupt #
    uint8_t c;  // ah command
    const char * name;
};

const IntInfo interrupt_list_no_ah[] =
{
   { 0x00, 0, "divide by zero" },
   { 0x01, 0, "trap / single-step" },
   { 0x02, 0, "non-maskable interrupt" },
   { 0x03, 0, "int3 / debug break" },
   { 0x04, 0, "overflow" },
   { 0x05, 0, "print-screen key" },
   { 0x06, 0, "undefined opcode" },
   { 0x08, 0, "hardware timer interrupt" },
   { 0x09, 0, "keyboard interrupt" },
   { 0x10, 0, "bios video" },
   { 0x11, 0, "bios equipment determination" },
   { 0x1b, 0, "ctrl-break key" },
   { 0x1c, 0, "software tick tock" },
   { 0x20, 0, "cp/m compatible exit app" },
   { 0x21, 0, "generic dos interrupt" },
   { 0x22, 0, "end application" },
   { 0x23, 0, "control c exit address" },
   { 0x24, 0, "fatal error handler address" },
   { 0x28, 0, "dos idle loop / scheduler" },
   { 0x2a, 0, "network information" },
   { 0x2f, 0, "dos multiplex" },
   { 0x33, 0, "mouse" },
   { 0x34, 0, "turbo c / microsoft floating point emulation" },
   { 0x35, 0, "turbo c / microsoft floating point emulation" },
   { 0x36, 0, "turbo c / microsoft floating point emulation" },
   { 0x37, 0, "turbo c / microsoft floating point emulation" },
   { 0x38, 0, "turbo c / microsoft floating point emulation" },
   { 0x39, 0, "turbo c / microsoft floating point emulation" },
   { 0x3a, 0, "turbo c / microsoft floating point emulation" },
   { 0x3b, 0, "turbo c / microsoft floating point emulation" },
   { 0x3c, 0, "turbo c / microsoft floating point emulation" },
   { 0x3d, 0, "turbo c / microsoft floating point emulation" },
   { 0x3e, 0, "turbo c / microsoft floating point emulation" },
   { 0x3f, 0, "overlay manager (microsoft link.exe)" },
   { 0xf0, 0, "gwbasic interpreter" },
};

const IntInfo interrupt_list[] =
{
    { 0x10, 0x00, "set video mode" },
    { 0x10, 0x01, "set cursor size" },
    { 0x10, 0x02, "set cursor position" },
    { 0x10, 0x03, "get cursor position" },
    { 0x10, 0x05, "set active displaypage" },
    { 0x10, 0x06, "scroll window up" },
    { 0x10, 0x07, "scroll window down" },
    { 0x10, 0x08, "read attributes+character at cursor position" },
    { 0x10, 0x09, "output character" },
    { 0x10, 0x0a, "output character only" },
    { 0x10, 0x0f, "get video mode" },
    { 0x10, 0x10, "set palette registers" },
    { 0x10, 0x11, "character generator ega" },
    { 0x10, 0x12, "alternate select ega" },
    { 0x10, 0x13, "write character string" },
    { 0x10, 0x14, "lcd handler" },
    { 0x10, 0x15, "return physical display characteristics" },
    { 0x10, 0x1a, "get/set video display combination" },
    { 0x10, 0x1b, "video functionality/state information" },
    { 0x10, 0x1c, "save/restore video state" },
    { 0x10, 0xef, "hercules -- get video adapter type and mode" },
    { 0x10, 0xfa, "ega register interface library" },
    { 0x12, 0x00, "get memory size" },
    { 0x16, 0x00, "get character" },
    { 0x16, 0x01, "keyboard status" },
    { 0x16, 0x02, "keyboard - get shift status" },
    { 0x16, 0x05, "keyboard - store keystroke in keyboard buffer" },
    { 0x16, 0x11, "get enhanced keystroke" },
    { 0x16, 0x55, "microsoft TSR internal (al ff/00 word, fe qbasic)" },
    { 0x17, 0x02, "check printer status" },
    { 0x1a, 0x00, "read real time clock" },
    { 0x1a, 0x02, "get real-time clock time" },
    { 0x21, 0x00, "exit app" },
    { 0x21, 0x02, "output character" },
    { 0x21, 0x06, "direct console character i/o" },
    { 0x21, 0x07, "direct character input without echo no ^c/^break check" },
    { 0x21, 0x08, "direct character input without echo with ^c/^break check" },
    { 0x21, 0x09, "print string $-terminated" },
    { 0x21, 0x0a, "buffered keyboard input" },
    { 0x21, 0x0b, "check standard input status" },
    { 0x21, 0x0c, "clear input buffer and execute int 0x21 on AL" },
    { 0x21, 0x0d, "disk reset" },
    { 0x21, 0x0e, "select disk" },
    { 0x21, 0x0f, "open file using FCB" },
    { 0x21, 0x10, "close file using FCB" },
    { 0x21, 0x11, "search first using FCB" },
    { 0x21, 0x12, "search next using FCB" },
    { 0x21, 0x13, "delete file using FCB" },
    { 0x21, 0x14, "sequential read using FCB" },
    { 0x21, 0x15, "sequential write using FCB" },
    { 0x21, 0x16, "create file using FCB" },
    { 0x21, 0x17, "rename file using FCB" },
    { 0x21, 0x19, "get default drive" },
    { 0x21, 0x1a, "set disk transfer address" },
    { 0x21, 0x1c, "get allocation information for specific drive" },
    { 0x21, 0x21, "random read using FCB" },
    { 0x21, 0x22, "random write using FCB" },
    { 0x21, 0x23, "get file size using FCB" },
    { 0x21, 0x25, "set interrupt vector" },
    { 0x21, 0x26, "create new PSP" },
    { 0x21, 0x27, "random block read using FCB" },
    { 0x21, 0x28, "write random using FCBs" },
    { 0x21, 0x29, "parse filename" },
    { 0x21, 0x2a, "get system date" },
    { 0x21, 0x2c, "get system time" },
    { 0x21, 0x2f, "get disk transfer area address" },
    { 0x21, 0x30, "get version number" },
    { 0x21, 0x31, "terminate and stay resident" },
    { 0x21, 0x33, "get/set ctrl-break status" },
    { 0x21, 0x34, "get address of DOS critical flag" },
    { 0x21, 0x35, "get interrupt vector" },
    { 0x21, 0x36, "get disk space" },
    { 0x21, 0x37, "get/set switchchar + device availability" },
    { 0x21, 0x38, "get/set country dependent information" },
    { 0x21, 0x39, "create directory" },
    { 0x21, 0x3a, "remove directory" },
    { 0x21, 0x3b, "change directory" },
    { 0x21, 0x3c, "create file" },
    { 0x21, 0x3d, "open file" },
    { 0x21, 0x3e, "close file" },
    { 0x21, 0x3f, "read from file using handle" },
    { 0x21, 0x40, "write to file using handle" },
    { 0x21, 0x41, "delete file" },
    { 0x21, 0x42, "move file pointer (seek)" },
    { 0x21, 0x43, "get/put file attributes" },
    { 0x21, 0x44, "ioctl" },
    { 0x21, 0x45, "create duplicate handle" },
    { 0x21, 0x46, "force duplicate handle" },
    { 0x21, 0x47, "get current directory" },
    { 0x21, 0x48, "allocate memory" },
    { 0x21, 0x49, "free memory" },
    { 0x21, 0x4a, "modify memory allocation" },
    { 0x21, 0x4b, "load or execute overlay or program" },
    { 0x21, 0x4c, "exit app" },
    { 0x21, 0x4d, "get exit code of subprogram" },
    { 0x21, 0x4e, "find first asciiz" },
    { 0x21, 0x4f, "find next asciiz" },
    { 0x21, 0x50, "set current process id (psp)" },
    { 0x21, 0x51, "get current process id (psp)" },
    { 0x21, 0x52, "get list of lists" },
    { 0x21, 0x55, "create new PSP" }, // undocumented/internal. Used by Turbo Pascal 5.5
    { 0x21, 0x56, "rename file" },
    { 0x21, 0x57, "get/set file date and time using handle" },
    { 0x21, 0x58, "get/set memory allocation strategy" },
    { 0x21, 0x59, "get extended error code" },
    { 0x21, 0x5f, "get redirection list entry" },
    { 0x21, 0x62, "get psp address" },
    { 0x21, 0x63, "get lead byte table" },
    { 0x21, 0x68, "fflush - commit file" },
};

const char * get_interrupt_string( uint8_t i, uint8_t c, bool & ah_used )
{
    ah_used = false;

    for ( int x = 0; x < _countof( interrupt_list ); x++ )
        if ( interrupt_list[ x ].i == i && interrupt_list[ x ].c == c )
        {
            ah_used = true;
            return interrupt_list[ x ].name;
        }

    for ( int x = 0; x < _countof( interrupt_list_no_ah ); x++ )
        if ( interrupt_list_no_ah[ x ].i == i )
            return interrupt_list_no_ah[ x ].name;

    return "unknown";
} //get_interrupt_string

bool process_key_event( INPUT_RECORD & rec, uint8_t & asciiChar, uint8_t & scancode )
{
    if ( KEY_EVENT != rec.EventType )
        return false;

    if ( !rec.Event.KeyEvent.bKeyDown )
        return false;

    asciiChar = rec.Event.KeyEvent.uChar.AsciiChar;
    scancode = (uint8_t) rec.Event.KeyEvent.wVirtualScanCode;
    const uint8_t asc = asciiChar; // non-writable shorthand
    const uint8_t sc = scancode;

    // don't pass back just an Alt/ctrl/shift/capslock without another character
    if ( ( 0 == asc ) && ( 0x38 == sc || 0x1d == sc || 0x2a == sc || 0x3a == sc || 0x36 == sc ) )
        return false;

    bool fshift = ( 0 != ( rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED ) );
    bool fctrl = ( 0 != ( rec.Event.KeyEvent.dwControlKeyState & ( LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED ) ) );
    bool falt = ( 0 != ( rec.Event.KeyEvent.dwControlKeyState & ( LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED ) ) );

    tracer.Trace( "    process key event sc/asc %02x%02x, shift %d, ctrl %d, alt %d\n", sc, asc, fshift, fctrl, falt );

    // control+ 1, 3-5, and 7-0 should be swallowed. 2 and 6 are allowed.
    if ( fctrl && ( 2 == sc || 4 == sc || 5 == sc || 6 == sc || 8 == sc || 9 == sc || 0xa == sc || 0xb == sc ) )
        return false;

    // control+ =, ;, ', `, ,, ., / should be swallowed
    if ( fctrl && ( 0x0d == sc || 0x27 == sc || 0x28 == sc || 0x29 == sc || 0x33 == sc || 0x34 == sc || 0x35 == sc ) )
        return false;

    // alt+ these characters has no meaning
    if ( falt && ( '\'' == asc || '`' == asc || ',' == asc || '.' == asc || '/' == asc || 0x4c == sc ) )
        return false;

    uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
    uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
    uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );

    // if the buffer is full, stop consuming new characters
    if ( ( *phead == ( *ptail + 2 ) ) || ( ( 0x1e == *phead ) && ( 0x3c == *ptail ) ) )
        return false;

    if ( falt )
    {
        // if A-Z, set low byte to 0

        if ( asc >= 0x61 && asc <= 0x7a )
            asciiChar = 0;
        else if ( asc >= 0x30 && asc <= 0x39 )
        {
            if ( 0x30 == asc )
                scancode = 0x81;
            else
                scancode = asc + 0x47;
            asciiChar = 0;
        }
        else if ( '-' == asc ) { scancode = 0x82; asciiChar = 0; }
        else if ( '=' == asc ) { scancode = 0x83; asciiChar = 0; }
        else if ( '[' == asc ) { scancode = 0x1a; asciiChar = 0; }
        else if ( ']' == asc ) { scancode = 0x1b; asciiChar = 0; }
        else if ( ';' == asc ) { scancode = 0x27; asciiChar = 0; }
        else if ( '\\' == asc ) { scancode = 0x26; asciiChar = 0; }
        else if ( 0x08 == asc ) { scancode = 0x0e; asciiChar = 0; }
    }

    if ( fctrl )
    {
        // ctrl 6
        if ( 0x07 == sc && 0 == asc ) asciiChar = 0x1e;
        else if ( 0x0c == sc ) asciiChar = 0x1f;
        else if ( 0x1a == sc ) asciiChar = 0x1b;
        else if ( 0x1b == sc ) asciiChar = 0x1d;
        else if ( 0x2b == sc ) asciiChar = 0x1c;
    }

    if ( 0x01 == sc ) // ESC
    {
        if ( falt ) { asciiChar = 0; }
    }
    else if ( 0x0f == sc ) // Tab
    {
        if ( falt ) { scancode = 0xa5; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x94; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x00; }
    }
    else if ( 0x35 == sc ) // keypad /
    {
        if ( falt ) { scancode = 0xa4; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x95; asciiChar = 0; }
    }
    else if ( 0x37 == sc ) // keypad *
    {
        if ( falt ) { asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x96; asciiChar = 0; }
    }
    else if ( sc >= 0x3b && sc <= 0x44 ) // F1 - F10
    {
        if ( falt ) scancode += 0x2d;
        else if ( fctrl ) scancode += 0x23;
        else if ( fshift ) scancode += 0x19;
    }
    else if ( 0x47 == sc ) // Home
    {
        if ( falt ) { scancode = 0x97; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x77; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x37; }
    }
    else if ( 0x48 == sc ) // up arrow
    {
        if ( falt ) { scancode = 0x98; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x8d; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x38; }
    }
    else if ( 0x49 == sc ) // page up
    {
        if ( falt ) { scancode = 0x99; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x84; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x39; }
    }
    else if ( 0x4a == sc ) // keypad -
    {
        if ( falt ) { scancode = 0x4a; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x8e; asciiChar = 0; }
    }
    else if ( 0x4b == sc ) // left arrow
    {
        if ( falt ) { scancode = 0x9b; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x73; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x34; }
    }
    else if ( 0x4c == sc ) // keypad 5
    {
        if ( fctrl ) { scancode = 0x8f; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x35; }
    }
    else if ( 0x4d == sc ) // right arrow
    {
        if ( falt ) { scancode = 0x9d; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x74; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x36; }
    }
    else if ( 0x4e == sc ) // keypad +
    {
        if ( falt ) { scancode = 0x4a; asciiChar = 0; }
    }
    else if ( 0x4f == sc ) // End
    {
        if ( falt ) { scancode = 0x9f; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x75; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x31; }
    }
    else if ( 0x50 == sc ) // down arrow
    {
        if ( falt ) { scancode = 0xa0; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x91; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x32; }
    }
    else if ( 0x51 == sc ) // page down
    {
        if ( falt ) { scancode = 0xa1; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x76; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x33; }
    }
    else if ( 0x52 == sc ) // Home
    {
        if ( falt ) { scancode = 0xa2; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x92; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x30; }
    }
    else if ( 0x53 == sc ) // Del
    {
        if ( falt ) { scancode = 0xa3; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x93; asciiChar = 0; }
        else if ( fshift ) { asciiChar = 0x2e; }
    }
    else if ( 0x57 == sc || 0x58 == sc ) // F11 - F12
    {
        scancode += 0x2e;
        if ( falt ) scancode += 6;
        else if ( fctrl ) scancode += 4;
        else if ( fshift ) scancode += 2;
    }

    return true;
} //process_key_event

bool peek_keyboard( uint8_t & asciiChar, uint8_t & scancode )
{
    // this mutex is because I don't know if PeekConsoleInput and ReadConsoleInput are individually or mutually reenterant.
    lock_guard<mutex> lock( g_mtxEverything );

    if ( g_injectControlC )
    {
        asciiChar = 0x03;
        scancode = 0x2e;
        return true;
    }

    INPUT_RECORD records[ 10 ];
    DWORD numRead = 0;
    BOOL ok = PeekConsoleInput( g_hConsoleInput, records, _countof( records ), &numRead );
    //tracer.Trace( "  PeekConsole returned %d, %d events\n", ok, numRead );
    if ( ok )
    {
        for ( uint32_t x = 0; x < numRead; x++ )
        {
            bool used = process_key_event( records[ x ], asciiChar, scancode );
            if ( !used )
                continue;
            tracer.Trace( "    peeked ascii %02x, scancode %02x\n", asciiChar, scancode );
            return true;
        }
    }

    // if none of the records were useful then clear them out

    if ( 0 != numRead )
        ReadConsoleInput( g_hConsoleInput, records, numRead, &numRead );

    return false;
} //peek_keyboard

bool peek_keyboard( bool throttle = false, bool sleep_on_throttle = false, bool update_display = false )
{
    static CDuration _durationLastPeek;
    static CDuration _durationLastUpdate;

    if ( throttle && !_durationLastPeek.HasTimeElapsedMS( 100 ) )
    {
        if ( update_display && g_use80x25 && _durationLastUpdate.HasTimeElapsedMS( 333 ) )
            UpdateDisplay();

        if ( sleep_on_throttle )
            Sleep( 1 );

        return false;
    }

    uint8_t a, s;
    return peek_keyboard( a, s );
} //peek_keyboard

void consume_keyboard()
{
    // this mutex is because I don't know if PeekConsoleInput and ReadConsoleInput are individually or mutually reenterant.
    lock_guard<mutex> lock( g_mtxEverything );

    uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
    uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
    uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
    tracer.Trace( "    initial state: head %04x, tail %04x\n", *phead, *ptail );

    if ( g_injectControlC ) // largeish hack for ^c handling
    {
        g_KbdIntWaitingForRead = false;
        g_injectControlC = false;
        uint8_t asciiChar = 0x03;
        uint8_t scancode = 0x2e;

        // if the buffer is full, stop consuming new characters
        if ( ! ( ( *phead == ( *ptail + 2 ) ) || ( ( 0x1e == *phead ) && ( 0x3c == *ptail ) ) ) )
        {
            pbiosdata[ *ptail ] = asciiChar;
            (*ptail)++;
            pbiosdata[ *ptail ] = scancode;
            (*ptail)++;
            if ( *ptail >= 0x3e )
                *ptail = 0x1e;
        }
    }

    INPUT_RECORD records[ 10 ];
    DWORD numRead = 0;
    BOOL ok = ReadConsoleInput( g_hConsoleInput, records, _countof( records ), &numRead );
    tracer.Trace( "    consume_keyboard ReadConsole returned %d, %d events\n", ok, numRead );
    if ( ok )
    {
        uint8_t asciiChar = 0, scancode = 0;
        for ( uint32_t x = 0; x < numRead; x++ )
        {
            bool used = process_key_event( records[ x ], asciiChar, scancode );
            if ( !used )
                continue;

            tracer.Trace( "    consumed ascii %02x, scancode %02x\n", asciiChar, scancode );

            // It's ok to send more keyboard int 9s since we've consumed a character
            g_KbdIntWaitingForRead = false;

            pbiosdata[ *ptail ] = asciiChar;
            (*ptail)++;
            pbiosdata[ *ptail ] = scancode;
            (*ptail)++;
            if ( *ptail >= 0x3e )
                *ptail = 0x1e;
        }

        tracer.Trace( "    final state: head %04x, tail %04x\n", *phead, *ptail );
    }
} //consume_keyboard

void i8086_hard_exit( const char * pcerror, uint8_t arg )
{
    g_consoleConfig.RestoreConsole( false );

    tracer.Trace( pcerror, arg );
    printf( pcerror, arg );
    tracer.Trace( "  %s\n", build_string() );
    printf( "  %s\n", build_string() );

    exit( 1 );
} //i8086_hard_exit

uint8_t i8086_invoke_in_al( uint16_t port )
{
    static uint8_t port40 = 0;
    //tracer.Trace( "invoke_in_al port %#x\n", port );

    if ( 0x3da == port )
    {
        // toggle this or apps will spin waiting for the I/O port to work.

        static uint8_t cga_status = 9;
        cga_status ^= 9;
        return cga_status;
    }
    else if ( 0x3ba == port )
    {
        // toggle this or apps will spin waiting for the I/O port to work.

        static uint8_t monochrome_status = 0x80;
        monochrome_status ^= 0x80;
        return monochrome_status;
    }
    else if ( 0x3d5 == port )
    {
        return 0;
    }
    else if ( 0x20 == port ) // pic1 int request register
    {
    }
    else if ( 0x40 == port ) // Programmable Interrupt Timer counter 0. connected to PIC chip.
    {
        return port40--;
    }
    else if ( 0x41 == port ) // Programmable Interrupt Timer counter 1. used for RAM refresh
    {
    }
    else if ( 0x42 == port ) // Programmable Interrupt Timer counter 2. connected to the speaker
    {
    }
    else if ( 0x43 == port ) // Programmable Interrupt Timer mode. write-only, can't be read
    {
    }
    else if ( 0x60 ==  port ) // keyboard data
    {
        tracer.Trace( "invoke_al_in port 60 keyboard data\n" );
        uint8_t asciiChar, scancode;
        if ( peek_keyboard( asciiChar, scancode ) )
        {
            // lie and say keyup so apps like Word 6.0 don't auto-repeat until the next keydown
            scancode |= 0x80;
            //tracer.Trace( "invoke_in_al, port %02x peeked a character and is returning %02x\n", port, scancode );
            return scancode;
        }
    }
    else if ( 0x61 == port ) // keyboard controller port
    {
    }
    else if ( 0x64 == port ) // keyboard controller read status
    {
    }

    tracer.Trace( "invoke_in_al, port %02x returning 0\n", port );
    return 0;
} //i8086_invoke_in_al

uint16_t i8086_invoke_in_ax( uint16_t port )
{
    tracer.Trace( "invoke_in_ax port %#x\n", port );
    return 0;
} //i8086_invoke_in_ax

void i8086_invoke_out_al( uint16_t port, uint8_t val )
{
    tracer.Trace( "invoke_out_al port %#x, val %#x\n", port, val );
} //i8086_invoke_out_al

void i8086_invoke_out_ax( uint16_t port, uint16_t val )
{
    tracer.Trace( "invoke_out_ax port %#x, val %#x\n", port, val );
} //i8086_invoke_out_ax

void i8086_invoke_halt()
{
    g_haltExecution = true;
} // i8086_invoke_halt

void FileTimeToDos( FILETIME & ftSystem, uint16_t & dos_time, uint16_t & dos_date )
{
    FILETIME ft;
    FileTimeToLocalFileTime( &ftSystem, &ft );
    SYSTEMTIME st = {0};
    FileTimeToSystemTime( &ft, &st );

    // low 5 bits seconds, next 6 bits minutes, high 5 bits hours

    dos_time = st.wSecond;
    dos_time |= ( st.wMinute << 5 );
    dos_time |= ( st.wHour << 11 );

    // low 5 bits day, next 4 bits month, high 7 bits year less 1980

    dos_date = st.wDay;
    dos_date |= ( st.wMonth << 5 );
    dos_date |= ( ( st.wYear - 1980 ) << 9 );
} //FileTimeToDos

bool ProcessFoundFile( DosFindFile * pff, WIN32_FIND_DATAA & fd )
{
    // these bits are the same on Windows and DOS

    uint8_t matching_attr = ( fd.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM ) );

    // return normal files always and any of the 3 special classes if their bit is set in search_attributes

    if ( ( 0 != matching_attr ) && ( 0 == ( matching_attr & pff->search_attributes ) ) )
    {
        tracer.Trace( "  file '%s' attr %#x doesn't match the attribute filter %#x\n", fd.cFileName, fd.dwFileAttributes, pff->search_attributes );
        return false;
    }

    tracer.Trace( "  actual found filename: '%s'\n", fd.cFileName );
    if ( 0 != fd.cAlternateFileName[ 0 ] )
        strcpy( pff->file_name, fd.cAlternateFileName );
    else if ( strlen( fd.cFileName ) < _countof( pff->file_name ) )
        strcpy( pff->file_name, fd.cFileName );
    else
    {
        // this only works on volumes that have the feature enabled. Most don't.

        DWORD result = GetShortPathNameA( fd.cFileName, pff->file_name, _countof( pff->file_name ) );
        if ( result > _countof( pff->file_name ) )
            return false; // files with long names are just invisible to DOS apps
    }

    _strupr( pff->file_name );
    pff->file_size = fd.nFileSizeLow;
    uint8_t attr = ( fd.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) );
    pff->file_attributes = attr;
    FileTimeToDos( fd.ftLastWriteTime, pff->file_time, pff->file_date );
    tracer.Trace( "  search found '%s', size %u, attributes %#x\n", pff->file_name, pff->file_size, pff->file_attributes );
    return true;
} //ProcessFoundFile

bool ProcessFoundFileFCB( WIN32_FIND_DATAA & fd, uint8_t attr, bool exFCB )
{
    // note: extended FCB code isn't well-tested. Multiplan setup.exe is the only app I know that uses it and it fails for other reasons
    // non-extended FCBs will have attr = 0

    attr &= ~ ( 8 ); // remove the volume label bit if set
    uint8_t matching_attr = ( fd.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM ) );

    if ( ( matching_attr && ( 0 == attr ) ) || ( ( 0 != attr) && ( 0 == ( attr & matching_attr ) ) ) )
    {
        tracer.Trace( "  file '%s' attr %#x doesn't match the attribute filter %#x\n", fd.cFileName, fd.dwFileAttributes, attr );
        return false;
    }

    tracer.Trace( "  actual found filename: '%s'\n", fd.cFileName );
    char acResult[ DOS_FILENAME_SIZE ];
    if ( 0 != fd.cAlternateFileName[ 0 ] )
        strcpy( acResult, fd.cAlternateFileName );
    else if ( strlen( fd.cFileName ) < _countof( acResult ) )
        strcpy( acResult, fd.cFileName );
    else
    {
        // this only works on volumes that have the feature enabled. Most don't.

        DWORD result = GetShortPathNameA( fd.cFileName, acResult, _countof( acResult ) );
        if ( result > _countof( acResult ) )
            return false; // files with long names are just invisible to DOS apps
    }

    _strupr( acResult );

    // now write the file into an FCB at the transfer address

    DOSFCB *pfcb = 0;
    if ( exFCB )
    {
        DOSEXFCB * pexfcb = (DOSEXFCB *) GetDiskTransferAddress();
        pexfcb->extendedFlag = 0xff;
        pexfcb->attr = ( fd.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                                 FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) );
        memset( pexfcb->reserved, 0, sizeof( pexfcb->reserved ) );
        pfcb = & (pexfcb->fcb);
    }
    else
        pfcb = (DOSFCB *) GetDiskTransferAddress();

    pfcb->drive = 0;
    for ( int i = 0; i < _countof( pfcb->name ); i++ )
        pfcb->name[ i ] = ' ';
    for ( int i = 0; i < _countof( pfcb->ext ); i++ )
        pfcb->ext[ i ] = ' ';
    for ( int i = 0; i < _countof( pfcb->name ) && 0 != acResult[i] && '.' != acResult[i]; i++ )
        pfcb->name[i] = acResult[i];
    char * pdot = strchr( acResult, '.' );
    if ( pdot )
    {
        pdot++;
        for ( int i = 0; i < _countof( pfcb->ext ) && 0 != acResult[i]; i++ )
            pfcb->ext[i] = pdot[ i ];
    }

    pfcb->fileSize = fd.nFileSizeLow;
    uint16_t file_time;
    FileTimeToDos( fd.ftLastWriteTime, file_time, pfcb->date );

    pfcb->TraceFirst16();
    return true;
} //ProcessFoundFileFCB

void PerhapsFlipTo80x25()
{
    static bool firstTime = true;

    if ( firstTime )
    {
        firstTime = false;
        if ( !g_forceConsole )
        {
            g_use80x25 = true;
            g_consoleConfig.EstablishConsole( ScreenColumns, ScreenRows, (void *) ControlHandler  );
            ClearDisplay();
        }
    }
} //PerhapsFlipTo80x25

// naming: row/col, upper/lower, left/right
void scroll_up( uint8_t * pbuf, int lines, int rul, int cul, int rlr, int clr )
{
    for ( int row = rul; row <= rlr; row++ )
    {
        int targetrow = row - lines;
        if ( targetrow >= rul )
            memcpy( pbuf + ( targetrow * ScreenColumns * 2 + cul * 2 ),
                    pbuf + ( row * ScreenColumns * 2 + cul * 2 ),
                    2 * ( 1 + clr - cul ) );

        if ( row > ( rlr - lines ) )
            memcpy( pbuf + ( row * ScreenColumns * 2 + cul * 2 ),
                    blankLine,
                    2 * ( 1 + clr - cul ) );
    }
} //scroll_up

void invoke_assembler_routine( uint16_t code_segment )
{
    // when the fint routine returns, execution will start here. The function
    // will return far to just after the fint invocation.

    cpu.push( cpu.get_cs() );
    cpu.push( cpu.get_ip() + 2 ); // return just past the fint x
    cpu.set_ip( 0 );
    cpu.set_cs( code_segment );
} //invoke_assembler_routine

void handle_int_10( uint8_t c )
{
    uint8_t row, col;

    switch( c )
    {
        case 0:
        {
            // set video mode. 0 = 40x25, 3 = 80x25, 13h = graphical. no return value

            PerhapsFlipTo80x25();
            uint8_t mode = cpu.al();
            tracer.Trace( "  set video mode to %#x\n", mode );

            if ( 2 == mode || 3 == mode ) // only 80x25 is supported with buffer address 0xb8000
            {
                g_videoMode = 3; // it's all we support
                uint8_t * pmode = GetMem( 0x40, 0x49 ); // update the mode in bios data
                *pmode = mode;
            }

            cpu.set_al( 0x30 );

            return;
        }
        case 1:
        {
            // set cursor size. 5 lower bits from each: ch = start line, cl = end line
            // bits 5 and 6 in ch: 0 = normal, 1 = invisible, 2 = slow, 3 = fast

            uint8_t cur_top = cpu.ch() & 0x1f;
            uint8_t cur_bottom = cpu.cl() & 0x1f;
            uint8_t cur_blink = ( cpu.ch() >> 5 ) & 3;

            tracer.Trace( "  set cursor size to top %u, bottom %u, blink %u\n", cur_top, cur_bottom, cur_blink );

            if ( 1 == cur_blink )
                g_consoleConfig.SetCursorInfo( 0 );
            else
                g_consoleConfig.SetCursorInfo( 100 );

            return;
        }
        case 2:
        {
            tracer.Trace( "  set cursor position to row %d col %d\n", cpu.dh(), cpu.dl() );

            GetCursorPosition( row, col );
            uint16_t lastRow = row;

            row = cpu.dh();
            col = cpu.dl();
            SetCursorPosition( row, col );

            if ( g_use80x25 )
                UpdateWindowsCursorPosition();
            else if ( 0 == col && ( row == ( lastRow + 1 ) ) )
                printf( "\n" );

            return;
        }
        case 3:
        {
            // get cursor position

            GetCursorPosition( row, col );
            cpu.set_dh( row );
            cpu.set_dl( col );
            cpu.set_ch( 0 );
            cpu.set_cl( 0 );
            tracer.Trace( "  get cursor position row %d col %d\n", cpu.dh(), cpu.dl() );

            return;
        }
        case 5:
        {
            // set active display page

            uint8_t page = cpu.al();
            if ( page <= 3 )
            {
                PerhapsFlipTo80x25();
                tracer.Trace( "  set video page to %d\n", page );
                SetActiveDisplayPage( page );
            }

            return;
        }
        case 6:
        {
            // scroll window up: AL: # of lines to scroll. If 0 or > #rows, clear entire window
            // bh: display attribute for blank lines
            // ch: row number of upper left corner
            // cl: column number of upper left corner
            // dh: row number of lower left corner
            // dl: column number of lower right corner
            // lines are inserted at the bottom with all lines moving up.

            int lines = (int) (uint8_t) cpu.al();
            if ( g_use80x25 )
            {
                init_blankline( (uint8_t) cpu.bh() );

                //printf( "%c[%dS", 27, cpu.al() );
                int rul = (int) (uint8_t) cpu.ch();
                int cul = (int) (uint8_t) cpu.cl();
                int rlr = (int) (uint8_t) cpu.dh();
                int clr = (int) (uint8_t) cpu.dl();

                tracer.Trace( "scroll window up %u lines, rul %u, cul %u, rlr %u, clr %u\n",
                              lines, rul, cul, rlr, clr );

                if ( clr < cul || rlr < rul )
                    return;

                uint8_t * pbuf = GetVideoMem();
                if ( 0 == lines || lines >= ScreenRows )
                {
                    if ( 0 == lines )
                    {
                        tracer.Trace( "SCROLLUP CLEAR!!!!!!!!\n", lines );
                        for ( int r = rul; r <= rlr; r++ )
                            memcpy( pbuf + ( r * ScreenColumns * 2 + cul * 2 ), blankLine, 2 * ( 1 + clr - cul ) );
                    }
                    else
                        ClearDisplay();
                }
                else
                {
                    // likely data: lines = 1, rul = 1, cul = 0, rlr = 24, clr = 79
                    //          or: lines = 1, rul = 0, cul = 0, rlr = 24, clr = 79

                    scroll_up( pbuf, lines, rul, cul, rlr, clr );
                }

                UpdateDisplay();
            }
            else if ( 0 == lines )
                g_consoleConfig.ClearScreen(); // works even when not initialized

            return;
        }
        case 7:
        {
            // scroll window down: AL: # of lines to scroll. If 0, clear entire window
            // bh: display attribute for blank lines
            // ch: row number of upper left corner
            // cl: column number of upper left corner
            // dh: row number of lower left corner
            // dl: column number of lower right corner
            // lines are inserted at the top with all lines moving down.

            int lines = (int) (uint8_t) cpu.al();
            if ( g_use80x25 )
            {
                init_blankline( (uint8_t) cpu.bh() );

                //printf( "%c[%dT", 27, cpu.al() );
                int rul = (int) (uint8_t) cpu.ch();
                int cul = (int) (uint8_t) cpu.cl();
                int rlr = (int) (uint8_t) cpu.dh();
                int clr = (int) (uint8_t) cpu.dl();
                tracer.Trace( "scroll window down %u lines, rul %u, cul %u, rlr %u, clr %u\n",
                              lines, rul, cul, rlr, clr );

                if ( clr < cul || rlr < rul )
                    return;

                uint8_t * pbuf = GetVideoMem();
                if ( 0 == lines || lines >= ScreenRows )
                {
                    if ( 0 == lines )
                    {
                        tracer.Trace( "SCROLLDOWN CLEAR!!!!!!!!\n", lines );
                        for ( int r = rul; r <= rlr; r++ )
                            memcpy( pbuf + ( r * ScreenColumns * 2 + cul * 2 ), blankLine, 2 * ( 1 + clr - cul ) );
                    }
                    else
                        ClearDisplay();
                }
                else
                {
                    // likely data: lines = 1, rul = 1, cul = 0, rlr = 24, clr = 79
                    //          or: lines = 1, rul = 0, cul = 0, rlr = 24, clr = 79

                    for ( int r = rlr; r >= rul; r-- )
                    {
                        int targetrow = r + lines;
                        if ( targetrow <= rlr )
                            memcpy( pbuf + ( targetrow * ScreenColumns * 2 + cul * 2 ),
                                    pbuf + ( r * ScreenColumns * 2 + cul * 2 ),
                                    2 * ( clr - cul ) );

                        if ( r <= ( rul + lines ) )
                            memcpy( pbuf + ( r * ScreenColumns * 2 + cul * 2 ),
                                    blankLine,
                                    2 * ( 1 + clr - cul ) );
                    }
                }

                UpdateDisplay();
            }
            else if ( 0 == lines )
                g_consoleConfig.ClearScreen(); // works even when not initialized

            return;
        }
        case 8:
        {
            // read attributes+character at current position. bh == display page
            // returns al character and ah attribute of character

            PerhapsFlipTo80x25();

            if ( g_use80x25 )
            {
                // gwbasic uses this for input$ to get the character typed.

                GetCursorPosition( row, col );
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;
                cpu.set_al( pbuf[ offset ] );
                cpu.set_ah( pbuf[ 1 + offset ] );
                tracer.Trace( " returning character %02x, '%c'\n", cpu.al(), printable( cpu.al() ) );
            }
            else
            {
                cpu.set_al( ' ' ); // apps don't do this when in teletype mode.
                tracer.Trace( " returning character %02x, '%c'\n", cpu.al(), printable( cpu.al() ) );
                cpu.set_ah( 0 );
            }

            return;
        }
        case 9:
        {
            // output character in AL. Attribute in BL, Count in CX. Do not move the cursor

            GetCursorPosition( row, col );
            tracer.Trace( "  output character %#x, '%c', %#x times, attribute %#x, row %u, col %u\n",
                          cpu.al(), printable( cpu.al() ), cpu.get_cx(), cpu.bl(), row, col );

            char ch = cpu.al();

            if ( g_use80x25 )
            {
                ch = printable( ch );
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.get_cx(); t++ )
                {
                    pbuf[ offset ] = ch;
                    pbuf[ 1 + offset ] = cpu.bl();
                }

                throttled_UpdateDisplay();
            }
            else
            {
                if ( 0x1b == ch ) // don't show escape characters; as left arrows aren't shown
                    ch = ' ';

                if ( 0xd != ch )
                    printf( "%c", ch );
            }

            return;
        }
        case 0xa:
        {
            // output character only. Just like output character, but ignore attributes

            GetCursorPosition( row, col );
            tracer.Trace( "  output character only %#x, %#x times, row %u, col %u\n",
                          cpu.al(), cpu.get_cx(), row, col );

            char ch = cpu.al();
            if ( 0x1b == ch ) // escape should be a left arrow, but it just confuses the console
                ch = ' ';

            if ( g_use80x25 )
            {
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.get_cx(); t++ )
                    pbuf[ offset ] = ch;

                throttled_UpdateDisplay();
            }
            else
            {
                if ( 0xd != ch )
                    printf( "%c", ch );
            }

            return;
        }
        case 0xf:
        {
            // get video mode

            //PerhapsFlipTo80x25();  too aggressive for some apps

            cpu.set_al( g_videoMode );
            cpu.set_ah( ScreenColumns ); // columns
            cpu.set_bh( GetActiveDisplayPage() ); // active display page

            tracer.Trace( "  returning video mode %u, columns %u, display page %u\n", cpu.al(), cpu.ah(), cpu.bh() );

            return;
        }
        case 0x10:
        {
            // set palette registers (ignore)

            return;
        }
        case 0x11:
        {
            // character generator (ignore)

            PerhapsFlipTo80x25();  // QuickPascal calls this, and it's a good indication of 80x25 mode
            return;
        }
        case 0x12:
        {
            // video subsystem configuration. alternate select. return some defaults

            if ( 0x10 == cpu.bl() )
            {
                // wordperfect uses this to see how much RAM is installed
                tracer.Trace( "app: '%s'\n", g_acApp );

                if ( ends_with( g_acApp, "wp.com" ) )
                {
                    cpu.set_bx( 0xa );
                    return;
                }

                // Quick C 2.01 thinks it's an EGA card at 80x43 if 0 is returned. I can't explain it.
                // This doesn't break Quick C 1.
                // Not setting bx to 0 breaks apps like QBX 7.1

                if ( ! ends_with( g_acApp, "qc.exe" ) ) 
                    cpu.set_bx( 0 );

                // setting this breaks quick pascal cpu.set_cx( 0x5 ); // primary cga 80x25
            }

            return;
        }
        case 0x15:
        {
            // get physical display charactics

            cpu.set_ax( 0 ); // none

            return;
        }
        case 0x1a:
        {
            if ( 0 == cpu.al() )
            {
                PerhapsFlipTo80x25();  // Turbo basic 1.1 calls this
                cpu.set_bl( 2 ); // CGA with color (active display)
                cpu.set_bh( 2 ); // CGA with color (alternate display)
            }
            return;
        }
        case 0x1b:
        {
            // video functionality / state information

            cpu.set_al( 0 ); // indicate that the call isn't supported and es:di wasn't populated with mcga+ info

            return;
        }
        case 0x1c:
        {
            // save/restore video state
            // ps50+, vga, so n/a

            cpu.set_al( 0 ); // not supported because only CGA is supported

            return;
        }
        case 0xef:
        {
            // get video adapter type and mode

            cpu.set_dl( 0xff ); // not a hercules-compatible card

            return;
        }
        case 0xfa:
        {
            // ega register interface library -- interrogate driver

            cpu.set_bx( 0 ); // RIL / mouse driver not present

            return;
        }
        default:
        {
            tracer.Trace( "unhandled int10 command %02x\n", c );
        }
    }
} //handle_int_10

void handle_int_16( uint8_t c )
{
    uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
    pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();
    uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
    uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
    //tracer.Trace( "  int_16 head: %04x, tail %04x\n", *phead, *ptail );

    switch( c )
    {
        case 0:
        case 0x10:
        {
            // get character. ascii into al, scancode into ah

            if ( g_use80x25 )
                UpdateDisplay();

#if 1
            invoke_assembler_routine( g_int16_0_seg );
            return;
#else
            while ( *phead == *ptail )
            {
                // block waiting for a character then return it.

                while ( !peek_keyboard( true, true, false ) )
                    continue;

                consume_keyboard();
            }

            cpu.set_al( pbiosdata[ *phead ] );
            (*phead)++;
            cpu.set_ah( pbiosdata[ *phead ] );
            (*phead)++;
            if ( *phead >= 0x3e )
                *phead = 0x1e;

            tracer.Trace( "  returning character %04x '%c'\n", cpu.get_ax(), printable( cpu.al() ) );
            tracer.Trace( "  int_16 exit head: %04x, tail %04x\n", *phead, *ptail );
            return;
#endif
        }
        case 1:
        case 0x11: // 0x11 is really the same behavior as 1 except it can return a wider set of characters
        {
            // check keyboard status. checks if a character is available. return it if so, but not removed from buffer
            // set zero flag if no character is available. clear zero flag if a character is available

            cpu.set_ah( 0 );

            // apps like WordStar draw a bunch of text to video memory then call this, which is the chance to update the display

            if ( g_use80x25 )
            {
                bool update = throttled_UpdateDisplay();
                if ( update )
                    g_int16_1_loop = false;
            }

            if ( *phead == *ptail )
            {
                cpu.set_zero( true );
                if ( g_int16_1_loop ) // avoid a busy loop it makes my fan loud
                    SleepAndScheduleInterruptCheck();
                else
                    g_int16_1_loop = true;
            }
            else
            {
                cpu.set_al( pbiosdata[ *phead ] );
                cpu.set_ah( pbiosdata[ 1 + ( *phead ) ] );
                cpu.set_zero( false );
            }

            tracer.Trace( "  returning flag %d, ax %04x\n", cpu.get_zero(), cpu.get_ax() );
            //tracer.Trace( "  int_16 exit head: %04x, tail %04x\n", *phead, *ptail );
            return;
        }
        case 2:
        {
            // get shift status (and alt/ctrl/etc.)

            cpu.set_al( pbiosdata[ 0x17 ] );
            tracer.Trace( "  keyboard flag status: %02x\n", pbiosdata[ 0x17 ] );
            return;
        }
        case 5:
        {
            // store keystroke in keyboard buffer
            // ch: scancode, cl ascii char
            // returns: al: 0 success, 1 keyboard buffer full

            if ( ! ( ( *phead == ( *ptail + 2 ) ) || ( ( 0x1e == *phead ) && ( 0x3c == *ptail ) ) ) )
            {
                pbiosdata[ *ptail ] = cpu.cl();
                (*ptail)++;
                pbiosdata[ *ptail ] = cpu.ch();
                (*ptail)++;
                if ( *ptail >= 0x3e )
                    *ptail = 0x1e;
                cpu.set_al( 0 );
                tracer.Trace( "  successfully stored keystroke in buffer\n" );
            }
            else
            {
                cpu.set_al( 1 );
                tracer.Trace( "  keyboard buffer full; can't store character\n" );
            }

            return;
        }
        case 0x55:
        {
            // microsoft internal TSRs al = fe for qbasic and al = 0/ff for word
            return;
        }
        default: 
            tracer.Trace( "unhandled int16 command %02x\n", c );
    }
} //handle_int_16

void HandleAppExit()
{
    tracer.Trace( "  HandleAppExit for app psp %#x, '%s'\n", g_currentPSP, GetCurrentAppPath() );
    DOSPSP * psp = (DOSPSP *) GetMem( g_currentPSP, 0 );
    psp->Trace();
    trace_all_allocations();

    // flush and close any files opened by this process

    fflush( 0 ); // the app may have written to files and not flushed or closed them

    trace_all_open_files();

    do
    {
        size_t index = FindFileEntryIndexByProcess( g_currentPSP );
        if ( -1 == index )
            break;
        uint16_t handle = g_fileEntries[ index ].handle;
        tracer.Trace( "  closing file an app leaked: '%s', handle %04x\n", g_fileEntries[ index ].path, handle );
        FILE * fp = RemoveFileEntry( handle );
        fclose( fp );
    } while ( true );

    g_appTerminationReturnCode = cpu.al();
    tracer.Trace( "  app exit code: %d\n", g_appTerminationReturnCode );
    uint16_t pspToDelete = g_currentPSP;
    tracer.Trace( "  app exiting, segment %04x, psp: %p, environment segment %04x\n", g_currentPSP, psp, psp->segEnvironment );
    FreeMemory( psp->segEnvironment );

    if ( psp && ( firstAppTerminateAddress != psp->int22TerminateAddress ) )
    {
        g_currentPSP = psp->segParent;
        cpu.set_cs( ( psp->int22TerminateAddress >> 16 ) & 0xffff );
        cpu.set_ip( psp->int22TerminateAddress & 0xffff );
        cpu.set_ss( psp->parentSS ); // not DOS standard, but workaround for apps like QCL.exe Quick C v 1.0 that doesn't restore the stack
        cpu.set_sp( psp->parentSP ); // ""
        tracer.Trace( "  returning from nested app to return address %04x:%04x\n", cpu.get_cs(), cpu.get_ip() );
    }
    else
    {
        cpu.end_emulation();
        g_haltExecution = true;
    }

    FreeMemory( pspToDelete );

    // free any allocations made by the app not already freed

    do
    {
        size_t index = FindAllocationEntryByProcess( pspToDelete );
        if ( -1 == index )
            break;

        tracer.Trace( "  freeing RAM an app leaked, segment %04x (MCB+1) para length %04x\n", g_allocEntries[ index ].segment, g_allocEntries[ index ].para_length );
        FreeMemory( g_allocEntries[ index ].segment );
    } while( true );
} //HandleAppExit

uint8_t HighestDrivePresent()
{
    DWORD dwDriveMask = GetLogicalDrives();

    // a = 0 ... z = 25

    for ( DWORD b = 0; b < 32; b++ )
    {
        DWORD bit = 0x80000000 >> b;
        if ( bit & dwDriveMask )
            return (uint8_t) ( 32 - b - 1 );
    }

    return 0;
} //HighestDrivePresent

void star_to_question( char * pstr, int len )
{
    for ( int i = 0; i < len; i++ )
    {
        if ( '*' == pstr[ i ] )
        {
            for ( int j = i; j < len; j++ )
                pstr[ j ] = '?';

            break;
        }
    }
} //star_to_question

void handle_int_21( uint8_t c )
{
    uint8_t row, col;
    bool ah_used = false;

    switch( c )
    {
        case 0:
        {
            // terminate program
    
            HandleAppExit();

            return;
        }
        case 2:
        {
            // output character in DL. move cursor as appropriate
            // todo: interpret 7 (beep), 8 (backspace), 9 (tab) to tab on multiples of 8. 10 lf should move cursor down and scroll if needed
    
            char ch = cpu.dl();

            if ( g_use80x25 )
            {
                uint8_t * pbuf = GetVideoMem();
                GetCursorPosition( row, col );
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                if ( 8 == ch )
                {
                    if ( col > 0 )
                    {
                        col--;
                        offset = row * 2 * ScreenColumns + col * 2;
                        pbuf[ offset ] = ' ';
                    }
                }
                else if ( 0xa == ch ) // CR
                    col = 0;
                else if ( 0xd == ch ) // LF
                {
                    if ( row >= ScreenRowsM1 )
                    {
                        tracer.Trace( "  line feed scrolling up a line\n"  );
                        scroll_up( pbuf, 1, 0, 0, ScreenRowsM1, ScreenColumnsM1 );
                    }
                    else
                        row = row + 1;
                }
                else
                {
                    pbuf[ offset ] = ch;
                    if ( 0 == pbuf[ offset + 1 ] )
                        pbuf[ offset + 1 ] = DefaultVideoAttribute;
                    col++;
                }
                SetCursorPosition( row, col );
            }
            else
            {
                if ( 0x0d != ch )
                {
                    if ( 8 == ch )
                        printf( "%c ", ch );
                    printf( "%c", ch );
                }
            }
    
            return;
        }
        case 6:
        {
            // direct console character I/O
            // DL = 0xff means get input into AL if available and set ZF to 0. Set ZF to 1 if no character is available
            // DL = !0xff means output the character
    
            if ( 0xff == cpu.dl() )
            {
                // input. don't block if nothing is available
                // Multiplan (v2) is the only app I've found that uses this function for input.

                uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
                uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
                uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
                tracer.Trace( "  int_21 character input head: %04x, tail %04x\n", *phead, *ptail );

                if ( *phead != *ptail )
                {
                    static bool mid_scancode_read = false;
                    cpu.set_zero( false );

                    if ( mid_scancode_read )
                    {
                        mid_scancode_read = false;
                        cpu.set_al( pbiosdata[ * ( phead + 1 ) ] );
                        (*phead) += 2;
                        if ( *phead >= 0x3e )
                            *phead = 0x1e;
                    }
                    else
                    {
                        cpu.set_al( pbiosdata[ *phead ] );

                        if ( 0 == *phead )
                            mid_scancode_read = true;
                        else
                        {
                            (*phead) += 2;
                            if ( *phead >= 0x3e )
                                *phead = 0x1e;
                        }
                    }
                }
                else
                {
                    cpu.set_zero( true );
                    SleepAndScheduleInterruptCheck(); // Multiplan v2 has a busy loop with this interrupt waiting for input
                }
            }
            else
            {
                // output
    
                char ch = cpu.dl();
                tracer.Trace( "    direct console output %02x, '%c'\n", (uint8_t) ch, printable( (uint8_t) ch ) );
                if ( 0x0d != ch )
                    printf( "%c", ch );
            }
    
            return;
        }
        case 7:
        case 8:
        {
            // character input. block until a keystroke is available.

            uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
            pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();

            if ( g_use80x25)
                UpdateDisplay();

#if 1
            invoke_assembler_routine( g_int21_8_seg );
            return;
#else
            uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
            uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
            bool first = true;

            while ( *phead == *ptail )
            {
                if ( first && g_use80x25 )
                {
                    first = false;
                    UpdateDisplay();
                }

                // wait for a character then return it.

                while ( !peek_keyboard( true, true, false ) )
                    continue;

                consume_keyboard();
            }

            cpu.set_al( pbiosdata[ *phead ] );
            (*phead)++;

            char scan_code = pbiosdata[ *phead ];
            (*phead)++; // consume the scancode though it's not returned

            if ( *phead >= 0x3e )
                *phead = 0x1e;

            tracer.Trace( "  int_21 7/8 exit head: %04x, tail %04x\n", *phead, *ptail );
            tracer.Trace( "  direct character input returning al %#x. scan_code %#x\n", cpu.al(), scan_code );
            return;
#endif
        }
        case 9:
        {
            // print string. prints chars up to a dollar sign $
    
            char * p = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.TraceBinaryData( (uint8_t *) p, 0x40, 2 );
            while ( *p && '$' != *p )
                printf( "%c", *p++ );
    
            return;
        }
        case 0xa:
        {
            // Buffered Keyboard input. DS::DX pointer to buffer. byte 0 count in, byte 1 count out excluding CR, byte 2 starts the response
            // The assembler version enables the emulator to send timer and keyboard interrupts.

#if 1
            invoke_assembler_routine( g_int21_a_seg );
            return;
#else
            uint8_t * p = GetMem( cpu.get_ds(), cpu.get_dx() );
            uint8_t maxLen = p[0];
            p[2] = 0;

            // This blocks and prevents timer interrupts from being processed.

            char * result = ConsoleConfiguration::portable_gets_s( (char *) p + 2, maxLen - 1 );
            if ( result )
            {
                size_t len = strlen( result );
                p[ 1 ] = (uint8_t) len;

                 // replace null termination with CR, which apps like DEBUG.COM require
                 // the CR isn't included in the string's length.

                p[ 2 + len ] = 0x0d;
            }
            else
                p[1] = 0;

            tracer.Trace( "  returning length %d, string '%.*s'\n", p[1], p[1], p + 2 );

            return;
#endif
        }
        case 0xb:
        {
            // check standard input status. Returns AL: 0xff if char available, 0 if not

            uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
            uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
            uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
            tracer.Trace( "  int_21 check input status head: %04x, tail %04x\n", *phead, *ptail );

            cpu.set_al( ( *phead != *ptail ) ? 0xff : 0 );

            return;
        }
        case 0xc:
        {
            // clear input buffer and execute int 0x21 on command in register AL

            while ( peek_keyboard() )
                consume_keyboard();
    
            cpu.set_ah( cpu.al() );
            tracer.Trace( "recursing to int 0x21 with command %#x\n", cpu.ah() );
            i8086_invoke_interrupt( 0x21 );
    
            return;
        }
        case 0xd:
        {
            // disk reset. ensures buffers are flushed to disk

            fflush( 0 ); // this flushes all open streams opened for write

            return;
        }
        case 0xe:
        {
            // select disk. dl = new default drive 0=a, 1=b...
            // on return: al = # of logical drives

            cpu.set_al( HighestDrivePresent() );
            tracer.Trace( "  new default drive: '%c'. highest drive present: %d\n", 'A' + cpu.dl(), cpu.al() );

            char acDir[ 3 ];
            acDir[0] = cpu.dl() + 'A';
            acDir[1] = ':';
            acDir[2] = 0;
            BOOL ok = SetCurrentDirectoryA( acDir );
            tracer.Trace( "  result of SetCurrentDirectory to '%s': %d\n", acDir, ok );

            return;
        }
        case 0xf:
        {
            // open file using FCB
    
            tracer.Trace( "open file using FCB. ds %u dx %u\n", cpu.get_ds(), cpu.get_dx() );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );

            if ( 0 == pfcb->drive )
                pfcb->drive = get_current_drive();
    
            cpu.set_al( 0xff );
            char filename[ DOS_FILENAME_SIZE ];
            if ( GetDOSFilename( *pfcb, filename ) )
            {
                tracer.Trace( "  opening %s\n", filename );
                pfcb->SetFP( 0 );
    
                FILE * fp = fopen( filename, "r+b" );
                if ( fp )
                {
                    tracer.Trace( "  file opened successfully\n" );
                    pfcb->SetFP( fp );
    
                    fseek( fp, 0, SEEK_END );
                    pfcb->fileSize = ftell( fp );
                    fseek( fp, 0, SEEK_SET );
    
                    pfcb->curBlock = 0;
                    pfcb->recSize = 0x80;
                    pfcb->curRecord = 0;
                    pfcb->recNumber = 0;
    
                    pfcb->Trace();
                    cpu.set_al( 0 );
                }
                else
                    tracer.Trace( "  ERROR: file open using FCB of %s failed, error %d = %s\n", filename, errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR: couldn't parse filename in FCB\n" );
    
            return;
        }
        case 0x10:
        {
            // close file using FCB
    
            tracer.Trace( "close file using FCB\n" );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
    
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                fclose( fp );
                pfcb->SetFP( 0 );
            }
            else
                tracer.Trace( "  ERROR: file close using FCB of a file that's not open\n" );
    
            return;
        }
        case 0x11:
        {
            // search first using FCB.
            // DS:DX points to FCB
            // if the first byte of the FCB (the drive) is 0xff it's an extended FCB.
            // returns AL = 0 if file found, FF if not found
            //    if found, DTA is used as an FCB ready for an open or delete

            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                FindClose( g_hFindFirst );
                g_hFindFirst = INVALID_HANDLE_VALUE;
            }

            bool extendedFCB = false;
            DOSFCB *pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            uint8_t attr = 0;
            if ( 0xff == pfcb->drive )
            {
                extendedFCB = true;
                DOSEXFCB * pexfcb = (DOSEXFCB *) pfcb;
                attr = pexfcb->attr;
                pfcb = & (pexfcb->fcb);
            }

            pfcb->TraceFirst16();
            char search_string[ 20 ];
            bool ok = GetDOSFilename( *pfcb, search_string );
            if ( ok )
            {
                tracer.Trace( "  searching for pattern '%s'\n", search_string );
                WIN32_FIND_DATAA fd = {0};
                g_hFindFirst = FindFirstFileA( search_string, &fd );
                if ( INVALID_HANDLE_VALUE != g_hFindFirst )
                {
                    do
                    {
                        ok = ProcessFoundFileFCB( fd, attr, extendedFCB );
                        if ( ok )
                        {
                            cpu.set_al( 0 );
                            break;
                        }

                        BOOL found = FindNextFileA( g_hFindFirst, &fd );
                        if ( !found )
                        {
                            cpu.set_al( 0xff );
                            tracer.Trace( "  WARNING: search next using FCB (in search first function) found no more, error %d\n", GetLastError() );
                            FindClose( g_hFindFirst );
                            g_hFindFirst = INVALID_HANDLE_VALUE;
                            break;
                        }
                    } while( true );
                }
                else
                {
                    cpu.set_al( 0xff );
                    tracer.Trace( "  WARNING: search first using FCB failed, error %d\n", GetLastError() );
                }
            }
            else
            {
                cpu.set_al( 0xff );
                tracer.Trace( "  ERROR: search first using FCB failed to parse the search string\n" );
            }

            return;
        }
        case 0x12:
        {
            // search next using FCB.
            // DS:DX points to FCB
            // returns AL = 0 if file found, FF if not found
            //    if found, DTA is used as an FCB ready for an open or delete

            if ( INVALID_HANDLE_VALUE == g_hFindFirst )
                cpu.set_al( 0xff );
            else
            {
                DOSFCB *pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
                uint8_t attr = 0;
                bool extendedFCB = false;
                if ( 0xff == pfcb->drive )
                {
                    extendedFCB = true;
                    DOSEXFCB * pexfcb = (DOSEXFCB *) pfcb;
                    attr = pexfcb->attr;
                    pfcb = & (pexfcb->fcb);
                }

                WIN32_FIND_DATAA fd = {0};

                do
                {
                    BOOL found = FindNextFileA( g_hFindFirst, &fd );
                    if ( found )
                    {
                        bool ok = ProcessFoundFileFCB( fd, attr, extendedFCB );
                        if ( ok )
                        {
                            cpu.set_al( 0 );
                            break;
                        }
                    }
                    else
                    {
                        cpu.set_al( 0xff );
                        tracer.Trace( "  WARNING: search next using FCB found no more, error %d\n", GetLastError() );
                        FindClose( g_hFindFirst );
                        g_hFindFirst = INVALID_HANDLE_VALUE;
                        break;
                    }
                } while( true );
            }
    
            return;
        }
        case 0x13:
        {
            // delete file using FCB
    
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
    
            cpu.set_al( 0xff );
            char filename[ DOS_FILENAME_SIZE ];
            if ( GetDOSFilename( *pfcb, filename ) )
            {
                tracer.Trace( "  deleting %s\n", filename );
    
                int removeok = ( 0 == remove( filename ) );
                if ( removeok )
                {
                    cpu.set_al( 0 );
                    tracer.Trace( "delete successful\n" );
                }
                else
                    tracer.Trace( "  ERROR: delete file failed, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR: couldn't parse filename in FCB\n" );
    
            return;
        }
        case 0x14:
        {
            // sequential read using FCB.
            // input: ds:dx points at the FCB
            // output: al: 0 success
            //             1 end of file, no data read
            //             2 segment wrap in DTA, no data read
            //             3 end of file, partial record read
            //         disk transfer area DTA is filled with data from the current file position in the random record/size of the FCB
            //         the file position is updated after the read
            //         partial reads are 0-filled
            //         one record is read

            cpu.set_al( 1 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                ULONG seekOffset = pfcb->curRecord * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                bool ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    memset( GetDiskTransferAddress(), 0, pfcb->recSize );
                    size_t num_read = fread( GetDiskTransferAddress(), 1, pfcb->recSize, fp );
                    if ( num_read )
                    {
                         tracer.Trace( "  read succeded: %u bytes. recsize %u bytes\n", num_read, pfcb->recSize );
                         if ( num_read == pfcb->recSize )
                             cpu.set_al( 0 );
                         else
                             cpu.set_al( 3 );
                         pfcb->curRecord++;
                    }
                    else
                         tracer.Trace( "  read failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR sequential read using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR sequential read using FCBs doesn't have an open file\n" );

            return;
        }
        case 0x15:
        {
            // sequential write using FCB.
            // input: ds:dx points at the FCB
            // output: al: 0 success
            //             1 disk full
            //             2 segment wrap in DTA, no data written
            //         current record field is updates
            //         one record is written

            cpu.set_al( 1 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                ULONG seekOffset = pfcb->curRecord * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                bool ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    size_t num_written = fwrite( GetDiskTransferAddress(), 1, pfcb->recSize, fp );
                    if ( num_written )
                    {
                         tracer.Trace( "  write succeded: %u bytes. recsize %u bytes\n", num_written, pfcb->recSize );
                         cpu.set_al( 0 );
                         pfcb->curRecord++;
                    }
                    else
                         tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR sequential write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR sequential write using FCBs doesn't have an open file\n" );

            return;
        }
        case 0x16:
        {
            // create using FCB
    
            tracer.Trace( "create using FCB. ds %u dx %u\n", cpu.get_ds(), cpu.get_dx() );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
            cpu.set_al( 0xff );
    
            if ( 0 == pfcb->drive )
                pfcb->drive = get_current_drive();
    
            char filename[ DOS_FILENAME_SIZE ];
            if ( GetDOSFilename( *pfcb, filename ) )
            {
                tracer.Trace( "  creating %s\n", filename );
                pfcb->SetFP( 0 );
    
                FILE * fp = fopen( filename, "w+b" );
                if ( fp )
                {
                    tracer.Trace( "  file created successfully\n" );
                    cpu.set_al( 0 );
                    pfcb->SetFP( fp );
    
                    pfcb->curBlock = 0;
                    pfcb->recSize = 0x80;
                    pfcb->fileSize = 0;
                    pfcb->recNumber = 0;
                    pfcb->curRecord = 0;
    
                    pfcb->Trace();
                }
                else
                    tracer.Trace( "  ERROR: file create using FCB of %s failed, error %d = %s\n", filename, errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR: can't parse filename from FCB\n" );
    
            return;
        }
        case 0x17:
        {
            // rename file using FCB. Returns AL 0 if success and 0xff on failure.
    
            cpu.set_al( 0xff );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
            DOSFCB * pfcbNew = (DOSFCB * ) ( 0x10 + (uint8_t *) pfcb );
    
            char oldFilename[ DOS_FILENAME_SIZE ] = {0};
            if ( GetDOSFilename( *pfcb, oldFilename ) )
            {
                char newFilename[ DOS_FILENAME_SIZE ] = {0};
                if ( GetDOSFilename( *pfcbNew, newFilename ) )
                {
                    tracer.Trace( "rename old name '%s', new name '%s'\n", oldFilename, newFilename );
    
                    if ( !rename( oldFilename, newFilename ) )
                    {
                        tracer.Trace( "rename successful\n" );
                        cpu.set_al( 0 );
                    }
                    else
                        tracer.Trace( "  ERROR: can't rename file, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR: can't parse new filename in FCB\n" );
            }
            else
                tracer.Trace( "  ERROR: can't parse old filename in FCB\n" );
    
            return;
        }
        case 0x19:
        {
            // get default drive. 0 == a:, 1 == b:, etc. returned in AL

            uint8_t drive = get_current_drive();
            cpu.set_al( drive );
            tracer.Trace( "  returning default drive as '%c', # %d\n", (char) drive + 'A' , drive );
            return;
        }
        case 0x1a:
        {
            // set disk transfer address from ds:dx
    
            tracer.Trace( "  set disk transfer address updated from %04x:%04x to %04x:%04x\n",
                          g_diskTransferSegment, g_diskTransferOffset, cpu.get_ds(), cpu.get_dx() );
            g_diskTransferSegment = cpu.get_ds();
            g_diskTransferOffset = cpu.get_dx();
    
            return;
        }
        case 0x1c:
        {
            // get allocation information for a specific drive
            // input: dl: drive. 00 == default, 1 == a ...
            // output: al: sectors per cluster or ff in invalid drive
            //         cx: bytes per sector
            //         dx: total number of clusters
            //         ds:bx: points to media id byte (f8 = hard disk)
            // make up something reasonable -- 8mb

            uint8_t drive = cpu.dl();

            if ( drive > 26 )
                drive = 0;

            if ( 0 == drive )
                drive = get_current_drive();
            else
                drive--;

            DWORD dwDriveMask = GetLogicalDrives(); // a = 0 ... z = 25

            if ( dwDriveMask & ( 1 << drive ) )
            {
                cpu.set_al( 8 );
                cpu.set_cx( 512 );
                cpu.set_dx( 2048 );
                tracer.Trace( "  reporting 8mb on drive %d (%c)\n", drive, drive + 'A' );
            }
            else
            {
                tracer.Trace( "  reporting drive %d (%c) is invalid\n", drive, drive + 'A' );
                cpu.set_al( 0xff );
            }

    
            return;
        }
        case 0x21:
        {
            // random read using FCBs.
            // input: ds:dx points at the FCB
            // output: al: 0 success
            //             1 end of file, no data read
            //             2 segment wrap in DTA, no data read
            //             3 end of file, partial record read
            //         disk transfer area DTA is filled with data from the current file position in the random record/size of the FCB
            //         the file position isn't updated after the read
            //         partial reads are 0-filled
            //         one record is read

            cpu.set_al( 1 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                // lotus 123 1.0a writes a random value to the high byte; mask it away

                ULONG seekOffset = ( pfcb->recNumber & 0xffffff ) * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                bool ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    memset( GetDiskTransferAddress(), 0, pfcb->recSize );
                    size_t num_read = fread( GetDiskTransferAddress(), 1, pfcb->recSize, fp );
                    if ( num_read )
                    {
                         tracer.Trace( "  read succeded: %u bytes. recsize %u bytes\n", num_read, pfcb->recSize );
                         if ( num_read == pfcb->recSize )
                             cpu.set_al( 0 );
                         else
                             cpu.set_al( 3 );
    
                         // don't update the fcb's record number for this version of the API
                    }
                    else
                         tracer.Trace( "  read failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random read using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR random read using FCBs doesn't have an open file\n" );

            return;
        }
        case 0x22:
        {
            // random write using FCBs. on output, 0 if success, 1 if disk full, 2 if DTA too small
            // CX has # of records written on exit
    
            cpu.set_al( 1 );
            uint16_t recsToWrite = cpu.get_cx();
            cpu.set_cx( 0 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                // lotus 123 1.0a writes a random value to the high byte; mask it away

                ULONG seekOffset = ( pfcb->recNumber & 0xffffff ) * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                bool ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    size_t num_written = fwrite( GetDiskTransferAddress(), recsToWrite, pfcb->recSize, fp );
                    if ( num_written )
                    {
                         tracer.Trace( "  write succeded: %u bytes\n", recsToWrite * pfcb->recSize );
                         cpu.set_cx( recsToWrite );
                         cpu.set_al( 0 );
    
                         // don't update the fcb's record number for this version of the API
                    }
                    else
                         tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random block write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR random block write using FCBs doesn't have an open file\n" );
    
            return;
        }
        case 0x25:
        {
            // set interrupt vector
    
            tracer.Trace( "  setting interrupt vector %02x %s to %04x:%04x\n", cpu.al(), get_interrupt_string( cpu.al(), 0, ah_used ), cpu.get_ds(), cpu.get_dx() );
            uint16_t * pvec = (uint16_t *) GetMem( 0, 4 * (uint16_t) cpu.al() );
            pvec[0] = cpu.get_dx();
            pvec[1] = cpu.get_ds();
            return;
        }
        case 0x26:
        {
            // Create PSP. on return: DX = segment address of new PSP
            // online documentation is conflicting about whether DX on entry points to the new PSP or if
            // this function should allocate the PSP, but apps do the former.
            // The current PSP should be copied on top of the new PSP.
            // The only apps I know of that use this are debug.com from MS-DOS 2.2 and Turbo Pascal 5.5

            uint16_t seg = cpu.get_dx();
            memcpy( GetMem( seg, 0 ), GetMem( g_currentPSP, 0 ), sizeof( DOSPSP ) );
            return;
        }
        case 0x27:
        {
            // random block read using FCBs
            // CX: number of records to read
            // DS:BX pointer to the FCB.
            // on exit, AL 0 success, 1 EOF no data read, 2 dta too small, 3 eof partial read (filled with 0s)
    
            cpu.set_al( 1 ); // eof
            ULONG cRecords = cpu.get_cx();
            cpu.set_cx( 0 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "random block read using FCBs\n" );
            pfcb->Trace();
            ULONG seekOffset = pfcb->recNumber * pfcb->recSize;

            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                fseek( fp, 0, SEEK_END );
                pfcb->fileSize = ftell( fp );
                fseek( fp, 0, SEEK_SET );
            }

            if ( seekOffset > pfcb->fileSize )
            {
                tracer.Trace( "  ERROR: random read beyond end of file offset %u, filesize %u\n", seekOffset, pfcb->fileSize );
                cpu.set_al( 1 ); // eof
            }
            else if ( seekOffset == pfcb->fileSize )
            {
                tracer.Trace( "  WARNING: random read at end of file offset %u, filesize %u\n", seekOffset, pfcb->fileSize );
                cpu.set_al( 1 ); // eof
            }
            else
            {
                if ( fp )
                {
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
                    bool ok = !fseek( fp, seekOffset, SEEK_SET );
                    if ( ok )
                    {
                        ULONG askedBytes = pfcb->recSize * cRecords;
                        memset( GetDiskTransferAddress(), 0, askedBytes );
                        ULONG toRead = __min( pfcb->fileSize - seekOffset, askedBytes );
                        size_t numRead = fread( GetDiskTransferAddress(), toRead, 1, fp );
                        if ( numRead )
                        {
                            if ( toRead == askedBytes )
                                cpu.set_al( 0 );
                            else
                                cpu.set_al( 3 ); // eof encountered, last record is partial
    
                            cpu.set_cx( (uint16_t) ( toRead / pfcb->recSize ) );
                            tracer.Trace( "  successfully read %u bytes, CX set to %u:\n", toRead, cpu.get_cx() );
                            tracer.TraceBinaryData( GetDiskTransferAddress(), toRead, 4 );
                            pfcb->curRecord += (uint8_t) cRecords;
                            pfcb->recNumber += (uint32_t) cRecords;
                        }
                        else
                            tracer.Trace( "  ERROR random block read using FCBs failed to read, error %d = %s\n", errno, strerror( errno ) );
                    }
                    else
                        tracer.Trace( "  ERROR random block read using FCBs failed to seek, error %d= %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random block read using FCBs doesn't have an open file\n" );
            }
    
            return;
        }
        case 0x28:
        {
            // random block write using FCBs.
            // in: CX = number of records, DS:BX the fcb
            // out: al = 0 if success, 1 if disk full, 2 if data too smaoo, cx = number of records written
    
            cpu.set_al( 1 );
            uint16_t recsToWrite = cpu.get_cx();
            cpu.set_cx( 0 );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                ULONG seekOffset = pfcb->recNumber * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                bool ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    size_t num_written = fwrite( GetDiskTransferAddress(), recsToWrite, pfcb->recSize, fp );
                    if ( num_written )
                    {
                         tracer.Trace( "  write succeded: %u bytes\n", recsToWrite * pfcb->recSize );
                         cpu.set_cx( recsToWrite );
                         cpu.set_al( 0 );
    
                         pfcb->recNumber += recsToWrite;
                    }
                    else
                         tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random block write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "  ERROR random block write using FCBs doesn't have an open file\n" );
    
            return;
        }
        case 0x29:
        {
            // parse filename
            // in: ds:si -- string to parse
            //     es:di -- buffer pointing to an fcb
            //     al:   -- bit 0: 0 parsing stops if file separator found
            //                     1 leading separators ignored (non-filename chars including arguments like -f in "-f sieve.c"
            //              bit 1: 0 drive number in fcb set to default if no drive in string
            //                     1 drive number in fcb not modified
            //              bit 2: 0 set filename in fcb to blanks if no filename in string
            //                     1 don't modify filename in fcb if no filename in string
            //              bit 3: 0 extension in fcb set to blanks if no extension in string
            //                     1 extension left unchanged if no extension in string
            // out: al:     0: no wildcards in name or extension
            //              1: wildcards appeared
            //           0xff: drive specifier invalid
            //      ds:si:  first byte after parsed string
            //      es:di:  buffer filled with unopened fcb

            char * pfile = (char *) GetMem( cpu.get_ds(), cpu.get_si() );
            if ( 0 != pfile[ 0 ] && ':' == pfile[ 1 ] )
                pfile += 2; // get past optional X:

            char * pfile_original = pfile;
            tracer.Trace( "  parse filename '%s'\n", pfile );
            tracer.TraceBinaryData( (uint8_t *) pfile, 64, 4 );
    
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.get_es(), cpu.get_di() );
            uint8_t input_al = cpu.al();

            if ( 0 == ( input_al & 2 ) )
                pfcb->drive = 0;
            if ( 0 == ( input_al & 4 ) )
                memset( pfcb->name, ' ', _countof( pfcb->name ) );
            if ( 0 == ( input_al & 8 ) )
                memset( pfcb->ext, ' ', _countof( pfcb->ext ) );

            pfcb->curBlock = 0;
            pfcb->recSize = 0;

            tracer.Trace( "  pfile before scan: %p\n", pfile );

            if ( 0 != ( input_al & 1 ) )
            {
                // skip leading separators

                while ( strchr( ":<|>+=;, \t", *pfile ) )
                    pfile++;
            }

            tracer.Trace( "  pfile after scan: %p\n", pfile );
            char * pf = pfile;
            if ( *pfile )
            {
        
                for ( int i = 0; i < _countof( pfcb->name ) && *pf && isFilenameChar( *pf ); i++ )
                    pfcb->name[ i ] = (char) toupper( *pf++ );
                if ( '.' == *pf )
                    pf++;
                for ( int i = 0; i < _countof( pfcb->ext ) && *pf && isFilenameChar( *pf ); i++ )
                    pfcb->ext[ i ] = (char) toupper( *pf++ );
    
                tracer.Trace( "  after copying filename, on char '%c', pf %p\n", *pf, pf );
        
                if ( strchr( pfile, '*' ) || strchr( pfile, '?' ) )
                    cpu.set_al( 1 );
                else
                    cpu.set_al( 0 );
    
                star_to_question( pfcb->name, 8 );
                star_to_question( pfcb->ext, 3 );
            }
    
            cpu.set_si( cpu.get_si() + (uint16_t) ( pf - pfile_original ) );

            pfcb->TraceFirst16();
            tracer.Trace( "  returning al %#x, si %04x\n", cpu.al(), cpu.get_si() );
    
            return;
        }
        case 0x2a:
        {
            // get system date. al is day of week 0-6 0=sunday, cx = year 1980-2099, dh = month 1-12, dl = day 1-31
    
            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_al( (uint8_t) st.wDayOfWeek );
            cpu.set_cx( st.wYear );
            cpu.set_dh( (uint8_t) st.wMonth );
            cpu.set_dl( (uint8_t) st.wDay );
    
            return;
        }           
        case 0x2c:
        {
            // get system time into DX (seconds : hundredths of a second), CX (hours : minutes)
    
            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_ch( (uint8_t) st.wHour );
            cpu.set_cl( (uint8_t) st.wMinute );
            cpu.set_dh( (uint8_t) st.wSecond );
            cpu.set_dl( (uint8_t) ( st.wMilliseconds / 10 ) );
            tracer.Trace( "  system time is %02d:%02d:%02d.%02d\n", cpu.ch(), cpu.cl(), cpu.dh(), cpu.dl() );

            #if false // useful when debugging
                cpu.set_ch( (uint8_t) 1 );
                cpu.set_cl( (uint8_t) 2 );
                cpu.set_dh( (uint8_t) 3 );
                cpu.set_dl( (uint8_t) 69 );
            #endif
    
            return;
        }
        case 0x2f:
        {
            // get disk transfer area address. returns es:bx

            cpu.set_es( g_diskTransferSegment );
            cpu.set_bx( g_diskTransferOffset );

            return;
        }
        case 0x30:
        {
            // get version number
    
            //cpu.set_al( 2 ); 
            //cpu.set_ah( 11 ); 
            cpu.set_al( 3 ); // Many apps require 3.0+
            cpu.set_ah( 3 ); 
    
            tracer.Trace( "  returning DOS version %d.%d\n", cpu.al(), cpu.ah() );
            return;
        }
        case 0x31:
        {
            // terminate and stay resident
            // DX: # of paragraphs to keep resident
            // AL == app return code

            size_t entry = FindAllocationEntry( g_currentPSP );
            if ( -1 == entry )
            {
                cpu.set_carry( true );
                tracer.Trace( "  ERROR: attempt to terminate and stay resident with a bogus PSP\n" );
                return;
            }

            assert( 0 != g_allocEntries.size() ); // loading the app creates 1 shouldn't be freed.
            trace_all_allocations();

            uint16_t new_paragraphs = cpu.get_dx();
            tracer.Trace( "  TSR and keep %#x paragraphs resident\n", new_paragraphs );
            g_allocEntries[ entry ].para_length = new_paragraphs;
            trace_all_allocations();

            DOSPSP * psp = (DOSPSP *) GetMem( g_currentPSP, 0 );
            if ( psp && ( firstAppTerminateAddress != psp->int22TerminateAddress ) )
            {
                g_appTerminationReturnCode = cpu.al();
                tracer.Trace( "  tsr termination return code: %d\n", g_appTerminationReturnCode );
                tracer.Trace( "  tsr's environment block: %04x\n", psp->segEnvironment );
                g_currentPSP = psp->segParent;
                cpu.set_cs( ( psp->int22TerminateAddress >> 16 ) & 0xffff );
                cpu.set_ip( psp->int22TerminateAddress & 0xffff );
                cpu.set_ss( psp->parentSS ); // not DOS standard, but workaround for apps like QCL.exe QuickC v1.0 that doesn't restore the stack
                cpu.set_sp( psp->parentSP ); // ""
                tracer.Trace( "  returning from tsr to return address %04x:%04x\n", cpu.get_cs(), cpu.get_ip() );
            }
            else
            {
                tracer.Trace( "  TSR attempted to TSR with no parent app to return to; exiting ntvdm\n" );
                cpu.end_emulation();
                g_haltExecution = true;
            }

            return;
        }
        case 0x33:
        {
            // get/set ctrl-break status
    
            cpu.set_dl( 0 ); // it's off regardless of what is set
            return;
        }
        case 0x34:
        {
            // get address of DOS critical flag into ES:BX.
            // undocumented DOS. QuickC version 2.51 calls this.
            // Use an address that will remain 0 with 0s on either side

            cpu.set_es( 0x50 );
            cpu.set_bx( 1 );
            return;
        }
        case 0x35:
        {
            // get interrupt vector. 
    
            uint16_t * pvec = (uint16_t *) GetMem( 0, 4 * (uint16_t) cpu.al() );
            cpu.set_bx( pvec[ 0 ] );
            cpu.set_es( pvec[ 1 ] );
            tracer.Trace( "  getting interrupt vector %02x %s which is %04x:%04x\n", cpu.al(), get_interrupt_string( cpu.al(), 0, ah_used ), cpu.get_es(), cpu.get_bx() );
            return;
        }
        case 0x36:
        {
            // get disk space: in: dl code (0 default, 1 = A, 2 = B...
            // output: ax: sectors per cluster, bx = # of available clusters, cx = bytes per sector, dx = total clusters
            // use believable numbers for DOS for lots of disk space free.
    
            cpu.set_ax( 8 );
            cpu.set_bx( 0x6fff );
            cpu.set_cx( 512 );
            cpu.set_dx( 0x7fff );
            return;
        }
        case 0x37:
        {
            // get/set switchchar. Undocumented but legal call in DOS 2.x. This is the character used for switches (arguments).
            // a popular alternative is '-'.
            // al: 0 = get, 1 = set, 2 = device availability (should /dev precede device names?)

            tracer.Trace( "  get/set switchchar. al is %02x\n", cpu.al() );

            if ( 0 == cpu.al() )
                cpu.set_dl( '/' );
            return;
        }
        case 0x38:
        {
            // get/set country dependent information.

            if ( 0 == cpu.al() )
            {
                cpu.set_carry( false );
                cpu.set_bx( 1 ); // USA
                uint8_t * pinfo = GetMem( cpu.get_ds(), cpu.get_dx() );
                memset( pinfo, 0, 0x20 );
                pinfo[ 2 ] = '$';
                pinfo[ 7 ] = ',';
                pinfo[ 9 ] = '.';
                pinfo[ 0xb ] = '/';
                pinfo[ 0xd ] = ':';
                pinfo[ 0x16 ] = ':';
            }
            else
            {
                cpu.set_carry( true );
                cpu.set_ax( 0x0c ); // access code invalid
            }

            return;
        }
        case 0x39:
        {
            // create directory ds:dx asciiz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  create directory '%s'\n", path );

            int ret = _mkdir( path );
            if ( 0 == ret )
                cpu.set_carry( false );
            else
            {
                cpu.set_carry( true );
                cpu.set_ax( 3 ); // path not found
                tracer.Trace( "  ERROR: create directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3a:
        {
            // remove directory ds:dx asciiz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  remove directory '%s'\n", path );

            int ret = _rmdir( path );
            if ( 0 == ret )
                cpu.set_carry( false );
            else
            {
                cpu.set_carry( true );
                cpu.set_ax( 3 ); // path not found
                tracer.Trace( "  ERROR: remove directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3b:
        {
            // change directory ds:dx asciiz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  change directory to '%s'\n", path );

            int ret = _chdir( path );
            if ( 0 == ret )
                cpu.set_carry( false );
            else
            {
                cpu.set_carry( true );
                cpu.set_ax( 3 ); // path not found
                tracer.Trace( "  ERROR: change directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3c:
        {
            // create file. DS:dx pointer to asciiz pathname. al= open mode (dos 2.x ignores). AX=handle
    
            char * path = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  create file '%s'\n", path );
            cpu.set_ax( 3 );
    
            FILE * fp = fopen( path, "w+b" );
            if ( fp )
            {
                FileEntry fe = {0};
                strcpy( fe.path, path );
                fe.fp = fp;
                fe.handle = FindFirstFreeFileHandle();
                fe.writeable = true;
                fe.seg_process = g_currentPSP;
                fe.refcount = 1;
                g_fileEntries.push_back( fe );
                cpu.set_ax( fe.handle );
                cpu.set_carry( false );
                tracer.Trace( "  successfully created file and using new handle %04x\n", cpu.get_ax() );
                trace_all_open_files();
            }
            else
            {
                tracer.Trace( "  ERROR: create file sz failed with error %d = %s\n", errno, strerror( errno ) );
                cpu.set_ax( 2 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x3d:
        {
            // open file. DS:dx pointer to asciiz pathname. al= open mode (dos 2.x ignores). AX=handle
    
            char * path = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.TraceBinaryData( (uint8_t *) path, 0x100, 2 );
            tracer.Trace( "  open file '%s'\n", path );
            uint8_t openmode = cpu.al();
            cpu.set_ax( 2 );

            size_t index = FindFileEntryFromPath( path );
            if ( -1 != index )
            {
                // file is already open. return the same handle and add to refcount.

                cpu.set_ax( g_fileEntries[ index ].handle );
                cpu.set_carry( false );
                g_fileEntries[ index ].refcount++;
                fseek( g_fileEntries[ index ].fp, 0, SEEK_SET ); // rewind it to the start

                tracer.Trace( "  successfully found already open file, using existing handle %04x\n", cpu.get_ax() );
                trace_all_open_files();
            }
            else
            {
                bool readOnly = ( 0 == openmode );

                if ( !_stricmp( path, "CON" ) || !_stricmp( path, "\\DEV\\CON" ) )
                {
                    cpu.set_ax( readOnly ? 0 : 1 );
                    cpu.set_carry( false );
                    tracer.Trace( "  successfully using built-in handle %d for CON\n", cpu.get_ax() );
                    return;
                }

                FILE * fp = fopen( path, readOnly ? "rb" : "r+b" );
                if ( fp )
                {
                    FileEntry fe = {0};
                    strcpy( fe.path, path );
                    fe.fp = fp;
                    fe.handle = FindFirstFreeFileHandle();
                    fe.writeable = !readOnly;
                    fe.seg_process = g_currentPSP;
                    fe.refcount = 1;
                    g_fileEntries.push_back( fe );
                    cpu.set_ax( fe.handle );
                    cpu.set_carry( false );
                    tracer.Trace( "  successfully opened file, using new handle %04x\n", cpu.get_ax() );
                    trace_all_open_files();
                }
                else
                {
                    tracer.Trace( "  ERROR: open file sz failed with error %d = %s\n", errno, strerror( errno ) );
                    cpu.set_ax( 2 );
                    cpu.set_carry( true );
                }
            }
    
            return;
        }
        case 0x3e:
        {
            // close file handle in BX

            trace_all_open_files();
            uint16_t handle = cpu.get_bx();
            if ( handle <= 4 )
            {
                tracer.Trace( "close of built-in handle ignored\n" );
                cpu.set_carry( false );
            }
            else
            {
                size_t index = FindFileEntryIndex( handle );
                if ( -1 != index )
                {
                    assert( g_fileEntries[ index ].refcount > 0 );
                    g_fileEntries[ index ].refcount--;
                    tracer.Trace( "  file close, new refcount %u\n", g_fileEntries[ index ].refcount );
                    if ( 0 == g_fileEntries[ index ].refcount )
                    {
                        FILE * fp = RemoveFileEntry( handle );
                        if ( fp )
                        {
                            tracer.Trace( "  close file handle %04x\n", handle );
                            fclose( fp );
                            cpu.set_carry( false );
                        }
                    }
                }
                else
                {
                    tracer.Trace( "  ERROR: close file handle couldn't find handle %04x\n", handle );
                    cpu.set_ax( 6 );
                    cpu.set_carry( true );
                }
            }
    
            return;
        }
        case 0x3f:
        {
            // read from file using handle. BX=handle, CX=num bytes, DS:DX: buffer
            // on output: AX = # of bytes read or if CF is set 5=access denied, 6=invalid handle.
    
            uint16_t h = cpu.get_bx();
            if ( h <= 4 )
            {
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                if ( 0 == h )
                {
#if 1
                    // This assembler version allows the emulator to send timer and keyboard interrupts

                    invoke_assembler_routine( g_int21_3f_seg );
                    return;
#else
                    // Callers like GWBasic ask for one character at a time but have no idea what a backspace is.
                    // So buffer until a cr, append a lf, and send that one character at a time.
    
                    uint16_t request_len = cpu.get_cx();
                    static char acBuffer[ 128 ] = {0};
    
                    if ( g_use80x25 )
                        UpdateDisplay();
    
                    uint8_t * p = GetMem( cpu.get_ds(), cpu.get_dx() );
                    cpu.set_carry( false );
    
                    while ( 0 == acBuffer[ 0 ] )
                    {
                        size_t len = _countof( acBuffer );
                        // This blocks and prevents timer interrupts from being processed.
                        char * result = ConsoleConfiguration::portable_gets_s( acBuffer, _countof( acBuffer ) );
                        if ( result )
                        {
                            strcat( acBuffer, "\r\n" );
                            break;
                        }
                    }

                    uint16_t string_len = (uint16_t) strlen( acBuffer );
                    uint16_t min_len = __min( string_len, request_len );
                    cpu.set_ax( min_len );
                    uint8_t * pvideo = GetVideoMem();
                    GetCursorPosition( row, col );
                    tracer.Trace( "  min len: %d\n", min_len );
                    for ( uint16_t x = 0; x < min_len; x++ )
                    {
                         p[x] = acBuffer[x];
                         tracer.Trace( "  returning character %02x = '%c'\n", p[x], printable( p[x] ) );

                         if ( g_use80x25 )
                         {
                             uint32_t offset = row * 2 * ScreenColumns + col * 2;
                             pvideo[ offset ] = printable( p[x] );
                             col++;
                             SetCursorPosition( row, col );
                         }
                    }

                    strcpy( acBuffer, acBuffer + min_len );
                    tracer.Trace( "  returning %u characters\n", min_len );

                    // gets_s writes to the display directly. redraw everything 

                    if ( g_use80x25 )
                        ClearLastUpdateBuffer();

                    return;
#endif
                }
                else
                {
                    cpu.set_carry( true );
                    tracer.Trace( "  attempt to read from output handle %04x\n", h );
                }
    
                return;
            }
    
            FILE * fp = FindFileEntry( cpu.get_bx() );
            if ( fp )
            {
                uint16_t len = cpu.get_cx();
                uint8_t * p = GetMem( cpu.get_ds(), cpu.get_dx() );
                tracer.Trace( "  read from file using handle %04x bytes at address %02x:%02x. offset just beyond: %02x\n", len, cpu.get_ds(), cpu.get_dx(), cpu.get_dx() + len );
    
                uint32_t cur = ftell( fp );
                fseek( fp, 0, SEEK_END );
                uint32_t size = ftell( fp );
                fseek( fp, cur, SEEK_SET );
                cpu.set_ax( 0 );
    
                if ( cur < size )
                {
                    uint32_t toRead = __min( len, size - cur );
                    memset( p, 0, toRead );
                    size_t numRead = fread( p, toRead, 1, fp );
                    if ( numRead )
                    {
                        cpu.set_ax( (uint16_t) toRead );
                        tracer.Trace( "  successfully read %04x (%u) bytes\n", toRead, toRead );
                        tracer.TraceBinaryData( p, toRead, 4 );
                    }
                }
                else
                    tracer.Trace( "  ERROR: attempt to read beyond the end of file\n" );
    
                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( "  ERROR: read from file handle couldn't find handle %04x\n", cpu.get_bx() );
                cpu.set_ax( 6 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x40:
        {
            // write to file using handle. BX=handle, CX=num bytes, DS:DX: buffer
            // on output: AX = # of bytes read or if CF is set 5=access denied, 6=invalid handle.
    
            uint16_t h = cpu.get_bx();
            cpu.set_carry( false );
            if ( h <= 4 )
            {
                cpu.set_ax( cpu.get_cx() );
    
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                uint8_t * p = GetMem( cpu.get_ds(), cpu.get_dx() );
    
                if ( 1 == h || 2 == h )
                {
                    if ( g_use80x25 )
                    {
                        uint8_t * pbuf = GetVideoMem();
                        GetCursorPosition( row, col );
                        tracer.Trace( "  starting to write pbuf %p, %u chars at row %u col %u, page %d\n", pbuf, cpu.get_cx(), row, col, GetActiveDisplayPage() );
        
                        for ( uint16_t t = 0; t < cpu.get_cx(); t++ )
                        {
                            uint8_t ch = p[ t ];
                            if ( 0x0a == ch ) // LF
                            {
                                col = 0;
                                SetCursorPosition( row, col );
                                tracer.Trace( "  linefeed, setting column to 0\n" );
                            }
                            else if ( 0x0d == ch ) // CR
                            {
                                if ( row >= ScreenRowsM1 )
                                {
                                    tracer.Trace( "  carriage scrolling up a line\n"  );
                                    scroll_up( pbuf, 1, 0, 0, ScreenRowsM1, ScreenColumnsM1 );
                                }
                                else
                                {
                                    tracer.Trace( "  carriage return, moving to next row\n" );
                                    row++;
                                    SetCursorPosition( row, col );
                                }
                            }
                            else
                            {
                                uint32_t offset = row * 2 * ScreenColumns + col * 2;
                                pbuf[ offset ] = printable( ch );
                                tracer.Trace( "  writing %02x '%c' to display offset %u at row %u col %u, existing attr %02x\n",
                                              ch, printable( ch ), offset, row, col, pbuf[ offset + 1 ] );

                                // apps like qbx set the attributes to 0 when running an app to clear the display, then
                                // set it back afterwards. 0 makes text invisible.

                                if ( 0 == pbuf[ offset + 1 ] )
                                    pbuf[ offset + 1 ] = DefaultVideoAttribute;

                                col++;
                                if ( col > ScreenColumns )
                                    col = 1;
                                SetCursorPosition( row, col );
                            }
    
                            UpdateWindowsCursorPosition();
                        }
                    }
                    else
                    {
                        tracer.Trace( "  writing text to display: '" );
                        for ( uint16_t x = 0; x < cpu.get_cx(); x++ )
                        {
                            if ( 0x0d != p[ x ] && 0x0b != p[ x ] )
                            {
                                printf( "%c", p[ x ] );
                                tracer.Trace( "%c", printable( p[x] ) );
                            }
                        }
                        tracer.Trace( "'\n" );
                    }
                }
                return;
            }
    
            FILE * fp = FindFileEntry( cpu.get_bx() );
            if ( fp )
            {
                uint16_t len = cpu.get_cx();
                uint8_t * p = GetMem( cpu.get_ds(), cpu.get_dx() );
                tracer.Trace( "  write file using handle, %04x bytes at address %p\n", len, p );
    
                cpu.set_ax( 0 );
    
                size_t numWritten = fwrite( p, len, 1, fp );
                if ( numWritten || ( 0 == len ) )
                {
                    cpu.set_ax( len );
                    tracer.Trace( "  successfully wrote %u bytes\n", len );
                    tracer.TraceBinaryData( p, len, 4 );
                }
                else
                    tracer.Trace( "  ERROR: attempt to write to file failed, error %d = %s\n", errno, strerror( errno ) );
    
                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( "  ERROR: write to file handle couldn't find handle %04x\n", cpu.get_bx() );
                cpu.set_ax( 6 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x41:
        {
            // delete file: ds:dx has asciiz name of file to delete.
            // return: cf set on error, ax = error code
    
            char * pfile = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  deleting file '%s'\n", pfile );
            trace_all_open_files();

            // apps like the Microsoft C Compiler V3 make the assumption that you can delete open files

            size_t index = FindFileEntryFromPath( pfile );
            if ( -1 != index )
            {
                uint16_t handle = g_fileEntries[ index ].handle;
                FILE * fp = RemoveFileEntry( handle );
                if ( fp )
                {
                    tracer.Trace( "  closing file handle %04x prior to delete\n", handle );
                    fclose( fp );
                }
            }

            int removeok = ( 0 == remove( pfile ) );
            if ( removeok )
                cpu.set_carry( false );
            else
            {
                tracer.Trace( "  ERROR: can't delete file '%s' error %d = %s\n", pfile, errno, strerror( errno ) );
                cpu.set_carry( true );
                cpu.set_ax( 2 );
            }
    
            return;
        }
        case 0x42:
        {
            // move file pointer (lseek)
            // bx == handle, cx:dx: 32-bit offset, al=mode. 0=beginning, 1=current. 2=end
    
            uint16_t handle = cpu.get_bx();

            if ( handle <= 4 ) // built-in handle
            {
                cpu.set_carry( false );
                return;
            }

            FILE * fp = FindFileEntry( handle );
            if ( fp )
            {
                uint32_t offset = ( ( (uint32_t) cpu.get_cx() ) << 16 ) | cpu.get_dx();
                uint8_t origin = cpu.al();
                if ( origin > 2 )
                {
                    tracer.Trace( "  ERROR: move file pointer file handle has invalid mode/origin %u\n", origin );
                    cpu.set_ax( 1 );
                    cpu.set_carry( true );
                    return;
                }
    
                tracer.Trace( "  move file pointer using handle %04x to %u bytes from %s\n", handle, offset,
                              0 == origin ? "end" : 1 == origin ? "current" : "end" );
    
                uint32_t cur = ftell( fp );
                fseek( fp, 0, SEEK_END );
                uint32_t size = ftell( fp );
                fseek( fp, cur, SEEK_SET );
                tracer.Trace( "  file size is %u\n", size );
    
                if ( 0 == origin )
                    fseek( fp, offset, SEEK_SET );
                else if ( 1 == origin )
                    fseek( fp, offset, SEEK_CUR );
                else 
                    fseek( fp, offset, SEEK_END );
    
                cur = ftell( fp );
                cpu.set_ax( cur & 0xffff );
                cpu.set_dx( ( cur >> 16 ) & 0xffff );
    
                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( "  ERROR: move file pointer file handle couldn't find handle %04x\n", handle );
                cpu.set_ax( 6 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x43:
        {
            // get/put file attributes
            // al: 0 == get file attributes, 1 == put file attributes
            // cx: attributes (bits: 0 ro, 1 hidden, 2 system, 3 volume, 4 subdir, 5 archive)
            // ds:dx: asciiz filename
            // returns: ax = error code if CF set. CX = file attributes on get.
    
            char * pfile = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  get/put file attributes on file '%s'\n", pfile );
            cpu.set_carry( true );
    
            if ( 0 == cpu.al() ) // get
            {
                uint32_t attr = GetFileAttributesA( pfile );
                if ( INVALID_FILE_ATTRIBUTES != attr )
                {
                    cpu.set_carry( false );
                    cpu.set_cx( ( attr & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) ) );
                    tracer.Trace( "  read get file attributes: cx %04x\n", cpu.get_cx() );
                }
                else
                    cpu.set_ax( (uint16_t) GetLastError() ); // most errors map OK (file not found, path not found, etc.)
            }
            else
            {
                tracer.Trace( "  put file attributes on '%s' to %04x\n", pfile, cpu.get_cx() );
                BOOL ok = SetFileAttributesA( pfile, cpu.get_cx() );
                cpu.set_carry( !ok );
            }
    
            tracer.Trace( "  result of get/put file attributes: carry %d\n", cpu.get_carry() );
    
            return;
        }
        case 0x44:
        {
            // i/o control for devices (ioctl)
    
            uint8_t subfunction = cpu.al();
            // handles 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
            uint16_t handle = cpu.get_bx();
            cpu.set_carry( false );
    
            tracer.Trace( "  ioctl subfunction %u, get device information for handle %04x\n", subfunction, handle );

            switch ( subfunction )
            {
                case 0:
                {
                    if ( handle <= 4 )
                    {
                        // get device information
        
                        cpu.set_dx( 0 );
                        cpu.set_carry( false );
    
                        // this distinction is important for gwbasic and qbasic-generated-apps for both input and output to work in both modes.
                        // The 0x80 bit indicates it's a character device (cursor movement, etc.) vs. a file (or a teletype)
    
                        if ( g_use80x25 )
                        {
                            if ( 0 == handle ) // stdin
                                cpu.set_dx( 0x81 );
                            else if ( 1 == handle ) //stdout
                                cpu.set_dx( 0x82 );
                            else if ( handle < 10 ) // stderr, etc.
                                cpu.set_dx( 0x80 );
                        }
                        else
                        {
                            if ( 0 == handle ) // stdin
                                cpu.set_dx( 0x1 );
                            else if ( 1 == handle ) //stdout
                                cpu.set_dx( 0x2 );
                            else if ( handle < 10 ) // stderr, etc.
                                cpu.set_dx( 0x0 );
                        }
                    }
                    else
                    {
                        FILE * fp = FindFileEntry( handle );
                        if ( !fp )
                        {
                            cpu.set_carry( true );
                            tracer.Trace( "    ERROR: ioctl on handle %04x failed because it's not a valid handle\n", handle );
                        }
                        else
                        {
                            cpu.set_carry( false );
                            cpu.set_dx( 0 );
                            int location = ftell( fp );
                            if ( 0 == location )
                                cpu.set_dx( cpu.get_dx() | 0x40 ); // file has not been written to
                        }
                    }
    
                    break;
                }
                case 1:
                {
                    // set device information (ignore)
    
                    break;
                }
                case 8:
                {
                    // check if block device is removable.
                    // input: bl = drive number. 0 == default, 1 == a, ...
                    // cf clear on success or set on failure
                    // ax = 0 removable, 1 fixed
                    // ax = error on error

                    tracer.Trace( "  check if drive (1=a...) %d is removable (it's not)\n", cpu.bl() );

                    cpu.set_carry( false );
                    cpu.set_ax( 1 );
                    break;
                }
                default:
                {
                    tracer.Trace( "unhandled IOCTL subfunction %#x\n", cpu.al() );
                    break;
                }
            }
    
            return;
        }
        case 0x45:
        {
            // create duplicate handle (dup)

            cpu.set_carry( true );
            uint16_t existing_handle = cpu.get_bx();
            size_t index = FindFileEntryIndex( existing_handle );

            if ( -1 != index )
            {
                FileEntry & entry = g_fileEntries[ index ];

                FILE * fp = fopen( entry.path, entry.writeable ? "r+b" : "rb" );
                if ( fp )
                {
                    FileEntry fe = {0};
                    strcpy( fe.path, entry.path );
                    fe.fp = fp;
                    fe.handle = FindFirstFreeFileHandle();
                    fe.writeable = entry.writeable;
                    fe.seg_process = g_currentPSP;
                    fe.refcount = 1;
                    g_fileEntries.push_back( fe );
                    cpu.set_ax( fe.handle );
                    cpu.set_carry( false );
                    tracer.Trace( "  successfully created duplicate handle of %04x as %04x\n", existing_handle, cpu.get_ax() );
                    trace_all_open_files();
                }
                else
                {
                    cpu.set_ax( 2 ); // file not found
                    tracer.Trace( "  ERROR: attempt to duplicate file handle failed opening file %s error %d: %s\n", entry.path, errno, strerror( errno ) );
                }
            }
            else
            {
                cpu.set_ax( 2 ); // file not found
                tracer.Trace( "  ERROR: attempt to duplicate non-existent handle %04x\n", existing_handle );
            }

            return;
        }
        case 0x46:
        {
            // force duplicate handle. BX: existing handle, CX: new handle
            // on return, CF set on error with error code in AX

            if ( cpu.get_bx() <= 4 && cpu.get_cx() <= 4 )
            {
                // probably mapping stderr to stdout, etc. Ignore

                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( "    ERROR: force duplicate for non-built-in handle is unhandled\n" );
                cpu.set_carry( true );
                cpu.set_ax( 2 );
            }

            return;
        }
        case 0x47:
        {
            // get current directory. BX = drive number, DS:SI = pointer to a 64-byte null-terminated buffer.
            // CF set on error. AX=15 for invalid drive
    
            cpu.set_carry( true );
            if ( GetCurrentDirectoryA( sizeof( cwd ), cwd ) )
            {
                char * paststart = cwd + 3; // result does not contain drive or initial backslash 'x:\'
                if ( strlen( paststart ) <= 63 )
                {
                    strcpy( (char *) GetMem( cpu.get_ds(), cpu.get_si() ), paststart );
                    tracer.Trace( "  returning current directory '%s'\n", paststart );
                    cpu.set_carry( false );
                }
            }
            else
                tracer.Trace( "  ERROR: unable to get the current working directory, error %d\n", GetLastError() );
    
            return;
        }
        case 0x48:
        {
            // allocate memory. bx # of paragraphs requested
            // on return, ax = segment of block or error code if cf set
            // bx = size of largest block available if cf set and ax = not enough memory
            // cf clear on success

            tracer.Trace( "  allocate memory %04x paragraphs\n", cpu.get_bx() );

            uint16_t largest_block = 0;
            uint16_t alloc_seg = AllocateMemory( cpu.get_bx(), largest_block );
            cpu.set_bx( largest_block );

            if ( 0 != alloc_seg )
            {
                cpu.set_carry( false );
                cpu.set_ax( alloc_seg );
            }
            else
            {
                cpu.set_carry( true );
                cpu.set_ax( 8 ); // insufficient memory
            }

            return;
        }
        case 0x49:
        {
            // free memory. es = segment to free
            // on return, ax = error if cf is set
            // cf clear on success

            tracer.Trace( "  free memory segment %04x\n", cpu.get_es() );
            trace_all_allocations();

            // treat free(0) as a success

            if ( 0 == cpu.get_es() )
            {
                cpu.set_carry( false );
                return;
            }

            bool ok = FreeMemory( cpu.get_es() );
            trace_all_allocations();
            cpu.set_carry( !ok );
            if ( !ok )
                cpu.set_ax( 07 ); // memory corruption

            return;
        }
        case 0x4a:
        {
            // modify memory allocation. VGA and other hardware start at a0000
            // lots of opportunity for improvement here.

            size_t entry = FindAllocationEntry( cpu.get_es() );
            if ( -1 == entry )
            {
                cpu.set_carry( true );
                cpu.set_bx( 0 );
                tracer.Trace( "  ERROR: attempt to modify an allocation that doesn't exist\n" );
                return;
            }

            size_t cEntries = g_allocEntries.size();
            assert( 0 != cEntries ); // loading the app creates 1 shouldn't be freed.
            assert( 0 != cpu.get_bx() ); // not legal to allocate 0 bytes
            trace_all_allocations();

            uint16_t maxParas;
            if ( entry == ( cEntries - 1 ) )
                maxParas = SegmentHardware - g_allocEntries[ entry ].segment;
            else
            {
                maxParas = g_allocEntries[ entry + 1 ].segment - g_allocEntries[ entry ].segment;
                if ( 0 != maxParas )
                    maxParas--;        // reserve space for the MCB
            }

            tracer.Trace( "  maximum reallocation paragraphs: %04x, requested size %04x\n", maxParas, cpu.get_bx() );

            if ( cpu.get_bx() > maxParas )
            {
                cpu.set_carry( true );
                cpu.set_ax( 8 ); // insufficient memory
                tracer.Trace( "  insufficient RAM for allocation request of %04x, telling caller %04x is available\n", cpu.get_bx(), maxParas );
                cpu.set_bx( maxParas );
            }
            else
            {
                cpu.set_carry( false );
                tracer.Trace( "  allocation length changed from %04x to %04x\n", g_allocEntries[ entry ].para_length - 1, cpu.get_bx() );
                g_allocEntries[ entry ].para_length = 1 + cpu.get_bx(); // para_length includes the MCB
                update_mcb_length( cpu.get_es() - 1, cpu.get_bx() );
                reset_mcb_tags();
                trace_all_allocations();
            }

            return;
        }
        case 0x4b:
        {
            // load or execute program
            // input: al: 0 = program, 1 = (undocumented) load and set cs:ip + ss:sp, 3 = overlay/load, 4 = msc spawn p_nowait
            //        ds:dx: ascii pathname
            //        es:bx: parameter block (except for mode 3)
            //            0-1: segment to environment block
            //            2-3: offset of command tail
            //            4-5: segment of command tail
            //            6-7: offset of first fcb to be copied to psp + 5c
            //            8-9: segment of first fcb
            //            a-b: offset of second fcb to be copied to new psp+6c
            //            c-d: segment of second fcb
            // output:    fCarry: clear if success, all registers destroyed
            //            ax:     if fCarry:
            //                1 = invalid function
            //                2 = file not found
            //                5 = access denied
            //                8 = insufficient memory
            //                a = environment invalid
            //                b = format invalid
            //            if al == 1: parameter block (struct AppExecute) updated: sp, ss, ip, cs
            //            if al == 1: AX pushed on the child's stack
            //                
            // notes:     all handles, devices, and i/o redirection are inherited
            // crawl up the stack to the get cs:ip one frame above. that's where we'll return once the child process is complete.

            uint8_t mode = cpu.al();

            if ( 0 != mode && 1 != mode && 3 != mode ) // only a subset of modes are implemented
            {
                tracer.Trace( "  CreateProcess load or execute with al mode %02x is unhandled\n", mode );
                cpu.set_carry( true );
                cpu.set_ax( 1 );
                return;
            }

            uint16_t save_ip = cpu.get_ip();
            uint16_t save_cs = cpu.get_cs();
            uint16_t save_sp = cpu.get_sp();
            uint16_t save_ss = cpu.get_ss();

            const char * pathToExecute = (const char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  CreateProcess mode %u path to execute: '%s'\n", mode, pathToExecute );

            char acCommandPath[ MAX_PATH ];
            strcpy( acCommandPath, pathToExecute );
            if ( '@' == acCommandPath[ 0 ] )
            {
                // DOS v2 command.com uses an '@' in this case. Not sure why.

                GetCurrentDirectoryA( sizeof( cwd ), cwd );
                acCommandPath[ 0 ] = cwd[ 0 ];
            }

            if ( 3 == mode )
            {
                AppExecuteMode3 * pae = (AppExecuteMode3 *) GetMem( cpu.get_es(), cpu.get_bx() );
                pae->Trace();

                uint16_t result = LoadOverlay( acCommandPath, pae->segLoadAddress, pae->segmentRelocationFactor );
                cpu.set_ax( result );
                cpu.set_carry( false );
                tracer.Trace( "  result of LoadOverlay: %#x\n", result );
                return;
            }

            AppExecute * pae = (AppExecute *) GetMem( cpu.get_es(), cpu.get_bx() );
            pae->Trace();
            const char * commandTail = (const char *) GetMem( pae->segCommandTail, pae->offsetCommandTail );
            DOSFCB * pfirstFCB = (DOSFCB *) GetMem( pae->segFirstFCB, pae->offsetFirstFCB );
            DOSFCB * psecondFCB = (DOSFCB *) GetMem( pae->segSecondFCB, pae->offsetSecondFCB );

            tracer.Trace( "  command tail: len %u, '%.*s'\n", *commandTail, *commandTail, commandTail + 1 );
            tracer.Trace( "  first and second fcbs: \n" );
            pfirstFCB->TraceFirst16();
            psecondFCB->TraceFirst16();

            tracer.Trace( "  list of open files:\n" );
            TraceOpenFiles();
            char acTail[ 128 ] = {0};
            memcpy( acTail, commandTail + 1, *commandTail );

            uint16_t segChildEnv = AllocateEnvironment( pae->segEnvironment, acCommandPath, 0 );
            if ( 0 != segChildEnv )
            {
                uint16_t seg_psp = LoadBinary( acCommandPath, acTail, segChildEnv, ( 0 == mode ),
                                               & pae->func1SS, & pae->func1SP, & pae->func1CS, & pae->func1IP );
                if ( 0 != seg_psp )
                {
                    if ( 1 == mode )
                    {
                       // put 0xffff on the top of the child's stack.

                       pae->func1SP -= 2;
                       uint16_t * pAX = (uint16_t *) GetMem( pae->func1SS, pae->func1SP );
                       *pAX = 0xffff;
                    }

                    strcpy( g_lastLoadedApp, acCommandPath );
                    DOSPSP * psp = (DOSPSP *) GetMem( seg_psp, 0 );
                    psp->segParent = g_currentPSP;
                    psp->parentSS = save_ss; // as a courtesy to sloppy apps that don't restore their stack and use the child process stack
                    psp->parentSP = save_sp;
                    g_currentPSP = seg_psp;
                    memcpy( psp->firstFCB, pfirstFCB, 16 );
                    memcpy( psp->secondFCB, psecondFCB, 16 );
                    psp->int22TerminateAddress = ( ( (uint32_t) save_cs ) << 16 ) | (uint32_t ) save_ip;
                    tracer.Trace( "  set terminate address to %04x:%04x\n", save_cs, save_ip );
                    tracer.Trace( "  new child psp %p:\n", psp );
                    psp->Trace();
                    cpu.set_carry( false );
                }
                else
                {
                    FreeMemory( segChildEnv );
                    cpu.set_ax( 1 );
                    cpu.set_carry( true );
                }
            }
            else
            {
                cpu.set_ax( 1 );
                cpu.set_carry( true );
            }
            return;
        }
        case 0x4c:
        {
            // exit app

            HandleAppExit();

            return;
        }
        case 0x4d:
        {
            // get exit code of subprogram

            cpu.set_al( g_appTerminationReturnCode );
            cpu.set_ah( 0 );
            tracer.Trace( "  exit code of subprogram: %d\n", g_appTerminationReturnCode );

            return;
        }
        case 0x4e:
        {
            // find first asciiz
            // in: cx = attribute used during search: 7..0 unused, unused, archive, directory, volume, system, hidden, read-only
            //     ds:dx pointer to null-terminated ascii string including wildcards
            // out: CF: true on error, false on success
            //      ax: error code if CF is true.
            //      disk transfer address: DosFindFile
    
            cpu.set_carry( true );
            DosFindFile * pff = (DosFindFile *) GetDiskTransferAddress();
            char * psearch_string = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            pff->search_attributes = cpu.get_cx() & 0x1e; // only directory, system, volume, and hidden are honored
            assert( 0x15 == offsetof( DosFindFile, file_attributes ) );
            tracer.Trace( "  Find First Asciiz for pattern '%s' attributes %#x\n", psearch_string, pff->search_attributes );

            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                FindClose( g_hFindFirst );
                g_hFindFirst = INVALID_HANDLE_VALUE;
            }
    
            WIN32_FIND_DATAA fd = {0};
            g_hFindFirst = FindFirstFileA( psearch_string, &fd );
            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                do
                {
                    bool ok = ProcessFoundFile( pff, fd );
                    if ( ok )
                    {
                        cpu.set_carry( false );
                        break;
                    }

                    BOOL found = FindNextFileA( g_hFindFirst, &fd );
                    if ( !found )
                    {
                        memset( pff, 0, sizeof( DosFindFile ) );
                        cpu.set_ax( 0x12 ); // no more files
                        tracer.Trace( "  WARNING: find first file found no matching files, error %d\n", GetLastError() );
                        FindClose( g_hFindFirst );
                        g_hFindFirst = INVALID_HANDLE_VALUE;
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( (uint16_t) GetLastError() ); // interesting errors actually match
                tracer.Trace( "  WARNING: find first file failed, error %d\n", GetLastError() );
            }
    
            return;
        }
        case 0x4f:
        {
            // find next asciiz. ds:dx should be unchanged from find first, but they are not used below.
    
            cpu.set_carry( true );
            DosFindFile * pff = (DosFindFile *) GetDiskTransferAddress();
            tracer.Trace( "  Find Next Asciiz\n" );
    
            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                do
                {
                    WIN32_FIND_DATAA fd = {0};
                    BOOL found = FindNextFileA( g_hFindFirst, &fd );
                    if ( found )
                    {
                        bool ok = ProcessFoundFile( pff, fd );
                        if ( ok )
                        {
                            cpu.set_carry( false );
                            break;
                        }
                    }
                    else
                    {
                        memset( pff, 0, sizeof( DosFindFile ) );
                        cpu.set_ax( 0x12 ); // no more files
                        tracer.Trace( "  WARNING: find next file found no more, error %d\n", GetLastError() );
                        FindClose( g_hFindFirst );
                        g_hFindFirst = INVALID_HANDLE_VALUE;
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( 0x12 ); // no more files
                tracer.Trace( "  ERROR: search for next without a prior successful search for first\n" );
            }
    
            return;
        }
        case 0x50:
        {
            // set current process id PSP. set the psp to BX

            tracer.Trace( "  old psp %#x, new psp %#x\n", g_currentPSP, cpu.get_bx() );

            // check if it looks valid before setting...

            DOSPSP * psp = (DOSPSP *) GetMem( cpu.get_bx(), 0 );
            if ( 0x20cd == psp->int20Code )
                g_currentPSP = cpu.get_bx();
            else
                tracer.Trace( "  error: psp looked invalid; not setting\n" );

            return;
        }
        case 0x51:
        {
            // get current process id PSP. set BX to the psp
            // Same as 0x62.
            // undocumented prior to DOS 5.0.

            tracer.Trace( "  current psp %#x\n", g_currentPSP );
            cpu.set_bx( g_currentPSP );
            return;
        }
        case 0x52:
        {
            // get list of lists. internal call. debug.com uses this but does nothing with the return values.
            // return: es:bx points to DOS list of lists
            // This arcane set of values+pointers are not emulated. return pointers to 0

            cpu.set_es( SegmentListOfLists );
            cpu.set_bx( OffsetListOfLists );
            return;
        }
        case 0x55:
        {
            // create new psp. internal/undocumented. called by Turbo Pascal 5.5
            // DX: new PSP segment address provided by the caller
            // SI: for DOS 3.0+, value to put in psp->topOfMemory

            DOSPSP * psp = (DOSPSP *) GetMem( cpu.get_dx(), 0 );
            memset( psp, 0, sizeof( DOSPSP ) );
            psp->segParent = g_currentPSP;
            psp->int20Code = 0x20cd;                  // int 20 instruction to terminate app like CP/M
            psp->topOfMemory = cpu.get_si();
            psp->comAvailable = 0xffff;               // .com programs bytes available in segment
            psp->int22TerminateAddress = firstAppTerminateAddress;

            g_currentPSP = cpu.get_dx();

            return;
        }
        case 0x56:
        {
            // rename file: ds:dx old name, es:di new name
            // CF set on error, AX with error code
    
            char * poldname = (char *) GetMem( cpu.get_ds(), cpu.get_dx() );
            char * pnewname = (char *) GetMem( cpu.get_es(), cpu.get_di() );
    
            tracer.Trace( "renaming file '%s' to '%s'\n", poldname, pnewname );
            int renameok = ( 0 == rename( poldname, pnewname ) );
            if ( renameok )
                cpu.set_carry( false );
            else
            {
                tracer.Trace( "  ERROR: can't rename file '%s' as '%s' error %d = %s\n", poldname, pnewname, errno, strerror( errno ) );
                cpu.set_carry( true );
                cpu.set_ax( 2 );
            }
    
            return;
        }
        case 0x57:
        {
            // get/set file date and time using handle
            // input:  al: 0: get, 1 set
            //         bx: handle
            //         cx: time to set if setting
            //         dx: date to set if setting
            // output: es:di pointer to buffer containing results
            //         CF set on error
            //         ax: error code if CF set
            //         cx: file time if getting
            //         dx: file date if getting
            //
    
            cpu.set_carry( true );
            uint16_t handle = cpu.get_bx();
            const char * path = FindFileEntryPath( handle );
            if ( path )
            {
                if ( 0 == cpu.al() )
                {
                    WIN32_FILE_ATTRIBUTE_DATA fad = {0};
                    if ( GetFileAttributesExA( path, GetFileExInfoStandard, &fad ) )
                    {
                        cpu.set_ax( 0 );
                        cpu.set_carry( false );
                        uint16_t dos_time, dos_date;
                        FileTimeToDos( fad.ftLastWriteTime, dos_time, dos_date );
                        cpu.set_cx( dos_time );
                        cpu.set_dx( dos_date );
                    }
                    else
                    {
                        tracer.Trace( "  ERROR: can't get/set file date and time; getfileattributesex failed %d\n", GetLastError() );
                        cpu.set_ax( 1 );
                    }
                }
                else if ( 1 == cpu.al() )
                {
                    // set is not implemented; pretend it worked just fine. Works 2.0 install needs this

                    cpu.set_carry( false );
                }
                else
                {
                    tracer.Trace( "  ERROR: can't get/set file date and time; command in al not valid: %d\n", cpu.al() );
                    cpu.set_ax( 1 );
                }
            }
            else
            {
                tracer.Trace( "  ERROR: can't get/set file date and time; file handle %04x not valid\n", handle );
                cpu.set_ax( 6 );
            }
    
            return;
        }
        case 0x58:
        {
            // get/set memory allocation strategy
            // al = 0 for get, 1 for set
            // bl = strategy in/out 0 == first fit, 1 = best fit, 2 = last fit (from top of memory down)
            // cf set on failure, clear on success

            if ( 0 == cpu.al() )
            {
                cpu.set_bl( 0 );
                cpu.set_carry( false );
            }
            else if ( 1 == cpu.al() )
            {
                tracer.Trace( " set memory allocation strategy to %u\n", cpu.bl() );
                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( " ERROR: memory allocation has unrecognized al: %u\n", cpu.al() );
                cpu.set_carry( true );
            }

            return;
        }
        case 0x59:
        {
            // get extended error code. stub for now until some app really needs it

            cpu.set_ax( 2 ); // last error. file not found
            cpu.set_bh( 1 ); // class. out of resources
            cpu.set_bl( 5 ); // suggestion action code. immediate abort.
            cpu.set_ch( 1 ); // suggestion action code. unknown
            return;
        }
        case 0x5f:
        {
            // network
            // input: al: 02 -- get redirection list entry

            cpu.set_carry( true ); // not supported
            cpu.set_ax( 0x32 ); // network request not supported
            return;
        }
        case 0x62:
        {
            // Get PSP Address (DOS 3.x)

            cpu.set_bx( g_currentPSP );
            return;
        }
        case 0x63:
        {
            // get lead byte table

            cpu.set_carry( true ); // not supported;
            return;
        }
        case 0x68:
        {
            // FFlush -- commit file
            // BX: file handle
            // on return, Carry Flag cleared on success and set on failure with error code in AX
            // Just flush all files and return success

            fflush( 0 );
            cpu.set_carry( false );
            return;
        }
        default:
        {
            tracer.Trace( "unhandled int21 command %02x\n", c );
        }
    }
} //handle_int_21

uint8_t toBCD( uint8_t x )
{
    if ( x <= 9 )
        return x;

    return ( ( x / 10 ) << 4 ) | ( x % 10 );
} //toBCD

void TrackInterruptsCalled( uint8_t interrupt_num, uint8_t ah, bool ah_used )
{
    bool found = false;
    size_t cEntries = g_InterruptsCalled.size();
    for ( size_t i = 0; i < cEntries; i++ )
    {
        IntCalled & ic = g_InterruptsCalled[ i ];
        if ( ( interrupt_num == ic.i ) && ( ( !ah_used ) || ( ah == ic.c ) ) )
        {
            ic.calls++;
            found = true;
            break;
        }
    }

    if ( !found )
    {
        IntCalled ic;
        ic.i = interrupt_num;
        ic.c = ah_used ? ah : 0xffff;
        ic.calls = 1;
        g_InterruptsCalled.push_back( ic );
    }
} //TrackInterruptsCalled

void i8086_invoke_interrupt( uint8_t interrupt_num )
{
    unsigned char c = cpu.ah();
    bool ah_used = false;
    const char * pintstr = get_interrupt_string( interrupt_num, c, ah_used );
    tracer.Trace( "int %02x ah %02x al %02x bx %04x cx %04x dx %04x di %04x si %04x ds %04x cs %04x ss %04x es %04x bp %04x sp %04x %s\n",
                  interrupt_num, cpu.ah(), cpu.al(),
                  cpu.get_bx(), cpu.get_cx(), cpu.get_dx(), cpu.get_di(), cpu.get_si(),
                  cpu.get_ds(), cpu.get_cs(), cpu.get_ss(), cpu.get_es(), cpu.get_bp(), cpu.get_sp(), pintstr );

    TrackInterruptsCalled( interrupt_num, c, ah_used );

    // restore interrupts since we won't exit with an iret because Carry in flags must be preserved as a return code

    cpu.set_interrupt( true );

    // try to figure out if an app is looping looking for keyboard input

    if ( ! ( ( 0x28 == interrupt_num ) ||
             ( ( 0x16 == interrupt_num ) && ( 1 == c || 2 == c ) ) ) )
        g_int16_1_loop = false;

    if ( 0 == interrupt_num )
    {
        tracer.Trace( "    divide by zero interrupt 0\n" );
        return;
    }
    else if ( 4 == interrupt_num )
    {
        tracer.Trace( "    overflow exception interrupt 4\n" );
        return;
    }
    else if ( 0x09 == interrupt_num )
    {
        consume_keyboard();
        return;
    }
    else if ( 0x10 == interrupt_num )
    {
        handle_int_10( c );
        return;
    }
    else if ( 0x11 == interrupt_num )
    {
        // bios equipment determination
        cpu.set_ax( 0x002c );
        return;
    }
    else if ( 0x12 == interrupt_num )
    {
        // .com apps like Turbo Pascal instead read from the Program Segment Prefix

        cpu.set_ax( 0x280 ); // 640K conventional RAM
        return;
    }
    else if ( 0x16 == interrupt_num )
    {
        handle_int_16( c );
        return;
    }
    else if ( 0x17 == interrupt_num )
    {
        if ( 2 == c )           // get printer status
            cpu.set_ah( 0 );
        return;
    }
    else if ( 0x1a == interrupt_num )
    {
        if ( 0 == c )
        {
            // read real time clock. get ticks since system boot. 18.2 ticks per second.

            ULONGLONG milliseconds = GetTickCount64();
            milliseconds *= 1821;
            milliseconds /= 100000;
            cpu.set_al( 0 );

            #if false // useful for creating logs that can be compared to fix bugs
                static ULONGLONG fakems = 0;
                fakems += 10;
                milliseconds = fakems;
            #endif

            cpu.set_ch( ( milliseconds >> 24 ) & 0xff );
            cpu.set_cl( ( milliseconds >> 16 ) & 0xff );
            cpu.set_dh( ( milliseconds >> 8 ) & 0xff );
            cpu.set_dl( milliseconds & 0xff );
            return;
        }
        else if ( 2 == c )
        {
            // get real-time clock time (at, xt286, ps)
            // returns: cf: clear
            //          ch: hour (bcd)
            //          cl: minutes (bcd)
            //          dh: seconds (bcd)
            //          dl: dayligh savings 0 == standard, 1 == daylight

            cpu.set_carry( false );

            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_ch( toBCD( (uint8_t) st.wHour ) );
            cpu.set_cl( toBCD( (uint8_t) st.wMinute ) );
            cpu.set_dh( toBCD( (uint8_t) st.wSecond ) );
            cpu.set_dl( 0 );
            return;
        }
    }
    else if ( 0x20 == interrupt_num ) // compatibility with CP/M apps for COM executables that jump to address 0 in its data segment
    {
        cpu.set_al( 0 ); // this cp/m exit mode had not return code and the default is 0
        HandleAppExit();
        return;
    }
    else if ( 0x21 == interrupt_num )
    {
        handle_int_21( c );
        return;
    }
    else if ( 0x22 == interrupt_num ) // terminate address
    {
        HandleAppExit();
        return;
    }
    else if ( 0x23 == interrupt_num ) // control "C" exit address
    {
        DOSPSP * psp = (DOSPSP *) GetMem( g_currentPSP, 0 );
        if ( psp && ( firstAppTerminateAddress != psp->int22TerminateAddress ) )
            HandleAppExit();

        return;
    }
    else if ( 0x24 == interrupt_num ) // fatal error handler
    {
        printf( "Abort, Retry, Ignore?\n" );
        i8086_hard_exit( "Abort, Retry, Ignore?\n", 0 );
    }
    else if ( 0x28 == interrupt_num )
    {
        // dos idle loop / scheduler
        // Some apps like Turbo C v1 and v2 call this repeatedly while busy compiling.
        // Other apps like Turbo Pascal 5.5 call this in a tight loop while idling.
        // So it's not obvious whether to Sleep here to reduce host system CPU usage.
        // For TC, only sleep if it's not obvious that it's building something.

        if ( ends_with( g_acApp, "TC.EXE" ) && tc_build_file_open() )
            g_int16_1_loop = false;
        else
            SleepAndScheduleInterruptCheck();

        return;
    }
    else if ( 0x2a == interrupt_num )
    {
        // dos network / netbios
        cpu.set_ah( 0 ); // no network installed (for function ah==00 )
        return;
    }
    else if ( 0x2f == interrupt_num )
    {
        // dos multiplex interrupt; get installed state of xml driver and other items.
        // AL = 0 to indicate nothing is installed for the many AX values that invoke this.

        if ( 0x1680 == cpu.get_ax() ) // program idle release timeslice
        {
            if ( g_use80x25 )
                UpdateDisplay();
            SleepAndScheduleInterruptCheck();
        }

        cpu.set_al( 0x01 ); // not installed, do NOT install
        return;
    }
    else if ( 0x33 == interrupt_num )
    {
        // mouse

        cpu.set_ax( 0 ); // hardware / driver not installed
        return;
    }

    tracer.Trace( "UNHANDLED pc interrupt: %02u == %#x, ah: %02u == %#x, al: %02u == %#x\n",
                  interrupt_num, interrupt_num, cpu.ah(), cpu.ah(), cpu.al(), cpu.al() );
} //i8086_invoke_interrupt

void InitializePSP( uint16_t segment, const char * acAppArgs, uint16_t segEnvironment )
{
    DOSPSP *psp = (DOSPSP *) GetMem( segment, 0 );
    memset( psp, 0, sizeof( DOSPSP ) );

    psp->int20Code = 0x20cd;                  // int 20 instruction to terminate app like CP/M
    psp->topOfMemory = SegmentHardware - 1;   // top of memorysegment in paragraph form
    psp->comAvailable = 0xffff;               // .com programs bytes available in segment
    psp->int22TerminateAddress = firstAppTerminateAddress;
    uint8_t len = (uint8_t) strlen( acAppArgs );
    psp->countCommandTail = len;
    strcpy( (char *) psp->commandTail, acAppArgs );
    psp->commandTail[ len ] = 0x0d;       // DOS has 0x0d at the end of the command tail, not a null
    psp->segEnvironment = segEnvironment;

    psp->Trace();
} //InitializePSP

bool IsBinaryCOM( const char * app )
{
    bool isCOM = ends_with( app, ".com" );

    if ( isCOM )
    {
        // look for signature indicating it's actually a .exe file named .com. Later versions of DOS really do this.

        tracer.Trace( "  checking if '%s' is actually a .exe\n", app );
        FILE * fp = fopen( app, "rb" );
        if ( !fp )
            return true;

        char ac[2];
        bool ok = ( 0 != fread( ac, _countof( ac ), 1, fp ) );
        fclose( fp );

        if ( !ok )
            return true;

        isCOM = ! ( 'M' == ac[0] && 'Z' == ac[1] );
    }

    return isCOM;
} //IsBinaryCOM

uint16_t LoadOverlay( const char * app, uint16_t segCode, uint16_t segRelocationFactor )
{
    // Used by int21, 4b mode 3: Load Overlay and don't execute.
    // returns AX return code: 1 on failure, 0 on success

    bool isCOM = IsBinaryCOM( app );

    if ( isCOM )
    {
        FILE * fp = fopen( app, "rb" );
        if ( 0 == fp )
        {
            tracer.Trace( "open .com file, error %d\n", errno );
            return 1;
        }
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        size_t blocks_read = fread( GetMem( segCode, 0 ), file_size, 1, fp );
        fclose( fp );

        if ( 1 != blocks_read )
        {
            tracer.Trace( "can't read .com file into RAM, error %d\n", errno );
            return 1;
        }
    }
    else // EXE
    {
        FILE * fp = fopen( app, "rb" );
        if ( 0 == fp )
        {
            tracer.Trace( "  can't open input executable '%s', error %d\n", app, errno );
            return 1;
        }
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        vector<uint8_t> theexe( file_size );
        size_t blocks_read = fread( theexe.data(), file_size, 1, fp );
        fclose( fp );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input exe file, error %d", errno );
            return 1;
        }

        ExeHeader & head = * (ExeHeader *) theexe.data();
        if ( 0x5a4d != head.signature )
        {
            tracer.Trace( "  exe isn't MZ" );
            return 1;
        }

        tracer.Trace( "  loading app %s\n", app );
        tracer.Trace( "  looks like an MZ exe... size %u, size from blocks %u, bytes in last block %u\n",
                      file_size, ( (uint32_t) head.blocks_in_file ) * 512, head.bytes_in_last_block );
        tracer.Trace( "  relocation entry count %u, header paragraphs %u (%u bytes)\n",
                      head.num_relocs, head.header_paragraphs, head.header_paragraphs * 16 );
        tracer.Trace( "  relative value of stack segment: %#x, initial sp: %#x, initial ip %#x, initial cs relative to segment: %#x\n",
                      head.ss, head.sp, head.ip, head.cs );
        tracer.Trace( "  relocation table offset %u, overlay number %u\n",
                      head.reloc_table_offset, head.overlay_number );

        if ( head.reloc_table_offset > 100 )
        {
            tracer.Trace( "  probably not a 16-bit exe; head.reloc_table_offset: %d", head.reloc_table_offset );
            return 1;
        }

        uint32_t codeStart = 16 * (uint32_t) head.header_paragraphs;
        uint32_t cbUsed = head.blocks_in_file * 512;
        if ( 0 != head.bytes_in_last_block )
            cbUsed -= ( 512 - head.bytes_in_last_block );
        cbUsed -= codeStart; // don't include the header
        tracer.Trace( "  bytes used by load module: %u, and code starts at %u\n", cbUsed, codeStart );

        uint8_t * pcode = GetMem( segCode, 0 );
        memcpy( pcode, theexe.data() + codeStart, cbUsed );
        tracer.Trace( "  start of the code:\n" );
        tracer.TraceBinaryData( pcode, 0x200, 4 );

        // apply relocation entries

        ExeRelocation * pRelocationEntries = (ExeRelocation *) ( theexe.data() + head.reloc_table_offset );
        for ( uint16_t r = 0; r < head.num_relocs; r++ )
        {
            uint32_t offset = pRelocationEntries[ r ].offset + pRelocationEntries[ r ].segment * 16;
            uint16_t * target = (uint16_t *) ( pcode + offset );
            //tracer.TraceQuiet( "  relocation %u offset %u, update %#02x to %#02x\n", r, offset, *target, *target + segRelocationFactor );
            *target += segRelocationFactor;
        }
    }

    return 0;
} //LoadOverlay

uint16_t LoadBinary( const char * acApp, const char * acAppArgs, uint16_t segEnvironment, bool setupRegs,
                     uint16_t * reg_ss, uint16_t * reg_sp, uint16_t * reg_cs, uint16_t * reg_ip )
{
    uint16_t psp = 0;
    bool isCOM = IsBinaryCOM( acApp );

    if ( isCOM )
    {
        // allocate 64k for the .COM file

        uint16_t paragraphs_free = 0;
        uint16_t ComSegment = AllocateMemory( 0x1000, paragraphs_free );
        psp = ComSegment;
        InitializePSP( ComSegment, acAppArgs, segEnvironment );

        tracer.Trace( "  loading com, ComSegment is %04x\n", ComSegment );

        if ( 0 == ComSegment )
        {
            tracer.Trace( "  insufficient ram available RAM to load .com, in paragraphs: %04x required, %04x available\n", 0x1000, paragraphs_free );
            return 0;
        }

        FILE * fp = fopen( acApp, "rb" );
        if ( 0 == fp )
        {
            tracer.Trace( "open .com file, error %d\n", errno );
            FreeMemory( ComSegment );
            return 0;
        }
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        size_t blocks_read = fread( GetMem( ComSegment, 0x100 ), file_size, 1, fp );
        fclose( fp );

        if ( 1 != blocks_read )
        {
            tracer.Trace( "can't read .com file into RAM, error %d\n", errno );
            FreeMemory( ComSegment );
            return 0;
        }
    
        // prepare to execute the COM file

        if ( setupRegs )
        {
            cpu.set_cs( ComSegment );
            cpu.set_ss( ComSegment );
            cpu.set_sp( 0xffff );
            cpu.set_ip( 0x100 );
            cpu.set_ds( ComSegment );
            cpu.set_es( ComSegment );
            tracer.Trace( "  loaded %s, app segment %04x, ip %04x, sp %04x\n", acApp, cpu.get_cs(), cpu.get_ip(), cpu.get_sp() );
        }
        else
        {
            *reg_ss = ComSegment;
            *reg_sp = 0xffff;
            *reg_cs = ComSegment;
            *reg_ip = 0x100;
            tracer.Trace( "  loaded %s but didn't initialize registers, app segment %04x, ip %04x\n", acApp, cpu.get_cs(), cpu.get_ip() );
        }
    }
    else // EXE
    {
        // Apps own all free memory by default. They can realloc this to free space for other allocations

        uint16_t paragraphs_remaining, paragraphs_free = 0;
        uint16_t DataSegment = AllocateMemory( 0xffff, paragraphs_free );
        assert( 0 == DataSegment );
        DataSegment = AllocateMemory( paragraphs_free, paragraphs_remaining );
        tracer.Trace( "  loading exe, DataSegment is %04x\n", DataSegment );

        if ( 0 == DataSegment )
            i8086_hard_exit( "  0 ram available RAM to load .exe\n", 0 );

        psp = DataSegment;
        InitializePSP( DataSegment, acAppArgs, segEnvironment );

        FILE * fp = fopen( acApp, "rb" );
        if ( 0 == fp )
        {
            tracer.Trace( "  can't open input executable '%s', error %d\n", acApp, errno );
            FreeMemory( DataSegment );
            return 0;
        }
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        vector<uint8_t> theexe( file_size );
        size_t blocks_read = fread( theexe.data(), file_size, 1, fp );
        fclose( fp );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input exe file, error %d", errno );
            FreeMemory( DataSegment );
            return 0;
        }

        ExeHeader & head = * (ExeHeader *) theexe.data();
        if ( 0x5a4d != head.signature )
        {
            tracer.Trace( "  exe isn't MZ" );
            FreeMemory( DataSegment );
            return 0;
        }

        tracer.Trace( "  loading app %s\n", acApp );
        tracer.Trace( "  looks like an MZ exe... size %u, size from blocks %u, bytes in last block %u\n",
                      file_size, ( (uint32_t) head.blocks_in_file ) * 512, head.bytes_in_last_block );
        tracer.Trace( "  relocation entry count %u, header paragraphs %u (%u bytes)\n",
                      head.num_relocs, head.header_paragraphs, head.header_paragraphs * 16 );
        tracer.Trace( "  relative value of stack segment: %#x, initial sp: %#x, initial ip %#x, initial cs relative to segment: %#x\n",
                      head.ss, head.sp, head.ip, head.cs );
        tracer.Trace( "  relocation table offset %u, overlay number %u\n",
                      head.reloc_table_offset, head.overlay_number );

        if ( head.reloc_table_offset > 100 )
        {
            tracer.Trace( "  probably not a 16-bit exe; head.reloc_table_offset: %d", head.reloc_table_offset );
            FreeMemory( DataSegment );
            return 0;
        }

        uint32_t codeStart = 16 * (uint32_t) head.header_paragraphs;
        uint32_t cbUsed = head.blocks_in_file * 512;
        if ( 0 != head.bytes_in_last_block )
            cbUsed -= ( 512 - head.bytes_in_last_block );
        cbUsed -= codeStart; // don't include the header
        tracer.Trace( "  bytes used by load module: %u, and code starts at %u\n", cbUsed, codeStart );

        if ( ( cbUsed / 16 ) > paragraphs_free )
        {
            tracer.Trace( "  insufficient ram available RAM to load .exe, in paragraphs: %04x required, %04x available\n", cbUsed / 16, paragraphs_free );
            FreeMemory( DataSegment );
            return 0;
        }

        const uint16_t CodeSegment = DataSegment + 16; //  data segment + 256 bytes (16 paragraphs) for the psp
        uint8_t * pcode = GetMem( CodeSegment, 0 );
        memcpy( pcode, theexe.data() + codeStart, cbUsed );
        tracer.Trace( "  start of the code:\n" );
        tracer.TraceBinaryData( pcode, 0x200, 4 );

        // apply relocation entries

        ExeRelocation * pRelocationEntries = (ExeRelocation *) ( theexe.data() + head.reloc_table_offset );
        for ( uint16_t r = 0; r < head.num_relocs; r++ )
        {
            uint32_t offset = pRelocationEntries[ r ].offset + pRelocationEntries[ r ].segment * 16;
            uint16_t * target = (uint16_t *) ( pcode + offset );
            //tracer.TraceQuiet( "  relocation %u offset %u, update %#02x to %#02x\n", r, offset, *target, *target + CodeSegment );
            *target += CodeSegment;
        }

        if ( setupRegs )
        {
            cpu.set_cs( CodeSegment + head.cs );
            cpu.set_ss( CodeSegment + head.ss );
            cpu.set_ds( DataSegment );
            cpu.set_es( cpu.get_ds() );
            cpu.set_sp( head.sp );
            cpu.set_ip( head.ip );
            cpu.set_ax( 0xffff ); // no drives in use
            tracer.Trace( "  loaded %s CS: %04x, SS: %04x, DS: %04x, SP: %04x, IP: %04x\n", acApp,
                          cpu.get_cs(), cpu.get_ss(), cpu.get_ds(), cpu.get_sp(), cpu.get_ip() );
        }
        else
        {
            *reg_ss = CodeSegment + head.ss;
            *reg_sp = head.sp;
            *reg_cs = CodeSegment + head.cs;
            *reg_ip = head.ip;
            tracer.Trace( "  loaded %s suspended (mode 1), cs %04x, ip %04x, ss %04x, sp %04x\n",
                          acApp, *reg_cs, *reg_ip, *reg_ss, *reg_sp );
        }
    }

    return psp;
} //LoadBinary

DWORD WINAPI PeekKeyboardThreadProc( LPVOID param )
{
    HANDLE aHandles[ 2 ];
    aHandles[ 0 ] = (HANDLE) param;    // the stop/shutdown event
    aHandles[ 1 ] = g_hConsoleInput;

    do
    {
        DWORD ret = WaitForMultipleObjects( 2, aHandles, FALSE, 20 );
        if ( WAIT_OBJECT_0 == ret )
            break;

        if ( !g_KbdIntWaitingForRead && !g_KbdPeekAvailable )
        {
            uint8_t asciiChar, scancode;
            if ( peek_keyboard( asciiChar, scancode ) )
            {
                tracer.Trace( "async thread (woken via %08x) noticed that a keystroke is available: %02x%02x\n", ret, scancode, asciiChar );
                g_KbdPeekAvailable = true;
                cpu.exit_emulate_early(); // no time to lose processing the keystroke
            }
        }
    } while( true );

    return 0;
} //PeekKeyboardThreadProc

uint16_t AllocateEnvironment( uint16_t segStartingEnv, const char * pathToExecute, const char * pcmdLineEnv )
{
    char fullPath[ MAX_PATH ];
    GetFullPathNameA( pathToExecute, _countof( fullPath ), fullPath, 0 );

    const char * pComSpec = "COMSPEC=COMMAND.COM";
    const char * pBriefFlags = "BFLAGS=-kzr -mDJL";
    uint16_t bytesNeeded = (uint16_t) strlen( fullPath );
    uint16_t startLen = 0;
    char * pEnvStart = (char *) GetMem( segStartingEnv, 0 );
    uint16_t cmdLineEnvLen = 0;

    if ( 0 != segStartingEnv )
    {
        char * pe = pEnvStart;
        do
        {
            size_t l = 1 + strlen( pe );
            startLen += (uint16_t) l;
            pe += l;
        } while ( 0 != *pe );
    
        bytesNeeded += startLen;
    }
    else
    {
        bytesNeeded += (uint16_t) 2 + (uint16_t) ( strlen( pComSpec ) + strlen( pBriefFlags ) );

        if ( pcmdLineEnv )
        {
            cmdLineEnvLen = 1 + (uint16_t) strlen( pcmdLineEnv );
            bytesNeeded += cmdLineEnvLen;
        }
    }

    // apps assume there is space at the end to write to. It should be at least 160 bytes in size

    bytesNeeded += (uint16_t) 256;

    uint16_t remaining;
    uint16_t segEnvironment = AllocateMemory( round_up_to( bytesNeeded, 16 ) / 16, remaining );
    if ( 0 == segEnvironment )
    {
        tracer.Trace( "can't allocate %d bytes for environment\n", bytesNeeded );
        return 0;
    }

    char * penvdata = (char *) GetMem( segEnvironment, 0 );
    char * penv = penvdata;

    if ( 0 == segStartingEnv )
    {
        strcpy( penv, pComSpec ); // it needs this or it can't load itself
        penv += 1 + strlen( penv );
    }
    else
    {
        memcpy( penv, pEnvStart, startLen );
        penv += startLen;
    }

    if ( ends_with( fullPath, "B.EXE" ) )
    {
        strcpy( penv, pBriefFlags ); // Brief: keyboard compat, no ^z at end, fast screen updates, my macros
        penv += 1 + strlen( penv );
    }

    if ( pcmdLineEnv )
    {
        strcpy( penv, pcmdLineEnv );
        _strupr( penv );
        char * p = penv;
        while ( *p )
        {
            if ( ',' == *p )
                *p = 0;
            p++;
        }

        penv += cmdLineEnvLen;
    }

    *penv++ = 0; // extra 0 to indicate there are no more environment variables
    * (uint16_t *)  ( penv ) = 0x0001; // one more additional item per DOS 3.0+
    penv += 2;

    strcpy( penv, fullPath );
    tracer.Trace( "  wrote full path to the environment: '%s'\n", penv );
    tracer.TraceBinaryData( (uint8_t *) penvdata, bytesNeeded, 4 );

    return segEnvironment;
} //AllocateEnvironment

bool InterruptHookedByApp( uint8_t i )
{
    uint16_t seg = ( (uint16_t *) GetMem( 0, 0 ) )[ 2 * i + 1 ]; // lower uint_16 has offset, upper has segment
    return ( InterruptRoutineSegment != seg );
} //InterruptHookedByApp

static void RenderNumber( long long n, char * ac )
{
    if ( n < 0 )
    {
        strcat( ac, "-" );
        RenderNumber( -n, ac );
        return;
    }
   
    if ( n < 1000 )
    {
        sprintf( ac + strlen( ac ), "%lld", n );
        return;
    }

    RenderNumber( n / 1000, ac );
    sprintf( ac + strlen( ac ), ",%03lld", n % 1000 );
    return;
} //RenderNumber

static char * RenderNumberWithCommas( long long n, char * ac )
{
    ac[ 0 ] = 0;
    RenderNumber( n, ac );
    return ac;
} //RenderNumberWithCommas

uint32_t GetBiosDailyTimer()
{
    // the daily timer bios value should increment 18.206 times per second -- every 54.9251 ms

#if true
    // this method is more accurate and rolls less often, but maybe 15% slower
    // get 100ns intervals since 1/1/1601.
    // 1 ms = 1000000 ns == 10000 100ns
    // Need to subtract starting system time or apps like Quick C 2.0 fail to run.

    uint64_t since_epoch;
    GetSystemTimeAsFileTime( (FILETIME *) & since_epoch );
    since_epoch -= g_startSystemTime;
    return (uint32_t) ( since_epoch / 549251 );
#else
    // less accurate and rolls more often, but maybe 15% faster
    return GetTickCount() / 55;
#endif
} //GetBiosDailyTimer

int main( int argc, char ** argv )
{
    GetSystemTimeAsFileTime( (FILETIME *) & g_startSystemTime );

    // put the app name without a path or .exe into g_thisApp

    char * pname = argv[ 0 ];
    char * plastslash = strrchr( pname, '\\' );
    if ( 0 != plastslash )
        pname = plastslash + 1;
    strcpy( g_thisApp, pname );
    char * pdot = strchr( g_thisApp, '.' );
    if ( pdot )
        *pdot = 0;

    memset( memory, 0, sizeof( memory ) );

    g_hConsoleOutput = GetStdHandle( STD_OUTPUT_HANDLE );
    g_hConsoleInput = GetStdHandle( STD_INPUT_HANDLE );

    init_blankline( DefaultVideoAttribute );

    char * pcAPP = 0;
    bool trace = FALSE;
    uint64_t clockrate = 0;
    bool showPerformance = false;
    char acAppArgs[127] = {0}; // max length for DOS command tail
    bool traceInstructions = false;
    bool force80x25 = false;
    bool clearDisplayOnExit = true;
    char * penvVars = 0;
    DWORD_PTR dwProcessAffinityMask = 0; // by default let the OS decide

    for ( int i = 1; i < argc; i++ )
    {
        char *parg = argv[i];
        char c = *parg;

        if ( ( 0 == pcAPP ) && ( '-' == c || '/' == c ) )
        {
            char ca = (char) tolower( parg[1] );

            if ( 'd' == ca )
                clearDisplayOnExit = false;
            else if ( 's' == ca )
            {
                if ( ':' == parg[2] )
                    clockrate = _strtoui64( parg + 3 , 0, 10 );
                else
                    usage( "colon required after s argument" );
            }
            else if ( 't' == ca )
                trace = true;
            else if ( 'i' == ca )
                traceInstructions = true;
            else if ( 'p' == ca )
                showPerformance = true;
            else if ( 'c' == parg[1] )
                g_forceConsole = true;
            else if ( 'C' == parg[1] )
                force80x25 = true;
            else if ( 'e' == ca )
            {
                if ( penvVars )
                    usage( "environment variables can only be specified once\n" );

                if ( ':' == parg[2] )
                    penvVars = parg + 3;
                else
                    usage( "colon required after e argument" );
            }
            else if ( 'h' == ca )
                g_PackedFileCorruptWorkaround = true;
            else if ( 'z' == ca )
            {
                if ( ':' == parg[2] )
                    dwProcessAffinityMask = _strtoui64( parg + 3 , 0, 16 );
                else
                    usage( "colon required after z argument" );
            }
            else
                usage( "invalid argument specified" );
        }
        else
        {
            if ( 0 == pcAPP )
                pcAPP = parg;
            else if ( strlen( acAppArgs ) + 3 + strlen( parg ) < _countof( acAppArgs ) )
            {
                // DOS puts a space before the first argument and between arguments

                strcat( acAppArgs, " " );
                strcat( acAppArgs, parg );
            }
        }
    }

    static WCHAR logFile[ MAX_PATH ];
    wsprintf( logFile, L"%S.log", g_thisApp );
    tracer.Enable( trace, logFile, true );
    tracer.SetQuiet( true );
    cpu.trace_instructions( traceInstructions );

    if ( 0 == pcAPP )
        usage( "no command specified" );

    //_assume( 0 != pcAPP ); // the compiler warns because it doesn't know usage() always exits.
    strcpy( g_acApp, pcAPP );
    _strupr( g_acApp );
    DWORD attr = GetFileAttributesA( g_acApp );
    if ( INVALID_FILE_ATTRIBUTES == attr )
    {
        if ( strstr( g_acApp, ".COM" ) || strstr( g_acApp, ".EXE" ) )
            usage( "can't find command file .com or .exe" );
        else
        {
            strcat( g_acApp, ".COM" );
            attr = GetFileAttributesA( g_acApp );
            if ( INVALID_FILE_ATTRIBUTES == attr )
            {
                char * dot = strstr( g_acApp, ".COM" );
                strcpy( dot, ".EXE" );
                attr = GetFileAttributesA( g_acApp );
                if ( INVALID_FILE_ATTRIBUTES == attr )
                    usage( "can't find command file" );
            }
        }
    }

    if ( 0 != dwProcessAffinityMask )
    {
        BOOL ok = SetProcessAffinityMask( (HANDLE) -1, dwProcessAffinityMask );
        tracer.Trace( "Result of SetProcessAffinityMask( %#x ) is %d\n", dwProcessAffinityMask, ok );
    }

    // global bios memory

    uint8_t * pbiosdata = GetMem( 0x40, 0 );
    * (uint16_t *) ( pbiosdata + 0x10 ) = 0x21;           // equipment list. diskette installed and initial video mode 0x20
    * (uint16_t *) ( pbiosdata + 0x13 ) = 640;            // contiguous 1k blocks (640 * 1024)
    * (uint16_t *) ( pbiosdata + 0x1a ) = 0x1e;           // keyboard buffer head
    * (uint16_t *) ( pbiosdata + 0x1c ) = 0x1e;           // keyboard buffer tail
    * (uint8_t *)  ( pbiosdata + 0x49 ) = g_videoMode;    // video mode is 80x25, 16 colors
    * (uint16_t *) ( pbiosdata + 0x4a ) = ScreenColumns;  // 80
    * (uint16_t *) ( pbiosdata + 0x4c ) = 0x1000;         // video regen buffer size
    * (uint8_t *)  ( pbiosdata + 0x60 ) = 7;              // cursor ending/bottom scan line
    * (uint8_t *)  ( pbiosdata + 0x61 ) = 6;              // cursor starting/top scan line
    * (uint16_t *) ( pbiosdata + 0x63 ) = 0x3d4;          // base port for 6845 CRT controller. color
    * (uint16_t *) ( pbiosdata + 0x72 ) = 0x1234;         // soft reset flag (bypass memteest and crt init)
    * (uint16_t *) ( pbiosdata + 0x80 ) = 0x1e;           // keyboard buffer start
    * (uint16_t *) ( pbiosdata + 0x82 ) = 0x3e;           // one byte past keyboard buffer start
    * (uint8_t *)  ( pbiosdata + 0x84 ) = ScreenRows;     // 25
    * (uint8_t *)  ( pbiosdata + 0x10f ) = 0;             // where GWBASIC checks if it's in a shelled command.com.
    * (uint8_t *) ( GetMem( 0xffff, 0xe ) ) = 0xff;       // original pc

#if 0
    // wordperfect 6.0 looks at +4 in lists of lists for a far pointer to the system file table
    // make that pointer point to an empty system file table

    * (uint16_t *) ( GetMem( SegmentListOfLists, OffsetListOfLists + 4 ) ) = OffsetSystemFileTable;
    * (uint16_t *) ( GetMem( SegmentListOfLists, OffsetListOfLists + 6 ) ) = SegmentListOfLists;
    * (uint16_t *) ( GetMem( SegmentListOfLists, OffsetSystemFileTable ) ) = 0xffff;
    * (uint16_t *) ( GetMem( SegmentListOfLists, OffsetSystemFileTable + 2 ) ) = 0xffff;
    * (uint16_t *) ( GetMem( SegmentListOfLists, OffsetSystemFileTable + 4 ) ) = 0x30; // lots of files available
#endif

    // 256 interrupt vectors at address 0 - 3ff. The first 0x40 are reserved for bios/dos and point to
    // routines starting at InterruptRoutineSegment. The routines are almost all the same -- fake opcode, interrupt #, retf 2
    // One exception is tick tock interrupt 0x1c, which just does an iret for performance.
    // Another is keyboard interrupt 9.
    // Interrupts 9 and 1c require an iret so flags are restored since these are externally, asynchronously triggered.
    // Other interrupts use far ret 2 (not iret) so as to not trash the flags (Z and C) used as return codes.
    // Functions are all allocated 5 bytes each.

    uint32_t * pVectors = (uint32_t *) GetMem( 0, 0 );
    uint8_t * pRoutines = (uint8_t *) GetMem( InterruptRoutineSegment, 0 );
    for ( uint32_t intx = 0; intx < 0x40; intx++ )
    {
        uint32_t offset = intx * 5;
        pVectors[ intx ] = ( InterruptRoutineSegment << 16 ) | ( offset );
        uint8_t * routine = pRoutines + offset;

        if ( 8 == intx )
        {
            routine[ 0 ] = 0xcd; // int
            routine[ 1 ] = 0x1c; // int 1c
            routine[ 2 ] = 0xcf; // iret
        }
        else if ( ( 9 == intx ) || ( intx <= 4 ) ) 
        {
            routine[ 0 ] = i8086_opcode_interrupt;
            routine[ 1 ] = (uint8_t) intx;
            routine[ 2 ] = 0xcf; // iret
        }
        else if ( 0x1c == intx )
            routine[ 0 ] = 0xcf; // iret
        else
        {
            routine[ 0 ] = i8086_opcode_interrupt;
            routine[ 1 ] = (uint8_t) intx;
            routine[ 2 ] = 0xca; // retf 2 instead of iret so C and Z flags aren't restored
            routine[ 3 ] = 2;
            routine[ 4 ] = 0;
        }
    }

    // write assembler routines into 0x0600 - 0x0bff. make each function segment-aligned so
    // exection can start at ip 0.

    uint16_t curseg = MachineCodeSegment;

    memcpy( GetMem( curseg, 0 ), int21_3f_code, sizeof( int21_3f_code ) );
    g_int21_3f_seg = curseg;
    curseg += ( round_up_to( sizeof( int21_3f_code ), 16 ) / 16 );

    memcpy( GetMem( curseg, 0 ), int21_a_code, sizeof( int21_a_code ) );
    g_int21_a_seg = curseg;
    curseg += ( round_up_to( sizeof( int21_a_code ), 16 ) / 16 );

    memcpy( GetMem( curseg, 0 ), int21_8_code, sizeof( int21_8_code ) );
    g_int21_8_seg = curseg;
    curseg += ( round_up_to( sizeof( int21_8_code ), 16 ) / 16 );

    memcpy( GetMem( curseg, 0 ), int16_0_code, sizeof( int16_0_code ) );
    g_int16_0_seg = curseg;
    curseg += ( round_up_to( sizeof( int16_0_code ), 16 ) / 16 );

    tracer.Trace( "machine code: 21_3f %04x, 21_a %04x, 21_8 %04x, 16_0 %04x\n",
                  g_int21_3f_seg, g_int21_a_seg, g_int21_8_seg, g_int16_0_seg );

    assert( curseg <= InterruptRoutineSegment );

    // allocate the environment space and load the binary

    uint16_t segEnvironment = AllocateEnvironment( 0, g_acApp, penvVars );
    if ( 0 == segEnvironment )
        i8086_hard_exit( "unable to create environment for the app\n", 0 );

    g_currentPSP = LoadBinary( g_acApp, acAppArgs, segEnvironment, true, 0, 0, 0, 0 );
    if ( 0 == g_currentPSP )
        i8086_hard_exit( "unable to load executable\n", 0 );

    // gwbasic calls ioctrl on stdin and stdout before doing anything that would indicate what mode it wants.
    // turbo pascal v3 doesn't give a good indication that it wants 80x25.
    // word for DOS 6.0 is the same -- hard code it

    if ( ends_with( g_acApp, "gwbasic.exe" ) || ends_with( g_acApp, "mips.com" ) || ends_with( g_acApp, "turbo.com" ) || ends_with( g_acApp, "word.exe" ) )
    {
        if ( !g_forceConsole )
            force80x25 = true;
    }

    if ( force80x25 )
        PerhapsFlipTo80x25();

    g_diskTransferSegment = cpu.get_ds();
    g_diskTransferOffset = 0x80; // same address as the second half of PSP -- the command tail
    g_haltExecution = false;
    cpu.set_interrupt( true ); // DOS starts apps with interrupts enabled
    uint32_t * pDailyTimer = (uint32_t *) ( pbiosdata + 0x6c );

    // Peek for keystrokes in a separate thread. Without this, some DOS apps would require polling in the loop below,
    // but keyboard peeks are very slow -- it makes cross-process calls. With the thread, the loop below is faster.
    // Note that kbhit() makes the same call interally to the same cross-process API. It's no faster.

    CSimpleThread peekKbdThread( PeekKeyboardThreadProc );
    uint64_t total_cycles = 0; // this will be inaccurate if I8086_TRACK_CYCLES isn't defined
    CPUCycleDelay delay( clockrate );
    high_resolution_clock::time_point tStart = high_resolution_clock::now();

    do
    {
        total_cycles += cpu.emulate( 1000 );

        if ( g_haltExecution )
            break;

        delay.Delay( total_cycles );

        // apps like mips.com write to video ram and never provide an opportunity to redraw the display

        if ( g_use80x25 )
            throttled_UpdateDisplay( 200 );

        uint32_t dt = GetBiosDailyTimer();
        bool timer_changed = ( dt != *pDailyTimer );
        if ( timer_changed )
            *pDailyTimer = dt;

        // check interrupt enable and trap flags externally to avoid side effects in the emulator

        if ( cpu.get_interrupt() && !cpu.get_trap() )
        {
            // if the keyboard peek thread has detected a keystroke, process it with an int 9.
            // don't plumb through port 60 since apps work without that.
    
            if ( !g_KbdIntWaitingForRead && g_KbdPeekAvailable )
            {
                tracer.Trace( "main loop: scheduling an int 9\n" );
                cpu.external_interrupt( 9 );
                g_KbdIntWaitingForRead = true;
                g_KbdPeekAvailable = false;
                continue;
            }

            // if interrupt 8 (timer) or 0x1c (tick tock) are hooked by an app and 55 milliseconds have elapsed,
            // invoke int 8, which by default then invokes int 1c.
    
            if ( timer_changed && ( InterruptHookedByApp( 0x1c ) || InterruptHookedByApp( 8 ) ) )
            {
                // on my machine this is invoked about every 72 million total_cycles if no throttle sleeping happened (tens of thousands if so)
    
                tracer.Trace( "sending an int 8, dt: %#x, total_cycles %llu\n", dt, total_cycles );
                cpu.external_interrupt( 8 );
                continue;
            }
        }
        else
        {
            if ( !g_KbdIntWaitingForRead && g_KbdPeekAvailable )
                tracer.Trace( "can't schedule a keyboard interrupt because they are disabled!\n" );
        }
    } while ( true );

    if ( g_use80x25 )  // get any last-second screen updates displayed
        UpdateDisplay();

    high_resolution_clock::time_point tDone = high_resolution_clock::now();

    peekKbdThread.EndThread();

    g_consoleConfig.RestoreConsole( clearDisplayOnExit );

    if ( showPerformance )
    {
        char ac[ 100 ];
        long long totalTime = duration_cast<std::chrono::milliseconds>( tDone - tStart ).count();
        printf( "\n" );
        printf( "elapsed milliseconds: %16s\n", RenderNumberWithCommas( totalTime, ac ) );

        #ifdef I8086_TRACK_CYCLES
            printf( "8086 cycles:      %20s\n", RenderNumberWithCommas( total_cycles, ac ) );
            printf( "clock rate: " );
            if ( 0 == clockrate )
            {
                printf( "      %20s\n", "unbounded" );
                uint64_t total_ms = total_cycles / 4770;
                printf( "approx ms at 4.77Mhz: %16s  == ", RenderNumberWithCommas( total_ms, ac ) );
                uint16_t days = (uint16_t) ( total_ms / 1000 / 60 / 60 / 24 );
                uint16_t hours = (uint16_t) ( ( total_ms % ( 1000 * 60 * 60 * 24 ) ) / 1000 / 60 / 60 );
                uint16_t minutes = (uint16_t) ( ( total_ms % ( 1000 * 60 * 60 ) ) / 1000 / 60 );
                uint16_t seconds = (uint16_t) ( ( total_ms % ( 1000 * 60 ) ) / 1000 );
                uint64_t milliseconds = ( ( total_ms % 1000 ) );
                printf( "%u days, %u hours, %u minutes, %u seconds, %llu milliseconds\n", days, hours, minutes, seconds, milliseconds );
            }
            else
                printf( "      %20s Hz\n", RenderNumberWithCommas( clockrate, ac ) );
        #endif

        printf( "app exit code:    %20d\n", g_appTerminationReturnCode );
    }

    {
        qsort( g_InterruptsCalled.data(), g_InterruptsCalled.size(), sizeof( IntCalled ), compare_int_entries );
        bool ah_used = false;
        size_t cEntries = g_InterruptsCalled.size();
        tracer.Trace( "Interrupt usage by the app:\n" );
        tracer.Trace( "  int     ah       calls    name\n" );
        for ( size_t i = 0; i < cEntries; i++ )
        {
            IntCalled & ic = g_InterruptsCalled[ i ];
            const char * pintstr = get_interrupt_string( ic.i, (uint8_t) ic.c, ah_used );
            if ( ah_used )
                tracer.Trace( "   %02x     %02x  %10d    %s\n", ic.i, ic.c, ic.calls, pintstr );
            else
                tracer.Trace( "   %02x         %10d    %s\n", ic.i, ic.calls, pintstr );
        }
    }

#ifndef NDEBUG
    cpu.trace_opcode_usage();
#endif

    tracer.Trace( "exit code of %s: %d\n", g_thisApp, g_appTerminationReturnCode );

    tracer.Shutdown();

    return g_appTerminationReturnCode; // return what the main app returned
} //main

