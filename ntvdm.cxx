// NT Virtual DOS Machine. Not the real one, but one that works on 64-bit Windows.
// Written by David Lee in late 2022
// This only simulates a small subset of DOS and BIOS behavior.
// I only implemented BIOS/DOS calls used by tested apps, so there are some big gaps.
// Only CGA/EGA/VGA text mode 3 (80 x 25/43/50 color) is supported.
// No graphics, sound, mouse, or anything else not needed for simple command-line apps.
// tested apps:
//    Turbo Pascal 1.00A, 2.00B, 3.02A, 4.0, 5.0, 5.5, 6.0, and 7.0, both the apps and the programs they generate.
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
//    PC-LISP V3.00
//    muLISP v5.10 interpreter (released by Microsoft).
//    Microsoft Pascal v1, v3, v4
//    MT+86 - Pascal   V3.1.1 from Digital Research
//    IBM Personal Computer Pascal Compiler Version 2.00 (generated apps require WRAP_HMA_ADDRESSES be true in i8086.hxx)
//    Microsoft COBOL Compiler Version 5.0 (compiler and generated apps). Linker requires 286 but QBX's linker works fine.
//    Digital Research PL/I-86 Compiler Version 1.0, link86, and generated apps.
//    Microsoft FORTRAN77, the linker, and generated apps.
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
//         https://fd.lod.bz/rbil/interrup/dos_kernel/2152.html
//         https://faydoc.tripod.com/structures/13/1378.htm
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
#include <chrono>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <string>
#include <regex>
#endif

#include <assert.h>
#include <vector>

#include <djltrace.hxx>
#include <djl_con.hxx>
#include <djl_cycle.hxx>
#include <djl_durat.hxx>
#include <djl_thrd.hxx>
#include <djl_kslog.hxx>
#include <djl8086d.hxx>
#include "i8086.hxx"

using namespace std;
using namespace std::chrono;

// using assembly for keyboard input enables timer interrupts while spinning for a keystroke

#define USE_ASSEMBLY_FOR_KBD true

// machine code for various interrupts. generated with mint.bat. 
uint64_t int16_0_code[] = { 
0x01b4c08e0040b806, 0x068326fafa741669, 0x001a3e832602001a, 0x001a06c726077c3e, 0x000000cb07fb001e, 
}; 
uint64_t int21_1_code[] = { 
0x10690ab4166900b4, 0x0000000000cb01b4, 
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

uint16_t AllocateEnvironment( uint16_t segStartingEnv, const char * pathToExecute, const char * pcmdLineEnv );
uint16_t LoadBinary( const char * app, const char * acAppArgs, uint16_t segment, bool setupRegs,
                     uint16_t * reg_ss, uint16_t * reg_sp, uint16_t * reg_cs, uint16_t * reg_ip, bool bootSectorLoad );
uint16_t LoadOverlay( const char * app, uint16_t segLoadAddress, uint16_t segmentRelocationFactor );

uint16_t round_up_to( uint16_t x, uint16_t multiple )
{
    if ( 0 == ( x % multiple ) )
        return x;

    return x + ( multiple - ( x % multiple ) );
} //round_up_to

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
        tracer.Trace( "  app execute block:\n" );
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
    uint16_t relative_ss;
    uint16_t sp;
    uint16_t checksum;
    uint16_t ip;
    uint16_t relative_cs;
    uint16_t reloc_table_offset;
    uint16_t overlay_number;

    void Trace()
    {
        tracer.Trace( "  exe header:\n" );
        tracer.Trace( "    signature:            %04x = %u\n", signature, signature );
        tracer.Trace( "    bytes in last block:  %04x = %u\n", bytes_in_last_block, bytes_in_last_block );
        tracer.Trace( "    blocks in file:       %04x = %u\n", blocks_in_file, blocks_in_file );
        tracer.Trace( "    num relocs:           %04x = %u\n", num_relocs, num_relocs );
        tracer.Trace( "    header paragraphs:    %04x = %u\n", header_paragraphs, header_paragraphs );
        tracer.Trace( "    min extra paragraphs: %04x = %u\n", min_extra_paragraphs, min_extra_paragraphs );
        tracer.Trace( "    max extra paragraphs: %04x = %u\n", max_extra_paragraphs, max_extra_paragraphs );
        tracer.Trace( "    relative ss:          %04x = %u\n", relative_ss, relative_ss );
        tracer.Trace( "    sp:                   %04x = %u\n", sp, sp );
        tracer.Trace( "    checksum:             %04x = %u\n", checksum, checksum );
        tracer.Trace( "    ip:                   %04x = %u\n", ip, ip );
        tracer.Trace( "    relative cs:          %04x = %u\n", relative_cs, relative_cs );
        tracer.Trace( "    reloc table offset:   %04x = %u\n", reloc_table_offset, reloc_table_offset );
        tracer.Trace( "    overlay_number:       %04x = %u\n", overlay_number, overlay_number );
    }
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
const uint8_t DefaultVideoMode = 3;                               // 3=80x25 16 colors
const uint32_t ScreenColumns = 80;      
const uint32_t DefaultScreenRows = 25;         
const uint32_t ScreenColumnsM1 = ScreenColumns - 1;               // columns minus 1
const uint32_t ScreenBufferSegment = 0xb800;                      // location in i8086 physical RAM of CGA display. 16k, 4k per page.
const uint32_t MachineCodeSegment = 0x0060;                       // machine code for keyboard/etc. starts here
const uint16_t AppSegment = 0x1000 / 16;                          // base address for apps in the vm. 4k. DOS uses 0x1920 == 6.4k
const uint32_t DOS_FILENAME_SIZE = 13;                            // 8 + 3 + '.' + 0-termination
const uint16_t InterruptRoutineSegment = 0x00c0;                  // interrupt routines start here.
const uint32_t firstAppTerminateAddress = 0xf000dead;             // exit ntvdm when this is the parent return address
const uint16_t SegmentListOfLists = 0x50;
const uint16_t OffsetListOfLists = 0xb0;
const uint64_t OffsetDeviceControlBlock = 0xe0;
#if 0
const uint16_t OffsetSystemFileTable = 0xf0;
#endif

CDJLTrace tracer;

static uint16_t g_segHardware = ScreenBufferSegment; // first byte beyond where apps have available memory
static uint16_t blankLine[ScreenColumns] = {0};      // an optimization for filling lines with blanks
static std::mutex g_mtxEverything;                   // one mutex for all shared state
static ConsoleConfiguration g_consoleConfig;         // to get into and out of 80x25 mode
static bool g_haltExecution = false;                 // true when the app is shutting down
static uint16_t g_diskTransferSegment = 0;           // segment of current disk transfer area
static uint16_t g_diskTransferOffset = 0;            // offset of current disk transfer area
static vector<FileEntry> g_fileEntries;              // vector of currently open files
static vector<FileEntry> g_fileEntriesFCB;           // vector of currently open files with FCBs
static vector<DosAllocation> g_allocEntries;         // vector of blocks allocated to DOS apps
static uint16_t g_currentPSP = 0;                    // psp of the currently running process
static bool g_use80xRowsMode = false;                // true to force 80 x 25/43/50 with cursor positioning
static bool g_forceConsole = false;                  // true to force teletype mode, with no cursor positioning
static bool g_int16_1_loop = false;                  // true if an app is looping to get keyboard input. don't busy loop.
static bool g_KbdPeekAvailable = false;              // true when peek on the keyboard sees keystrokes
static bool g_int9_pending = false;                  // true if an int9 was scheduled but not yet invoked
static long g_injectedControlC = 0;                  // # of control c events to inject
static int g_appTerminationReturnCode = 0;           // when int 21 function 4c is invoked to terminate an app, this is the app return code
static char g_acRoot[ MAX_PATH ];                    // host folder ending in slash/backslash that maps to DOS "C:\"
static char g_acApp[ MAX_PATH ];                     // the DOS .com or .exe being run
static char g_thisApp[ MAX_PATH ];                   // name of this exe (argv[0]), likely NTVDM
static char g_lastLoadedApp[ MAX_PATH ] = {0};       // path of most recenly loaded program (though it may have terminated)
static bool g_PackedFileCorruptWorkaround = false;   // if true, allocate memory starting at 64k, not AppSegment
static uint16_t g_int21_3f_seg = 0;                  // segment where this code resides with ip = 0
static uint16_t g_int21_a_seg = 0;                   // "
static uint16_t g_int21_1_seg = 0;                   // "
static uint16_t g_int21_8_seg = 0;                   // "
static uint16_t g_int16_0_seg = 0;                   // "
static char cwd[ MAX_PATH ] = {0};                   // used as a temporary in several locations
static vector<IntCalled> g_InterruptsCalled;         // track interrupt usage
static high_resolution_clock::time_point g_tAppStart; // system time at app start
static uint8_t g_bufferLastUpdate[ 80 * 50 * 2 ] = {0}; // used to check for changes in video memory. At most we support 80 by 50
static CKeyStrokes g_keyStrokes;                     // read or write keystrokes between kslog.txt and the app
static bool g_UseOneThread = false;                  // true if no keyboard thread should be used
static uint64_t g_msAtStart = 0;                     // milliseconds since epoch at app start
static bool g_SendControlCInt = false;               // set to TRUE when/if a ^C is detected and an interrupt should be sent

#ifndef _WIN32
static bool g_forcePathsUpper = false;               // helpful for Linux
static bool g_forcePathsLower = false;               // helpful for Linux
static bool g_altPressedRecently = false;            // hack because I can't figure out if ALT is currently pressed
#endif

bool ValidDOSFilename( char * pc )
{
    if ( 0 == *pc )
        return false;

    if ( !strcmp( pc, "." ) )
        return false;

    if ( !strcmp( pc, ".." ) )
        return false;

    const char * pcinvalid = "<>,;:=?[]%|()/\\";
    for ( size_t i = 0; i < strlen( pcinvalid ); i++ )
        if ( strchr( pc, pcinvalid[i] ) )
            return false;

    size_t len = strlen( pc );

    if ( len > 12 )
        return false;

    char * pcdot = strchr( pc, '.' );

    if ( !pcdot && ( len > 8 ) )
        return false;

    if ( pcdot && ( ( pcdot - pc ) > 8 ) )
        return false;

    return true;
} //ValidDOSFilename

bool ValidDOSPathname( char * pc )
{
    if ( !strcmp( pc, "." ) )
        return true;

    if ( !strcmp( pc, ".." ) )
        return true;

    return ValidDOSFilename( pc );
} //ValidDOSPathname

void backslash_to_slash( char * p )
{
    while ( *p )
    {
        if ( '\\' == *p )
            *p = '/';
        p++;
    }
} //backslash_to_slash

void slash_to_backslash( char * p )
{
    while ( *p )
    {
        if ( '/' == *p )
            *p = '\\';
        p++;
    }
} //slash_to_backslash

void cr_to_zero( char * p )
{
    while ( *p )
    {
        if ( '\r' == *p )
        {
            *p = 0;
            break;
        }
        p++;
    }
} //cr_to_zero

int ends_with( const char * str, const char * end )
{
    size_t len = strlen( str );
    size_t lenend = strlen( end );

    if ( len < lenend )
        return false;

    return !_stricmp( str + len - lenend, end );
} //ends_with

int begins_with( const char * str, const char * start )
{
    while ( *str && *start )
    {
        if ( tolower( *str ) != tolower( *start ) )
            return false;

        str++;
        start++;
    }

    return true;
} //begins_with

const char * DOSToHostPath( const char * p )
{
    const char * poriginal = p;
    char dos_path[ MAX_PATH ];
    strcpy( dos_path, p );
    slash_to_backslash( dos_path ); // DOS lets apps use forward slashes (Brief does this)
    p = dos_path;

    static char host_path[ MAX_PATH ];

    if ( ':' == p[1] )
    {
        strcpy( host_path, g_acRoot );
        if ( '\\' == p[2] )
            strcat( host_path, p + 3 ); // the whole path
        else
            strcpy( host_path, p + 2 ); // just the filename assumed to be in the current directory
    }
    else if ( '\\' == p[0] )
    {
        if ( ! begins_with( poriginal, g_acRoot ) )
        {
            strcpy( host_path, g_acRoot );
            strcat( host_path, p + 1 );
        }
    }
    else
        strcpy( host_path, p );

#ifndef _WIN32
    backslash_to_slash( host_path );
    cr_to_zero( host_path );

    char * start = host_path;
    if ( '/' == host_path[0] )
        start += strlen( g_acRoot );
    if ( g_forcePathsLower )
        strlwr( start );
    else if ( g_forcePathsUpper )
        strupr( start );
#endif

    tracer.Trace( "  translated dos path '%s' to host path '%s'\n", p, host_path );
    assert( !strstr( host_path, "//" ) );
    assert( !strstr( host_path, "\\\\" ) );

    return host_path;
} //DOSToHostPath

#ifdef _WIN32
static HANDLE g_hConsoleOutput = 0;                // the Windows console output handle
static HANDLE g_hConsoleInput = 0;                 // the Windows console input handle
static HANDLE g_heventKeyStroke;
#endif

uint8_t * GetDiskTransferAddress() { return cpu.flat_address8( g_diskTransferSegment, g_diskTransferOffset ); }

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

static void version()
{
    printf( "%s\n", build_string() );
    exit( 1 );
} //version

static void usage( char const * perr )
{
    g_consoleConfig.RestoreConsole( false );

    if ( perr )
        printf( "error: %s\n", perr );

    printf( "Usage: %s [OPTION]... PROGRAM [ARGUMENT]...\n", g_thisApp );
    printf( "Emulates an 8086 and MS-DOS 3.30 runtime environment.\n" );
    printf( "\n" );
    printf( "  -b               load/run program as the boot sector at 07c0:0000\n" );
    printf( "  -c               tty mode. don't automatically make text area 80x25.\n" );
    printf( "  -C               make text area 80x25 (not tty mode). also -C:43 -C:50\n" );
    printf( "  -d               don't clear the display on exit\n" );
    printf( "  -e:env,...       define environment variables.\n" );
    printf( "  -h               load high above 64k and below 0xa0000.\n" );
    printf( "  -i               trace instructions to %s.log.\n", g_thisApp );
    printf( "  -m               after the app ends, print video memory\n" );
    printf( "  -p               show performance stats on exit.\n" );
    printf( "  -r:root          root folder that maps to C:\\\n" );
    printf( "  -t               enable debug tracing to %s.log\n", g_thisApp );
#ifdef I8086_TRACK_CYCLES
    printf( "  -s:X             set processor speed in Hz.\n" );
    printf( "                     for 4.77 MHz 8086 use -s:4770000.\n" );
    printf( "                     for 4.77 MHz 8088 use -s:4500000.\n" );
#endif
#ifndef _WIN32
    printf( "  -u               force DOS paths to be uppercase\n" );
    printf( "  -l               force DOS paths to be lowercase\n" );
#endif
/* work in progress
    printf( "            -kr    read keystrokes from kslog.txt\n" );
    printf( "            -kw    write keywtrokes to kslog.txt\n" );
*/
    printf( "  -v               output version information and exit.\n" );
    printf( "  -?               output this help and exit.\n" );
    printf( "\n" );
    printf( "Examples:\n" );
#ifdef _WIN32
    printf( "  %s -u -e:include=.\\inc msc.exe demo.c,,\\;\n", g_thisApp );
    printf( "  %s -u -e:lib=.\\lib link.exe demo,,\\;\n", g_thisApp );
    printf( "  %s -u -e:include=.\\inc,lib=.\\lib demo.exe one two three\n", g_thisApp );
    printf( "  %s -r:. QBX\n", g_thisApp );
#else
    printf( "  %s -u -e:include=.\\\\inc msc.exe demo.c,,\\\\;\n", g_thisApp );
    printf( "  %s -u -e:lib=.\\\\lib link.exe demo,,\\\\;\n", g_thisApp );
    printf( "  %s -u -e:include=.\\\\inc,lib=.\\\\lib demo.exe one two three\n", g_thisApp );
    printf( "  %s -r:. -u QBX\n", g_thisApp );
#endif
    printf( "  %s -s:4770000 turbo.com\n", g_thisApp );
    printf( "%s\n", build_string() );
    exit( 1 );
} //usage

class CKbdBuffer
{
    private:
        uint8_t * pbiosdata;
        uint16_t * phead;
        uint16_t * ptail;

    public:
        CKbdBuffer()
        {
            pbiosdata = cpu.flat_address8( 0x40, 0 );
            phead = (uint16_t *) ( pbiosdata + 0x1a );
            ptail = (uint16_t *) ( pbiosdata + 0x1c );
        }

        bool IsFull() { return ( ( *phead == ( *ptail + 2 ) ) || ( ( 0x1e == *phead ) && ( 0x3c == *ptail ) ) ); }
        bool IsEmpty() { return ( *phead == *ptail ); }

        void Add( uint8_t asciiChar, uint8_t scancode, bool userGenerated = true )
        {
            if ( userGenerated )
            {
                uint16_t stroke = ( ( (uint16_t) scancode ) << 8 ) | asciiChar;
                g_keyStrokes.Append( stroke );
            }

            if ( IsFull() )
                tracer.Trace( "  dropping keystroke on the flooer because the DOS buffer is full\n" );
            else
            {
                pbiosdata[ *ptail ] = asciiChar;
                (*ptail)++;
                pbiosdata[ *ptail ] = scancode;
                (*ptail)++;
                if ( *ptail >= 0x3e )
                    *ptail = 0x1e;
                tracer.Trace( "    added asciichar %02x scancode %02x, new head = %04x, tail: %04x\n", asciiChar, scancode, *phead, *ptail );
            }
        } //Add

        uint8_t CurAsciiChar()
        {
            assert( !IsEmpty() );
            return pbiosdata[ *phead ];
        } //CurAsciiChar

        uint8_t CurScancode()
        {
            assert( !IsEmpty() );
            return pbiosdata[ 1 + ( *phead ) ];
        } //CurScancode

        uint8_t Consume()
        {
            assert( !IsEmpty() );
            uint8_t r = pbiosdata[ *phead ];
            (*phead)++;
            if ( *phead >= 0x3e )
                *phead = 0x1e;
            tracer.Trace( "    consumed char %02x, new head = %04x, tail %04x\n", r, *phead, *ptail );
            return r;
        } //Consume

        uint32_t FreeSpots()
        {
            if ( IsFull() )
                return 0;

            if ( IsEmpty() )
                return 16;

            uint16_t head = *phead;
            uint16_t tail = *ptail;
            assert( 0 == ( head & 1 ) );
            assert( 0 == ( tail & 1 ) );
            assert( head >= 0x1e && head < 0x3e );
            assert( tail >= 0x1e && tail < 0x3e );

            uint32_t count = 0;
            if ( head > tail )
                count = ( head - tail ) / 2;
            else
                count = 16 - ( ( tail - head ) / 2 );

            assert( count >= 1 && count <= 15 );
            tracer.Trace( "    free spots in kbd buffer: %u. head %02x, tail %02x\n", count, head, tail );
            return count;
        } //FreeSpots
};

uint64_t time_since_last()
{
    static high_resolution_clock::time_point tPrev = high_resolution_clock::now();
    high_resolution_clock::time_point tNow = high_resolution_clock::now();

    uint64_t duration = duration_cast<std::chrono::milliseconds>( tNow - tPrev ).count();
    tPrev = tNow;
    return duration;
} //time_since_last

uint8_t GetActiveDisplayPage()
{
    uint8_t activePage = * cpu.flat_address8( 0x40, 0x62 );
    assert( activePage <= 3 );
    return activePage;
} //GetActiveDisplayPage

void SetActiveDisplayPage( uint8_t page )
{
    assert( page <= 3 );
    * cpu.flat_address8( 0x40, 0x62 ) = page;
} //SetActiveDisplayPage

uint8_t GetVideoMode()
{
    return * cpu.flat_address8( 0x40, 0x49 );
} //GetVideoMode

void SetVideoMode( uint8_t mode )
{
    * cpu.flat_address8( 0x40, 0x49 ) = mode;
} //SetVideoMode

uint8_t GetVideoModeOptions()
{
    return * cpu.flat_address8( 0x40, 0x87 );
} //GetVideoModeOptions

void SetVideoModeOptions( uint8_t val )
{
    * cpu.flat_address8( 0x40, 0x87 ) = val;
} //SetVideoModeOptions

uint8_t GetVideoDisplayCombination()
{
    return * cpu.flat_address8( 0x40, 0x8a );
} //GetVideoDisplayCombination

void SetVideoDisplayCombination( uint8_t comb )
{
    * cpu.flat_address8( 0x40, 0x8a ) = comb;
} //SetVideoDisplayCombination

uint8_t GetScreenRows()
{
    return 1 + cpu.mbyte( 0x40, 0x84 );
} //GetScreenRows

uint8_t GetScreenRowsM1()
{
    return  cpu.mbyte( 0x40, 0x84 );
} //GetScreenRowsM1

void SetScreenRows( uint8_t rows )
{
    tracer.Trace( "  setting screen rows to %u\n", rows );
    * cpu.flat_address8( 0x40, 0x84 ) = rows - 1;
} //SetScreenRows

void TraceBiosInfo()
{
    tracer.Trace( "  bios information:\n" );
    tracer.Trace( "    0x49 display mode:              %#x\n", cpu.mbyte( 0x40, 0x49 ) );
    tracer.Trace( "    0x62 active display page:       %#x\n", cpu.mbyte( 0x40, 0x62 ) );
    tracer.Trace( "    0x84 screen rows:               %#x\n", cpu.mbyte( 0x40, 0x84 ) );
    tracer.Trace( "    0x87 ega feature bits:          %#x\n", cpu.mbyte( 0x40, 0x87 ) );
    tracer.Trace( "    0x89 video display area:        %#x\n", cpu.mbyte( 0x40, 0x89 ) );
    tracer.Trace( "    0x8a video display combination: %#x\n", cpu.mbyte( 0x40, 0x8a ) );
} //TraceBiosInfo

uint8_t * GetVideoMem()
{
    return cpu.flat_address8( ScreenBufferSegment, 0x1000 * GetActiveDisplayPage() );
} //GetVideoMem

bool DisplayUpdateRequired()
{
    return ( 0 != memcmp( g_bufferLastUpdate, GetVideoMem(), sizeof( g_bufferLastUpdate ) ) );
} //DisplayUpdateRequired

void SleepAndScheduleInterruptCheck()
{
    if ( g_UseOneThread && g_consoleConfig.throttled_kbhit() )
        g_KbdPeekAvailable = true; // make sure an int9 gets scheduled

    CKbdBuffer kbd_buf;
    if ( kbd_buf.IsEmpty() && !DisplayUpdateRequired() && !g_KbdPeekAvailable )
    {
        tracer.Trace( "  sleeping in SleepAndScheduleInterruptCheck. g_KbdPeekAvailable %d\n", g_KbdPeekAvailable );
#ifdef _WIN32
        DWORD dw = WaitForSingleObject( g_heventKeyStroke, 1 );
        tracer.Trace( "  sleep woke up due to %s\n", ( 0 == dw ) ? "keystroke event signaled" : "timeout" );
#else
        sleep_ms( 10 );
#endif
        // just because the event was signaled doesn't ensure a keystroke is available. It may be from earlier, but that's OK
    }
    cpu.exit_emulate_early(); // fall out of the instruction loop early to check for a timer or keyboard interrupt
} //SleepAndScheduleInterruptCheck

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

static void trace_all_open_files_fcb()
{
    size_t cEntries = g_fileEntriesFCB.size();
    tracer.Trace( "  all fcb files, count %d:\n", cEntries );
    for ( size_t i = 0; i < cEntries; i++ )
    {
        FileEntry & fe = g_fileEntriesFCB[ i ];
        tracer.Trace( "    fcb file entry %d, fp %p, handle %u, writable %d, process %u, refcount %u, path %s\n", i,
                      fe.fp, fe.handle, fe.writeable, fe.seg_process, fe.refcount, fe.path );
    }
} //trace_all_open_files_fcb

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

#ifdef _WIN32
    GetCurrentDirectoryA( sizeof( cwd ), cwd );
    return (uint8_t) toupper( cwd[0] ) - 'A';
#else
    return 2; // 'C'
#endif
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

FILE * RemoveFileEntryFCB( const char * pname )
{
    for ( size_t i = 0; i < g_fileEntriesFCB.size(); i++ )
    {
        if ( !_stricmp( pname, g_fileEntriesFCB[ i ].path ) )
        {
            FILE * fp = g_fileEntriesFCB[ i ].fp;
            tracer.Trace( "  removing fcb file entry %s: %d\n", g_fileEntriesFCB[ i ].path, i );
            g_fileEntriesFCB.erase( g_fileEntriesFCB.begin() + i );
            return fp;
        }
    }

    tracer.Trace( "  ERROR: could not remove fcb file entry for name '%s'\n", pname );
    return 0;
} //RemoveFileEntryFCB

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
    return (size_t) -1;
} //FindFileEntryIndex

size_t FindFileEntryIndexByProcess( uint16_t seg )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( seg == g_fileEntries[ i ].seg_process )
            return i;
    }
    return (size_t) -1;
} //FindFileEntryIndexByProcess

size_t FindFileEntryIndexByProcessFCB( uint16_t seg )
{
    for ( size_t i = 0; i < g_fileEntriesFCB.size(); i++ )
    {
        if ( seg == g_fileEntriesFCB[ i ].seg_process )
            return i;
    }
    return (size_t) -1;
} //FindFileEntryIndexByProcessFCB

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
    return (size_t) -1;
} //FindFileEntryFromPath

FILE * FindFileEntryFromFileFCB( const char * pfile )
{
    for ( size_t i = 0; i < g_fileEntriesFCB.size(); i++ )
    {
        if ( !_stricmp( pfile, g_fileEntriesFCB[ i ].path ) )
        {
            tracer.Trace( "  found fcb file entry '%s': %d\n", g_fileEntriesFCB[ i ].path, i );
            return g_fileEntriesFCB[ i ].fp;
        }
    }

    tracer.Trace( "  NOTICE: could not find fcb file entry for path %s\n", pfile );
    return 0;
} //FindFileEntryFromFileFCB

uint16_t FindFirstFreeFileHandle()
{
    // Apps like the QuickBasic compiler (bc.exe) depend on the side effect that after a file
    // is closed and a new file is opened the lowest possible free handle value is used for the
    // newly opened file. It's a bug in the app, but it's not getting fixed.

    qsort( g_fileEntries.data(), g_fileEntries.size(), sizeof( FileEntry ), compare_file_entries );

    // DOS uses this, since 0-4 are for built-in handles. stdin, stdout, stderr, com1, lpt1

    uint16_t freehandle = 5;

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
// that (unfortunately) assume it's there to do their thing (QuickC 1.x and 2.x are prime examples).
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
        DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) cpu.flat_address( da.segment - 1, 0 );

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
    return (size_t) -1;
} //FindAllocationEntry

size_t FindAllocationEntryByProcess( uint16_t segment )
{
    for ( size_t i = 0; i < g_allocEntries.size(); i++ )
    {
        if ( segment == g_allocEntries[ i ].seg_process )
            return i;
    }

    return (size_t) -1;
} //FindAllocationEntry

void reset_mcb_tags()
{
    // 1) update header with M for non-last and Z for last entries
    // 2) update paras to point to the next MCB, even if that means lying about the actually allocated size
    // 3) ... unless it's the last (Z) MCB, in which case update paras to reflect reality

    for ( size_t i = 0; i < g_allocEntries.size(); i++ )
    {
        DosAllocation & da = g_allocEntries[i];
        DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) cpu.flat_address( da.segment - 1, 0 );
        
        if ( i == ( g_allocEntries.size() - 1 ) )
        {
            pmcb->header = 'Z';
            pmcb->paras = da.para_length - 1; // mcb has user-known-size. DosAllocation has actual size
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
    DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) cpu.flat_address( segMCB, 0 );

    pmcb->header = 'M'; // mark as not the last in the chain
    pmcb->psp = ( 0 == g_currentPSP ) ? 8 : g_currentPSP; // this is what's expected
    pmcb->paras = paragraphs;
    memset( pmcb->appname, 0, sizeof( pmcb->appname ) );
} //initialize_mcb

void update_mcb_length( uint16_t segMCB, uint16_t paragraphs )
{
    DOSMemoryControlBlock *pmcb = (DOSMemoryControlBlock *) cpu.flat_address( segMCB, 0 );
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
    // I'm looking at you link.exe v5.10 from 1990. QBX and cl.exe run link.exe as a child process, so look for that too.
    // debug.com from DOS v2 is even worse about this.

    uint16_t spaceBetween = ( ends_with( g_acApp, "LINK.EXE" ) || ends_with( g_lastLoadedApp, "LINK.EXE" ) ) ? 0x40 : 0;
    spaceBetween = ( ends_with( g_acApp, "DEBUG.COM" ) || ends_with( g_lastLoadedApp, "DEBUG.COM" ) ) ? 0x60 : spaceBetween;
    if ( ends_with( g_acApp, "ILINK.EXE" ) )
        spaceBetween = 0;

    if ( 0 == cEntries )
    {
        // Microsoft (R) Overlay Linker Version 3.61 with Quick C v 1.0 is a packed EXE that requires /h to be in high memory

        const uint16_t baseSeg = g_PackedFileCorruptWorkaround ? ( 65536 / 16 ) : AppSegment;
        const uint16_t ParagraphsAvailable = g_segHardware - baseSeg - 1;  // hardware starts at 0xb800, apps load at baseSeg, -1 for MCB

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

        uint16_t *pFirstBlockInListOfLists = cpu.flat_address16( SegmentListOfLists, OffsetListOfLists - 2 );
        *pFirstBlockInListOfLists = allocatedSeg;
        tracer.Trace( "  wrote segment of first allocation %04x to list of lists - 2 at %04x:%04x\n", allocatedSeg, SegmentListOfLists, OffsetListOfLists - 2 );
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
            else if ( ( after + request_paragraphs + spaceBetween ) <= g_segHardware )
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
                else if ( ( after + request_paragraphs ) <= g_segHardware )
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
            assert( firstFreeSeg <= g_segHardware );
            largest_block = g_segHardware - firstFreeSeg;
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
    largest_block = g_segHardware - allocatedSeg;
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
        // Turbo Pascal v5.5 exits a process never created except via int21 0x55, which frees that PSP,
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

#ifdef _WIN32
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
#endif

void UpdateScreenCursorPosition( uint8_t row, uint8_t col )
{
    tracer.Trace( "  updating screen cursor position to %d %d\n", row, col );
    assert( g_use80xRowsMode );
#ifdef _WIN32    
    COORD pos = { col, row };
    SetConsoleCursorPosition( g_hConsoleOutput, pos );
#else
    printf( "%c[%d;%dH", 27, row + 1, col + 1 );    // vt-100 row/col are 1-based
    fflush( stdout );
#endif
} //UpdateScreenCursorPosition

void GetCursorPosition( uint8_t & row, uint8_t & col )
{
    uint8_t * cursordata = cpu.flat_address8( 0x40, 0x50 ) + ( GetActiveDisplayPage() * 2 );

    col = cursordata[ 0 ];
    row = cursordata[ 1 ];
} //GetCursorPosition

void SetCursorPosition( uint8_t row, uint8_t col )
{
    uint8_t * cursordata = cpu.flat_address8( 0x40, 0x50 ) + ( GetActiveDisplayPage() * 2 );

    cursordata[ 0 ] = col;
    cursordata[ 1 ] = row;

    if ( g_use80xRowsMode )
        UpdateScreenCursorPosition( row, col );
} //SetCursorPosition

void UpdateScreenCursorPosition()
{
    assert( g_use80xRowsMode );
    uint8_t row, col;
    GetCursorPosition( row, col );
    UpdateScreenCursorPosition( row, col );
} //UpdateScreenCursorPosition

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
#ifdef _WIN32
    val |= ( 0 != isPressed( VK_RSHIFT ) );
    val |= ( isPressed( VK_LSHIFT ) << 1 );
    val |= ( ( isPressed( VK_LCONTROL ) || isPressed( VK_RCONTROL ) ) << 2 );
    val |= ( ( isPressed( VK_LMENU ) || isPressed( VK_RMENU ) ) << 3 ); // alt
    val |= ( keyState( VK_SCROLL ) << 4 );
    val |= ( keyState( VK_NUMLOCK ) << 5 );
    val |= ( keyState( VK_CAPITAL ) << 6 );
    val |= ( keyState( VK_INSERT ) << 7 );
#else
    if ( g_altPressedRecently )
        val |= ( 1 << 3 );
#endif
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
    uint8_t  commandTail[127];       // command line characters after executable, CR terminated

    void TraceHandleMap()
    {
        tracer.Trace( "    handlemap: " );
        for ( size_t x = 0; x < _countof( fileHandles ); x++ )
            tracer.Trace( "%02x ", fileHandles[ x ] );
        tracer.Trace( "\n" );
    }

    void Trace()
    {
        assert( 0x16 == offsetof( DOSPSP, segParent ) );
        assert( 0x5c == offsetof( DOSPSP, firstFCB ) );
        assert( 0x6c == offsetof( DOSPSP, secondFCB ) );
        assert( 0x80 == offsetof( DOSPSP, countCommandTail ) );

        tracer.Trace( "  PSP: %04x\n", GetSegment( (uint8_t *) this ) );
        tracer.TraceBinaryData( (uint8_t *) this, sizeof( DOSPSP ), 4 );
        tracer.Trace( "    topOfMemory: %04x\n", topOfMemory );
        tracer.Trace( "    segParent: %04x\n", segParent );
        tracer.Trace( "    return address: %04x\n", int22TerminateAddress );
        tracer.Trace( "    command tail: len %u, '%.*s'\n", countCommandTail, countCommandTail, commandTail );
        tracer.Trace( "    handleArraySize: %u\n", (uint32_t) handleArraySize );
        tracer.Trace( "    handleArrayPointer: %04x\n", handleArrayPointer );

        TraceHandleMap();

        tracer.Trace( "    segEnvironment: %04x\n", segEnvironment );
        if ( 0 != segEnvironment )
        {
            const char * penv = (char *) cpu.flat_address( segEnvironment, 0 );
            tracer.TraceBinaryData( (uint8_t *) penv, 0x100, 6 );
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
    uint8_t reserved[8];     // unused
    uint8_t curRecord;       // where sequential i/o starts
    uint32_t recNumber;      // where random i/o starts. the high byte is only valid when recSize < 64 bytes. Some apps don't allocate this field for sequential I/O!

    uint32_t BlockSize() { return (uint32_t) recSize * 128; }
    uint32_t SequentialOffset() { return ( ( (uint32_t) curBlock * BlockSize() ) + ( (uint32_t) curRecord * (uint32_t) recSize ) ); }
    uint32_t RandomRecordNumber()
    {
        if ( recSize >= 64 )
            return ( recNumber & 0xffffff );

        return recNumber;
    }
    void SetRandomRecordNumber( uint32_t x )
    {
        memcpy( &recNumber, &x, ( recSize >= 64 ) ? 3 : 4 );
    }
    uint32_t RandomOffset() { return ( RandomRecordNumber() * recSize ); }
    void SetSequentialFromRandom()
    {
        uint32_t o = RandomOffset();
        curBlock = (uint16_t) ( o / BlockSize() );
        curRecord = (uint8_t) ( RandomRecordNumber() % 128 );
        tracer.Trace( "  fcb after SetSequentialFromRandom:\n" );
        Trace();
        tracer.Trace( "  SequentialOffset %u, RandomOffset %u\n", SequentialOffset(), RandomOffset() );
        assert( SequentialOffset() == RandomOffset() );
    } //SetSequentialFromRandom

    void SetRandomFromSequential()
    {
        if ( 0 == recSize ) // an app bug for sure
            return;

        SetRandomRecordNumber( SequentialOffset() / recSize );
        tracer.Trace( "  SequentialOffset %u, RandomOffset %u\n", SequentialOffset(), RandomOffset() );
        assert( SequentialOffset() == RandomOffset() );
    } //SetRandomFromSequential

    void TraceFirst16()
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

        tracer.Trace( "  fcb first 16: %p\n", this );
        tracer.Trace( "    drive        %u\n", drive );
        tracer.Trace( "    filename     '%c%c%c%c%c%c%c%c'\n",
                      name[0],name[1],name[2],name[3],
                      name[4],name[5],name[6],name[7] );
        tracer.Trace( "    filename     %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n",
                      name[0],name[1],name[2],name[3],
                      name[4],name[5],name[6],name[7] );
        tracer.Trace( "    ext          '%c%c%c'\n", ext[0],ext[1],ext[2] );
        tracer.Trace( "    ext          %02x, %02x, %02x\n", ext[0],ext[1],ext[2] );
        tracer.Trace( "    curBlock:    %u\n", curBlock );
        tracer.Trace( "    recSize:     %u\n", recSize );
    } //TraceFirst16

    void TraceFirst24()
    {
        TraceFirst16();
        tracer.Trace( "    fileSize:    %u\n", fileSize );

        uint32_t day = date & 0x1f;
        uint32_t month = ( date >> 5 ) & 0xf;
        uint32_t year = (date >> 9 ) & 0x7f;
        tracer.Trace( "    day:         %u\n", day );
        tracer.Trace( "    month:       %u\n", month );
        tracer.Trace( "    year -1980:  %u\n", year );

        uint32_t secs = 2 * ( time & 0x1f );
        uint32_t minutes = ( time >> 5 ) & 0x3f;
        uint32_t hours = ( time >> 11 ) & 0x1f;
        tracer.Trace( "    secs:        %u\n", secs );
        tracer.Trace( "    minutes:     %u\n", minutes );
        tracer.Trace( "    hours:       %u\n", hours );
    } //TraceFirst24

    void Trace()
    {
        TraceFirst24();

        tracer.Trace( "    curRecord:   %u\n", curRecord );
        tracer.Trace( "    recNumber:   %u\n", RandomRecordNumber() );
    } //Trace
};

struct DOSEXFCB
{
    uint8_t extendedFlag; // 0xff to indicate it's an extended FCB
    uint8_t reserved[ 5 ];
    uint8_t attr;
    DOSFCB fcb;
};
#pragma pack(pop)

uint16_t MapFileHandleCobolHack( uint16_t x )
{
    if ( ends_with( g_acApp, "cobol.exe" ) )
    {
        FILE * fp = FindFileEntry( x );
        if ( 0 == fp )
        {
            // MS cobol v5 reads and writes to the file handle table in the psp.
            // This is undocumented behavior.

            if ( 0x13 == x )
            {
                // grab the handle cobol stuck in the last slot, which it copied
                // from the handle table so it can hard-code handle 0x13.

                DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
                return psp->fileHandles[ 0x13 ];
            }
        }
    }

    return x;
} //MapFileHandleCobolHack

void UpdateHandleMap()
{
    // This updates the file handle table in the PSP to reflect currently open files.
    // The only app I know that needs this is Microsoft COBOL v5, which reads handles
    // from the table and puts them at the end of the list so it can hard-code handle
    // 0x13 for many file operations and not pass the actual handle through.
    // This is undocumented behavior.

    DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
    memset( & ( psp->fileHandles[5] ), 0xff, sizeof( psp->fileHandles ) - 5 );

    size_t cEntries = g_fileEntries.size();
    if ( 0 != cEntries )
    {
        cEntries = get_min( cEntries, _countof( psp->fileHandles ) );

        for ( int i = 0; i < cEntries; i++ )
        {
            FileEntry & fe = g_fileEntries[ i ];
            psp->fileHandles[ fe.handle ] = (uint8_t) fe.handle;
        }
    }

    psp->TraceHandleMap();
} //UpdateHandleMap

const char * GetCurrentAppPath()
{
    DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
    const char * penv = (char *) cpu.flat_address( psp->segEnvironment, 0 );

    size_t len = strlen( penv );
    while ( 0 != len )
    {
        penv += ( 1 + len );
        len = strlen( penv );
    }

    penv += 3; // get past final 0 and count of extra strings.
    return penv;
} //GetCurrentAppPath

bool GetDOSFilenameFromFCB( DOSFCB &fcb, char * filename )
{
    char * orig = filename;

    for ( int i = 0; i < 8; i++ )
    {
        if ( ' ' == fcb.name[i] || 0 == fcb.name[i] )
            break;

        *filename++ = fcb.name[i];
    }

    bool dot_added = false;

    for ( int i = 0; i < 3; i++ )
    {
        if ( ' ' == fcb.ext[i] || 0 == fcb.ext[i] )
            break;

        if ( !dot_added )
        {
            *filename++ = '.';
            dot_added = true;
        }

        *filename++ = fcb.ext[i];
    }

    *filename = 0;

#ifndef _WIN32 
    if ( g_forcePathsUpper )
        strupr( orig );
    else if ( g_forcePathsLower )
        strlwr( orig );
#endif    

    return ( 0 != *orig && '.' != *orig );
} //GetDOSFilenameFromFCB

void ClearLastUpdateBuffer()
{
    memset( g_bufferLastUpdate, 0, sizeof( g_bufferLastUpdate ) );
} //ClearLastUpdateBuffer

void traceDisplayBuffers()
{
    for ( int i = 0; i < 2; i++ )
    {
        tracer.Trace( "  cga memory buffer %d\n", i );
        uint8_t * pbuf = cpu.flat_address8( ScreenBufferSegment, (uint16_t) ( 0x1000 * i ) );
        for ( size_t y = 0; y < GetScreenRows(); y++ )
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

void printDisplayBuffer( int buffer )
{
    printf( "  cga memory buffer %d\n", buffer );
    uint8_t * pbuf = cpu.flat_address8( ScreenBufferSegment, (uint16_t) ( 0x1000 * buffer ) );
    for ( size_t y = 0; y < GetScreenRows(); y++ )
    {
        size_t yoffset = y * ScreenColumns * 2;
        bool blank = true;
        for ( size_t x = 0; x < ScreenColumns; x++ )
        {
            size_t offset = yoffset + x * 2;
            if ( ' ' != printable( pbuf[ offset ] ) )
            {
                blank = false;
                break;
            }
        }

        if ( !blank )
        {
            printf( "    row %02zd: '", y );
            for ( size_t x = 0; x < ScreenColumns; x++ )
            {
                size_t offset = yoffset + x * 2;
                printf( "%c", printable( pbuf[ offset ] ) );
            }
            printf( "'\n" );
        }
    }
} //printDisplayBuffer

void traceDisplayBufferAsHex()
{
    tracer.Trace( "cga memory buffer %d\n" );
    uint8_t * pbuf = GetVideoMem();
    for ( size_t y = 0; y < GetScreenRows(); y++ )
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

static wchar_t awcLowDOSChars[ 32 ] =
{ 
    0,      0x263a, 0x2638, 0x2665, 0x2666, 0x2663, 0x2600, 0x2022, 0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
    0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x2582, 0x25bc,
};

#ifdef _WIN32

void UpdateDisplayRow( uint32_t y )
{
    assert( g_use80xRowsMode );
    if ( y >= GetScreenRows() )
        return;

    uint8_t * pbuf = GetVideoMem();
    uint32_t yoffset = y * ScreenColumns * 2;

    memcpy( g_bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 );
    WORD aAttribs[ ScreenColumns ];
    char ac[ ScreenColumns ];
    for ( size_t x = 0; x < ScreenColumns; x++ )
    {
        size_t offset = yoffset + x * 2;
        ac[ x ] = pbuf[ offset ];
        if ( 0 == ac[ x ] )
            ac[ x ] = ' '; // brief alternately writes 0 then ':' for the clock
        aAttribs[ x ] = pbuf[ 1 + offset ]; 
    }

    WCHAR awcLine[ ScreenColumns ];
    MultiByteToWideChar( 437, 0, ac, ScreenColumns, awcLine, ScreenColumns );

    // CP 437 doesn't handle characters 0 to 31 or 0x7f correctly. 

    for ( size_t x = 0; x < ScreenColumns; x++ )
    {
        if ( awcLine[ x ] < 32 )
            awcLine[ x ] = awcLowDOSChars[ awcLine[ x ] ];
        else if ( 0x7f == awcLine[ x ] )
            awcLine[ x ] = 0x2302;
    }

    #if false
        //tracer.Trace( "    row %02u: '%.80s'\n", y, ac );
        tracer.Trace( "    row %02u: '", y );
        for ( size_t c = 0; c < ScreenColumns; c++ )
            tracer.Trace( "%c", printable( ac[ c ] ) );
        tracer.Trace( "'\n" );
    #endif
    
    COORD pos = { 0, (SHORT) y };
    SetConsoleCursorPosition( g_hConsoleOutput, pos );
    
    BOOL ok = WriteConsoleW( g_hConsoleOutput, awcLine, ScreenColumns, 0, 0 );
    if ( !ok )
        tracer.Trace( "writeconsole failed row %u with error %d\n", y, GetLastError() );
    
    DWORD dwWritten;
    ok = WriteConsoleOutputAttribute( g_hConsoleOutput, aAttribs, ScreenColumns, pos, &dwWritten );
    if ( !ok )
        tracer.Trace( "writeconsoleoutputattribute failed row %u with error %d\n", y, GetLastError() );
} //UpdateDisplayRow

#else

void DecodeAttributes( uint8_t a, uint8_t & fg, uint8_t & bg, bool & intense )
{
    fg = ( a & 7 );
    bg = ( ( a >> 4 ) & 7 );
    intense = ( 0 != ( a & 8 ) );
} //DecodeAttributes

static const uint8_t FGColorMap[ 8 ] =
{
    30, 34, 32, 36, 31, 35, 33, 37,
};

static const uint8_t BGColorMap[ 8 ] =
{
    40, 44, 42, 46, 41, 45, 43, 47,
};

uint8_t MapAsciiArt( uint8_t x )
{
    if ( 0 == x ) // brief alternately writes 0 then ':' for the clock
        return ' ';
    if ( 7 == x ) // round dot for radio buttons
        return '+';
    if ( 0xc4 == x || 0x1a == x || 0x1b == x || 0xcd == x || 0x10 == x )
        return '-';
    if ( 0xb3 == x || 0xba == x || 0x17 == x || 0x18 == x || 0x19 == x )
        return '|';
    if ( 0xda == x || 0xc3 == x || 0xb4 == x || 0xbf == x || 0xd9 == x || 0xc0 == x || 0xd5 == x || 0xb8 == x || 0xc9 == x || 0xbb == x || 
         0xc8 == x || 0xbc == x || 0xd4 == x || 0xbe == x || 0xcb == x || 0xcc == x || 0xca == x || 0xce == x || 0xb9 == x )
        return '+';
    if ( 0xb0 == x || 0xb1 == x || 0xb2 == x || 4 == x || 0xfe == x || 0x12 == x )
        return ' ';
    return x;
} //MapAsciiArt

void UpdateDisplayRow( uint32_t y )
{
    assert( g_use80xRowsMode );
    if ( y >= GetScreenRows() )
        return;

    uint8_t * pbuf = GetVideoMem();
    uint32_t yoffset = y * ScreenColumns * 2;

    memcpy( g_bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 );
    uint8_t aAttribs[ ScreenColumns ];
    char ac[ ScreenColumns ];
    bool sameAttribs = true;
    for ( size_t x = 0; x < ScreenColumns; x++ )
    {
        size_t offset = yoffset + x * 2;
        ac[ x ] = MapAsciiArt( pbuf[ offset ] );
        aAttribs[ x ] = pbuf[ 1 + offset ]; 
        if ( aAttribs[ 0 ] != aAttribs[ x ] )
            sameAttribs = false;
    }

    #if false
        //tracer.Trace( "    updaterow %02u: '%.80s'\n", y, ac );
        tracer.Trace( "    udrow %02u: '", y );
        for ( size_t c = 0; c < ScreenColumns; c++ )
            tracer.Trace( "%c", printable( ac[ c ] ) );
        tracer.Trace( "'\n" );
    #endif

    uint8_t fgRGB, bgRGB;
    bool intense;
    printf( "%c[%d;1H", 27, y + 1 );

    if ( sameAttribs )
    {
        DecodeAttributes( aAttribs[ 0 ], fgRGB, bgRGB, intense );
        printf( "%c[%d;%d;%dm", 27, intense ? 1 : 0, FGColorMap[ fgRGB ], BGColorMap[ bgRGB ] );
        printf( "%.*s", ScreenColumns, ac ); // vt-100 row/col are 1-based
    }
    else
    {
        for ( size_t x = 0; x < ScreenColumns; x++ )
        {
            if ( ( 0 == x ) || ( aAttribs[ x ] != aAttribs[ x - 1 ] ) )
            {
                DecodeAttributes( aAttribs[ x ], fgRGB, bgRGB, intense );
                printf( "%c[%d;%d;%dm", 27, intense ? 1 : 0, FGColorMap[ fgRGB ], BGColorMap[ bgRGB ] );
            }
            putchar( ac[ x ] );
        }
    }
    UpdateScreenCursorPosition();
} //UpdateDisplayRow

#endif

bool UpdateDisplay()
{
    assert( g_use80xRowsMode );
    uint8_t * pbuf = GetVideoMem();

    if ( DisplayUpdateRequired() )
    {
        //tracer.Trace( "UpdateDisplay with changes\n" );
        #if false && _WIN32
            CONSOLE_SCREEN_BUFFER_INFOEX csbi = { 0 };
            csbi.cbSize = sizeof( csbi );
            GetConsoleScreenBufferInfoEx( g_hConsoleOutput, &csbi );

            tracer.Trace( "  UpdateDisplay: pbuf %p, csbi size %d %d, window %d %d %d %d\n",
                          pbuf, csbi.dwSize.X, csbi.dwSize.Y,
                          csbi.srWindow.Left, csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom );
        #endif

        for ( uint32_t y = 0; y < GetScreenRows(); y++ )
        {
            uint32_t yoffset = y * ScreenColumns * 2;
            if ( memcmp( g_bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 ) )
                UpdateDisplayRow( y );
        }

        //if ( tracer.IsEnabled() )
        //    traceDisplayBufferAsHex();
        //traceDisplayBuffers();
        UpdateScreenCursorPosition(); // restore cursor position to where it was before
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

    //tracer.Trace( "  not updating display; throttled\n" );
    return false;
} //throttled_UpdateDisplay

void ClearDisplay()
{
    assert( g_use80xRowsMode );
    uint8_t * pbuf = GetVideoMem();

    for ( size_t y = 0; y < GetScreenRows(); y++ )
        memcpy( pbuf + ( y * 2 * ScreenColumns ), blankLine, sizeof( blankLine ) );
} //ClearDisplay

#ifdef _WIN32
    BOOL WINAPI ControlHandlerProc( DWORD fdwCtrlType )
    {
        // this happens in a third thread, which is implicitly created
    
        if ( CTRL_C_EVENT == fdwCtrlType )
        {
            tracer.Trace( "ControlHandlerProc is incrementing the ^c count\n ");
            InterlockedIncrement( &g_injectedControlC );
            g_SendControlCInt = true;
            return TRUE;
        }
    
        return FALSE;
    } //ControlHandlerProc
#else
    void ControlHandlerProc( int signal ) {}
#endif

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
   { 0x12, 0, "memory size determination" },
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
    { 0x10, 0x0e, "write text in teletype mode" },
    { 0x10, 0x0f, "get video mode" },
    { 0x10, 0x10, "set palette registers" },
    { 0x10, 0x11, "character generator ega" },
    { 0x10, 0x12, "alternate select ega/vga" },
    { 0x10, 0x13, "write character string" },
    { 0x10, 0x14, "lcd handler" },
    { 0x10, 0x15, "return physical display characteristics" },
    { 0x10, 0x1a, "get/set video display combination" },
    { 0x10, 0x1b, "video functionality/state information" },
    { 0x10, 0x1c, "save/restore video state" },
    { 0x10, 0xef, "hercules -- get video adapter type and mode" },
    { 0x10, 0xfa, "ega register interface library" },
    { 0x10, 0xfe, "(topview) get video buffer" },
    { 0x10, 0xff, "(topview) update real screen from video buffer" },
    { 0x14, 0x01, "serial i/o transmit character" },
    { 0x14, 0x02, "serial i/o receive character" },
    { 0x16, 0x00, "get character" },
    { 0x16, 0x01, "keyboard status" },
    { 0x16, 0x02, "keyboard - get shift status" },
    { 0x16, 0x05, "keyboard - store keystroke in keyboard buffer" },
    { 0x16, 0x10, "get character" },
    { 0x16, 0x11, "get enhanced keystroke" },
    { 0x16, 0x55, "microsoft TSR internal (al ff/00 word, fe qbasic)" },
    { 0x17, 0x02, "check printer status" },
    { 0x1a, 0x00, "read real time clock" },
    { 0x1a, 0x02, "get real-time clock time" },
    { 0x21, 0x00, "exit app" },
    { 0x21, 0x01, "keyboard input with echo" },
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
    { 0x21, 0x24, "set relative record field in FCB" },
    { 0x21, 0x25, "set interrupt vector" },
    { 0x21, 0x26, "create new PSP" },
    { 0x21, 0x27, "random block read using FCB" },
    { 0x21, 0x28, "random block write using FCBs" },
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
    { 0x21, 0xdd, "novell netware 4.0 - set error mode" },
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

#ifdef _WIN32
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

    // if the buffer is full, stop consuming new characters
    CKbdBuffer kbd_buf;
    if ( kbd_buf.IsFull() )
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
        if ( falt ) { scancode = 0x4e; asciiChar = 0; }
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
    else if ( 0x52 == sc ) // INS
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

    if ( g_keyStrokes.KeystrokeAvailable() )
    {
        uint16_t x = g_keyStrokes.Peek();
        asciiChar = x & 0xff;
        scancode = x >> 8;
        return true;
    }

    if ( 0 != g_injectedControlC )
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
            tracer.Trace( "    peeked ascii %02x = '%c', scancode %02x\n", asciiChar, printable( asciiChar ), scancode );
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
        if ( update_display && g_use80xRowsMode && _durationLastUpdate.HasTimeElapsedMS( 333 ) )
            UpdateDisplay();

        if ( sleep_on_throttle )
        {
            tracer.Trace( "sleeping in peek_keyboard\n" );
            WaitForSingleObject( g_heventKeyStroke, 1 );
        }

        return false;
    }

    uint8_t a, s;
    return peek_keyboard( a, s );
} //peek_keyboard

void InjectKeystrokes()
{
    CKbdBuffer kbd_buf;
    while ( g_keyStrokes.KeystrokeAvailable() && !kbd_buf.IsFull() )
    {
        uint16_t x = g_keyStrokes.ConsumeNext();
        tracer.Trace( "injecting keystroke %04x from log file\n", x );
        kbd_buf.Add( x & 0xff, x >> 8 );
    }

    while ( ( 0 != InterlockedCompareExchange( & g_injectedControlC, 0, 0 ) ) && !kbd_buf.IsFull() ) // largeish hack for ^c handling
    {
        tracer.Trace( "injecting controlc count %d\n", InterlockedCompareExchange( & g_injectedControlC, 0, 0 ) );
        InterlockedDecrement( &g_injectedControlC );
        kbd_buf.Add( 0x03, 0x2e );
    }
} //InjectKeystrokes

void consume_keyboard()
{
    // this mutex is because I don't know if PeekConsoleInput and ReadConsoleInput are individually or mutually reenterant.
    lock_guard<mutex> lock( g_mtxEverything );

    InjectKeystrokes();
    CKbdBuffer kbd_buf;

    // An extra int9 may have been triggered after all input events have been consumed.
    // Check GetNumberOfConsoleInputEvents before ReadConsoleInput, because the latter will block for a keystroke

    DWORD available = 0;
    BOOL ok = GetNumberOfConsoleInputEvents( g_hConsoleInput, &available );
    if ( ok && ( 0 != available ) )
    {
        INPUT_RECORD records[ 10 ];
        uint32_t toRead = get_min( (uint32_t) available, kbd_buf.FreeSpots() );
        toRead = get_min( (uint32_t) _countof( records ), toRead );
        if ( toRead > 0 )
        {
            tracer.Trace( "    %llu consume_keyboard calling ReadConsole\n", time_since_last() );
            DWORD numRead = 0;
            ok = ReadConsoleInput( g_hConsoleInput, records, toRead, &numRead );
            tracer.Trace( "    %llu consume_keyboard ReadConsole returned %d, %d events\n", time_since_last(), ok, numRead );
            if ( ok )
            {
                uint8_t asciiChar = 0, scancode = 0;
                for ( DWORD x = 0; x < numRead; x++ )
                {
                    bool used = process_key_event( records[ x ], asciiChar, scancode );
                    if ( !used )
                        continue;
        
                    tracer.Trace( "    consumed ascii %02x, scancode %02x\n", asciiChar, scancode );
                    kbd_buf.Add( asciiChar, scancode );
                }
            }
        }
    }
} //consume_keyboard

#else

void InjectKeystrokes()
{
} //InjectKeystrokes

const uint8_t ascii_to_scancode[ 128 ] =
{
      0,  30,  48,  46,  32,  18,  33,  34, // 0
     35,  15,  28,  37,  38,  28,  49,  24, // 8   note: 10 should be 36 for ^j, but it's overloaded on Linux
     25,  16,  19,  31,  20,  22,  47,  17, // 16
     45,  21,  44,   1,  43,  27,   0,   0, // 24
     57,   2,  40,   4,   5,   6,   8,  40, // 32
     10,  11,   9,  13,  51,  12,  52,  53, // 40
     11,   2,   3,   4,   5,   6,   7,   8, // 48
      9,  10,  39,  39,  51,  13,  52,  53, // 56
      3,  30,  48,  46,  32,  18,  33,  34, // 64
     35,  23,  36,  37,  38,  50,  49,  24, // 72
     25,  16,  19,  31,  20,  22,  47,  17, // 80
     45,  21,  44,  26,  43,  27,   7,  12, // 88
     41,  30,  48,  46,  32,  18,  33,  34, // 96
     35,  23,  36,  37,  38,  50,  49,  24, // 104
     25,  16,  19,  31,  20,  22,  47,  17, // 112
     45,  21,  24,  26,  43,  27,  41,  14, // 120
};     

// broken cases on Linux:
// - no way to determine if keypad + is from the keypad and not the plus key (Brief is sad)
// - linux translates ^j as ascii 0xa instead of 0x6a, so ^j output is wrong for DOS apps.
// - no global way to track if the ALT key is currently down, so the hacks below with g_altPressedRecently.
// - ^[ comes through as just an ESC with no indication that the [ key was pressed
// - this is perhaps the ugliest code ever; I must refactor it.
// - ^; comes through as a plain ESC
// helpful: http://www.osfree.org/docs/cmdref/cmdref.2.0476.php

void consume_keyboard()
{
    const uint8_t CTRL_DOWN = 53;
    const uint8_t ALT_DOWN = 51;
    const uint8_t SHIFT_DOWN = 50;
    const uint8_t MODIFIER_DOWN = 59;
    CKbdBuffer kbd_buf;
    g_altPressedRecently = false;

    while ( g_consoleConfig.portable_kbhit() )
    {
        uint8_t asciiChar = 0xff & g_consoleConfig.portable_getch();
        uint8_t scanCode = ascii_to_scancode[ asciiChar ];
        tracer.Trace( "    consumed ascii %02x, scancode %02x\n", asciiChar, scanCode );
        if ( 27 == asciiChar ) // escape
        {
            if ( g_consoleConfig.portable_kbhit() )
            {
                uint8_t secondAscii = g_consoleConfig.portable_getch();
                tracer.Trace( "    secondAscii: '%c' == %d\n", secondAscii, secondAscii );
                if ( '[' == secondAscii )
                {
                    if ( g_consoleConfig.portable_kbhit() )
                    {
                        uint8_t thirdAscii = g_consoleConfig.portable_getch();
                        tracer.Trace( "    thirdAscii: '%c' == %d\n", thirdAscii, thirdAscii );
                        if ( 'D' == thirdAscii )
                            kbd_buf.Add( 0, 75 ); // left arrow
                        else if ( 'B' == thirdAscii )
                            kbd_buf.Add( 0, 80 ); // down arrow
                        else if ( 'C' == thirdAscii )
                            kbd_buf.Add( 0, 77 ); // right arrow
                        else if ( 'A' == thirdAscii )
                            kbd_buf.Add( 0, 72 ); // up arrow
                        else if ( '1' == thirdAscii ) // F5-F8
                        {
                            uint8_t fnumber = g_consoleConfig.portable_getch();
                            tracer.Trace( "    f5-f8 fnumber: %d\n", fnumber );
                            uint8_t following = g_consoleConfig.portable_getch(); // discard the following character
                            tracer.Trace( "    following character: %d\n", following );

                            if ( MODIFIER_DOWN == following ) // ALT/CTRL depressed for F5-F8
                            {
                                int nextA = g_consoleConfig.portable_getch(); // consume yet more
                                tracer.Trace( "    nextA: %d\n", nextA );

                                if ( 53 == fnumber )
                                {
                                    int nextB = g_consoleConfig.portable_getch(); // consume yet more
                                    tracer.Trace( "    nextB: %d\n", nextB );
                                    if ( CTRL_DOWN == nextA )
                                        kbd_buf.Add( 0, 98 ); // CTRL
                                    else if ( ALT_DOWN == nextA )
                                    {
                                        kbd_buf.Add( 0, 108 ); // ALT
                                        g_altPressedRecently = true;
                                    }
                                    else if ( SHIFT_DOWN == nextA )
                                        kbd_buf.Add( 0, 88 ); // F5
                                }
                                else if ( fnumber >= 55 && fnumber <= 57 )
                                {
                                    int nextB = g_consoleConfig.portable_getch(); // consume yet more
                                    if ( CTRL_DOWN == nextA )
                                        kbd_buf.Add( 0, fnumber + 44 ); // CTRL
                                    else if ( ALT_DOWN == nextA )
                                    {
                                        kbd_buf.Add( 0, fnumber + 54 ); // ALT
                                        g_altPressedRecently = true;
                                    }
                                    else if ( SHIFT_DOWN == nextA )
                                        kbd_buf.Add( 0, fnumber + 34 ); // SHIFT
                                }
                            }
                            else if ( ALT_DOWN == following ) // ALT depressed for F1-F4, HOME, etc.
                            {
                                g_altPressedRecently = true;
                                int next = g_consoleConfig.portable_getch();
                                tracer.Trace( "    next: %d\n", next );
                                if ( next >= 80 && next <= 83 ) // F1-F4
                                    kbd_buf.Add( 0, next + 24 );
                                else if ( 70 == next ) // END
                                    kbd_buf.Add( 0, 159 );
                                else if ( 72 == next ) // HOME
                                    kbd_buf.Add( 0, 151 );
                                else if ( 68 == next ) // LEFT
                                    kbd_buf.Add( 0, 155 );
                                else if ( 66 == next ) // DOWN
                                    kbd_buf.Add( 0, 160 );
                                else if ( 67 == next ) // RIGHT
                                    kbd_buf.Add( 0, 157 );
                                else if ( 65 == next ) // UP
                                    kbd_buf.Add( 0, 152 );
                                else
                                    tracer.Trace( "    unhandled ALT F1-F4 etc.\n" );
                            }
                            else if ( CTRL_DOWN == following ) // CTRL depressed for F1-F4, etc.
                            {
                                int next = g_consoleConfig.portable_getch();
                                tracer.Trace( "    next: %d\n", next );
                                if ( 68 == next ) // left
                                    kbd_buf.Add( 0, 115 );
                                else if ( 66 == next ) // down
                                    kbd_buf.Add( 0, 145 );
                                else if ( 67 == next ) // right
                                    kbd_buf.Add( 0, 116 );
                                else if ( 65 == next ) // up
                                    kbd_buf.Add( 0, 141 );
                                else if ( 72 == next ) // home
                                    kbd_buf.Add( 0, 119 );
                                else if ( 70 == next ) // end
                                    kbd_buf.Add( 0, 117 );
                                else if ( next >= 80 && next <= 83) // F1-F4
                                    kbd_buf.Add( 0, next + 14 );
                            }
                            else if ( SHIFT_DOWN == following ) // SHIFT pressed for F1-F4, etc.
                            {
                                int next = g_consoleConfig.portable_getch();
                                tracer.Trace( "    next: %d\n", next );
                                if ( 68 == next ) // left
                                    kbd_buf.Add( 52, 75 );
                                else if ( 66 == next ) // down
                                    kbd_buf.Add( 50, 80 );
                                else if ( 67 == next ) // right
                                    kbd_buf.Add( 54, 77 );
                                else if ( 65 == next ) // up
                                    kbd_buf.Add( 56, 72 );
                                else if ( 72 == next ) // home
                                    kbd_buf.Add( 55, 71 );
                                else if ( 70 == next ) // end
                                    kbd_buf.Add( 49, 79 );
                                else if ( next >= 80 && next <= 83) // F1-F4
                                    kbd_buf.Add( 0, next + 4 );
                            }
                            else
                            {
                                if ( 53 == fnumber )
                                    kbd_buf.Add( 0, 63 ); // F5
                                else if ( fnumber >= 55 && fnumber <= 57 ) // F6..F8
                                    kbd_buf.Add( 0, fnumber + 9 );
                            }
                        }
                        else if ( '2' == thirdAscii ) // INS + F9-F12
                        {
                            uint8_t fnumber = g_consoleConfig.portable_getch();
                            tracer.Trace( "    ins + f9-f12 fnumber: %d\n", fnumber );
                            if ( 126 == fnumber )
                                kbd_buf.Add( 0, 82 ); // INS
                            else
                            {
                                int next = 0;
                                if ( fnumber < 121 )
                                    next = g_consoleConfig.portable_getch();

                                tracer.Trace( "    next %d\n", next );

                                if ( MODIFIER_DOWN == next ) // ALT/CTRL is pressed
                                {
                                    int nextA = g_consoleConfig.portable_getch();
                                    int nextB = g_consoleConfig.portable_getch();
                                    tracer.Trace( "    nextA: %d, nextB: %d\n", nextA, nextB );

                                    if ( ALT_DOWN == nextA ) // ALT
                                    {
                                        g_altPressedRecently = true;
                                        if ( 48 == fnumber )
                                            kbd_buf.Add( 0, 112 );
                                        else if ( 49 == fnumber )
                                            kbd_buf.Add( 0, 113 );
                                        else if ( 51 == fnumber )
                                            kbd_buf.Add( 0, 139 );
                                        else if ( 52 == fnumber )
                                            kbd_buf.Add( 0, 140 );
                                        else
                                            tracer.Trace( "unknown ESC [ 2 escape sequence %d\n", fnumber );
                                    }
                                    else if ( CTRL_DOWN == nextA ) // CTRL
                                    {
                                        if ( 48 == fnumber )
                                            kbd_buf.Add( 0, 102 );
                                        else if ( 49 == fnumber )
                                            kbd_buf.Add( 0, 103 );
                                        else if ( 51 == fnumber )
                                            kbd_buf.Add( 0, 137 );
                                        else if ( 52 == fnumber )
                                            kbd_buf.Add( 0, 138 );
                                        else
                                            tracer.Trace( "unknown ESC [ 2 escape sequence %d\n", fnumber );
                                    }
                                    else if (SHIFT_DOWN == nextA )
                                    {
                                        if ( 48 == fnumber )
                                            kbd_buf.Add( 0, 92 );
                                        else if ( 49 == fnumber )
                                            kbd_buf.Add( 0, 93 );
                                        else if ( 51 == fnumber )
                                            kbd_buf.Add( 0, 135 );
                                        else if ( 52 == fnumber )
                                            kbd_buf.Add( 0, 136 );
                                        else
                                            tracer.Trace( "unknown ESC [ 2 escape sequence %d\n", fnumber );
                                    }
                                }
                                else if ( 51 == next )
                                {
                                    uint8_t nextA = g_consoleConfig.portable_getch();
                                    tracer.Trace( "    nextA: %d\n", nextA );
                                    kbd_buf.Add( 0, 162 ); // ALT + INS
                                    g_altPressedRecently = true;
                                }
                                else if ( 53 == next )
                                {
                                    uint8_t nextA = g_consoleConfig.portable_getch();
                                    tracer.Trace( "    nextA: %d\n", nextA );
                                    kbd_buf.Add( 0, 146 ); // INS
                                }
                                else if ( 126 == next )
                                {
                                    if ( 48 == fnumber )
                                        kbd_buf.Add( 0, 67 );
                                    else if ( 49 == fnumber )
                                        kbd_buf.Add( 0, 68 );
                                    else if ( 51 == fnumber )
                                        kbd_buf.Add( 0, 133 );
                                    else if ( 52 == fnumber )
                                        kbd_buf.Add( 0, 134 );
                                    else
                                        tracer.Trace( "unknown ESC [ 2 escape sequence %d\n", fnumber );
                                }
                                else
                                    tracer.Trace( "unknown next %d\n", next );
                            }
                        }                       
                        else if ( '3' == thirdAscii ) // DEL
                        {
                            uint8_t nextA = g_consoleConfig.portable_getch();
                            tracer.Trace( "    nextA for DEL: %d\n", nextA );
                            if ( MODIFIER_DOWN == nextA )
                            {
                                uint8_t nextB = g_consoleConfig.portable_getch();
                                uint8_t nextC = g_consoleConfig.portable_getch();
                                tracer.Trace( "    DEL nextB %d, nextC %d\n", nextB, nextC );
                                if ( ALT_DOWN == nextB )
                                {
                                    kbd_buf.Add( 0, 163 ); // ALT + DEL
                                    g_altPressedRecently = true;
                                }
                                else if ( CTRL_DOWN == nextB )
                                    kbd_buf.Add( 0, 147 ); // CTRL + DEL
                                else if ( SHIFT_DOWN == nextB )
                                    kbd_buf.Add( 46, 83 );
                            }
                            else if ( 126 == nextA )
                                kbd_buf.Add( 0, 83 );
                            else
                                tracer.Trace( "unknown nextA %d\n", nextA );
                        }
                        else if ( '5' == thirdAscii ) // pgup
                        {
                            uint8_t nextA = g_consoleConfig.portable_getch(); 
                            tracer.Trace( "    nextA for pgup: %d\n", nextA );
                            if ( MODIFIER_DOWN == nextA )
                            {
                                uint8_t nextB = g_consoleConfig.portable_getch();
                                uint8_t nextC = g_consoleConfig.portable_getch();
                                tracer.Trace( "    nextB: %d, nextC %d\n", nextB, nextC );
                                if ( CTRL_DOWN == nextB ) // CTRL
                                    kbd_buf.Add( 0, 132 );
                                else if ( ALT_DOWN == nextB ) //ALT
                                {
                                    kbd_buf.Add( 0, 153 ); // ALT + pgup
                                    g_altPressedRecently = true;
                                }
                                else if ( SHIFT_DOWN == nextB )
                                    kbd_buf.Add( 57, 73 );
                            }
                            else
                                kbd_buf.Add( 0, 73 );
                        }
                        else if ( '6' == thirdAscii ) // pgdown
                        {
                            uint8_t nextA = g_consoleConfig.portable_getch(); 
                            tracer.Trace( "    nextA for pgup: %d\n", nextA );
                            if ( MODIFIER_DOWN == nextA )
                            {
                                uint8_t nextB = g_consoleConfig.portable_getch();
                                uint8_t nextC = g_consoleConfig.portable_getch();
                                tracer.Trace( "    nextB: %d, nextC %d\n", nextB, nextC );
                                if ( CTRL_DOWN == nextB ) // CTRL
                                    kbd_buf.Add( 0, 118 );
                                else if ( ALT_DOWN == nextB ) // ALT
                                {
                                    kbd_buf.Add( 0, 161 ); // ALT + pgdown
                                    g_altPressedRecently = true;
                                }
                                else if ( SHIFT_DOWN == nextB )
                                    kbd_buf.Add( 51, 81 );
                            }
                            else
                                kbd_buf.Add( 0, 81 );
                        }
                        else if ( 'H' == thirdAscii ) // home
                            kbd_buf.Add( 0, 71 );
                        else if ( 'F' == thirdAscii ) // end
                            kbd_buf.Add( 0, 79 );
                        else if ( 'Z' == thirdAscii ) // shift tab
                            kbd_buf.Add( 0, 15 );
                        else
                            tracer.Trace( "unknown [ ESC sequence char %d == '%c'\n", thirdAscii, thirdAscii );
                    }
                    else
                    {
                        kbd_buf.Add( 0, 26 ); // ALT '['
                        g_altPressedRecently = true;
                    }
                }
                else if ( secondAscii <= 'z' && secondAscii >= 'a' )
                {
                    // ALT + a through z 

                    // somewhat massive hack because I don't know how to tell if ALT is pressed on Linux
                    g_altPressedRecently = true;
                    scanCode = ascii_to_scancode[ secondAscii - 'a' + 1 ];
                    kbd_buf.Add( 0, scanCode );
                }
                else if ( 'O' == secondAscii ) // F1-F4
                {
                    uint8_t fnumber = g_consoleConfig.portable_getch();
                    tracer.Trace( "f1-f4 fnumber: %d\n", fnumber );
                    
                    if ( fnumber >= 80 && fnumber <= 83 ) // 80-83 map to scancode 59-62
                        kbd_buf.Add( 0, fnumber - 21 );
                    else if ( 65 == fnumber )
                        kbd_buf.Add( 0, 0x48 ); // up
                    else if ( 66 == fnumber )
                        kbd_buf.Add( 0, 0x50 ); // down
                    else if ( 67 == fnumber )
                        kbd_buf.Add( 0, 0x4d ); // right
                    else if ( 68 == fnumber )
                        kbd_buf.Add( 0, 0x4b ); // left
                    else
                        tracer.Trace( "unknown ESC O fnumber %d\n", fnumber );
                }
                else if ( '\\' == secondAscii )
                {
                    kbd_buf.Add( 0, 38 ); // ALT '\\'
                    g_altPressedRecently = true;
                }
                else if ( ';' == secondAscii )
                {
                    kbd_buf.Add( 0, 39 ); // ALT ';'
                    g_altPressedRecently = true;
                }
                else if ( ']' == secondAscii )
                {
                    kbd_buf.Add( 0, 27 ); // ALT ']'
                    g_altPressedRecently = true;
                }
                else if ( '-' == secondAscii )
                {
                    kbd_buf.Add( 0, 130 ); // ALT '-' (normal and numeric keypad, can't distinguish)
                    g_altPressedRecently = true;
                }
                else if ( '=' == secondAscii )
                {
                    kbd_buf.Add( 0, 131 ); // ALT '='
                    g_altPressedRecently = true;
                }
                else if ( '*' == secondAscii )
                {
                    kbd_buf.Add( 0, 55 ); // ALT '*'
                    g_altPressedRecently = true;
                }
                else if ( 127 == secondAscii ) // ALT + DEL
                {
                    kbd_buf.Add( 0, 14 );
                    g_altPressedRecently = true;
                }
                else if ( '+' == secondAscii ) // ALT + numeric keypad +
                {
                    kbd_buf.Add( 0, 78 );
                    g_altPressedRecently = true;
                }
                else if ( ',' == secondAscii || '.' == secondAscii || '/' == secondAscii || '\'' == secondAscii || '`' == secondAscii )
                    tracer.Trace( "  swallowing ALT + character '%c' == %d\n", secondAscii, secondAscii );
                else
                {
                    tracer.Trace( "unknown ESC second character %d == '%c'\n", secondAscii, secondAscii );
                    kbd_buf.Add( asciiChar, scanCode );
                    kbd_buf.Add( secondAscii, ascii_to_scancode[ secondAscii ] );
                }
            }
            else
            {
                tracer.Trace( "  no character following ESC\n" );
                kbd_buf.Add( asciiChar, 1 ); // plain old escape character
            }
        }
#if 0
        else if ( 8 == asciiChar ) // swap backspace with ^backspace
            kbd_buf.Add( 127, 14 );
#endif
        else if ( 127 == asciiChar )
            kbd_buf.Add( 8, 14 ); // swap backspace with ^backspace
        else
        {
            if ( 0x3 == asciiChar && 0x2e == scanCode )
                g_SendControlCInt = true;
            kbd_buf.Add( asciiChar, scanCode );
        }
    }

    uint8_t * pbiosdata = cpu.flat_address8( 0x40, 0 );
    pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();
} //consume_keyboard

bool peek_keyboard( uint8_t & asciiChar, uint8_t & scancode )
{
    if ( g_consoleConfig.portable_kbhit() )
    {
        if ( g_UseOneThread )
            g_KbdPeekAvailable = true; // make sure an int9 gets scheduled

        asciiChar = 'a'; // not sure how to peek and not consume the character on linux, so lie
        scancode = 30;
        return true;
    }
    return false;
} //peek_keyboard

bool peek_keyboard( bool throttle = false, bool sleep_on_throttle = false, bool update_display = false )
{
    static CDuration _durationLastPeek;
    static CDuration _durationLastUpdate;

    if ( throttle && !_durationLastPeek.HasTimeElapsedMS( 100 ) )
    {
        if ( update_display && g_use80xRowsMode && _durationLastUpdate.HasTimeElapsedMS( 333 ) )
            UpdateDisplay();

        if ( sleep_on_throttle )
        {
            tracer.Trace( "sleeping in peek_keyboard\n" );
            sleep_ms( 1 );
        }

        return false;
    }

    uint8_t a, s;
    return peek_keyboard( a, s );
} //peek_keyboard

#endif

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
        tracer.Trace( "  invoke_in_al port 60 keyboard data\n" );
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

    tracer.Trace( "  invoke_in_al, port %02x returning 0\n", port );
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

    if ( 0x20 == port && 0x20 == val ) // End Of Interrupt to 8259A PIC. Enable subsequent interrupts
        g_int9_pending = false;
} //i8086_invoke_out_al

void i8086_invoke_out_ax( uint16_t port, uint16_t val )
{
    tracer.Trace( "invoke_out_ax port %#x, val %#x\n", port, val );
} //i8086_invoke_out_ax

void i8086_invoke_halt()
{
    g_haltExecution = true;
} // i8086_invoke_halt

bool starts_with( const char * str, const char * start )
{
    size_t len = strlen( str );
    size_t lenstart = strlen( start );

    if ( len < lenstart )
        return false;

    for ( int i = 0; i < lenstart; i++ )
        if ( toupper( str[ i ] ) != toupper( start[ i ] ) )
            return false;

    return true;
} //starts_with

#ifdef _WIN32
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
        FileTimeToDos( fd.ftLastWriteTime, pfcb->time, pfcb->date );
    
        pfcb->TraceFirst24();
        return true;
    } //ProcessFoundFileFCB
    
    static HANDLE g_hFindFirst = INVALID_HANDLE_VALUE;

    void CloseFindFirst()
    {
        if ( INVALID_HANDLE_VALUE != g_hFindFirst )
        {
            FindClose( g_hFindFirst );
            g_hFindFirst = INVALID_HANDLE_VALUE;
        }
    } //CloseFindFirst

#else

    #include <dirent.h>
    static DIR * g_FindFirst = 0;
    static char g_acFindFirstFolder[ MAX_PATH ] = {0};
    static char g_acFindFirstPattern[ MAX_PATH ] = {0};
    struct LINUX_FIND_DATA
    {
        char cFileName[ MAX_PATH ];
    };

    // regex code found on stackoverflow from pooya13

    std::regex wildcardToRegex( const std::string& wildcard, bool caseSensitive = true )
    {
        std::string regexString( wildcard );
        regexString = std::regex_replace( regexString, std::regex("\\\\"), "\\\\" );
        regexString = std::regex_replace( regexString, std::regex("\\^"), "\\^" );
        regexString = std::regex_replace( regexString, std::regex("\\."), "\\." );
        regexString = std::regex_replace( regexString, std::regex("\\$"), "\\$" );
        regexString = std::regex_replace( regexString, std::regex("\\|"), "\\|" );
        regexString = std::regex_replace( regexString, std::regex("\\("), "\\(" );
        regexString = std::regex_replace( regexString, std::regex("\\)"), "\\)" );
        regexString = std::regex_replace( regexString, std::regex("\\{"), "\\{" );
        regexString = std::regex_replace( regexString, std::regex("\\{"), "\\}" );
        regexString = std::regex_replace( regexString, std::regex("\\["), "\\[" );
        regexString = std::regex_replace( regexString, std::regex("\\]"), "\\]" );
        regexString = std::regex_replace( regexString, std::regex("\\+"), "\\+" );
        regexString = std::regex_replace( regexString, std::regex("\\/"), "\\/" );

        // Convert wildcard specific chars * and ? to their regex equivalents:

        regexString = std::regex_replace( regexString, std::regex("\\?"), "." );
        regexString = std::regex_replace( regexString, std::regex("\\*"), ".*" );

        return std::regex( regexString, caseSensitive ? std::regex_constants::ECMAScript : std::regex_constants::icase );
    } //wildcardToRegex

    bool wildMatch( const std::string & input, const std::string & wildcard )
    {
        // very expensive to initialize this every time, but it's an emulator doing disk I/O, so it's OK

        std::string wc = wildcard;
        const char * pwildcard = wildcard.c_str();
        char ac[ 20 ];

        // in DOS, ???????? for filename and ??? for extension are really *, not literally matching that number of characters

        if ( !strcmp( pwildcard, "????????.???" ) )
            wc = "*.*";
        else if ( starts_with( pwildcard, "????????." ) )
        {
            strcpy( ac, "*." );
            strcat( ac, pwildcard + 9);
            wc = ac;
        }
        else if ( ends_with ( pwildcard, ".???" ) )
        {
            strcpy( ac, pwildcard );
            char * pdot = strchr( ac, '.' );
            strcpy( pdot, ".*" );
            wc = ac;
        }

        if ( !strcmp( wc.c_str(), "*.*" ) &&
             ( !strcmp( input.c_str(), "." ) || !strcmp( input.c_str(), ".." ) ) )
             return true;

        tracer.Trace( "  modified wildcard from '%s' to '%s'\n", wildcard.c_str(), wc.c_str() );
        std::regex rgx = wildcardToRegex( wc, false );
        return std::regex_match( input, rgx );
    } //wildMatch

    void tmTimeToDos( long sec, uint16_t & dos_time, uint16_t & dos_date )
    {
        struct tm lt;
        localtime_r( &sec, &lt );
        uint16_t _mday = lt.tm_mday;
        uint16_t _mon = 1 + lt.tm_mon;
        uint16_t _year = 1900 + lt.tm_year - 1980;
        dos_date = _mday | ( _mon << 5 ) | ( _year << 9 );
    
        uint16_t _hour = lt.tm_hour;
        uint16_t _min = lt.tm_min; 
        uint16_t _sec = lt.tm_sec; // 2-second granularity not enforced
        dos_time = _sec | ( _min << 5 ) | ( _hour << 11 );

        tracer.Trace( "  tmTimeToDos: lt_tm_mday %u, lt.tm_mon %u, lt.tm_year %u\n", lt.tm_mday, lt.tm_mon, lt.tm_year );
        tracer.Trace( "  tmTimeToDos: _mday %u _mon %u _year %u\n", _mday, _mon, _year );
    } //tmTimeToDos

    bool ProcessFoundFile( DosFindFile * pff, LINUX_FIND_DATA & fd )
    {
        const char * linuxPath = fd.cFileName;
        struct stat statbuf;
        int ret = stat( linuxPath, & statbuf );
        if ( 0 != ret )
            return false;

        uint8_t matching_attr = 0;
        if ( !S_ISREG( statbuf.st_mode ) )
            matching_attr |= 0x10; // directory

        // return normal files always and any of the 3 special classes if their bit is set in search_attributes

        if ( ( 0 != matching_attr ) && ( 0 == ( matching_attr & pff->search_attributes ) ) )
        {
            tracer.Trace( "  file '%s' doesn't match the attribute filter %#x\n", fd.cFileName, pff->search_attributes );
            return false;
        }

        char * justFilename = fd.cFileName;
        char * slash = strrchr( justFilename, '/' );
        if ( 0 != slash )
            justFilename = slash + 1;

        tracer.Trace( "  actual found filename: '%s'\n", justFilename );
        if ( strlen( justFilename ) < _countof( pff->file_name ) )
            strcpy( pff->file_name, justFilename );
        else
            return false;

        _strupr( pff->file_name );
        pff->file_size = statbuf.st_size;
        pff->file_attributes = matching_attr;
#ifdef __APPLE__
        tmTimeToDos( statbuf.st_mtimespec.tv_sec, pff->file_time, pff->file_date );
#else
        tmTimeToDos( statbuf.st_mtim.tv_sec, pff->file_time, pff->file_date );
#endif
        tracer.Trace( "  search found '%s', size %u, attributes %#x\n", pff->file_name, pff->file_size, pff->file_attributes );
        return true;
    } //ProcessFoundFile

    bool ProcessFoundFileFCB( LINUX_FIND_DATA & fd, uint8_t attr, bool exFCB )
    {
        // note: extended FCB code isn't well-tested. Multiplan setup.exe is the only app I know that uses it and it fails for other reasons
        // non-extended FCBs will have attr = 0

        attr &= ~ ( 8 ); // remove the volume label bit if set
        const char * linuxPath = fd.cFileName;
        struct stat statbuf;
        int ret = stat( linuxPath, & statbuf );
        if ( 0 != ret )
            return false;

        uint8_t matching_attr = 0;
        if ( !S_ISREG( statbuf.st_mode ) )
            matching_attr |= 0x10; // directory

        // return normal files always and any of the 3 special classes if their bit is set in search_attributes (well, just DIR on Linux)

        if ( ( 0 != matching_attr ) && ( 0 == ( matching_attr & attr ) ) )
        {
            tracer.Trace( "  file '%s' doesn't match the attribute filter %#x\n", fd.cFileName, attr );
            return false;
        }

        tracer.Trace( "  actual found filename: '%s'\n", fd.cFileName );
        char * justFilename = fd.cFileName;
        char * slash = strrchr( justFilename, '/' );
        if ( 0 != slash )
            justFilename = slash + 1;

        char acResult[ DOS_FILENAME_SIZE ];
        if ( strlen( justFilename ) < _countof( acResult ) )
            strcpy( acResult, justFilename );
        else
            return false;

        _strupr( acResult );

        // now write the file into an FCB at the transfer address

        DOSFCB *pfcb = 0;
        if ( exFCB )
        {
            DOSEXFCB * pexfcb = (DOSEXFCB *) GetDiskTransferAddress();
            pexfcb->extendedFlag = 0xff;
            pexfcb->attr = matching_attr;
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

        pfcb->fileSize = statbuf.st_size;
#ifdef __APPLE__
        tmTimeToDos( statbuf.st_mtimespec.tv_sec, pfcb->time, pfcb->date );
#else
        tmTimeToDos( statbuf.st_mtim.tv_sec, pfcb->time, pfcb->date );
#endif
        pfcb->TraceFirst24();
        return true;
    } //ProcessFoundFileFCB

    void CloseFindFirst()
    {
        if ( 0 != g_FindFirst )
        {
            closedir( g_FindFirst );
            g_FindFirst = 0;
        }
    } //CloseFindFirst

    void FindCloseLinux( DIR * pdir )
    {
        closedir( pdir );
    } //FindCloseLinux

    bool FindNextFileLinux( DIR * pdir, LINUX_FIND_DATA & fd )
    {
        const char * justPattern = g_acFindFirstPattern;

        do
        {
            struct dirent * pent = readdir( pdir );
            if ( 0 == pent )
            {
                tracer.Trace( "  readdir returned a 0 for dirent pointer. pdir is %p\n", pdir );
                return false;
            }

            // ignore files DOS just wouldn't understand

            tracer.Trace( "  checking filename '%s'\n", pent->d_name );
            if ( !ValidDOSPathname( pent->d_name ) )
            {
                tracer.Trace( "  filename isn't valid\n" );
                continue;
            }

            if ( !wildMatch( pent->d_name, justPattern ) )
            {
                tracer.Trace( "  filename didn't match pattern\n" );
                continue;
            }

            fd.cFileName[ 0 ] = 0;
            if ( 0 != g_acFindFirstFolder[0] )
            {
                strcpy( fd.cFileName, g_acFindFirstFolder );
                strcat( fd.cFileName, "/" );
            }

            strcat( fd.cFileName, pent->d_name );
            tracer.Trace( "  FindNextFileLinux is returning '%s'\n", fd.cFileName );
            return true;
        } while ( true );

        return false;            
    } //FindNextFileLinux

    DIR * FindFirstFileLinux( const char * pattern, LINUX_FIND_DATA & fd )
    {
        g_acFindFirstFolder[ 0 ] = 0;
        g_acFindFirstPattern[ 0 ] = 0;
        const char * justPattern = pattern;
        DIR * pdir = 0;
        const char * plast = strrchr( pattern, '/' );
        if ( 0 == plast )
            pdir = opendir( "." );
        else
        {
            strcpy( g_acFindFirstFolder, pattern );
            g_acFindFirstFolder[ plast - pattern ] = 0;
            pdir = opendir( g_acFindFirstFolder );
            tracer.Trace( "  opendir for folder '%s'\n", g_acFindFirstFolder );
            justPattern = 1 + plast;
        }

        tracer.Trace( "  opendir returned %p\n", pdir );

        if ( 0 == pdir )
        {
            tracer.Trace( "  errno: %d\n", errno );
            return 0;
        }

        strcpy( g_acFindFirstPattern, justPattern );
        bool found = FindNextFileLinux( pdir, fd );

        if ( !found )
        {
            tracer.Trace( "  FindFirstFileLinux found nothing, so closing the directory\n" );
            closedir( pdir );
            return 0;
        }

        return pdir;
    } //FindFirstFileLinux

#endif

bool GetFileDOSTimeDate( const char * path, uint16_t & dos_time, uint16_t & dos_date )
{
    #ifdef _WIN32                    
        WIN32_FILE_ATTRIBUTE_DATA fad = {0};
        if ( GetFileAttributesExA( path, GetFileExInfoStandard, &fad ) )
        {
            FileTimeToDos( fad.ftLastWriteTime, dos_time, dos_date );
            return true;
        }

        tracer.Trace( "  ERROR: can't get/set file date and time; getfileattributesex failed %d\n", GetLastError() );
    #else
        struct stat statbuf;
        int ret = stat( path, & statbuf );
        if ( 0 == ret )
        {
            #ifdef __APPLE__
                tmTimeToDos( statbuf.st_mtimespec.tv_sec, dos_time, dos_date );
            #else
                tmTimeToDos( statbuf.st_mtim.tv_sec, dos_time, dos_date );
            #endif

            return true;
        }

        tracer.Trace( "  ERROR: can't get DOS file date and time; stat failed %d\n", errno );
    #endif

    return false;
} //GetFileDOSTimeDate

static bool s_firstTimeFlip = true;

void PerhapsFlipTo80xRows()
{
    if ( s_firstTimeFlip )
    {
        s_firstTimeFlip = false;
        if ( !g_forceConsole )
        {
            g_use80xRowsMode = true;
            g_consoleConfig.EstablishConsoleOutput( ScreenColumns, GetScreenRows() );
            ClearDisplay();
        }
    }
} //PerhapsFlipTo80xRows

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

            uint8_t oldScreenRows = GetScreenRows();
            PerhapsFlipTo80xRows();
            uint8_t mode = cpu.al();

            if ( 0x80 & mode )
                SetVideoModeOptions( 0x80 | GetVideoModeOptions() );

            mode &= 0x7f; // strip the top bit which prevents ega/mcga/vga from clearing the display
            tracer.Trace( "  set video mode to %#x, options are %#x\n", mode, GetVideoModeOptions() );

            if ( 2 == mode || 3 == mode ) // only 80x25 is supported with buffer address 0xb8000
                SetVideoMode( 3 ); // it's all we support

            // apps like quickp expect that setting the mode to 3 resets the line count to 25.
            // apps like qbx call set video mode not expecting the line count to be reset to 25.

            static bool ignorableFirstCall = true;

            if ( 25 != oldScreenRows && !ignorableFirstCall )
            {
                SetScreenRows( 25 );
                g_consoleConfig.RestoreConsoleOutput( false );
                s_firstTimeFlip = true;
                PerhapsFlipTo80xRows();
                ClearLastUpdateBuffer();
            }

            ignorableFirstCall = false;

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
            uint8_t prevRow = 0, prevCol = 0;
            if ( !g_use80xRowsMode )
                GetCursorPosition( prevRow, prevCol );

            row = cpu.dh();
            col = cpu.dl();
            SetCursorPosition( row, col );

            if ( !g_use80xRowsMode )
            {
                if ( 0 == col && ( row == ( prevRow + 1 ) ) )
                {
                    printf( "\n" );
                    fflush( stdout );
                }
            }

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
                PerhapsFlipTo80xRows();
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
            if ( g_use80xRowsMode )
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
                if ( 0 == lines || lines >= GetScreenRows() )
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
            if ( g_use80xRowsMode )
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
                if ( 0 == lines || lines >= GetScreenRows() )
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

            PerhapsFlipTo80xRows();

            if ( g_use80xRowsMode )
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

            if ( g_use80xRowsMode )
            {
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.get_cx(); t++ )
                {
                    pbuf[ offset ] = ch;
                    pbuf[ 1 + offset ] = cpu.bl();
                }

                UpdateDisplayRow( row );
            }
            else
            {
                if ( 0x1b == ch ) // don't show escape characters; as left arrows aren't shown
                    ch = ' ';

                if ( 0xd != ch )
                {
                    printf( "%c", ch );
                    fflush( stdout );
                }
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

            if ( g_use80xRowsMode )
            {
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.get_cx(); t++ )
                    pbuf[ offset ] = ch;

                UpdateDisplayRow( row );
            }
            else
            {
                if ( 0xd != ch )
                {
                    printf( "%c", ch );
                    fflush( stdout );
                }
            }

            return;
        }
        case 0xe:
        {
            // write text in teletype mode.
            // I've found no real-world app that uses this, so while I tested it I'm not sure if it's correct.
            // al == ascii character, bh = video page number, bl = foreground pixel color (graphics mode only)
            // advanced cursor. BEL(7), BS(8), LF(A), and CR(D) are honored

            GetCursorPosition( row, col );
            tracer.Trace( "  write text in teletype mode %#x, page %#x, row %u, col %u\n", cpu.al(), cpu.bh(), row, col );

            char ch = cpu.al();
            if ( 0x1b == ch ) // escape should be a left arrow, but it just confuses the console
                ch = ' ';

            uint8_t page = cpu.bh();
            if ( page > 3 )
                page = 0;

            if ( g_use80xRowsMode )
            {
                uint8_t curPage = GetActiveDisplayPage();
                SetActiveDisplayPage( page );
                uint8_t * pbuf = GetVideoMem();

                if ( 0x0a == ch ) // LF
                {
                    col = 0;
                    SetCursorPosition( row, col );
                    tracer.Trace( "  linefeed, setting column to 0\n" );
                }
                else if ( 0x0d == ch ) // CR
                {
                    if ( row >= GetScreenRowsM1() )
                    {
                        tracer.Trace( "  carriage scrolling up a line\n"  );
                        scroll_up( pbuf, 1, 0, 0, GetScreenRowsM1(), ScreenColumnsM1 );
                    }
                    else
                    {
                        tracer.Trace( "  carriage return, moving to next row\n" );
                        row++;
                        SetCursorPosition( row, col );
                    }
                }
                else if ( 0x08 == ch ) // backspace
                {
                    if ( col > 0 )
                    {
                        col--;
                        SetCursorPosition( row, col );
                    }
                }
                else
                {
                    uint32_t offset = row * 2 * ScreenColumns + col * 2;
                    pbuf[ offset ] = ch;
                    UpdateDisplayRow( row );

                    col++;
                    if ( col >= ScreenColumns )
                        col = 0;
                    SetCursorPosition( row, col );
                }

                SetActiveDisplayPage( curPage ); // restore the original page
            }
            else
            {
                if ( 0xd != ch )
                {
                    printf( "%c", ch );
                    fflush( stdout );
                }
            }

            return;
        }
        case 0xf:
        {
            // get video mode / get video state

            //PerhapsFlipTo80xRows();  too aggressive for some apps

            uint8_t mode = GetVideoMode();
            uint8_t options = GetVideoModeOptions();

            cpu.set_al( mode | ( options & 0x80 ) );
            cpu.set_ah( ScreenColumns ); // columns
            cpu.set_bh( GetActiveDisplayPage() ); // active display page

            tracer.Trace( "  returning video mode %u, columns %u, display page %u, options %#x\n", cpu.al(), cpu.ah(), cpu.bh(), GetVideoModeOptions() );
            return;
        }
        case 0x10:
        {
            // set palette registers (ignore)

            return;
        }
        case 0x11:
        {
            // character generator

            TraceBiosInfo();
            tracer.Trace( "  character generator routine %#x\n", cpu.al() );
            PerhapsFlipTo80xRows();  // QuickPascal calls this, and it's a good indication of 80x25 mode
            switch ( cpu.al() )
            {
                case 0x12: // ROM 8x8 double dot character definitions. switch to 80x50 mode
                {
                    SetScreenRows( 50 );
                    g_consoleConfig.RestoreConsoleOutput( false );
                    s_firstTimeFlip = true;
                    PerhapsFlipTo80xRows();
                    ClearLastUpdateBuffer();
                    break;
                }
                case 0x14: // ROM 8x16 double dot. switch to 80x25 mode
                {
                    SetScreenRows( 25 );
                    g_consoleConfig.RestoreConsoleOutput( false );
                    s_firstTimeFlip = true;
                    PerhapsFlipTo80xRows();
                    ClearLastUpdateBuffer();
                    break;
                }
                case 0x30: // get current character generator information
                {
                    uint8_t rows = GetScreenRows();
                    cpu.set_dl( rows - 1 ); // rows less 1
                    uint8_t points = ( rows == 25 ? 16 : rows == 50 ? 8 : 14 );
                    cpu.set_cx( points );
                    cpu.set_es( 0x50 ); // somewhat random
                    cpu.set_bp( 0 );
                    tracer.Trace( "  returning dl (rows) %u and cx %u points\n", cpu.dl(), cpu.get_cx() );
                    break;
                }
            }

            return;
        }
        case 0x12:
        {
            // video subsystem configuration. alternate select ega/vga. return some defaults

            PerhapsFlipTo80xRows();
            TraceBiosInfo();
            if ( 0x10 == cpu.bl() )
            {
                // wordperfect uses this to see how much RAM is installed
                tracer.Trace( "  app check in video subsystem config, app: '%s'\n", g_acApp );

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
                cpu.set_cx( 3 );  // 256k installed
            }
            else if ( 0x32 == cpu.bl() )
            {
                // cpu access to video ram

                tracer.Trace( "  enable cpu access to video RAM: %d\n", cpu.al() );
                cpu.set_al( 0 ); // indicate failure
            }
            else if ( 0x30 == cpu.bl() )
            {
                // select scan lines for alphanumeric modes. al = 0 for 200, 1 for 350, and 2 for 400.
                // in theory some apps use this to get to 25, 43, and 50 line mode. But no apps I've tested require it.

                tracer.Trace( "  select scan lines for alphanumeric modes: %u\n", cpu.al() );
                cpu.set_al( 0x12 ); // indicate success
            }
            else
                tracer.Trace( "  unhandled code %#x\n", cpu.bl() );

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
            // get/set Video Display Combination (VGA)

            PerhapsFlipTo80xRows();  // Turbo basic 1.1 calls this

            if ( 0 == cpu.al() ) // get
            {
                // 2 == CGA color display.  8 == VGA with analog color display
                cpu.set_al( 0x1a );
                cpu.set_bl( GetVideoDisplayCombination() );
                cpu.set_bh( 0 ); // no inactive display
                tracer.Trace( "  getting video display combination, returning %#x\n", cpu.bl() );
            }
            else if ( 1 == cpu.al() ) // set
            {
                tracer.Trace( "  setting video display combination to %#x\n", cpu.bl() );
                cpu.set_al( 0x1a );
            }

            TraceBiosInfo();
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
        case 0xfe:
        {
            // (topview) get video buffer. do nothing and use the default video buffer.
            return;
        }
        case 0xff:
        {
            // (topview) update real screen from video buffer. No topview support, but why not update the display?

            if ( g_use80xRowsMode )
                UpdateDisplay();

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
    uint8_t * pbiosdata = cpu.flat_address8( 0x40, 0 );
    pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();

    CKbdBuffer kbd_buf;

    switch( c )
    {
        case 0:
        case 0x10:
        {
            // get character. ascii into al, scancode into ah

            if ( g_use80xRowsMode )
                UpdateDisplay();

            InjectKeystrokes();

#if USE_ASSEMBLY_FOR_KBD
            invoke_assembler_routine( g_int16_0_seg );
#else
            while ( kbd_buf.IsEmpty() )
            {
                // block waiting for a character then return it.

                while ( !peek_keyboard( true, true, false ) )
                    continue;

                consume_keyboard();
            }

            cpu.set_al( kbd_buf.Consume() ); // ascii
            cpu.set_ah( kbd_buf.Consume() ); // scancode

            tracer.Trace( "  returning character %04x '%c'\n", cpu.get_ax(), printable( cpu.al() ) );
#endif

            return;
        }
        case 1:
        case 0x11: // 0x11 is really the same behavior as 1 except it can return a wider set of characters
        {
            // check keyboard status. checks if a character is available. return it if so, but not removed from buffer
            // set zero flag if no character is available. clear zero flag if a character is available

            cpu.set_ah( 0 );

            // apps like WordStar draw a bunch of text to video memory then call this, which is the chance to update the display

            if ( g_use80xRowsMode )
            {
                bool update = throttled_UpdateDisplay();
                if ( update )
                    g_int16_1_loop = false;
            }

            InjectKeystrokes();

            if ( kbd_buf.IsEmpty() )
            {
                cpu.set_zero( true );
                if ( g_int16_1_loop ) // avoid a busy loop it makes my fan loud
                    SleepAndScheduleInterruptCheck();
                else
                    g_int16_1_loop = true;
            }
            else
            {
                cpu.set_al( kbd_buf.CurAsciiChar() );
                cpu.set_ah( kbd_buf.CurScancode() );
                cpu.set_zero( false );
            }

            if ( cpu.get_zero() )
                tracer.Trace( "  returning carry flag 1; no character available\n" );
            else
                tracer.Trace( "  returning carry flag %d, ax %04x, ascii '%c'\n", cpu.get_zero(), cpu.get_ax(), printable( cpu.al() ) );
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

            if ( ! kbd_buf.IsFull() )
            {
                kbd_buf.Add( cpu.cl(), cpu.ch(), false );
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
    DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
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

    trace_all_open_files_fcb();

    do
    {
        size_t index = FindFileEntryIndexByProcessFCB( g_currentPSP );
        if ( -1 == index )
            break;
        tracer.Trace( "  closing fcb file an app leaked: '%s'\n", g_fileEntriesFCB[ index ].path );
        FILE * fp = RemoveFileEntryFCB( g_fileEntriesFCB[ index ].path );
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
        tracer.Trace( "  ending emulation by telling cpu emulator to stop once it starts running again\n" );
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

    cpu.set_carry( false ); // indicate that the Create Process int x21 x4b (EXEC/Load and Execute Program) succeeded.
} //HandleAppExit

uint8_t HighestDrivePresent()
{
#ifdef _WIN32
    DWORD dwDriveMask = GetLogicalDrives();

    // a = 0 ... z = 25

    for ( DWORD b = 0; b < 32; b++ )
    {
        DWORD bit = 0x80000000 >> b;
        if ( bit & dwDriveMask )
            return (uint8_t) ( 32 - b - 1 );
    }

    return 0;
#else
    return 2; // 'C'
#endif
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

bool command_exists( char * pc )
{
    if ( file_exists( pc ) )
        return true;

    if ( ends_with( pc, ".com" ) || ends_with( pc, ".exe" ) )
        return false;

    char ac[ MAX_PATH ];
    strcpy( ac, pc );
    strcat( ac, ".COM" );
    if ( file_exists( ac ) )
    {
        strcat( pc, ".COM" );
        return true;
    }

    strcpy( ac, pc );
    strcat( ac, ".EXE" );
    if ( file_exists( ac ) )
    {
        strcat( pc, ".EXE" );
        return true;
    }

    return false;
} //command_exists

#ifndef _WIN32
    const int day_of_week( int year, int month, int day )
    {
        return( day                                                     
                + ((153 * (month + 12 * ((14 - month) / 12) - 3) + 2) / 5)
                + (365 * (year + 4800 - ((14 - month) / 12)))             
                + ((year + 4800 - ((14 - month) / 12)) / 4)               
                - ((year + 4800 - ((14 - month) / 12)) / 100)             
                + ((year + 4800 - ((14 - month) / 12)) / 400)             
                - 32045                                                   
              ) % 7;
    } //day_of_week
#endif

bool FindCommandInPath( char * pc )
{
    // check the current directory

    if ( command_exists( pc ) )
    {
        tracer.Trace( "  command '%s' found without searching path\n", pc );
        return true;
    }

    // if a path was specified already, don't check the environment's path variable

    if ( ':' == pc[1] || '\\' == *pc || '/' == *pc )
        return false;

    DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
    const char * penv = (char *) cpu.flat_address( psp->segEnvironment, 0 );

    // iterate through environment variables looking for PATH=

    size_t len = strlen( penv );
    while ( 0 != len )
    {
        if ( begins_with( penv, "path=" ) )
        {
            // iterate through the semicolon-separated paths in the path, which is null-terminated

            penv += 5;

            do
            {
                while ( *penv && ';' == *penv )
                    penv++;
    
                char acPath[ MAX_PATH ];
                int i = 0;
                while ( *penv && ';' != *penv )
                    acPath[ i++ ] = *penv++;

                if ( 0 == i )
                {
                    tracer.Trace( "  empty value in path variable; giving up\n" );
                    return false;
                }
    
                acPath[ i ] = 0;
                if ( '\\' != acPath[ i - 1 ] && '/' != acPath[ i - 1 ] )
                {
                    acPath[ i++ ] = '\\';
                    acPath[ i ] = 0;
                }
    
                tracer.Trace( "  testing path '%s'\n", acPath );
                strcat( acPath, pc );
                if ( command_exists( acPath ) )
                {
                    strcpy( pc, acPath );
                    tracer.Trace( "  found the command at '%s'\n", acPath );
                    return true;
                }
            } while( true );
        }
        penv += ( 1 + len );
        len = strlen( penv );
    }

    tracer.Trace( "  command '%s' not found\n", pc );
    return false;
} //FindCommandInPath

void wait_for_kbd_to_al()
{
    bool first = true;

    CKbdBuffer kbd_buf;
    while ( kbd_buf.IsEmpty() )
    {
        if ( first && g_use80xRowsMode )
        {
            first = false;
            UpdateDisplay();
        }

        // wait for a character then return it.

        while ( !peek_keyboard( true, true, false ) )
            continue;

        consume_keyboard();
    }

    cpu.set_al( kbd_buf.Consume() );
    char scan_code = kbd_buf.Consume(); // unused

    tracer.Trace( "  character input returning al %02x. scan_code %02x\n", cpu.al(), scan_code );
} //wait_for_kbd_to_al

void output_character( char ch )
{
    if ( g_use80xRowsMode )
    {
        uint8_t row, col;
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
            if ( row >= GetScreenRowsM1() )
            {
                tracer.Trace( "  line feed scrolling up a line\n"  );
                scroll_up( pbuf, 1, 0, 0, GetScreenRowsM1(), ScreenColumnsM1 );
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
            fflush( stdout );
        }
    }
} //output_character

void handle_int_21( uint8_t c )
{
    char filename[ DOS_FILENAME_SIZE ];
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
        case 1:
        {
            // keyboard input with echo
            // wait for keyboard input from stdin. echo to stdout
            // return character in AL. return 0 to signal subsequent call will get scancode.
            // ^c and ^break are checked.
            // just like function 7 except character is echoed to stdout.

            // character input. block until a keystroke is available.

            uint8_t * pbiosdata = cpu.flat_address8( 0x40, 0 );
            pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();

            if ( g_use80xRowsMode)
                UpdateDisplay();

            InjectKeystrokes();

#if USE_ASSEMBLY_FOR_KBD
            invoke_assembler_routine( g_int21_1_seg );
#else
            wait_for_kbd_to_al();
            output_character( cpu.al() );
#endif

            return;
        }
        case 2:
        {
            // output character in DL. move cursor as appropriate
            // todo: interpret 7 (beep), 8 (backspace), 9 (tab) to tab on multiples of 8. 10 lf should move cursor down and scroll if needed
    
            char ch = cpu.dl();
            tracer.Trace( "  output char %d == %#02x == '%c'\n", ch, ch, printable( ch ) );
            output_character( ch );
            return;
        }
        case 6:
        {
            // direct console character I/O
            // DL = 0xff means get input into AL if available and set ZF to 0. Set ZF to 1 if no character is available
            //      no echo is produced
            //      return 0 for extended keystroke, then on subsequent call return the scancode
            //      ignore ctrl+break and ctrl+prtsc
            // DL = !0xff means output the character
    
            if ( 0xff == cpu.dl() )
            {
                // input. don't block if nothing is available
                // Multiplan (v2) is the only app I've found that uses this function for input. Microsoft LISP uses it too.

                CKbdBuffer kbd_buf;
                InjectKeystrokes();

                if ( !kbd_buf.IsEmpty() )
                {
                    static bool mid_scancode_read = false;
                    cpu.set_zero( false );

                    tracer.Trace( "  direct console io 6. mid_scancode_read: %d\n", mid_scancode_read );

                    if ( mid_scancode_read )
                    {
                        mid_scancode_read = false;
                        cpu.set_al( kbd_buf.CurScancode() );
                        tracer.Trace( "    set al to %02x\n", cpu.al() );
                        kbd_buf.Consume(); // asciichar
                        kbd_buf.Consume(); // scancode
                    }
                    else
                    {
                        cpu.set_al( kbd_buf.CurAsciiChar() );
                        tracer.Trace( "    set al to %02x\n", cpu.al() );

                        if ( 0 == cpu.al() )
                            mid_scancode_read = true;
                        else
                        {
                            kbd_buf.Consume(); // asciichar
                            kbd_buf.Consume(); // scancode
                        }
                    }
                }
                else
                {
                    cpu.set_zero( true );
                    cpu.set_al( 0 ); // this isn't documented, but DOS does this and mulisp will go into an infinite loop otherwise

                    // Multiplan v2 has a busy loop with this interrupt waiting for input
                    // mulisp calls this from many contexts that are hard to differentiate

                    if ( !ends_with( g_acApp, "mulisp.com" ) )
                        SleepAndScheduleInterruptCheck();
                }
            }
            else
            {
                // output
    
                char ch = cpu.dl();
                tracer.Trace( "    direct console output %02x, '%c'\n", (uint8_t) ch, printable( (uint8_t) ch ) );
                if ( 0x0d != ch )
                {
                    printf( "%c", ch );
                    fflush( stdout );
                }
            }
    
            return;
        }
        case 7:
        case 8:
        {
            // character input. block until a keystroke is available.

            uint8_t * pbiosdata = cpu.flat_address8( 0x40, 0 );
            pbiosdata[ 0x17 ] = get_keyboard_flags_depressed();

            if ( g_use80xRowsMode)
                UpdateDisplay();

            InjectKeystrokes();

#if USE_ASSEMBLY_FOR_KBD
            invoke_assembler_routine( g_int21_8_seg );
#else
            bool first = true;

            CKbdBuffer kbd_buf;
            while ( kbd_buf.IsEmpty() )
            {
                if ( first && g_use80xRowsMode )
                {
                    first = false;
                    UpdateDisplay();
                }

                // wait for a character then return it.

                while ( !peek_keyboard( true, true, false ) )
                    continue;

                consume_keyboard();
            }

            cpu.set_al( kbd_buf.Consume() );
            char scan_code = kbd_buf.Consume(); // unused

            tracer.Trace( "  direct character input returning al %02x. scan_code %02x\n", cpu.al(), scan_code );
#endif

            return;
        }
        case 9:
        {
            // print string. prints chars up to a dollar sign $
    
            char * p = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.TraceBinaryData( (uint8_t *) p, 0x40, 2 );
            while ( *p && '$' != *p )
                printf( "%c", *p++ );
            fflush( stdout );
    
            return;
        }
        case 0xa:
        {
            // Buffered Keyboard input. DS::DX pointer to buffer. byte 0 count in, byte 1 count out excluding CR, byte 2 starts the response
            // The assembler version enables the emulator to send timer and keyboard interrupts.

#if USE_ASSEMBLY_FOR_KBD
            invoke_assembler_routine( g_int21_a_seg );
#else
            uint8_t * p = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
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
#endif

            return;
        }
        case 0xb:
        {
            // check standard input status. Returns AL: 0xff if char available, 0 if not

            InjectKeystrokes();
            CKbdBuffer kbd_buf;
            cpu.set_al( kbd_buf.IsEmpty() ? 0 : 0xff );
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
#ifdef _WIN32            
            BOOL ok = SetCurrentDirectoryA( acDir );
            tracer.Trace( "  result of SetCurrentDirectory to '%s': %d\n", acDir, ok );
#endif            

            return;
        }
        case 0xf:
        {
            // open file using FCB
    
            tracer.Trace( "  open file using FCB. ds %u dx %u\n", cpu.get_ds(), cpu.get_dx() );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );

            if ( 0 == pfcb->drive )
                pfcb->drive = get_current_drive();
    
            cpu.set_al( 0xff );
            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                tracer.Trace( "  opening %s\n", filename );

                // if the file is already open then close it. Digital Research CB86.EXE does this.

                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( 0 != fp )
                {
                    RemoveFileEntryFCB( filename );
                    fclose( fp );
                }

                fp = fopen( filename, "r+b" );
                if ( fp )
                {
                    tracer.Trace( "  file opened successfully\n" );
        
                    if ( 0 == pfcb->drive )
                        pfcb->drive = 1 + get_current_drive();
                    pfcb->curBlock = 0;
                    pfcb->recSize = 0x80;
                    pfcb->fileSize = portable_filelen( fp );
                    GetFileDOSTimeDate( filename, pfcb->time, pfcb->date );
                    pfcb->curRecord = 0; // documentation says this shouldn't be initialized here

                    // Don't initialize recNumber as apps like PLI.EXE use files with sequential I/O only and
                    // don't allocate enough RAM in the FCB for the recNumber (the last field). Memory trashing would result.
                    // pfcb->recNumber = 0;
    
                    FileEntry fe = {0};
                    strcpy( fe.path, filename );
                    fe.fp = fp;
                    fe.handle = 0; // FCB files don't have handles
                    fe.writeable = true;
                    fe.seg_process = g_currentPSP;
                    fe.refcount = 1;
                    g_fileEntriesFCB.push_back( fe );
                    tracer.Trace( "  successfully opened file\n" );
                    trace_all_open_files_fcb();
        
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
    
            cpu.set_al( 0xff );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                tracer.Trace( "  close file using FCB: '%s'\n", filename );

                FILE * fp = RemoveFileEntryFCB( filename );
                if ( 0 != fp )
                {
                    cpu.set_al( 0 );
                    tracer.Trace( "  successfully closed already open file\n" );
                    fclose( fp );
                    trace_all_open_files_fcb();
                }
                else
                    tracer.Trace( "  ERROR: file close using FCB of a file that's not open\n" );
            }
            else
                tracer.Trace( "  ERROR: file close is unable to parse filename from FCB\n" );
    
            return;
        }
        case 0x11:
        {
            // search first using FCB.
            // DS:DX points to FCB
            // if the first byte of the FCB (the drive) is 0xff it's an extended FCB.
            // returns AL = 0 if file found, FF if not found
            //    if found, DTA is used as an FCB ready for an open or delete

            CloseFindFirst();
            bool extendedFCB = false;
            DOSFCB *pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            uint8_t attr = 0;
            if ( 0xff == pfcb->drive )
            {
                extendedFCB = true;
                DOSEXFCB * pexfcb = (DOSEXFCB *) pfcb;
                attr = pexfcb->attr;
                pfcb = & (pexfcb->fcb);
            }

            pfcb->TraceFirst16();
            char search_string[ DOS_FILENAME_SIZE ];
            bool ok = GetDOSFilenameFromFCB( *pfcb, search_string );
            if ( ok )
            {
                tracer.Trace( "  searching for pattern '%s'\n", search_string );
#ifdef _WIN32                
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
                            CloseFindFirst();
                            break;
                        }
                    } while( true );
                }
                else
                {
                    cpu.set_al( 0xff );
                    tracer.Trace( "  WARNING: search first using FCB failed, error %d\n", GetLastError() );
                }
#else
                LINUX_FIND_DATA lfd = {0};
                tracer.TraceBinaryData( (uint8_t *) search_string, strlen( search_string ), 4 );
                const char * linuxSearch = DOSToHostPath( search_string );
                tracer.Trace( "  linux search string: '%s'\n", linuxSearch );
                tracer.TraceBinaryData( (uint8_t *) linuxSearch, strlen( linuxSearch ), 4 );
                g_FindFirst = FindFirstFileLinux( linuxSearch, lfd );
                if ( 0 != g_FindFirst )
                {
                    do
                    {
                        bool ok = ProcessFoundFileFCB( lfd, attr, extendedFCB );
                        if ( ok )
                        {
                            cpu.set_al( 0 );
                            break;
                        }

                        bool found = FindNextFileLinux( g_FindFirst, lfd );
                        if ( !found )
                        {
                            cpu.set_al( 0xff );
                            tracer.Trace( "  WARNING: search next using FCB (in search first function) found no more, error %d\n", errno );
                            CloseFindFirst();
                            break;
                        }
                    } while( true );
                }
                else
                {
                    cpu.set_al( 0xff );
                    tracer.Trace( "  WARNING: find first file using FCB failed to find anything\n" );
                } 
#endif
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

#ifdef _WIN32
            if ( INVALID_HANDLE_VALUE == g_hFindFirst )
                cpu.set_al( 0xff );
            else
            {
                DOSFCB *pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
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
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
#else
            if ( 0 != g_FindFirst )
            {
                DOSFCB *pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
                uint8_t attr = 0;
                bool extendedFCB = false;
                if ( 0xff == pfcb->drive )
                {
                    extendedFCB = true;
                    DOSEXFCB * pexfcb = (DOSEXFCB *) pfcb;
                    attr = pexfcb->attr;
                    pfcb = & (pexfcb->fcb);
                }

                do
                {
                    LINUX_FIND_DATA lfd = {0};
                    bool found = FindNextFileLinux( g_FindFirst, lfd );
                    if ( found )
                    {
                        bool ok = ProcessFoundFileFCB( lfd, attr, extendedFCB );
                        if ( ok )
                        {
                            cpu.set_al( 0 );
                            break;
                        }
                    }
                    else
                    {
                        cpu.set_al( 0xff ); // no more files
                        tracer.Trace( "  WARNING: find next file using FCB found no matching files\n" );
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_al( 0xff ); 
                tracer.Trace( "  ERROR: search for next FCB without a prior successful search for first\n" );
            }                    
#endif            
    
            return;
        }
        case 0x13:
        {
            // delete file using FCB
    
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
    
            cpu.set_al( 0xff );
            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                tracer.Trace( "  deleting '%s'\n", filename );

                // the file may be open (Digital Research's CB86 Basic Compiler does this). If so, close it.

                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( 0 != fp )
                {
                    RemoveFileEntryFCB( filename );
                    tracer.Trace( "  closing an open file before deleting it\n" );
                    fclose( fp );
                    trace_all_open_files_fcb();
                }

                int removeok = ( 0 == remove( filename ) );
                if ( removeok )
                {
                    cpu.set_al( 0 );
                    tracer.Trace( "  delete successful\n" );
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
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    uint32_t seekOffset = pfcb->SequentialOffset();
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
                    tracer.Trace( "  using disk transfer address %04x:%04x\n", g_diskTransferSegment, g_diskTransferOffset );
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
                    tracer.Trace( "  ERROR sequential read using FCB doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR sequential read using FCB can't parse filename\n" );

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
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    uint32_t seekOffset = pfcb->SequentialOffset();
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
                             tracer.TraceBinaryData( GetDiskTransferAddress(), (uint32_t) num_written, 4 );
                        }
                        else
                             tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                    }
                    else
                        tracer.Trace( "  ERROR sequential write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR sequential write using FCBs doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR sequential write using FCB can't parse filename\n" );

            return;
        }
        case 0x16:
        {
            // create file using FCB
            // Microsoft Pascal v1.0's first pass pas1 creates and writes ^z then 511 zeros to "con.lst".
            // ms-dos sends that to the console because "con". No special "con" handling exists here.
    
            tracer.Trace( "  create file using FCB. ds %u dx %u\n", cpu.get_ds(), cpu.get_dx() );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
            cpu.set_al( 0xff );
    
            if ( 0 == pfcb->drive )
                pfcb->drive = get_current_drive();
    
            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                tracer.Trace( "  creating '%s'\n", filename );
    
                FILE * fp = fopen( filename, "w+b" );
                if ( fp )
                {
                    tracer.Trace( "  file created successfully\n" );
                    cpu.set_al( 0 );
    
                    pfcb->curBlock = 0;
                    pfcb->recSize = 0x80;
                    pfcb->fileSize = 0;
                    GetFileDOSTimeDate( filename, pfcb->time, pfcb->date );
                    pfcb->curRecord = 0;
    
                    // Don't initialize recNumber as apps like PLI.EXE use files with sequential I/O only and
                    // don't allocate enough RAM in the FCB for the recNumber (the last field). Memory trashing would result.
                    // pfcb->recNumber = 0;

                    FileEntry fe = {0};
                    strcpy( fe.path, filename );
                    fe.fp = fp;
                    fe.handle = 0;
                    fe.writeable = true;
                    fe.seg_process = g_currentPSP;
                    fe.refcount = 1;
                    g_fileEntriesFCB.push_back( fe );
                    trace_all_open_files_fcb();

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
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            tracer.TraceBinaryData( (uint8_t *) pfcb, sizeof( DOSFCB ), 2 );
            DOSFCB * pfcbNew = (DOSFCB * ) ( 0x10 + (uint8_t *) pfcb );
    
            char oldFilename[ DOS_FILENAME_SIZE ] = {0};
            if ( GetDOSFilenameFromFCB( *pfcb, oldFilename ) )
            {
                char newFilename[ DOS_FILENAME_SIZE ] = {0};
                if ( GetDOSFilenameFromFCB( *pfcbNew, newFilename ) )
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

#ifdef _WIN32
            uint32_t driveMask = GetLogicalDrives(); // a = 0 ... z = 25
#else
            uint32_t driveMask = 2;
#endif            

            if ( driveMask & ( 1 << drive ) )
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
            //         the random file position isn't updated after the read
            //         partial reads are 0-filled
            //         one record is read
            // The sequential offset is set to match the random offset before the read

            cpu.set_al( 1 );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    uint32_t seekOffset = pfcb->RandomOffset();
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
                    pfcb->SetSequentialFromRandom(); // Digital Research PL/I compiler/linker depends on this
    
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
                        tracer.Trace( "  ERROR random read using FCB failed to seek, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random read using FCB doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR random read using FCB can't parse filename\n" );

            return;
        }
        case 0x22:
        {
            // random write using FCBs. on output, 0 if success, 1 if disk full, 2 if DTA too small
            // The sequential offset is set to match the random offset before the write
            // Apps that use this: Lotus 123 v1.0A and Microsoft Pascal v1.0.
    
            cpu.set_al( 1 );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    uint32_t seekOffset = pfcb->RandomOffset();
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
                    pfcb->SetSequentialFromRandom(); // Digital Research PL/I compiler/linker depends on this
    
                    bool ok = !fseek( fp, seekOffset, SEEK_SET );
                    if ( ok )
                    {
                        size_t num_written = fwrite( GetDiskTransferAddress(), 1, pfcb->recSize, fp );
                        if ( num_written )
                        {
                             tracer.Trace( "  write succeded: %u bytes\n", pfcb->recSize );
                             cpu.set_al( 0 );
        
                             // don't update the fcb's record number for this version of the API
                        }
                        else
                             tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                    }
                    else
                        tracer.Trace( "  ERROR random write using FCB failed to seek, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random write using FCB doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR random write using FCB can't parse filename\n" );
    
            return;
        }
        case 0x24:
        {
            // set relative record field in FCB.
            // Modifies open FCB in DS:DX for random operation based on current sequential position
            // returns nothing
            // Digital Research's PLI.EXE compiler is the only app I know that uses this.

            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->SetRandomFromSequential();
            tracer.Trace( "  updated FCB:\n" );
            pfcb->Trace();

            return;
        }
        case 0x25:
        {
            // set interrupt vector
    
            tracer.Trace( "  setting interrupt vector %02x %s to %04x:%04x\n", cpu.al(), get_interrupt_string( cpu.al(), 0, ah_used ), cpu.get_ds(), cpu.get_dx() );
            uint16_t * pvec = cpu.flat_address16( 0, 4 * (uint16_t) cpu.al() );
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
            memcpy( cpu.flat_address( seg, 0 ), cpu.flat_address( g_currentPSP, 0 ), sizeof( DOSPSP ) );
            return;
        }
        case 0x27:
        {
            // random block read using FCBs
            // CX: number of records to read
            // DS:BX pointer to the FCB.
            // on exit, AL 0 success, 1 EOF no data read, 2 dta too small, 3 eof partial read (filled with 0s)
            // also, sequential I/O position is updated 
    
            cpu.set_al( 1 ); // eof
            uint32_t cRecords = cpu.get_cx();
            cpu.set_cx( 0 );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  random block read using FCBs, cRecords %u\n", cRecords );
            pfcb->Trace();
            uint32_t seekOffset = pfcb->RandomOffset();

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    pfcb->fileSize = portable_filelen( fp );

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
                        tracer.Trace( "  seek offset: %u\n", seekOffset );
                        bool ok = !fseek( fp, seekOffset, SEEK_SET );
                        if ( ok )
                        {
                            uint32_t askedBytes = pfcb->recSize * cRecords;
                            memset( GetDiskTransferAddress(), 0, askedBytes );
                            uint32_t toRead = get_min( pfcb->fileSize - seekOffset, askedBytes );
                            size_t numRead = fread( GetDiskTransferAddress(), 1, toRead, fp );
                            if ( numRead )
                            {
                                tracer.Trace( "  numRead: %zd, toRead %zd, askedBytes %u\n", numRead, toRead, askedBytes );
                                if ( numRead == askedBytes )
                                    cpu.set_al( 0 );
                                else
                                    cpu.set_al( 3 ); // eof encountered, last record is partial
        
                                cpu.set_cx( (uint16_t) ( toRead / pfcb->recSize ) );
                                tracer.Trace( "  successfully read %u bytes of %u requested, CX set to %u, al set to %u:\n", numRead, toRead, cpu.get_cx(), cpu.al() );
                                tracer.Trace( "  used disk transfer address %04x:%04x\n", g_diskTransferSegment, g_diskTransferOffset );
                                tracer.TraceBinaryData( GetDiskTransferAddress(), toRead, 4 );
                                pfcb->SetRandomRecordNumber( pfcb->RandomRecordNumber() + (uint32_t) cpu.get_cx() );
                                pfcb->SetSequentialFromRandom(); // the next sequential I/O expects this to be set. Thanks DRI compilers and linkers.
                            }
                            else
                                tracer.Trace( "  ERROR random block read using FCBs failed to read, error %d = %s\n", errno, strerror( errno ) );
                        }
                        else
                            tracer.Trace( "  ERROR random block read using FCBs failed to seek, error %d= %s\n", errno, strerror( errno ) );
                    }
                }
                else
                    tracer.Trace( "  ERROR random block read using FCBs doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR random block read using FCBs can't parse filename\n" );
    
            return;
        }
        case 0x28:
        {
            // random block write using FCBs.
            // in: CX = number of records, DS:BX the fcb
            // out: al = 0 if success, 1 if disk full, 2 if data too small, cx = number of records written
    
            cpu.set_al( 1 );
            uint16_t recsToWrite = cpu.get_cx();
            cpu.set_cx( 0 );
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            pfcb->Trace();

            // pas1 of v1 of the Microsoft Pascal compiler writes 0 records just before closing the .lst file

            if ( 0 == recsToWrite )
            {
                tracer.Trace( "  attempt to write 0 records is ignored\n" );
                cpu.set_al( 0 );
                return;
            }

            if ( GetDOSFilenameFromFCB( *pfcb, filename ) )
            {
                FILE * fp = FindFileEntryFromFileFCB( filename );
                if ( fp )
                {
                    uint32_t seekOffset = pfcb->RandomOffset();
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
    
                    bool ok = !fseek( fp, seekOffset, SEEK_SET );
                    if ( ok )
                    {
                        size_t num_written = fwrite( GetDiskTransferAddress(), recsToWrite, pfcb->recSize, fp );
                        if ( num_written )
                        {
                             tracer.Trace( "  write succeded: %u bytes\n", recsToWrite * pfcb->recSize );
                             tracer.TraceBinaryData( GetDiskTransferAddress(), recsToWrite * pfcb->recSize, 4 );
                             cpu.set_cx( recsToWrite );
                             cpu.set_al( 0 );
                             pfcb->SetRandomRecordNumber( pfcb->RandomRecordNumber() + (uint32_t) recsToWrite );
                             pfcb->SetSequentialFromRandom(); // the next sequential I/O expects this to be set. Thanks DRI compilers and liners.
                        }
                        else
                             tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                    }
                    else
                        tracer.Trace( "  ERROR random block write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "  ERROR random block write using FCBs doesn't have an open file\n" );
            }
            else
                tracer.Trace( "  ERROR random block write using FCBs can't parse filename\n" );
    
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

            char * pfile = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_si() );
            if ( 0 != pfile[ 0 ] && ':' == pfile[ 1 ] )
                pfile += 2; // get past optional X:

            char * pfile_original = pfile;
            tracer.Trace( "  parse filename '%s'\n", pfile );
            tracer.TraceBinaryData( (uint8_t *) pfile, 64, 4 );
    
            DOSFCB * pfcb = (DOSFCB *) cpu.flat_address( cpu.get_es(), cpu.get_di() );
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
    
#ifdef _WIN32            
            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_al( (uint8_t) st.wDayOfWeek );
            cpu.set_cx( st.wYear );
            cpu.set_dh( (uint8_t) st.wMonth );
            cpu.set_dl( (uint8_t) st.wDay );
#else
            time_t t = time( 0 );
            struct tm current_dt = *localtime( &t );
            cpu.set_al( (uint8_t) day_of_week( current_dt.tm_year + 1900, current_dt.tm_mon, current_dt.tm_mday ) );
            tracer.Trace( "year value: %d\n", current_dt.tm_year );
            cpu.set_cx( (uint16_t) ( current_dt.tm_year + 1900 ) );
            cpu.set_dh( (uint8_t) current_dt.tm_mon + 1 );
            cpu.set_dl( (uint8_t) current_dt.tm_mday );
#endif            
    
            return;
        }           
        case 0x2c:
        {
            // get system time into DX (seconds : hundredths of a second), CX (hours : minutes)
    
            system_clock::time_point now = system_clock::now();
            uint64_t ms = duration_cast<milliseconds>( now.time_since_epoch() ).count() % 1000;
            time_t time_now = system_clock::to_time_t( now );
            struct tm * plocal = localtime( & time_now );

            cpu.set_ch( (uint8_t) plocal->tm_hour );
            cpu.set_cl( (uint8_t) plocal->tm_min );
            cpu.set_dh( (uint8_t) plocal->tm_sec );
            cpu.set_dl( (uint8_t) ( ms / 10 ) );
            tracer.Trace( "  system time is %02d:%02d:%02d.%02d\n", cpu.ch(), cpu.cl(), cpu.dh(), cpu.dl() );

            #if false // useful when debugging to keep trace files consistent between runs
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

            assert( 0 != g_allocEntries.size() ); // loading the app creates 1 that shouldn't be freed.
            trace_all_allocations();

            uint16_t new_paragraphs = cpu.get_dx();
            tracer.Trace( "  TSR and keep %#x paragraphs resident\n", new_paragraphs );
            g_allocEntries[ entry ].para_length = new_paragraphs;
            trace_all_allocations();

            DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
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
    
            uint16_t * pvec = cpu.flat_address16( 0, 4 * (uint16_t) cpu.al() );
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
                uint8_t * pinfo = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
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
            char * pathOriginal = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * path = DOSToHostPath( pathOriginal );            
            tracer.Trace( "  create directory '%s'\n", path );

#ifdef _WIN32
            int ret = _mkdir( path );
#else
            int ret = mkdir( path, 0x777 );
#endif
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
            char * pathOriginal = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * path = DOSToHostPath( pathOriginal );
            tracer.Trace( "  remove directory '%s'\n", path );

#ifdef _WIN32
            int ret = _rmdir( path );
#else
            int ret = rmdir( path );
#endif                        
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
            char * pathOriginal = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * path = DOSToHostPath( pathOriginal );
            tracer.Trace( "  change directory to '%s'. original path '%s'\n", path, pathOriginal );

#ifdef _WIN32
            int ret = _chdir( path );
#else
            int ret = chdir( path );
#endif            
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
    
            char * original_path = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * path = DOSToHostPath( original_path );
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
                UpdateHandleMap();
            }
            else
            {
                tracer.Trace( "  ERROR: create file sz failed with error %d = %s\n", errno, strerror( errno ) );
                if ( 1 == errno || 13 == errno ) // permission denied
                    cpu.set_ax( 5 );
                else
                    cpu.set_ax( 2 ); // file not found
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x3d:
        {
            // open file. DS:dx pointer to asciiz pathname. al= open mode (dos 2.x ignores). AX=handle
    
            char * original_path = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * path = DOSToHostPath( original_path );
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

                if ( !_stricmp( original_path, "CON" ) ||
                     !_stricmp( original_path, "\\DEV\\CON" ) ||
                     !_stricmp( original_path, "/dev/con" ) )
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
                    UpdateHandleMap();
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
            handle = MapFileHandleCobolHack( handle );

            if ( handle <= 4 )
            {
                tracer.Trace( "  close of built-in handle ignored\n" );
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
                            tracer.Trace( "  close file handle %04x, fp %p\n", handle, fp );
                            fclose( fp );
                            cpu.set_carry( false );
                            UpdateHandleMap();
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

            uint16_t handle = cpu.get_bx();
            handle = MapFileHandleCobolHack( handle );
            tracer.Trace( "  handle %04x, mapped %04x, bytes %04x\n", cpu.get_bx(), handle, cpu.get_cx() );

            if ( handle <= 4 )
            {
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                if ( 0 == handle )
                {
                    if ( g_use80xRowsMode )
                        UpdateDisplay();
    
#if USE_ASSEMBLY_FOR_KBD
                    // This assembler version allows the emulator to send timer and keyboard interrupts

                    invoke_assembler_routine( g_int21_3f_seg );
#else
                    // Callers like GWBasic ask for one character at a time but have no idea what a backspace is.
                    // So buffer until a cr, append a lf, and send that one character at a time.
    
                    uint16_t request_len = cpu.get_cx();
                    static char acBuffer[ 128 ] = {0};
    
                    uint8_t * p = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
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
                    uint16_t min_len = get_min( string_len, request_len );
                    cpu.set_ax( min_len );
                    uint8_t * pvideo = GetVideoMem();
                    GetCursorPosition( row, col );
                    tracer.Trace( "  min len: %d\n", min_len );
                    for ( uint16_t x = 0; x < min_len; x++ )
                    {
                         p[x] = acBuffer[x];
                         tracer.Trace( "  returning character %02x = '%c'\n", p[x], printable( p[x] ) );

                         if ( g_use80xRowsMode )
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

                    if ( g_use80xRowsMode )
                        ClearLastUpdateBuffer();
#endif

                    return;
                }
                else
                {
                    cpu.set_carry( true );
                    tracer.Trace( "  attempt to read from output handle %04x\n", handle );
                }
    
                return;
            }
    
            FILE * fp = FindFileEntry( handle );
            if ( fp )
            {
                uint16_t len = cpu.get_cx();
                uint8_t * p = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
                tracer.Trace( "  read from file using handle %u fp %p %04x bytes at address %02x:%02x. offset just beyond: %02x\n",
                              cpu.get_bx(), fp, len, cpu.get_ds(), cpu.get_dx(), cpu.get_dx() + len );
                uint32_t cur = ftell( fp );
                uint32_t size = portable_filelen( fp );
                cpu.set_ax( 0 );
                tracer.Trace( "  cur: %u, size %u\n", cur, size );

                cur = ftell( fp );
                tracer.Trace( "  latest cur: %u\n", cur );
    
                if ( cur < size )
                {
                    uint32_t toRead = get_min( (uint32_t) len, size - cur );
                    memset( p, 0, toRead );
                    tracer.Trace( "  attempting to read %u bytes \n", toRead );
                    size_t numRead = fread( p, 1, toRead, fp );
                    if ( numRead )
                    {
                        cpu.set_ax( (uint16_t) toRead );
                        tracer.Trace( "  successfully read %04x (%u) bytes\n", toRead, toRead );
                        tracer.TraceBinaryData( p, toRead, 4 );
                    }
                    else
                    {
                        if ( feof( fp ) )
                            tracer.Trace( "  ERROR: can't read because we're at the end of the file\n" );
                        else
                            tracer.Trace( "  ERROR: failed to read fp %p, error %d = %s\n", fp, errno, strerror( errno ) );
                    }
                }
                else
                    tracer.Trace( "  ERROR: attempt to read beyond the end of file\n" );
    
                cpu.set_carry( false );
            }
            else
            {
                tracer.Trace( "  ERROR: read from file handle couldn't find handle %04x\n", handle );
                cpu.set_ax( 6 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x40:
        {
            // write to file using handle. BX=handle, CX=num bytes, DS:DX: buffer
            // on output: AX = # of bytes read or if CF is set 5=access denied, 6=invalid handle.
    
            uint16_t handle = cpu.get_bx();
            handle = MapFileHandleCobolHack( handle );
            tracer.Trace( "  handle %04x, mapped %04x, bytes %04x\n", cpu.get_bx(), handle, cpu.get_cx() );

            cpu.set_carry( false );
            if ( handle <= 4 )
            {
                cpu.set_ax( cpu.get_cx() );
    
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                uint8_t * p = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
    
                if ( 1 == handle || 2 == handle )
                {
                    if ( g_use80xRowsMode )
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
                                if ( row >= GetScreenRowsM1() )
                                {
                                    tracer.Trace( "  carriage scrolling up a line\n"  );
                                    scroll_up( pbuf, 1, 0, 0, GetScreenRowsM1(), ScreenColumnsM1 );
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
                                if ( col >= ScreenColumns )
                                    col = 0;
                                SetCursorPosition( row, col );
                            }
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
                        fflush( 0 ); // Linux needs this or the output is cached
                    }
                }
                return;
            }
    
            FILE * fp = FindFileEntry( handle );
            if ( fp )
            {
                uint16_t len = cpu.get_cx();
                uint8_t * p = cpu.flat_address8( cpu.get_ds(), cpu.get_dx() );
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
                tracer.Trace( "  ERROR: write to file handle couldn't find handle %04x\n", handle );
                cpu.set_ax( 6 );
                cpu.set_carry( true );
            }
    
            return;
        }
        case 0x41:
        {
            // delete file: ds:dx has asciiz name of file to delete.
            // return: cf set on error, ax = error code
    
            char * original_path = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * pfile = DOSToHostPath( original_path );
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
            // on success, set cx:dx to the current offset from the start of the file.

            uint16_t handle = cpu.get_bx();
            handle = MapFileHandleCobolHack( handle );
            int32_t offset = ( ( (int32_t) cpu.get_cx() ) << 16 ) | cpu.get_dx();
            tracer.Trace( "  move file pointer; original handle %04x, mapped handle %04x, offset %d\n", cpu.get_bx(), handle, offset );

            if ( handle <= 4 ) // built-in handle
            {
                cpu.set_carry( false );
                return;
            }

            FILE * fp = FindFileEntry( handle );
            if ( fp )
            {
                uint8_t origin = cpu.al();
                if ( origin > 2 )
                {
                    tracer.Trace( "  ERROR: move file pointer file handle has invalid mode/origin %u\n", origin );
                    cpu.set_ax( 1 );
                    cpu.set_carry( true );
                    return;
                }
    
                tracer.Trace( "  move file pointer using handle %04x to %d bytes from %s\n", handle, offset,
                              0 == origin ? "beginning" : 1 == origin ? "current" : "end" );
    
                uint32_t cur = ftell( fp );
                fseek( fp, 0, SEEK_END );
                uint32_t size = ftell( fp );
                fseek( fp, cur, SEEK_SET );
                tracer.Trace( "  file size is %u, current offset is %u\n", size, cur );
    
                if ( 0 == origin )
                    fseek( fp, offset, SEEK_SET );
                else if ( 1 == origin )
                    fseek( fp, offset, SEEK_CUR );
                else 
                    fseek( fp, offset, SEEK_END );
    
                cur = ftell( fp );
                cpu.set_ax( cur & 0xffff );
                cpu.set_dx( ( cur >> 16 ) & 0xffff );
    
                tracer.Trace( "  updated file offset is %u\n", cur );
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
    
            char * pfile = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            tracer.Trace( "  get/put file attributes on file '%s'\n", pfile );
            const char * hostPath = DOSToHostPath( pfile );
            cpu.set_carry( true );
    
            if ( 0 == cpu.al() ) // get
            {
                if ( !_stricmp( pfile, "CON" ) ||
                     !_stricmp( pfile, "\\DEV\\CON" ) ||
                     !_stricmp( pfile, "/dev/con" ) )
                {
                    cpu.set_cx( 0 );
                    cpu.set_carry( false );
                    tracer.Trace( "  get file attributes on 'con': cx %04x\n", cpu.get_cx() );
                    return;
                }

#ifdef _WIN32                
                uint32_t attr = GetFileAttributesA( hostPath );
                if ( INVALID_FILE_ATTRIBUTES != attr )
                {
                    cpu.set_carry( false );
                    cpu.set_cx( ( attr & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) ) );
                    tracer.Trace( "  read get file attributes: cx %04x\n", cpu.get_cx() );
                }
                else
                    cpu.set_ax( (uint16_t) GetLastError() ); // most errors map OK (file not found, path not found, etc.)
#else
                struct stat statbuf;
                int ret = stat( hostPath, & statbuf );
                if ( 0 == ret )
                {
                    cpu.set_carry( false );
                    uint16_t attribs = 0;
                    if ( !S_ISREG( statbuf.st_mode ) )
                        attribs |= 0x10;
                    cpu.set_cx( attribs );

                    tracer.Trace( "  get file attributes: cx %04x\n", cpu.get_cx() );
                }
                else
                {
                    tracer.Trace( "  ERROR: unable to get file attributes on linux path '%s'\n", hostPath );
                    cpu.set_ax( 2 ); // file not found
                }
#endif                
            }
            else
            {
                tracer.Trace( "  put file attributes on '%s' to %04x\n", hostPath, cpu.get_cx() );
#ifdef _WIN32                
                BOOL ok = SetFileAttributesA( hostPath, cpu.get_cx() );
                cpu.set_carry( !ok );
#endif                
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
    
                        // This distinction is important for gwbasic and qbasic-generated-apps for both input and output to work in both modes.
                        // The 0x80 bit indicates it's a character device (cursor movement, etc.) vs. a file (or a teletype).
                        // Apps generated by quick basic 1 output to both the display memory and handle 1 if we tell the truth,
                        // so lie and say file/teletype are files. That doesn't break any other apps.

                        uint16_t result = 0;
    
                        if ( 0 == handle ) // stdin
                            result = 1;
                        else if ( 1 == handle ) // stdout
                            result = 2;

                        if ( g_use80xRowsMode )
                            result |= 0x80;

                        tracer.Trace( "  handle %u result %u\n", handle, result );
                        cpu.set_dx( result );
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

            if ( existing_handle <= 4 )
            {
                cpu.set_ax( existing_handle );
                cpu.set_carry( false );
                tracer.Trace( "  fake duplicate of built-in handle\n" );
                return;
            }

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
#ifdef _WIN32            
            if ( GetCurrentDirectoryA( sizeof( cwd ), cwd ) )
            {
                tracer.Trace( "  cwd '%s', g_acRoot '%s'\n", cwd, g_acRoot );
                size_t len_cur = strlen( cwd );
                size_t len_root = strlen( g_acRoot );
                if ( !strncmp( cwd, g_acRoot, len_root - 1 ) )
                {
                    assert( len_cur >= ( len_root - 1 ) );
                    size_t to_copy = 1 + len_cur - ( len_root - 1 );
                    assert( to_copy >= 1 );
                    memcpy( cwd + 3, cwd + get_min( len_cur, len_root ), to_copy );
                    tracer.Trace( "  removed root from current directory: '%s'\n", cwd );
                }

                char * past_start = cwd + 3; // result does not contain drive or initial backslash 'x:\'
                if ( strlen( past_start ) <= 63 )
                {
                    strcpy( (char *) cpu.flat_address( cpu.get_ds(), cpu.get_si() ), past_start );
                    tracer.Trace( "  returning current directory '%s'\n", past_start );
                    cpu.set_carry( false );
                }
            }
            else
                tracer.Trace( "  ERROR: unable to get the current working directory, error %d\n", GetLastError() );
#else
            char acCurDir[ MAX_PATH ];
            if ( getcwd( acCurDir, sizeof( acCurDir ) ) )            
            {
                tracer.Trace( "  acCurDir '%s', g_acRoot '%s'\n", acCurDir, g_acRoot );
                size_t len_cur = strlen( acCurDir );
                size_t len_root = strlen( g_acRoot );
                if ( !memcmp( acCurDir, g_acRoot, len_root - 1 ) )
                {
                    assert( len_cur >= ( len_root - 1 ) );
                    size_t to_copy = 1 + len_cur - ( len_root - 1 );
                    assert( to_copy >= 1 );
                    tracer.Trace( "  len_root %d, len_cur %d, to_copy %d\n", len_root, len_cur, to_copy );
                    memcpy( acCurDir + 1, acCurDir + get_min( len_cur, len_root ), to_copy );
                    tracer.Trace( "  removed root from current directory: '%s'\n", acCurDir );
                }

                slash_to_backslash( acCurDir );
                strcpy( cwd, "C:" );
                strcat( cwd, acCurDir );
                tracer.Trace( "  cwd: '%s'\n", cwd );                
                char * past_start = cwd + 3; // result does not contain drive or initial backslash 'x:\'
                if ( strlen( past_start ) <= 63 )
                {
                    strcpy( (char *) cpu.flat_address( cpu.get_ds(), cpu.get_si() ), past_start );
                    tracer.Trace( "  returning current directory: '%s'\n", past_start );
                    cpu.set_carry( false );
                }
            }
            else
                tracer.Trace( "  ERROR: unable to get the current working directory, error %d\n", errno );
#endif            
    
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
                maxParas = g_segHardware - g_allocEntries[ entry ].segment;
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

            const char * originalPath = (const char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * pathToExecute = DOSToHostPath( originalPath );
            tracer.Trace( "  CreateProcess mode %u path to execute: '%s'\n", mode, pathToExecute );

            char acCommandPath[ MAX_PATH ];
            strcpy( acCommandPath, pathToExecute );
            if ( '@' == acCommandPath[ 0 ] )
            {
                // DOS v2 command.com uses an '@' in this case. Not sure why.

#ifdef _WIN32
                GetCurrentDirectoryA( sizeof( cwd ), cwd );
                acCommandPath[ 0 ] = cwd[ 0 ];
#else
                acCommandPath[ 0 ] = 'C';
#endif                                
            }

            bool found = FindCommandInPath( acCommandPath );
            if ( !found )
            {
                cpu.set_ax( 2 );
                cpu.set_carry( true );
                tracer.Trace( "  result of CreateProcess: command not found error 2\n" );
                return;
            }

            if ( 3 == mode )
            {
                AppExecuteMode3 * pae = (AppExecuteMode3 *) cpu.flat_address( cpu.get_es(), cpu.get_bx() );
                pae->Trace();

                uint16_t result = LoadOverlay( acCommandPath, pae->segLoadAddress, pae->segmentRelocationFactor );
                cpu.set_ax( result );
                cpu.set_carry( false );
                tracer.Trace( "  result of LoadOverlay: %#x\n", result );
                return;
            }

            AppExecute * pae = (AppExecute *) cpu.flat_address( cpu.get_es(), cpu.get_bx() );
            pae->Trace();
            const char * commandTail = (const char *) cpu.flat_address( pae->segCommandTail, pae->offsetCommandTail );
            DOSFCB * pfirstFCB = (DOSFCB *) cpu.flat_address( pae->segFirstFCB, pae->offsetFirstFCB );
            DOSFCB * psecondFCB = (DOSFCB *) cpu.flat_address( pae->segSecondFCB, pae->offsetSecondFCB );

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
                                               & pae->func1SS, & pae->func1SP, & pae->func1CS, & pae->func1IP, false );
                if ( 0 != seg_psp )
                {
                    if ( 1 == mode )
                    {
                       // put 0xffff on the top of the child's stack.

                       pae->func1SP -= 2;
                       uint16_t * pAX = cpu.flat_address16( pae->func1SS, pae->func1SP );
                       *pAX = 0xffff;
                    }

                    strcpy( g_lastLoadedApp, acCommandPath );
                    DOSPSP * psp = (DOSPSP *) cpu.flat_address( seg_psp, 0 );
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
                    cpu.set_carry( false ); // meaningless because the child app is starting now
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

            cpu.set_al( (int8_t) g_appTerminationReturnCode );
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
            char * psearch_string = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            const char * hostSearch = DOSToHostPath( psearch_string );
            pff->search_attributes = cpu.get_cx() & 0x1e; // only directory, system, volume, and hidden are honored
            assert( 0x15 == offsetof( DosFindFile, file_attributes ) );
            tracer.Trace( "  Find First Asciiz for pattern '%s' host '%s' attributes %#x\n", psearch_string, hostSearch, pff->search_attributes );

            CloseFindFirst();

#ifdef _WIN32
            WIN32_FIND_DATAA fd = {0};
            g_hFindFirst = FindFirstFileA( hostSearch, &fd );
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
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( (uint16_t) GetLastError() ); // interesting errors actually match
                tracer.Trace( "  WARNING: find first file failed, error %d\n", GetLastError() );
            }
#else

            LINUX_FIND_DATA lfd = {0};
            g_FindFirst = FindFirstFileLinux( hostSearch, lfd );
            if ( 0 != g_FindFirst )
            {
                do
                {
                    bool ok = ProcessFoundFile( pff, lfd );
                    if ( ok )
                    {
                        tracer.Trace( "  FindFirst processed found file '%s'\n", lfd.cFileName );
                        cpu.set_carry( false );
                        break;
                    }

                    bool found = FindNextFileLinux( g_FindFirst, lfd );
                    if ( !found )
                    {
                        memset( pff, 0, sizeof( DosFindFile ) );
                        cpu.set_ax( 0x12 ); // no more files
                        tracer.Trace( "  WARNING: find first file found no matching files\n" );
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( 2 ); // not found
                tracer.Trace( "  WARNING: find first file failed to find anything\n" );
            }                    
#endif            
    
            return;
        }
        case 0x4f:
        {
            // find next asciiz. ds:dx should be unchanged from find first, but they are not used below for Win32
    
            cpu.set_carry( true );
            DosFindFile * pff = (DosFindFile *) GetDiskTransferAddress();
            tracer.Trace( "  Find Next Asciiz\n" );
    
#ifdef _WIN32
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
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( 0x12 ); // no more files
                tracer.Trace( "  ERROR: search for next without a prior successful search for first\n" );
            }
#else
            if ( 0 != g_FindFirst )
            {
                do
                {
                    LINUX_FIND_DATA lfd = {0};
                    bool found = FindNextFileLinux( g_FindFirst, lfd );
                    if ( found )
                    {
                        bool ok = ProcessFoundFile( pff, lfd );
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
                        tracer.Trace( "  WARNING: find next file found no matching files\n" );
                        CloseFindFirst();
                        break;
                    }
                } while( true );
            }
            else
            {
                cpu.set_ax( 0x12 ); 
                tracer.Trace( "  ERROR: search for next without a prior successful search for first\n" );
            }                    

#endif            
    
            return;
        }
        case 0x50:
        {
            // set current process id PSP. set the psp to BX

            tracer.Trace( "  old psp %#x, new psp %#x\n", g_currentPSP, cpu.get_bx() );

            // check if it looks valid before setting -- an int 20 instruction to terminate an app like CP/M

            DOSPSP * psp = (DOSPSP *) cpu.flat_address( cpu.get_bx(), 0 );
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

            DOSPSP * psp = (DOSPSP *) cpu.flat_address( cpu.get_dx(), 0 );
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
    
            char * poldname = (char *) cpu.flat_address( cpu.get_ds(), cpu.get_dx() );
            char * pnewname = (char *) cpu.flat_address( cpu.get_es(), cpu.get_di() );

            char acOld[ MAX_PATH ];
            const char * pfile = DOSToHostPath( poldname );
            strcpy( acOld, pfile );
            poldname = acOld;
            char acNew[ MAX_PATH ];
            pfile = DOSToHostPath( pnewname );
            strcpy( acNew, pfile );
            pnewname = acNew;
    
            tracer.Trace( "renaming file '%s' to '%s', pointers are %p to %p\n", poldname, pnewname, poldname, pnewname );
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
            const char * hostPath = DOSToHostPath( path );
            if ( path )
            {
                if ( 0 == cpu.al() )
                {
                    uint16_t dos_time, dos_date;
                    if ( GetFileDOSTimeDate( hostPath, dos_time, dos_date ) )
                    {
                        cpu.set_ax( 0 );
                        cpu.set_carry( false );
                        cpu.set_cx( dos_time );
                        cpu.set_dx( dos_date );
                    }
                    else
                        cpu.set_ax( 1 );
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
        case 0xdd:
        {
            // novell netware 4.0 - set error mode. ignore.
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
             ( ( 0x16 == interrupt_num ) && ( 1 == c || 2 == c || 0x11 == c ) ) ) )
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
    else if ( 9 == interrupt_num )
    {
        consume_keyboard();
        g_int9_pending = false;
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
        cpu.set_ax( 0x002c );  // 80x25 color and >=64k installed
        return;
    }
    else if ( 0x12 == interrupt_num )
    {
        // memory size determination
        // .com apps like Turbo Pascal instead read from the Program Segment Prefix

        cpu.set_ax( 0x280 ); // 640K conventional RAM. # of contiguous 1k blocks not including video memory or extended RAM
        return;
    }
    else if ( 0x14 == interrupt_num )
    {
        tracer.Trace( "serial I/O interrupt\n" );
        // serial I/O

        if ( 1 == cpu.ah() ) // transmit
        {

        }
        else if ( 2 == cpu.ah() ) // receive
        {
            char ch = 0;
            int result = read( 0, &ch, 1 );
            tracer.Trace( "  result of read: %d, ch %02x = '%c'\n", result, ch, printable( ch ) );
            if ( 1 != result )
            {
                cpu.set_ah( 0x87 );
                cpu.set_al( 0 );
            }
            else
            {
                cpu.set_ah( 0 );
                cpu.set_al( ch );
            }
        }
        else
            tracer.Trace( "  unhandled serial I/O command AH %#02x\n", cpu.ah() );
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

#ifdef _WIN32
            ULONGLONG milliseconds = GetTickCount64();
#else
            struct timeval tv = {0};
            gettimeofday( &tv, NULL );
            uint64_t milliseconds = ( (uint64_t) tv.tv_sec * 1000ULL ) + ( (uint64_t) tv.tv_usec / 1000ULL );
#endif            

            milliseconds -= g_msAtStart;
            milliseconds *= 18206ULL;
            milliseconds /= 1000000ULL;
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

#ifdef _WIN32
            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_ch( toBCD( (uint8_t) st.wHour ) );
            cpu.set_cl( toBCD( (uint8_t) st.wMinute ) );
            cpu.set_dh( toBCD( (uint8_t) st.wSecond ) );
            cpu.set_dl( 0 );
#else            
            system_clock::time_point now = system_clock::now();
            uint64_t ms = duration_cast<milliseconds>( now.time_since_epoch() ).count() % 1000;
            time_t time_now = system_clock::to_time_t( now );
            struct tm * plocal = localtime( & time_now );

            cpu.set_ch( toBCD( (uint8_t) plocal->tm_hour ) );
            cpu.set_cl( toBCD( (uint8_t) plocal->tm_min ) );
            cpu.set_dh( toBCD( (uint8_t) plocal->tm_sec ) );
            cpu.set_dl( 0 );
#endif            
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
        DOSPSP * psp = (DOSPSP *) cpu.flat_address( g_currentPSP, 0 );
        if ( psp )
        {
            printf( "^C" );
            HandleAppExit();
        }

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
            if ( g_use80xRowsMode )
                UpdateDisplay();
            SleepAndScheduleInterruptCheck();
            cpu.set_al( 0x01 ); // not installed, do NOT install
        }
        else if ( 0x98 == cpu.ah() || 0x97 == cpu.ah() ) // micro focus cobol. 
        {
            // don't set al to anything or cobol and its generated apps won't run
        }
        else if ( 0x1687 == cpu.get_ax() ) // undocumented, used by cobol
            cpu.set_ax( 0 );
        else
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
    DOSPSP * psp = (DOSPSP *) cpu.flat_address( segment, 0 );
    memset( psp, 0, sizeof( DOSPSP ) );

    psp->int20Code = 0x20cd;                  // int 20 instruction to terminate app like CP/M
    psp->topOfMemory = g_segHardware - 1;     // top of memorysegment in paragraph form
    psp->comAvailable = 0xffff;               // .com programs bytes available in segment
    psp->int22TerminateAddress = firstAppTerminateAddress;
    uint8_t len = (uint8_t) strlen( acAppArgs );
    psp->countCommandTail = len;
    strcpy( (char *) psp->commandTail, acAppArgs );
    psp->commandTail[ len ] = 0x0d;           // DOS has a CR / 0x0d at the end of the command tail, not a null
    psp->segEnvironment = segEnvironment;
    memset( 1 + (char *) & ( psp->firstFCB ), ' ', 11 );
    memset( 1 + (char *) & ( psp->secondFCB ), ' ', 11 );
    psp->handleArraySize = 20;
    psp->handleArrayPointer = offsetof( DOSPSP, fileHandles );
    memset( & ( psp->fileHandles[0] ), 0xff, sizeof( psp->fileHandles ) );
    for ( uint8_t x = 0; x <= 4; x++ )
        psp->fileHandles[ x ] = x;

    // put the first argument in the first fcb if it looks like it may be an 8.3 filename

    if ( acAppArgs && acAppArgs[0] )
    {
        const char * pFirstArg = acAppArgs;
        while ( ' ' == *pFirstArg )
            pFirstArg++;
    
        if ( pFirstArg[ 0 ] )
        {
            tracer.Trace( "  app arguments: '%s'\n", pFirstArg );
            const char * pEnd = pFirstArg + strlen( pFirstArg );
            const char * pSpace = strchr( pFirstArg, ' ' );
            if ( pSpace )
                pEnd = pSpace;
    
            size_t full_len = ( pEnd - pFirstArg );
    
            if ( full_len <= 12 ) // 8.3
            {
                size_t name_len = full_len;
                size_t ext_len = 0;
                const char * pDot = strchr( pFirstArg, '.' );
                if ( pDot && ( pDot < pEnd ) )
                {
                    name_len = ( pDot - pFirstArg );
                    ext_len = full_len - name_len - 1;
                }
                tracer.Trace( "  first fcb filename info: full_len %zd, name_len %zd, ext_len %zd\n", full_len, name_len, ext_len );

                if ( name_len <= 8 )
                {
                    DOSFCB * pfirstFCB = (DOSFCB *) psp->firstFCB;
                    for ( size_t i = 0; i < name_len; i++ )
                        pfirstFCB->name[ i ] = (char) toupper( pFirstArg[ i ] );

                    if ( ext_len )
                        for ( size_t i = 0; i < ext_len; i++ )
                            pfirstFCB->ext[ i ] = (char) toupper( pDot[ 1 + i ] );
                }
            }
        }
    }

    psp->Trace();
} //InitializePSP

bool IsBinaryCOM( const char * app, FILE * fp )
{
    bool isCOM = ends_with( app, ".com" );
    if ( isCOM )
    {
        // look for signature indicating it's actually a .exe file named .com. Later versions of DOS really do this.

        tracer.Trace( "  checking if '%s' is actually a .exe\n", app );
        char ac[2];

        uint32_t cur = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        bool ok = ( 1 == fread( ac, _countof( ac ), 1, fp ) );
        fseek( fp, cur, SEEK_SET );

        if ( !ok )
            return true;

        isCOM = ! ( 'M' == ac[0] && 'Z' == ac[1] );
    }

    return isCOM;
} //IsBinaryCOM

uint16_t LoadOverlay( const char * app, uint16_t CodeSegment, uint16_t segRelocationFactor )
{
    // Used by int21, 4b mode 3: Load Overlay and don't execute.
    // returns AX return code: 1 on failure, 0 on success
    // QuickPascal v1.0 uses this to run child process apps it built (with or without the debugger)

    tracer.Trace( "  in LoadOverlay\n" );
    CFile file( fopen( app, "rb" ) );
    if ( 0 == file.get() )
    {
        tracer.Trace( "can't open executable file, error %d\n", errno );
        return 1;
    }

    if ( IsBinaryCOM( app, file.get() ) )
    {
        long file_size = portable_filelen( file.get() );
        if ( file_size > ( 65536 - 0x100) )
        {
            tracer.Trace( "can't read .com file into RAM -- it's too big!\n" );
            return 1;
        }
    
        size_t blocks_read = fread( cpu.flat_address( CodeSegment, 0 ), file_size, 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "can't read .com file into RAM, error %d\n", errno );
            return 1;
        }
    }
    else // EXE
    {
        ExeHeader head = { 0 };
        size_t blocks_read = fread( & head, sizeof( head ), 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input executable head '%s', error %d\n", app, errno );
            return 1;
        }

        head.Trace();

        if ( 0x5a4d != head.signature )
        {
            tracer.Trace( "  exe isn't MZ\n" );
            return 1;
        }

        if ( head.reloc_table_offset > 100 )
        {
            tracer.Trace( "  probably not a 16-bit exe; head.reloc_table_offset: %u", head.reloc_table_offset );
            return 1;
        }

        uint32_t codeStart = 16 * (uint32_t) head.header_paragraphs;
        uint32_t imageSize = (uint32_t) head.blocks_in_file * 512;
        if ( 0 != head.bytes_in_last_block )
            imageSize -= ( 512 - head.bytes_in_last_block );
        imageSize -= codeStart; // don't include the header
        tracer.Trace( "  image size of code and initialized data: %u, code starts at %u\n", imageSize, codeStart );

        uint8_t * pcode = cpu.flat_address8( CodeSegment, 0 );
        fseek( file.get(), codeStart, SEEK_SET );
        blocks_read = fread( pcode, imageSize, 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input exe file image, error %d", errno );
            return 1;
        }

        tracer.Trace( "  start of the code:\n" );
        tracer.TraceBinaryData( pcode, get_min( imageSize, (uint32_t) 0x100 ), 4 );

        if ( 0 != head.num_relocs )
        {
            vector<ExeRelocation> relocations( head.num_relocs );
            fseek( file.get(), head.reloc_table_offset, SEEK_SET );
            blocks_read = fread( relocations.data(), head.num_relocs * sizeof( ExeRelocation ), 1, file.get() );
            if ( 1 != blocks_read )
            {
                tracer.Trace( "  can't read input exe file relocation data, error %d", errno );
                return 1;
            }
    
            for ( uint16_t r = 0; r < head.num_relocs; r++ )
            {
                uint32_t offset = (uint32_t) relocations[ r ].offset + (uint32_t) relocations[ r ].segment * 16;
                uint16_t * target = (uint16_t *) ( pcode + offset );
                //tracer.TraceQuiet( "  relocation %u offset %u, update %#02x to %#02x\n", r, offset, *target, *target + segRelocationFactor );
                *target += segRelocationFactor;
            }
        }
    }

    return 0;
} //LoadOverlay

uint16_t LoadAsBootSector( const char * acApp, const char * acAppArgs, uint16_t segEnvironment )
{
    // create a dummy PSP
    uint16_t paragraphs_free = 0;
    uint16_t BSSegment = AllocateMemory( 0x1000, paragraphs_free );
    if ( 0 == BSSegment )
    {
        tracer.Trace( "  insufficient ram available RAM to load boot sector, in paragraphs: %04x required, %04x available\n", 0x1000, paragraphs_free );
        return 0;
    }

    tracer.Trace( "  loading boot sector, BSSegment is %04x\n", BSSegment );
    InitializePSP( BSSegment, acAppArgs, segEnvironment );

    CFile file( fopen( acApp, "rb" ) );
    if ( 0 == file.get() )
    {
        tracer.Trace( "open boot sector file, error %d\n", errno );
        FreeMemory( BSSegment );
        return 0;
    }
    
    long file_size = portable_filelen( file.get() );
    if ( 512 != file_size )
    {
        tracer.Trace( "error: boot sector file isn't 512 bytes\n" );
        FreeMemory( BSSegment );
        return 0;
    }
    
    size_t blocks_read = fread( cpu.flat_address( 0x7c0, 0 ), 512, 1, file.get() );
    if ( 1 != blocks_read )
    {
        tracer.Trace( "can't read boot sector file into RAM, error %d\n", errno );
        FreeMemory( BSSegment );
        return 0;
    }
    
    // prepare to execute the boot sector

    cpu.set_cs( 0x7c0 );
    cpu.set_ss( 0x7c0 );
    cpu.set_sp( 0xffff );
    cpu.set_ip( 0 );
    cpu.set_ds( 0x7c0 );
    cpu.set_es( 0x7c0 );
    tracer.Trace( "  loaded %s, app segment %04x, ip %04x, sp %04x\n", acApp, cpu.get_cs(), cpu.get_ip(), cpu.get_sp() );

    return BSSegment;
} //LoadAsBootSector

uint16_t LoadBinary( const char * acApp, const char * acAppArgs, uint16_t segEnvironment, bool setupRegs,
                     uint16_t * reg_ss, uint16_t * reg_sp, uint16_t * reg_cs, uint16_t * reg_ip, bool bootSectorLoad )
{
    if ( bootSectorLoad )
        return LoadAsBootSector( acApp, acAppArgs, segEnvironment );

    CFile file( fopen( acApp, "rb" ) );
    if ( 0 == file.get() )
    {
        tracer.Trace( "  can't open input executable '%s', error %d\n", acApp, errno );
        return 0;
    }

    uint16_t psp = 0;
    if ( IsBinaryCOM( acApp, file.get() ) )
    {
        // allocate 64k for the .COM file

        uint16_t paragraphs_free = 0;
        uint16_t ComSegment = AllocateMemory( 0x1000, paragraphs_free );
        if ( 0 == ComSegment )
        {
            tracer.Trace( "  insufficient ram available RAM to load .com, in paragraphs: %04x required, %04x available\n", 0x1000, paragraphs_free );
            return 0;
        }

        psp = ComSegment;
        InitializePSP( ComSegment, acAppArgs, segEnvironment );
        tracer.Trace( "  loading com, ComSegment is %04x\n", ComSegment );

        long file_size = portable_filelen( file.get() );
        if ( file_size > ( 65536 - 0x100) )
        {
            tracer.Trace( "can't read .com file into RAM -- it's too big!\n" );
            FreeMemory( ComSegment );
            return 0;
        }
    
        size_t blocks_read = fread( cpu.flat_address( ComSegment, 0x100 ), file_size, 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "can't read .com file into RAM, error %d\n", errno );
            FreeMemory( ComSegment );
            return 0;
        }

        // ensure the last two bytes (the top of the stack) are 0 so ret at app end exits the app via cp/m legacy mode

        uint16_t * pstacktop = cpu.flat_address16( ComSegment, 0xfffe );
        *pstacktop = 0;

        // prepare to execute the COM file

        if ( setupRegs )
        {
            cpu.set_cs( ComSegment );
            cpu.set_ss( ComSegment );
            cpu.set_sp( 0xfffe );          // word at the top is reserved for return address of 0
            cpu.set_ip( 0x100 );
            cpu.set_ds( ComSegment );
            cpu.set_es( ComSegment );
            tracer.Trace( "  loaded %s, app segment %04x, ip %04x, sp %04x\n", acApp, cpu.get_cs(), cpu.get_ip(), cpu.get_sp() );
        }
        else
        {
            *reg_ss = ComSegment;
            *reg_sp = 0xfffe;
            *reg_cs = ComSegment;
            *reg_ip = 0x100;
            tracer.Trace( "  loaded %s but didn't initialize registers, app segment %04x, ip %04x\n", acApp, cpu.get_cs(), cpu.get_ip() );
        }
    }
    else // EXE
    {
        ExeHeader head = { 0 };
        size_t blocks_read = fread( & head, sizeof( head ), 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input executable head '%s', error %d\n", acApp, errno );
            return 0;
        }

        head.Trace();

        if ( 0x5a4d != head.signature )
        {
            tracer.Trace( "  exe isn't MZ\n" );
            return 0;
        }

        if ( head.reloc_table_offset > 100 )
        {
            tracer.Trace( "  probably not a 16-bit exe; head.reloc_table_offset: %u", head.reloc_table_offset );
            return 0;
        }

        uint32_t codeStart = 16 * (uint32_t) head.header_paragraphs;
        uint32_t imageSize = (uint32_t) head.blocks_in_file * 512;
        if ( 0 != head.bytes_in_last_block )
            imageSize -= ( 512 - head.bytes_in_last_block );
        imageSize -= codeStart; // don't include the header
        tracer.Trace( "  image size of code and initialized data: %u, code starts at %u\n", imageSize, codeStart );

        uint16_t paragraphs_free = 0;
        uint16_t requested_paragraphs = 0xffff;
        uint16_t image_paragraphs = (uint16_t) ( round_up( imageSize, (uint32_t) 16 ) / (uint32_t) 16 );
        tracer.Trace( "  image_paragraphs: %04x\n", image_paragraphs );
        uint16_t required_paragraphs = head.min_extra_paragraphs + image_paragraphs;
        tracer.Trace( "  required_paragraphs: %04x\n", required_paragraphs );

        if ( 0xffff != head.max_extra_paragraphs )
        {
            if ( head.max_extra_paragraphs < head.min_extra_paragraphs )
                requested_paragraphs = head.min_extra_paragraphs + image_paragraphs;
            else if ( ( (uint32_t) head.max_extra_paragraphs + (uint32_t) image_paragraphs ) < (uint32_t) 0xffff )
                requested_paragraphs = head.max_extra_paragraphs + image_paragraphs;

            tracer.Trace( "  adjusted requested_paragraphs %04x\n", requested_paragraphs );
        }

        uint16_t DataSegment = AllocateMemory( requested_paragraphs, paragraphs_free );
        if ( 0 == DataSegment )
        {
            if ( required_paragraphs > paragraphs_free )
            {
                tracer.Trace( "  insufficient RAM. required_paragraphs > image_paragraphs %04x > %04x\n", required_paragraphs, image_paragraphs );
                return 0;
            }

            DataSegment = AllocateMemory( paragraphs_free, paragraphs_free );
        }

        if ( 0 == DataSegment )
        {
            tracer.Trace( "  insufficient RAM to load data segment for .exe\n" );
            return 0;
        }

        tracer.Trace( "  loading exe, DataSegment is %04x\n", DataSegment );
        psp = DataSegment;
        InitializePSP( DataSegment, acAppArgs, segEnvironment );

        tracer.Trace( "  loading app %s\n", acApp );
        tracer.Trace( "  looks like an MZ exe... size from blocks %u, bytes in last block %u\n",
                      ( (uint32_t) head.blocks_in_file ) * 512, head.bytes_in_last_block );

        const uint16_t CodeSegment = DataSegment + 16; //  data segment + 256 bytes (16 paragraphs) for the psp
        uint8_t * pcode = cpu.flat_address8( CodeSegment, 0 );
        fseek( file.get(), codeStart, SEEK_SET );
        blocks_read = fread( pcode, imageSize, 1, file.get() );
        if ( 1 != blocks_read )
        {
            tracer.Trace( "  can't read input exe file image, error %d", errno );
            FreeMemory( DataSegment );
            return 0;
        }

        tracer.Trace( "  start of the code:\n" );
        tracer.TraceBinaryData( pcode, get_min( imageSize, (uint32_t) 0x100 ), 4 );

        if ( 0 != head.num_relocs )
        {
            vector<ExeRelocation> relocations( head.num_relocs );
            fseek( file.get(), head.reloc_table_offset, SEEK_SET );
            blocks_read = fread( relocations.data(), head.num_relocs * sizeof( ExeRelocation ), 1, file.get() );
            if ( 1 != blocks_read )
            {
                tracer.Trace( "  can't read input exe file relocation data, error %d", errno );
                FreeMemory( DataSegment );
                return 0;
            }
    
            for ( uint16_t r = 0; r < head.num_relocs; r++ )
            {
                uint32_t offset = (uint32_t) relocations[ r ].offset + (uint32_t) relocations[ r ].segment * 16;
                uint16_t * target = (uint16_t *) ( pcode + offset );
                //tracer.TraceQuiet( "  relocation %u offset %u, update %#02x to %#02x\n", r, offset, *target, *target + CodeSegment );
                *target += CodeSegment;
            }
        }

        if ( setupRegs )
        {
            cpu.set_cs( CodeSegment + head.relative_cs );
            cpu.set_ss( CodeSegment + head.relative_ss );
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
            *reg_ss = CodeSegment + head.relative_ss;
            *reg_sp = head.sp;
            *reg_cs = CodeSegment + head.relative_cs;
            *reg_ip = head.ip;
            tracer.Trace( "  loaded %s suspended (mode 1), cs %04x, ip %04x, ss %04x, sp %04x\n",
                          acApp, *reg_cs, *reg_ip, *reg_ss, *reg_sp );
        }
    }

    return psp;
} //LoadBinary

#ifdef _WIN32
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

            if ( !g_KbdPeekAvailable )
            {
                uint8_t asciiChar, scancode;
                if ( peek_keyboard( asciiChar, scancode ) )
                {
                    tracer.Trace( "%llu async thread (woken via %s) noticed that a keystroke is available: %02x%02x == '%c'\n",
                                time_since_last(), ( 1 == ret ) ? "console input" : "timeout", scancode, asciiChar, printable( asciiChar ) );
                    g_KbdPeekAvailable = true; // make sure an int9 gets scheduled
                    SetEvent( g_heventKeyStroke ); // if the main thread is sleeping waiting for input, wake it.
                    cpu.exit_emulate_early(); // no time to lose processing the keystroke
                }
            }
        } while( true );

        return 0;
    } //PeekKeyboardThreadProc
#else
    void * PeekKeyboardThreadProc( void * param )
    {
        tracer.Trace( "in peekkeyboardthreadproc for linux\n" );
        CSimpleThread & thread = * (CSimpleThread *) param;

        do
        {
            struct timespec to;
            clock_gettime( CLOCK_REALTIME, &to );
            to.tv_nsec += ( 20 * 1000000 ); // 20 milliseconds
            if ( to.tv_nsec >= 1000000000 ) // overflow
            {
                to.tv_sec += 1;
                to.tv_nsec -= 1000000000;
            }

            int err;
            {
                C_pthread_mutex_t_lock mtx_lock( thread.the_mutex );
                err = pthread_cond_timedwait( & thread.the_condition, & thread.the_mutex, & to );
            }

            if ( ETIMEDOUT == err )
            {
                // too chatty tracer.Trace( "peekkeyboardthreadproc timed out in the wait\n" );
                // check for keyboard input below
            }
            else if ( 0 == err )
            {
                tracer.Trace( "peekkeyboardthreadproc condition was signaled\n" );
                break;
            }
            else
            {
                tracer.Trace( "peekkeyboardthreadproc error on cond_timewait: %d\n", err );
                break;
            }

            if ( !g_KbdPeekAvailable )
            {
                if ( g_consoleConfig.portable_kbhit() )
                {
                    tracer.Trace( "async thread noticed that a keystroke is available\n" );
                    g_KbdPeekAvailable = true; // make sure an int9 gets scheduled
                    cpu.exit_emulate_early();  // no time to lose processing the keystroke
                }
            }
        } while( true );

        tracer.Trace( "falling out of peekkeyboardthreadproc for linux\n" );
        return 0;
    } //PeekKeyboardThreadProc
#endif

bool linux_same( char a, char b )
{
    bool aslash = ( a == '\\' || a == '/' );
    bool bslash = ( b == '\\' || b == '/' );
    return ( ( toupper( a ) == toupper( b ) ) || ( aslash && bslash ) );
} //linux_same

bool linux_starts_with( const char * str, const char * start )
{
    size_t len = strlen( str );
    size_t lenstart = strlen( start );

    if ( len < lenstart )
        return false;

    for ( int i = 0; i < lenstart; i++ )
        if ( !linux_same( str[ i ], start[ i ] ) )
            return false;

    return true;
} //linux_starts_with

void SquashDOSFullPathToRoot( char * fullPath )
{
    tracer.Trace( "  squash starting with '%s'\n", fullPath );
#ifdef _WIN32
    if ( starts_with( fullPath, g_acRoot ) )
    {
        size_t len_root = strlen( g_acRoot );
        size_t len_full = strlen( fullPath );
        size_t to_move = len_full - len_root;
        memmove( fullPath + 3, fullPath + len_root, to_move + 1 );
    }
#else
    if ( linux_starts_with( fullPath + 2, g_acRoot ) )
    {
        size_t len_root = strlen( g_acRoot );
        size_t len_full = strlen( fullPath );
        size_t to_move = len_full - len_root;
        memmove( fullPath + 3, fullPath + len_root + 2, to_move + 1 );
    }
#endif
    tracer.Trace( "  squash ending with '%s'\n", fullPath );
} //SquashDOSFullPathToRoot

uint16_t AllocateEnvironment( uint16_t segStartingEnv, const char * pathToExecute, const char * pcmdLineEnv )
{
    char fullPath[ MAX_PATH ];
#ifdef _WIN32    
    GetFullPathNameA( pathToExecute, _countof( fullPath ), fullPath, 0 );
#else
    char * fpath = realpath( pathToExecute, 0 );
    if ( !fpath )
    {
        tracer.Trace( "realpath failed, error %d\n", errno );
        return 0;
    }

    strcpy( fullPath, "C:" );
    slash_to_backslash( fpath );
    strcpy( fullPath + 2, fpath );
    free( fpath );
#endif

    SquashDOSFullPathToRoot( fullPath );
    tracer.Trace( "  full path of binary: '%s'\n", fullPath );

    const char * pComSpec = "COMSPEC=COMMAND.COM";
    const char * pBriefFlags = "BFLAGS=-kzr -mDJL";
    uint16_t bytesNeeded = (uint16_t) strlen( fullPath );
    uint16_t startLen = 0;
    char * pEnvStart = (char *) cpu.flat_address( segStartingEnv, 0 );
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

    bytesNeeded += (uint16_t) 512;

    uint16_t remaining;
    uint16_t segEnvironment = AllocateMemory( round_up_to( bytesNeeded, 16 ) / 16, remaining );
    if ( 0 == segEnvironment )
    {
        tracer.Trace( "can't allocate %d bytes for environment\n", bytesNeeded );
        return 0;
    }

    char * penvdata = (char *) cpu.flat_address( segEnvironment, 0 );
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
    uint16_t seg = ( cpu.flat_address16( 0, 0 ) )[ 2 * i + 1 ]; // lower uint_16 has offset, upper has segment
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

    high_resolution_clock::time_point tNow = high_resolution_clock::now();
    uint64_t diff = duration_cast<std::chrono::nanoseconds>( tNow - g_tAppStart ).count();
    return (uint32_t) ( diff / 54925100 );
} //GetBiosDailyTimer

int main( int argc, char * argv[] )
{
    try
    {
        char * posval = getenv( "OS" );
        g_UseOneThread = ( ( 0 != posval ) && !strcmp( posval, "RVOS" ) );
    
        g_consoleConfig.EstablishConsoleInput( (void *) ControlHandlerProc );
        g_tAppStart = high_resolution_clock::now();    

#ifdef _WIN32
        g_msAtStart = GetTickCount64(); // struct timeval not availabe on Win32
#else
        struct timeval tv = {0};
        gettimeofday( &tv, NULL );
        g_msAtStart = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
#endif

#ifndef _WIN32
        tzset(); // or localtime_r won't work correctly
#endif        

        // put the app name without a path or .exe into g_thisApp

        char * pname = argv[ 0 ];
#ifdef _WIN32
        char * plastslash = strrchr( pname, '\\' );
#else
        char * plastslash = strrchr( pname, '/' );
#endif

        if ( 0 != plastslash )
            pname = plastslash + 1;
        strcpy( g_thisApp, pname );
        char * pdot = strchr( g_thisApp, '.' );
        if ( pdot )
            *pdot = 0;
    
        memset( memory, 0, sizeof( memory ) );

#ifdef _WIN32
        g_hConsoleOutput = GetStdHandle( STD_OUTPUT_HANDLE );
        g_hConsoleInput = GetStdHandle( STD_INPUT_HANDLE );
        g_heventKeyStroke = CreateEvent( 0, FALSE, FALSE, 0 );
#endif

        init_blankline( DefaultVideoAttribute );
    
        char * pcAPP = 0;
        bool trace = false;
        uint64_t clockrate = 0;
        bool showPerformance = false;
        char acAppArgs[127] = {0}; // max length for DOS command tail
        bool traceInstructions = false;
        bool force80xRows = false;
        uint8_t rowCount = 25;
        bool clearDisplayOnExit = true;
        bool bootSectorLoad = false;
        bool printVideoMemory = false;
        char * penvVars = 0;
        static char acRootArg[ MAX_PATH ];
#ifdef _WIN32
        strcpy( acRootArg, "\\" );
        DWORD_PTR dwProcessAffinityMask = 0; // by default let the OS decide
#else
        strcpy( acRootArg, "/" );
#endif

        CKeyStrokes::KeystrokeMode keystroke_mode = CKeyStrokes::ksm_None; //CKeyStrokes::KeystrokeMode::ksm_None;
    
        for ( int i = 1; i < argc; i++ )
        {
            char *parg = argv[i];
            char c = *parg;
    
            if ( ( 0 == pcAPP ) && ( '-' == c
#if defined( WATCOM ) || defined( _WIN32 )
                || '/' == c
#endif
               ) )
            {
                char ca = (char) tolower( parg[1] );
    
                if ( 'b' == ca )
                    bootSectorLoad = true;
                else if ( 's' == ca )
                {
                    if ( ':' == parg[2] )
                        clockrate = strtoull( parg + 3 , 0, 10 );
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
                {
                    force80xRows = true;
                    if ( ':' == parg[2] )
                    {
                        rowCount = (uint8_t) strtoul( parg + 3, 0, 10 );
                        if ( rowCount > 50 || rowCount < 25 )
                            rowCount = 25;
                    }
                }
                else if ( 'd' == ca )
                    clearDisplayOnExit = false;
#ifndef _WIN32
                else if ( 'u' == ca )
                    g_forcePathsUpper = true;
                else if ( 'l' == ca )
                    g_forcePathsLower = true;            
#endif            
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
                else if ( 'k' == ca )
                {
                    char cmode = (char) tolower( parg[2] );
                    if ( 'w' == cmode )
                        keystroke_mode = CKeyStrokes::ksm_Write;
                    else if ( 'r' == cmode )
                        keystroke_mode = CKeyStrokes::ksm_Read;
                    else
                        usage( "invalid keystroke mode" );
                }
                else if ( 'm' == ca )
                    printVideoMemory = true;
                else if ( 'r' == ca )
                {
                    if ( ':' != parg[2] )
                        usage( "colon required after r argument" );
                    strcpy( acRootArg, parg + 3 );
                }
#ifdef _WIN32            
                else if ( 'z' == ca )
                {
                    if ( ':' == parg[2] )
                        dwProcessAffinityMask = _strtoui64( parg + 3, 0, 16 );
                    else
                        usage( "colon required after z argument" );
                }
#endif            
                else if ( 'v' == ca )
                    version();
                else if ( '?' == ca )
                    usage( 0 );
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
    
#ifdef _WIN32
        static wchar_t logFile[ MAX_PATH ];
        wsprintf( logFile, L"%S.log", g_thisApp );
#else
        static char logFile[ MAX_PATH + 10 ];
        sprintf( logFile, "%s.log", g_thisApp );
#endif    
        tracer.Enable( trace, logFile, true );
        tracer.SetQuiet( true );
        cpu.trace_instructions( traceInstructions );
    
        tracer.Trace( "Use one thread: %d\n", g_UseOneThread );
    
#ifdef _WIN32    
        GetFullPathNameA( acRootArg, _countof( g_acRoot ), g_acRoot, 0 );
        DWORD attr = GetFileAttributesA( g_acRoot );
        if ( ( INVALID_FILE_ATTRIBUTES == attr ) || ( 0 == ( attr & FILE_ATTRIBUTE_DIRECTORY ) ) )
            usage( "/r root argument isn't a folder" );
        size_t len = strlen( g_acRoot );
        if ( 0 == len )
            usage( "error parsing /r argument. does the folder exist?" );
        if ( '\\' != g_acRoot[ len - 1 ] )
            strcat( g_acRoot, "\\" );
#else
        char * fpath = realpath( acRootArg, 0 );
        if ( !fpath )
            usage( "error parsing /r argument" );
        strcpy( g_acRoot, fpath );
        free( fpath );
        size_t len = strlen( g_acRoot );
        if ( 0 == len )
            usage( "error parsing /r argument. does the folder exist?" );
        if ( '/' != g_acRoot[ len - 1 ] )
            strcat( g_acRoot, "/" );
        struct stat statbuf;
        int ret = stat( g_acRoot, & statbuf );
        if ( !S_ISDIR( statbuf.st_mode ) )
            usage( "/r root argument isn't a folder" );
#endif
        tracer.Trace( "root full path: '%s'\n", g_acRoot );

        if ( 0 == pcAPP )
        {
            usage( "no command specified" );
            assume_false; // prevent false prefast warning from the msft compiler
        }
    
        strcpy( g_acApp, pcAPP );
#ifdef _WIN32    
        _strupr( g_acApp );
#else
        const char * pLinuxPath = DOSToHostPath( g_acApp );
        strcpy( g_acApp, pLinuxPath );
#endif    

        if ( !file_exists( g_acApp ) )
        {
            if ( ends_with( g_acApp, ".com" ) || ends_with( g_acApp, ".exe" ) )
                usage( "can't find command file .com or .exe" );
            else
            {
                strcat( g_acApp, ".COM" );
                if ( !file_exists( g_acApp ) )
                {
                    char * dot = strstr( g_acApp, ".COM" );
                    strcpy( dot, ".EXE" );
                    if ( !file_exists( g_acApp ) )
                    {
#ifdef _WIN32                    
                        usage( "can't find command file" );
#else                    
                        tracer.Trace( "couldn't find input file '%s'\n", g_acApp );
                        char * pdot = strrchr( g_acApp, '.' );
                        strcpy( pdot, ".com" );
                        if ( !file_exists( g_acApp ) )
                        {
                            strcpy( pdot, ".exe" );
                            if ( !file_exists( g_acApp ) )
                            {
                                tracer.Trace( "looked last for '%s'\n", g_acApp );
                                usage( "can't find command file" );
                            }
                        }
#endif               
                    }     
                }
            }
        }

        // Microsoft Pascal v1.0 requires end of 64k block, not the middle of a block
        // Overload -h to do this as well -- have a conformant address space for apps.
    
        if ( ends_with( g_acApp, "pas2.exe" ) || g_PackedFileCorruptWorkaround )
            g_segHardware = 0xa000;

#ifdef _WIN32
        if ( 0 != dwProcessAffinityMask )
        {
            BOOL ok = SetProcessAffinityMask( (HANDLE) -1, dwProcessAffinityMask );
            tracer.Trace( "Result of SetProcessAffinityMask( %#x ) is %d\n", dwProcessAffinityMask, ok );
        }
#endif    

        g_keyStrokes.SetMode( keystroke_mode );
    
        // global bios memory
    
        uint8_t * pbiosdata = cpu.flat_address8( 0x40, 0 );
        * (uint16_t *) ( pbiosdata + 0x10 ) = 0x21;           // equipment list. diskette installed and initial video mode 0x20
        * (uint16_t *) ( pbiosdata + 0x13 ) = 640;            // contiguous 1k blocks (640 * 1024)
        * (uint16_t *) ( pbiosdata + 0x1a ) = 0x1e;           // keyboard buffer head
        * (uint16_t *) ( pbiosdata + 0x1c ) = 0x1e;           // keyboard buffer tail
        * (uint8_t *)  ( pbiosdata + 0x49 ) = DefaultVideoMode; // video mode is 3 == 80x25, 16 colors
        * (uint16_t *) ( pbiosdata + 0x4a ) = ScreenColumns;  // 80
        * (uint16_t *) ( pbiosdata + 0x4c ) = 0x1000;         // video regen buffer size
        * (uint8_t *)  ( pbiosdata + 0x60 ) = 7;              // cursor ending/bottom scan line
        * (uint8_t *)  ( pbiosdata + 0x61 ) = 6;              // cursor starting/top scan line
        * (uint8_t *)  ( pbiosdata + 0x62 ) = 0;              // current display page
        * (uint16_t *) ( pbiosdata + 0x63 ) = 0x3d4;          // base port for 6845 CRT controller. color
        * (uint16_t *) ( pbiosdata + 0x65 ) = 41;             // 6845 crt mode control register value
        * (uint16_t *) ( pbiosdata + 0x66 ) = 48;             // cga palette mask
        * (uint16_t *) ( pbiosdata + 0x72 ) = 0x1234;         // soft reset flag (bypass memteest and crt init)
        * (uint16_t *) ( pbiosdata + 0x80 ) = 0x1e;           // keyboard buffer start
        * (uint16_t *) ( pbiosdata + 0x82 ) = 0x3e;           // one byte past keyboard buffer start
        * (uint8_t *)  ( pbiosdata + 0x84 ) = DefaultScreenRows - 1; // 25 - 1
        * (uint8_t *)  ( pbiosdata + 0x87 ) = 0x60;           // video mode options for ega+
        * (uint8_t *)  ( pbiosdata + 0x88 ) = 9;              // ega feature bits
        * (uint8_t *)  ( pbiosdata + 0x89 ) = 0x51;           // video display area (400 line mode, vga active)
        * (uint8_t *)  ( pbiosdata + 0x8a ) = 0x8;            // 2 == CGA color, 8 == VGA color
        * (uint8_t *)  ( pbiosdata + 0x10f ) = 0;             // where GWBASIC checks if it's in a shelled command.com.
        * (uint8_t *)  ( cpu.flat_address8( 0xf000, 0xfff0 ) ) = 0xea;   // power on entry point (used by mulisp to detect if it's a standard PC) ea = jmp far
        * (uint8_t *)  ( cpu.flat_address8( 0xf000, 0xfff1 ) ) = 0xc0;   // "
        * (uint8_t *)  ( cpu.flat_address8( 0xf000, 0xfff2 ) ) = 0x12;   // "
        * (uint8_t *)  ( cpu.flat_address8( 0xf000, 0xfff3 ) ) = 0x00;   // "
        * (uint8_t *)  ( cpu.flat_address8( 0xf000, 0xfff4 ) ) = 0xf0;   // "
        * (uint8_t *)  ( cpu.flat_address8( 0xffff, 0xe ) ) = 0xff;      // original pc
    
#if 0
        // wordperfect 6.0 looks at +4 in lists of lists for a far pointer to the system file table.
        // make that pointer point to an empty system file table.
    
        * ( cpu.flat_address16( SegmentListOfLists, OffsetListOfLists + 4 ) ) = OffsetSystemFileTable;
        * ( cpu.flat_address16( SegmentListOfLists, OffsetListOfLists + 6 ) ) = SegmentListOfLists;
        * ( cpu.flat_address16( SegmentListOfLists, OffsetSystemFileTable ) ) = 0xffff;
        * ( cpu.flat_address16( SegmentListOfLists, OffsetSystemFileTable + 2 ) ) = 0xffff;
        * ( cpu.flat_address16( SegmentListOfLists, OffsetSystemFileTable + 4 ) ) = 0x30; // lots of files available
#endif
    
        // put dummy values in the list of lists
        uint16_t * pListOfLists = cpu.flat_address16( SegmentListOfLists, OffsetListOfLists );
        pListOfLists[ 2 ] = OffsetDeviceControlBlock; // low dword of first drive parameter block (ffff is end of list)
        pListOfLists[ 3 ] = SegmentListOfLists;       // high "
        uint16_t * pDeviceControlBlock = cpu.flat_address16( SegmentListOfLists, OffsetDeviceControlBlock );
        *pDeviceControlBlock = 0xffff; // end of list
    
        // 256 interrupt vectors at address 0 - 3ff. The first 0x40 are reserved for bios/dos and point to
        // routines starting at InterruptRoutineSegment. The routines are almost all the same -- fake opcode, interrupt #, retf 2
        // One exception is tick tock interrupt 0x1c, which just does an iret for performance.
        // Another is keyboard interrupt 9.
        // Interrupts 9 and 1c require an iret so flags are restored since these are externally, asynchronously triggered.
        // Other interrupts use far ret 2 (not iret) so as to not trash the flags (Z and C) used as return codes.
        // Functions are all allocated 5 bytes each.
    
        uint32_t * pVectors = (uint32_t *) cpu.flat_address( 0, 0 );
        uint8_t * pRoutines = cpu.flat_address8( InterruptRoutineSegment, 0 );
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
        // execution can start at ip 0.
    
#if USE_ASSEMBLY_FOR_KBD
        uint16_t curseg = MachineCodeSegment;
    
        memcpy( cpu.flat_address( curseg, 0 ), int21_3f_code, sizeof( int21_3f_code ) );
        g_int21_3f_seg = curseg;
        curseg += ( round_up_to( sizeof( int21_3f_code ), 16 ) / 16 );
    
        memcpy( cpu.flat_address( curseg, 0 ), int21_a_code, sizeof( int21_a_code ) );
        g_int21_a_seg = curseg;
        curseg += ( round_up_to( sizeof( int21_a_code ), 16 ) / 16 );
    
        memcpy( cpu.flat_address( curseg, 0 ), int21_1_code, sizeof( int21_1_code ) );
        g_int21_1_seg = curseg;
        curseg += ( round_up_to( sizeof( int21_1_code ), 16 ) / 16 );
    
        memcpy( cpu.flat_address( curseg, 0 ), int21_8_code, sizeof( int21_8_code ) );
        g_int21_8_seg = curseg;
        curseg += ( round_up_to( sizeof( int21_8_code ), 16 ) / 16 );
    
        memcpy( cpu.flat_address( curseg, 0 ), int16_0_code, sizeof( int16_0_code ) );
        g_int16_0_seg = curseg;
        curseg += ( round_up_to( sizeof( int16_0_code ), 16 ) / 16 );
    
        tracer.Trace( "machine code: 21_3f %04x, 21_a %04x, 21_1 %04x, 21_8 %04x, 16_0 %04x\n",
                      g_int21_3f_seg, g_int21_a_seg, g_int21_1_seg, g_int21_8_seg, g_int16_0_seg );
    
        assert( curseg <= InterruptRoutineSegment );
#endif

        // allocate the environment space and load the binary
    
        uint16_t segEnvironment = AllocateEnvironment( 0, g_acApp, penvVars );
        if ( 0 == segEnvironment )
            i8086_hard_exit( "unable to create environment for the app\n", 0 );
    
        g_currentPSP = LoadBinary( g_acApp, acAppArgs, segEnvironment, true, 0, 0, 0, 0, bootSectorLoad );
        if ( 0 == g_currentPSP )
            i8086_hard_exit( "unable to load executable\n", 0 );
    
        // gwbasic calls ioctrl on stdin and stdout before doing anything that would indicate what mode it wants.
        // turbo pascal v3 doesn't give a good indication that it wants 80x25.
        // word for DOS 6.0 is the same -- hard code it
    
        if ( ends_with( g_acApp, "gwbasic.exe" ) || ends_with( g_acApp, "mips.com" ) ||
             ends_with( g_acApp, "turbo.com" ) || ends_with( g_acApp, "word.exe" ) ||
             ends_with( g_acApp, "bc.exe" )  || ends_with( g_acApp, "mulisp.com" ) )
        {
            if ( !g_forceConsole )
                force80xRows = true;
        }

        if ( force80xRows )
        {
            SetScreenRows( rowCount );
            PerhapsFlipTo80xRows();
        }
    
        g_diskTransferSegment = cpu.get_ds();
        g_diskTransferOffset = 0x80; // same address as the second half of PSP -- the command tail
        g_haltExecution = false;
        cpu.set_interrupt( true ); // DOS starts apps with interrupts enabled
        uint32_t * pDailyTimer = (uint32_t *) ( pbiosdata + 0x6c );
    
        // Peek for keystrokes in a separate thread. Without this, some DOS apps would require polling in the loop below,
        // but keyboard peeks are very slow -- it makes cross-process calls. With the thread, the loop below is faster.
        // Note that kbhit() makes the same call interally to the same cross-process API. It's no faster.
    
        unique_ptr<CSimpleThread> peekKbdThread( g_UseOneThread ? 0 : new CSimpleThread( PeekKeyboardThreadProc ) );
    
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
    
            if ( g_use80xRowsMode )
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
    
                if ( g_UseOneThread && g_consoleConfig.throttled_kbhit() )
                    g_KbdPeekAvailable = true; // make sure an int9 gets scheduled
    
                if ( g_SendControlCInt )
                {
                    tracer.Trace( "scheduling an int x23 -- control C\n" );
                    g_SendControlCInt = false;
                    cpu.external_interrupt( 0x23 );
                    continue;
                }
    
                if ( g_KbdPeekAvailable && !g_int9_pending )
                {
                    tracer.Trace( "%llu main loop: scheduling an int 9 -- keyboard\n", time_since_last() );
                    cpu.external_interrupt( 9 );
                    g_int9_pending = true;
                    g_KbdPeekAvailable = false;
                    continue;
                }
    
                // if interrupt 8 (timer) or 0x1c (tick tock) are hooked by an app and 55 milliseconds have elapsed,
                // invoke int 8, which by default then invokes int 1c.
        
                if ( timer_changed && ( InterruptHookedByApp( 0x1c ) || InterruptHookedByApp( 8 ) ) )
                {
                    // on my machine this is invoked about every 72 million total_cycles if no throttle sleeping happened (tens of thousands if so)
        
                    tracer.Trace( "scheduling an int 8 -- timer, dt: %#x, total_cycles %llu\n", dt, total_cycles );
                    cpu.external_interrupt( 8 );
                    continue;
                }
            }
            else
            {
                if ( g_KbdPeekAvailable )
                    tracer.Trace( "can't schedule a keyboard int 9 because interrupts are disabled!\n" );
            }
        } while ( true );
    
        if ( g_use80xRowsMode )  // get any last-second screen updates displayed
            UpdateDisplay();
    
        high_resolution_clock::time_point tDone = high_resolution_clock::now();
    
        if ( !g_UseOneThread )
            peekKbdThread->EndThread();
    
        g_consoleConfig.RestoreConsole( clearDisplayOnExit );
#ifdef _WIN32
        CloseHandle( g_heventKeyStroke );
#endif

        if ( printVideoMemory )
            printDisplayBuffer( GetActiveDisplayPage() );
    
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
    
            #ifndef NDEBUG
                uint8_t unique_first_opcodes = cpu.trace_opcode_usage();
                printf( "unique first opcodes: %16u\n", unique_first_opcodes );
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
    }
    catch ( bad_alloc & e )
    {
        printf( "caught exception bad_alloc -- out of RAM. If in RVOS use -h or -m to add RAM. %s\n", e.what() );
    }
    catch ( exception & e )
    {
        printf( "caught a standard execption: %s\n", e.what() );
    }
    catch( ... )
    {
        printf( "caught a generic exception\n" );
    }

    tracer.Trace( "exit code of %s: %d\n", g_thisApp, g_appTerminationReturnCode );
    tracer.Shutdown();

    return g_appTerminationReturnCode; // return what the main app returned
} //main

