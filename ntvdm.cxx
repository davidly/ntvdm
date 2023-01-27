// NT Virtual DOS Machine. Not the real one, but one that works on 64-bit Windows.
// Written by David Lee
// This only simulates a small subset of DOS and BIOS behavior.
// I only implemented BIOS/DOS calls used by tested apps, so there are some big gaps.
// Only CGA text modes 2 and 3 (80x25 greyscale and color) are supported.
// No graphics, sound, mouse, or anything else not needed for simple command-line apps.
// tested apps:
//    Turbo Pascal 1.00A and 3.02A, both the apps and the programs they generate.
//    masm.exe V1.10 and link.exe V2.00 from MS-DOS v2.0
//    ba BASIC compiler in my ttt repo
//    Wordstar Release 4
//    GWBASIC.COM.
//    QBASIC 7.1 works aside from commands that invoke other executables (e.g. the compiler)
//    Apps the QBASIC compiler creates.
//    Brief 3.1. Use b.exe's -k flag for compatible keyboard handling. (automatically set in code below)
//    ExeHdr: Microsoft (R) EXE File Header Utility  Version 2.01
//    Link.exe: Microsoft (R) Segmented-Executable Linker  Version 5.10
//    BC.exe: Microsoft Basic compiler 7.10 (part of Quick Basic 7.1)
// I went from 8085/6800/Z80 machines to Amiga to IBM 360/370 to VAX/VMS to Unix to
// Windows to OS/2 to NT, and mostly skipped DOS programming. Hence this fills a gap
// in my knowledge.
//
// Useful: http://www2.ift.ulaval.ca/~marchand/ift17583/dosints.pdf
//         https://en.wikipedia.org/wiki/Program_Segment_Prefix
//         https://stanislavs.org/helppc/bios_data_area.html
//         https://stanislavs.org/helppc/scan_codes.html
//
// Memory map:
//     0x00000 -- 0x003ff   interrupt vectors; only claimed first x40 of slots for bios/DOS
//     0x00400 -- 0x007ff   bios data
//     0x00800 -- 0x00bff   where I put the environment. 1k seems like a lot.
//     0x00c00 -- 0x00fff   interrupt routines (here, not in BIOS space because it fits)
//     0x01000 -- 0x0191f   unused (nearly 4k at offset 4k)
//     0x01920 -- 0x9ffff   apps are loaded here, where DOS does it
//     0xa0000 -- 0xeffff   reserved for hardware
//     0xf0000 -- 0xfbfff   system monitor (0 for now)
//     0xfc000 -- 0xfffff   bios code and hard-coded bios data (mostly 0 for now)

#define UNICODE

#include <time.h>
#include <sys/timeb.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <conio.h>
#include <direct.h>
#include <io.h>
#include <assert.h>
#include <vector>

#include <djltrace.hxx>
#include <djl_con.hxx>
#include <djl_perf.hxx>
#include <djl_cycle.hxx>
#include <djl8086d.hxx>
#include "i8086.hxx"

struct FileEntry
{
    char path[ MAX_PATH ];
    FILE * fp;
    uint16_t handle; // DOS handle, not host OS
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
    uint16_t segment;
    uint16_t para_length;
};

const uint32_t ScreenColumns = 80;                                // this is the only mode supported
const uint32_t ScreenRows = 25;                                   // this is the only mode supported
const uint32_t ScreenColumnsM1 = ScreenColumns - 1;               // columns minus 1
const uint32_t ScreenRowsM1 = ScreenRows - 1;                     // rows minus 1
const uint32_t ScreenBufferSize = 2 * ScreenColumns * ScreenRows; // char + attribute
const uint32_t ScreenBufferAddress = 0xb8000;                     // location in i8086 physical RAM of CGA display. 16k, 4k per page.
const uint32_t AppSegmentOffset = 0x1920;                         // base address for apps in the vm. 8k. DOS uses 0x1920 == 6.4k
const uint16_t AppSegment = AppSegmentOffset / 16;                // works at segment 0 as well, but for fun...
const uint16_t SegmentHardware = 0xa000;                          // where hardware starts
const uint16_t SegmentsAvailable = SegmentHardware - AppSegment;  // hardware starts at 0xa000, apps load at AppSegment  
const uint32_t DOS_FILENAME_SIZE = 13;                            // 8 + 3 + '.' + 0-termination

static uint16_t blankLine[ScreenColumns] = {0};
static HANDLE g_hFindFirst = INVALID_HANDLE_VALUE;

CDJLTrace tracer;
static bool g_haltExecution = false;           // true when the app is shutting down
static uint8_t * g_DiskTransferAddress;        // where apps read/write data for i/o
static vector<FileEntry> g_fileEntries;        // vector of currently open files
static vector<DosAllocation> g_allocEntries;   // vector of blocks allocated to DOS apps
static bool g_use80x25 = false;                // true to force 80x25 with cursor positioning
static uint8_t g_videoMode = 3;                // 2=80x25 16 grey, 3=80x25 16 colors
static HANDLE g_hConsole = 0;                  // the Windows console handle
static bool g_forceConsole = false;            // true to force teletype mode, with no cursor positioning
static ConsoleConfiguration g_consoleConfig;   // to get into and out of 80x25 mode
static bool g_int16_1_loop = false;            // true if an app is looping to get keyboard input. don't busy loop.
static bool g_KbdIntWaitingForRead = false;    // true when a kbd int happens and no read has happened since
static bool g_injectControlC = false;          // true when ^c is hit and it must be put in the keyboard buffer
static char g_acApp[ MAX_PATH ];               // the DOS .com or .exe being run

#pragma pack( push, 1 )
struct DosFindFile
{
    uint8_t undocumented[ 0x15 ];  // no attempt to mock this because I haven't found apps that use it

    uint8_t file_attributes;
    uint16_t file_time;
    uint16_t file_date;
    uint32_t file_size;
    char file_name[ DOS_FILENAME_SIZE ];          // 8.3, blanks stripped, null-terminated
};
#pragma pack(pop)

static int compare_alloc_entries( const void * a, const void * b )
{
    // sort by segment, low to high

    DosAllocation const * pa = (DosAllocation const *) a;
    DosAllocation const * pb = (DosAllocation const *) b;

    return pa->segment - pb->segment;
} //compare_alloc_entries

static int compare_file_entries( const void * a, const void * b )
{
    // sort by file handle, low to high

    FileEntry const * pa = (FileEntry const *) a;
    FileEntry const * pb = (FileEntry const *) b;

    return pa->handle - pb->handle;
} //compare_file_entries

FILE * RemoveFileEntry( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            FILE * fp = g_fileEntries[ i ].fp;
            tracer.Trace( "  removing file entry %s: %p\n", g_fileEntries[ i ].path, fp );
            g_fileEntries.erase( g_fileEntries.begin() + i );
            return fp;
        }
    }

    tracer.Trace( "ERROR: could not remove file entry for handle %04x\n", handle );
    return 0;
} //RemoveFileEntry

FILE * FindFileEntry( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            tracer.Trace( "  found file entry '%s': %p\n", g_fileEntries[ i ].path, g_fileEntries[ i ].fp );
            return g_fileEntries[ i ].fp;
        }
    }

    tracer.Trace( "ERROR: could not find file entry for handle %04x\n", handle );
    return 0;
} //FindFileEntry

const char * FindFileEntryPath( uint16_t handle )
{
    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( handle == g_fileEntries[ i ].handle )
        {
            tracer.Trace( "  found file entry '%s': %p\n", g_fileEntries[ i ].path, g_fileEntries[ i ].fp );
            return g_fileEntries[ i ].path;
        }
    }

    tracer.Trace( "ERROR: could not find file entry for handle %04x\n", handle );
    return 0;
} //FindFileEntryPath

uint16_t FindFirstFreeFileHandle()
{
    // Apps like the QuickBasic compiler (bc.exe) depend on the side effect that after a file
    // is closed and a new file is opened the lowest possible free handle value is used for the
    // newly opened file. It's a bug in the app, but it's not getting fixed.

    qsort( g_fileEntries.data(), g_fileEntries.size(), sizeof FileEntry, compare_file_entries );
    uint16_t freehandle = 0xa;

    for ( size_t i = 0; i < g_fileEntries.size(); i++ )
    {
        if ( g_fileEntries[ i ].handle != freehandle )
            return freehandle;
        else
            freehandle++;
    }

    return freehandle;
} //FindFirstFreeFileHandle

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

    tracer.Trace( "ERROR: could not find alloc entry for segment %04x\n", segment );
    return -1;
} //FindAllocationEntry

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

uint8_t * GetMem( uint16_t seg, uint16_t offset )
{
    return memory + ( ( ( (uint32_t) seg ) << 4 ) + offset );
} //GetMem

uint8_t * GetVideoMem()
{
    uint8_t activePage = * GetMem( 0x40, 0x62 );
    assert( activePage <= 3 );
    return memory + ScreenBufferAddress + 0x1000 * activePage;
} //GetVideoMem

void GetCursorPosition( uint8_t & row, uint8_t & col )
{
    uint8_t * pbiosdata = GetMem( 0x40, 0 );
    col = * (uint8_t *) ( pbiosdata + 0x50 );
    row = * (uint8_t *) ( pbiosdata + 0x51 );
} //GetCursorPosition

void SetCursorPosition( uint8_t row, uint8_t col )
{
    uint8_t * pbiosdata = GetMem( 0x40, 0 );
    * (uint8_t *) ( pbiosdata + 0x50 ) = col;
    * (uint8_t *) ( pbiosdata + 0x51 ) = row;
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

void DumpBinaryData( uint8_t * pData, uint32_t length, uint32_t indent )
{
    __int64 offset = 0;
    __int64 beyond = length;
    const __int64 bytesPerRow = 32;
    uint8_t buf[ bytesPerRow ];

    while ( offset < beyond )
    {
        tracer.Trace( "" );

        for ( uint32_t i = 0; i < indent; i++ )
            tracer.TraceQuiet( " " );

        tracer.TraceQuiet( "%#10llx  ", offset );

        __int64 cap = __min( offset + bytesPerRow, beyond );
        __int64 toread = ( ( offset + bytesPerRow ) > beyond ) ? ( length % bytesPerRow ) : bytesPerRow;

        memcpy( buf, pData + offset, toread );

        for ( __int64 o = offset; o < cap; o++ )
            tracer.TraceQuiet( "%02x ", buf[ o - offset ] );

        uint32_t spaceNeeded = ( bytesPerRow - ( cap - offset ) ) * 3;

        for ( ULONG sp = 0; sp < ( 1 + spaceNeeded ); sp++ )
            tracer.TraceQuiet( " " );

        for ( __int64 o = offset; o < cap; o++ )
        {
            char ch = buf[ o - offset ];

            if ( ch < ' ' || 127 == ch )
                ch = '.';
            tracer.TraceQuiet( "%c", ch );
        }

        offset += bytesPerRow;

        tracer.TraceQuiet( "\n" );
    }
} //DumpBinaryData

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
    uint8_t curRecord;
    uint32_t recNumber;      // where random reads start

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
};
#pragma pack(pop)

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
    SetConsoleCursorPosition( g_hConsole, pos );
} //UpdateWindowsCursorPosition

bool UpdateDisplay()
{
    static uint8_t bufferLastUpdate[ ScreenBufferSize ] = {0}; // used to check for changes in video memory
    assert( g_use80x25 );
    uint8_t * pbuf = GetVideoMem();

    if ( memcmp( bufferLastUpdate, pbuf, sizeof( bufferLastUpdate ) ) )
    {
        //tracer.Trace( "UpdateDisplay with changes\n" );
        #if false
            CONSOLE_SCREEN_BUFFER_INFOEX csbi = { 0 };
            csbi.cbSize = sizeof csbi;
            GetConsoleScreenBufferInfoEx( g_hConsole, &csbi );

            tracer.Trace( "  UpdateDisplay: pbuf %p, csbi size %d %d, window %d %d %d %d\n",
                          pbuf, csbi.dwSize.X, csbi.dwSize.Y,
                          csbi.srWindow.Left, csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom );
        #endif

        for ( uint16_t y = 0; y < ScreenRows; y++ )
        {
            uint32_t yoffset = y * ScreenColumns * 2;

            if ( memcmp( bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 ) )
            {
                memcpy( bufferLastUpdate + yoffset, pbuf + yoffset, ScreenColumns * 2 );
                char aChars[ScreenColumns]; // 8-bit not 16 bit or the wrong codepage is used
                WORD aAttribs[ScreenColumns];
                for ( uint16_t x = 0; x < _countof( aChars ); x++ )
                {
                    uint32_t offset = yoffset + x * 2;
                    aChars[ x ] = pbuf[ offset ];
                    aAttribs[ x ] = pbuf[ 1 + offset ];
                }
    
                #if false
                    tracer.Trace( "    row %02u: '%.80s'\n", y, aChars );
                #endif
    
                COORD pos = { 0, (SHORT) y };
                SetConsoleCursorPosition( g_hConsole, pos );
    
                BOOL ok = WriteConsoleA( g_hConsole, aChars, ScreenColumns, 0, 0 );
                if ( !ok )
                    tracer.Trace( "writeconsolea failed with error %d\n", GetLastError() );
    
                DWORD dwWritten;
                ok = WriteConsoleOutputAttribute( g_hConsole, aAttribs, ScreenColumns, pos, &dwWritten );
                if ( !ok )
                    tracer.Trace( "writeconsoleoutputattribute failed with error %d\n", GetLastError() );
            }
        }

        UpdateWindowsCursorPosition();
        return true;
    }

    //tracer.Trace( "UpdateDisplay not updated; no changes\n" );
    return false;
} //UpdateDisplay

bool throttled_UpdateDisplay()
{
    static ULONGLONG last_call = 0;
    ULONGLONG this_call = GetTickCount64();

    if ( ( this_call - last_call ) > 50 )
    {
        last_call = this_call;
        return UpdateDisplay();
    }

    return false;
} //throttled_UpdateDisplay();

void ClearDisplay()
{
    assert( g_use80x25 );
    uint8_t * pbuf = GetVideoMem();

    for ( uint16_t y = 0; y < ScreenRows; y++ )
        memcpy( pbuf + ( y * 2 * ScreenColumns ), blankLine, sizeof( blankLine ) );
} //ClearDisplay

BOOL WINAPI ControlHandler( DWORD fdwCtrlType )
{
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
   { 0x00, 0, "divide error" },
   { 0x01, 0, "single-step" },
   { 0x02, 0, "non-maskable interrupt" },
   { 0x03, 0, "1-byte interrupt" },
   { 0x04, 0, "internal overflow" },
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
    { 0x10, 0x1b, "undocumented. qbasic apps call this" },
    { 0x10, 0xef, "undocumented. qbasic apps call this" },
    { 0x12, 0x00, "get memory size" },
    { 0x16, 0x00, "get character" },
    { 0x16, 0x01, "keyboard status" },
    { 0x16, 0x02, "keyboard - get shift status" },
    { 0x1a, 0x00, "read real time clock" },
    { 0x21, 0x00, "exit app" },
    { 0x21, 0x02, "output character" },
    { 0x21, 0x06, "direct console character i/o" },
    { 0x21, 0x09, "print string $-terminated" },
    { 0x21, 0x0a, "buffered keyboard input" },
    { 0x21, 0x0c, "clear input buffer and execute int 0x21 on AL" },
    { 0x21, 0x0f, "open using FCB" },
    { 0x21, 0x10, "close using FCB" },
    { 0x21, 0x13, "delete file using FCBs" },
    { 0x21, 0x16, "create file using FCBs" },
    { 0x21, 0x17, "rename file using FCBs" },
    { 0x21, 0x19, "get default drive" },
    { 0x21, 0x1a, "set disk transfer address" },
    { 0x21, 0x22, "random write using FCBs" },
    { 0x21, 0x23, "get file size using FCBs" },
    { 0x21, 0x25, "set interrupt vector" },
    { 0x21, 0x27, "random block read using FCB" },
    { 0x21, 0x28, "write random using FCBs" },
    { 0x21, 0x29, "parse filename" },
    { 0x21, 0x2a, "get system date" },
    { 0x21, 0x2c, "get system time" },
    { 0x21, 0x30, "get version number" },
    { 0x21, 0x33, "get/set ctrl-break status" },
    { 0x21, 0x35, "get interrupt vector" },
    { 0x21, 0x36, "get disk space" },
    { 0x21, 0x37, "get query switchchar" },
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
    { 0x21, 0x47, "get current directory" },
    { 0x21, 0x48, "allocate memory" },
    { 0x21, 0x49, "free memory" },
    { 0x21, 0x4a, "modify memory allocation" },
    { 0x21, 0x4c, "exit app" },
    { 0x21, 0x4e, "find first asciz" },
    { 0x21, 0x4f, "find next asciz" },
    { 0x21, 0x56, "rename file" },
    { 0x21, 0x57, "get/set file date and time using handle" },
    { 0x21, 0x58, "get/set memory allocation strategy" },
    { 0x21, 0x59, "get extended error code" },
    { 0x21, 0x63, "get lead byte table" },
};

const char * get_interrupt_string( uint8_t i, uint8_t c )
{
    for ( int x = 0; x < _countof( interrupt_list ); x++ )
        if ( interrupt_list[ x ].i == i && interrupt_list[ x ].c == c )
            return interrupt_list[ x ].name;

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
    scancode = rec.Event.KeyEvent.wVirtualScanCode;
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
    else if ( 0x42 == sc ) // keypad -
    {
        if ( falt ) { scancode = 0x4a; asciiChar = 0; }
        else if ( fctrl ) { scancode = 0x8e; asciiChar = 0; }
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
    if ( g_injectControlC )
    {
        asciiChar = 0x03;
        scancode = 0x2e;
        return true;
    }

    INPUT_RECORD records[ 10 ];
    DWORD numRead = 0;
    BOOL ok = PeekConsoleInput( g_consoleConfig.GetInputHandle(), records, _countof( records ), &numRead );
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

    // if none of the records were useful, clear them out

    if ( 0 != numRead )
        ReadConsoleInput( g_consoleConfig.GetInputHandle(), records, numRead, &numRead );

    return false;
} //peek_keyboard

bool peek_keyboard( bool throttle = false, bool sleep_on_throttle = false, bool update_display = false )
{
    static ULONGLONG last_call = 0;
    static ULONGLONG last_update = 0;

    ULONGLONG this_call = GetTickCount64();
    if ( throttle && ( this_call - last_call ) < 50 )
    {
        if ( update_display && g_use80x25 && ( ( this_call - last_update ) > 333 ) )
        {
            last_update = this_call;
            UpdateDisplay();
        }

        if ( sleep_on_throttle )
            SleepEx( 1, FALSE );

        return false;
    }

    last_call = this_call;
    uint8_t a, s;
    return peek_keyboard( a, s );
} //peek_keyboard

void consume_keyboard()
{
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
    BOOL ok = ReadConsoleInput( g_consoleConfig.GetInputHandle(), records, _countof( records ), &numRead );
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

uint8_t i8086_invoke_in_al( uint16_t port )
{
    if ( 0x3da == port )
    {
        // toggle this or apps will spin waiting for the I/O port to work.

        static uint8_t cga_status = 9;
        cga_status ^= 9;
        return cga_status;
    }
    else if ( 0x20 == port ) // pic1 int request register
    {
    }
    else if ( 0x60 ==  port ) // keyboard data
    {
        uint8_t asciiChar, scancodeChar;
        if ( peek_keyboard( asciiChar, scancodeChar ) )
        {
            tracer.Trace( "invoke_in_al, port %02x peeked a character and is returning %02x\n", port, asciiChar );
            return asciiChar;
        }
    }
    else if ( 0x61 == port ) // keyboard controller port
    {
    }
    else if ( 0x64 == port ) // keyboard controller read status
    {
    }
    else
        tracer.Trace( "reading from unimplemented port %02x\n", port );

    tracer.Trace( "invoke_in_al, port %02x returning 0\n", port );
    return 0;
} //i8086_invoke_in_al

uint16_t i8086_invoke_in_ax( uint16_t port )
{
    return 0;
} //i8086_invoke_in_ax

void i8086_invoke_halt()
{
    g_haltExecution = true;
} // i8086_invoke_halt

void FileTimeToDos( FILETIME & ft, uint16_t & dos_time, uint16_t & dos_date )
{
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

void ProcessFoundFile( DosFindFile * pff, WIN32_FIND_DATAA & fd )
{
    tracer.Trace( "actual found filename: '%s'\n", fd.cFileName );
    if ( 0 != fd.cAlternateFileName[ 0 ] )
        strcpy( pff->file_name, fd.cAlternateFileName );
    else if ( strlen( fd.cFileName ) < _countof( pff->file_name ) )
        strcpy( pff->file_name, fd.cFileName );
    else
        GetShortPathNameA( fd.cFileName, pff->file_name, _countof( pff->file_name ) );

    pff->file_size = ( fd.nFileSizeLow );

    // these bits are the same
    pff->file_attributes = ( fd.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                                     FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) );

    FileTimeToDos( fd.ftLastWriteTime, pff->file_time, pff->file_date );
    tracer.Trace( "  search found '%s', size %u\n", pff->file_name, pff->file_size );
} //ProcessFoundFile

void PerhapsFlipTo80x25()
{
    static bool firstTime = true;

    if ( firstTime )
    {
        firstTime = false;
        if ( !g_forceConsole )
        {
            g_use80x25 = true;
            g_consoleConfig.EstablishConsole( ScreenColumns, ScreenRows, ControlHandler  );
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
                    2 * ( clr - cul ) );

        if ( row > ( rlr - lines ) )
            memcpy( pbuf + ( row * ScreenColumns * 2 + cul * 2 ),
                    blankLine,
                    2 * ( 1 + clr - cul ) );
    }
} //scroll_up

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
            tracer.Trace( "set video mode to %#x\n", mode );

            if ( 2 == mode || 3 == mode ) // only 80x25 is supported with buffer address 0xb8000
            {
                g_videoMode = mode;
                uint8_t * pmode = GetMem( 0x40, 0x49 ); // update the mode in bios data
                *pmode = mode;
            }

            return;
        }
        case 1:
        {
            // set cursor size. 5 lower bits from each: ch = start line, cl = end line
            // bits 5 and 6 in ch: 0 = normal, 1 = invisible, 2 = slow, 3 = fast

            uint8_t cur_top = cpu.ch() & 0x1f;
            uint8_t cur_bottom = cpu.cl() & 0x1f;
            uint8_t cur_blink = ( cpu.ch() >> 5 ) & 3;

            tracer.Trace( "set cursor size to top %u, bottom %u, blink %u\n", cur_top, cur_bottom, cur_blink );

            if ( 1 == cur_blink )
                g_consoleConfig.SetCursorInfo( 0 );
            else
                g_consoleConfig.SetCursorInfo( 100 );

            return;
        }
        case 2:
        {
            tracer.Trace( "  set cursor position to %d %d\n", cpu.dh(), cpu.dl() );

            GetCursorPosition( row, col );
            uint16_t lastRow = row;
            uint16_t lastCol = col;

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
            tracer.Trace( "  get cursor position %d %d\n", cpu.dh(), cpu.dl() );

            return;
        }
        case 5:
        {
            // set active display page

            uint8_t page = cpu.al();
            if ( page <= 3 )
                 * GetMem( 0x40, 0x62 ) = page;

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

            if ( g_use80x25 )
            {
                init_blankline( (uint8_t) cpu.bh() );

                //printf( "%c[%dS", 27, cpu.al() );
                int lines = (int) (uint8_t) cpu.al();
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
                        for ( int row = rul; row <= rlr; row++ )
                            memcpy( pbuf + ( row * ScreenColumns * 2 + cul * 2 ), blankLine, 2 * ( 1 + clr - cul ) );
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

            if ( g_use80x25 )
            {
                init_blankline( (uint8_t) cpu.bh() );

                //printf( "%c[%dT", 27, cpu.al() );
                int lines = (int) (uint8_t) cpu.al();
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
                        for ( int row = rul; row <= rlr; row++ )
                            memcpy( pbuf + ( row * ScreenColumns * 2 + cul * 2 ), blankLine, 2 * ( 1 + clr - cul ) );
                    }
                    else
                        ClearDisplay();
                }
                else
                {
                    // likely data: lines = 1, rul = 1, cul = 0, rlr = 24, clr = 79
                    //          or: lines = 1, rul = 0, cul = 0, rlr = 24, clr = 79

                    for ( int row = rlr; row >= rul; row-- )
                    {
                        int targetrow = row + lines;
                        if ( targetrow <= rlr )
                            memcpy( pbuf + ( targetrow * ScreenColumns * 2 + cul * 2 ),
                                    pbuf + ( row * ScreenColumns * 2 + cul * 2 ),
                                    2 * ( clr - cul ) );

                        if ( row <= ( rul + lines ) )
                            memcpy( pbuf + ( row * ScreenColumns * 2 + cul * 2 ),
                                    blankLine,
                                    2 * ( 1 + clr - cul ) );
                    }
                }

                UpdateDisplay();
            }

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
                          cpu.al(), printable( cpu.al() ), cpu.cx, cpu.bl(), row, col );

            char ch = cpu.al();

            if ( g_use80x25 )
            {
                ch = printable( ch );
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.cx; t++ )
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
                          cpu.al(), cpu.cx, row, col );

            char ch = cpu.al();
            if ( 0x1b == ch ) // escape should be a left arrow, but it just confuses the console
                ch = ' ';

            if ( g_use80x25 )
            {
                uint8_t * pbuf = GetVideoMem();
                uint32_t offset = row * 2 * ScreenColumns + col * 2;

                for ( uint16_t t = 0; t < cpu.cx; t++ )
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

            PerhapsFlipTo80x25();

            cpu.set_al( g_videoMode );
            cpu.set_ah( ScreenColumns ); // columns
            cpu.set_bh( * GetMem( 0x40, 0x62 ) ); // active display page

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

            return;
        }
        case 0x12:
        {
            // video subsystem configuration. alternate select. return some defaults

            if ( 0x10 == cpu.bl() )
            {
                cpu.bx = 0;
                cpu.cx = 0;
            }

            return;
        }
        case 0x15:
        {
            // get physical display charactics

            cpu.ax = 0; // none

            return;
        }
        case 0x1b:
        {
            // unknown. qbasic generated .exe files call this?!?

            return;
        }
        case 0xef:
        {
            // unknown. qbasic generated .exe files call this?!?

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
        {
            // get character

            if ( g_use80x25 )
                UpdateDisplay();

            while ( *phead == *ptail )
            {
                // wait for a character then return it.

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

            tracer.Trace( "  returning character %#x '%c'\n", cpu.ax, printable( cpu.ax ) );
            tracer.Trace( "  int_16 exit head: %04x, tail %04x\n", *phead, *ptail );
            return;
        }
        case 1:
        {
            // check keyboard status. checks if a character is available. return it if so, but not removed from buffer

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
                cpu.fZero = true;
                if ( g_int16_1_loop ) // avoid a busy loop it makes my fan loud
                    SleepEx( 1, FALSE );
            }
            else
            {
                cpu.set_al( pbiosdata[ *phead ] );
                cpu.set_ah( pbiosdata[ 1 + ( *phead ) ] );
                cpu.fZero = false;
            }

            tracer.Trace( "  returning flag %d, ax %04x\n", cpu.fZero, cpu.ax );
            tracer.Trace( "  int_16 exit head: %04x, tail %04x\n", *phead, *ptail );
            g_int16_1_loop = true;
            return;
        }
        case 2:
        {
            // get shift status (and alt/ctrl/etc.)

            cpu.set_al( pbiosdata[ 0x17 ] );
            tracer.Trace( "  keyboard flag status: %02x\n", pbiosdata[ 0x17 ] );
            return;
        }
    }

    tracer.Trace( "unhandled int16 command %02x\n", c );
} //handle_int_16

void handle_int_21( uint8_t c )
{
    static char cwd[ MAX_PATH ] = {0};
    uint8_t row, col;

    switch( c )
    {
        case 0:
        {
            // terminate program
    
            g_haltExecution = true;
            cpu.end_emulation();
            return;
        }
        case 2:
        {
            // output character.
            // todo: interpret 7 (beep), 8 (backspace), 9 (tab) to tab on multiples of 8. 10 lf should move cursor down and scroll if needed
    
            if ( g_use80x25 )
            {
                uint8_t * pbuf = GetVideoMem();
                GetCursorPosition( row, col );
                uint32_t offset = row * 2 * ScreenColumns + col * 2;
                pbuf[ offset ] = cpu.dl();
                col++;
                SetCursorPosition( row, col );
            }
            else
            {
                char ch = cpu.dl();
                if ( 0x0d != ch )
                    printf( "%c", ch );
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
                // input

                uint8_t * pbiosdata = (uint8_t *) GetMem( 0x40, 0 );
                uint16_t * phead = (uint16_t *) ( pbiosdata + 0x1a );
                uint16_t * ptail = (uint16_t *) ( pbiosdata + 0x1c );
                tracer.Trace( "  int_21 character input head: %04x, tail %04x\n", *phead, *ptail );

                if ( *phead != *ptail )
                {
                    static bool mid_scancode_read = false;
                    cpu.fZero = false;

                    if ( mid_scancode_read )
                    {
                        mid_scancode_read = false;
                        cpu.set_al( pbiosdata[ * ( phead + 1 ) ] );
                        (*phead) += 2;
                    }
                    else
                    {
                        cpu.set_al( pbiosdata[ *phead ] );

                        if ( 0 == *phead )
                            mid_scancode_read = true;
                        else
                            (*phead) += 2;
                    }
                }
                else
                    cpu.fZero = true;
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
        case 9:
        {
            // print string. prints chars up to a dollar sign $
    
            char * p = (char *) GetMem( cpu.ds, cpu.dx );
            DumpBinaryData( (uint8_t *) p, 0x40, 2 );
            while ( *p && '$' != *p )
                printf( "%c", *p++ );
    
            return;
        }
        case 0xa:
        {
            // Buffered Keyboard input. DS::DX pointer to buffer. byte 0 count in, byte 1 count out excluding CR, byte 2 starts the response
    
            uint8_t * p = GetMem( cpu.ds, cpu.dx );
            uint8_t maxLen = p[0];
    
            char * result = gets_s( (char *) p + 2, maxLen );
            if ( result )
                p[1] = strlen( result );
            else
                p[1] = 0;
    
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
        case 0xf:
        {
            // open using FCB
    
            tracer.Trace( "open using FCB. ds %u dx %u\n", cpu.ds, cpu.dx );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            DumpBinaryData( (uint8_t *) pfcb, sizeof DOSFCB, 2 );
    
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
    
                    pfcb->Trace();
                    cpu.set_al( 0 );
                }
                else
                    tracer.Trace( "ERROR: file open using FCB of %s failed, error %d = %s\n", filename, errno, strerror( errno ) );
            }
            else
                tracer.Trace( "ERROR: couldn't parse filename in FCB\n" );
    
            return;
        }
        case 0x10:
        {
            // close file using FCB
    
            tracer.Trace( "close file using FCB\n" );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            pfcb->Trace();
    
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                fclose( fp );
                pfcb->SetFP( 0 );
            }
            else
                tracer.Trace( "ERROR: file close using FCB of a file that's not open\n" );
    
            return;
        }
        case 0x13:
        {
            // delete file using FCB
    
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            DumpBinaryData( (uint8_t *) pfcb, sizeof DOSFCB, 2 );
    
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
                    tracer.Trace( "ERROR: delete file failed, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "ERROR: couldn't parse filename in FCB\n" );
    
            return;
        }
        case 0x16:
        {
            // create using FCB
    
            tracer.Trace( "create using FCB. ds %u dx %u\n", cpu.ds, cpu.dx );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            DumpBinaryData( (uint8_t *) pfcb, sizeof DOSFCB, 2 );
            cpu.set_al( 0xff );
    
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
    
                    pfcb->fileSize = 0;
                    pfcb->curBlock = 0;
                    pfcb->recSize = 0x80;
    
                    pfcb->Trace();
                }
                else
                    tracer.Trace( "ERROR: file create using FCB of %s failed, error %d = %s\n", filename, errno, strerror( errno ) );
            }
            else
                tracer.Trace( "ERROR: can't parse filename from FCB\n" );
    
            return;
        }
        case 0x17:
        {
            // rename file using FCB. Returns AL 0 if success and 0xff on failure.
    
            cpu.set_al( 0xff );
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  mem: %p, pfcb %p\n", memory, pfcb );
            DumpBinaryData( (uint8_t *) pfcb, sizeof DOSFCB, 2 );
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
                        tracer.Trace( "ERROR: can't rename file, error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "ERROR: can't parse new filename in FCB\n" );
            }
            else
                tracer.Trace( "ERROR: can't parse old filename in FCB\n" );
    
            return;
        }
        case 0x19:
        {
            // get current default drive. 0 == a:, 1 == b:, etc. returned in AL
    
            GetCurrentDirectoryA( sizeof cwd, cwd );
            _strupr( cwd );
            cpu.set_al( cwd[0] - 'A' );
            return;
        }
        case 0x1a:
        {
            // set disk transfer address
    
            uint8_t * old = g_DiskTransferAddress;
            g_DiskTransferAddress = (uint8_t *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  set disk transfer address updated from %p to %p (bx %u)\n", old, g_DiskTransferAddress, cpu.dx );
    
            return;
        }
        case 0x22:
        {
            // random write using FCBs. on output, 0 if success, 1 if disk full, 2 if DTA too small
            // CX has # of records written on exit
    
            cpu.set_al( 1 );
            uint16_t recsToWrite = cpu.cx;
            cpu.cx = 0;
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                ULONG seekOffset = pfcb->recNumber * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                int ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    size_t num_written = fwrite( g_DiskTransferAddress, recsToWrite, pfcb->recSize, fp );
                    if ( num_written )
                    {
                         tracer.Trace( "  write succeded: %u bytes\n", recsToWrite * pfcb->recSize );
                         cpu.cx = recsToWrite;
                         cpu.set_al( 0 );
    
                         // don't update the fcb's record number for this version of the API
                    }
                    else
                         tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "ERROR random block write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "ERROR random block write using FCBs doesn't have an open file\n" );
    
            return;
        }
        case 0x25:
        {
            // set interrupt vector
    
            tracer.Trace( "  setting interrupt vector %02x %s to %04x:%04x\n", cpu.al(), get_interrupt_string( cpu.al(), 0 ), cpu.ds, cpu.dx );
            uint16_t * pvec = (uint16_t *) GetMem( 0, 4 * (uint16_t) cpu.al() );
            pvec[0] = cpu.dx;
            pvec[1] = cpu.ds;
    
            if ( 0x1c == cpu.al() )
                uint32_t dw = ( (uint32_t) cpu.ds << 16 ) | cpu.dx;
            return;
        }           
        case 0x27:
        {
            // random block read using FCBs
            // CX: number of records to read
            // DS:BX pointer to the FCB.
            // on exit, AL 0 success, 1 EOF no data read, 2 dta too small, 3 eof partial read (filled with 0s)
    
            cpu.set_al( 1 ); // eof
            ULONG cRecords = cpu.cx;
            cpu.cx = 0;
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "random block read using FCBs\n" );
            pfcb->Trace();
            ULONG seekOffset = pfcb->recNumber * pfcb->recSize;
            if ( seekOffset > pfcb->fileSize )
            {
                tracer.Trace( "ERROR: random read beyond end of file offset %u, filesize %u\n", seekOffset, pfcb->fileSize );
                cpu.set_al( 1 ); // eof
            }
            else if ( seekOffset == pfcb->fileSize )
            {
                tracer.Trace( "WARNING: random read at end of file offset %u, filesize %u\n", seekOffset, pfcb->fileSize );
                cpu.set_al( 1 ); // eof
            }
            else
            {
                FILE * fp = pfcb->GetFP();
                if ( fp )
                {
                    tracer.Trace( "  seek offset: %u\n", seekOffset );
                    int ok = !fseek( fp, seekOffset, SEEK_SET );
                    if ( ok )
                    {
                        ULONG askedBytes = pfcb->recSize * cRecords;
                        memset( g_DiskTransferAddress, 0, askedBytes );
                        ULONG toRead = __min( pfcb->fileSize - seekOffset, askedBytes );
                        size_t numRead = fread( g_DiskTransferAddress, toRead, 1, fp );
                        if ( numRead )
                        {
                            if ( toRead == askedBytes )
                                cpu.set_al( 0 );
                            else
                                cpu.set_al( 3 ); // eof encountered, last record is partial
    
                            cpu.cx = toRead / pfcb->recSize;
                            tracer.Trace( "  successfully read %u bytes, CX set to %u:\n", toRead, cpu.cx );
                            DumpBinaryData( g_DiskTransferAddress, toRead, 0 );
                            pfcb->curRecord += cRecords;;
                            pfcb->recNumber += cRecords;
                        }
                        else
                            tracer.Trace( "ERROR random block read using FCBs failed to read, error %d = %s\n", errno, strerror( errno ) );
                    }
                    else
                        tracer.Trace( "ERROR random block read using FCBs failed to seek, error %d= %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "ERROR random block read using FCBs doesn't have an open file\n" );
            }
    
            return;
        }
        case 0x28:
        {
            // random block write using FCBs.
            // in: CX = number of records, DS:BX the fcb
            // out: al = 0 if success, 1 if disk full, 2 if data too smaoo, cx = number of records written
    
            cpu.set_al( 1 );
            uint16_t recsToWrite = cpu.cx;
            cpu.cx = 0;
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.ds, cpu.dx );
            pfcb->Trace();
            FILE * fp = pfcb->GetFP();
            if ( fp )
            {
                ULONG seekOffset = pfcb->recNumber * pfcb->recSize;
                tracer.Trace( "  seek offset: %u\n", seekOffset );
                int ok = !fseek( fp, seekOffset, SEEK_SET );
                if ( ok )
                {
                    size_t num_written = fwrite( g_DiskTransferAddress, recsToWrite, pfcb->recSize, fp );
                    if ( num_written )
                    {
                         tracer.Trace( "  write succeded: %u bytes\n", recsToWrite * pfcb->recSize );
                         cpu.cx = recsToWrite;
                         cpu.set_al( 0 );
    
                         pfcb->recNumber += recsToWrite;
                    }
                    else
                         tracer.Trace( "  write failed with error %d = %s\n", errno, strerror( errno ) );
                }
                else
                    tracer.Trace( "ERROR random block write using FCBs failed to seek, error %d = %s\n", errno, strerror( errno ) );
            }
            else
                tracer.Trace( "ERROR random block write using FCBs doesn't have an open file\n" );
    
            return;
        }
        case 0x29:
        {
            // parse filename
            // in: ds:si -- string to parse
            //     es:di -- buffer pointing to an fcb
            //     al:   -- bit 0: 0 parsing stops if file separator found
            //                     1 leading separator ignored
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
    
            char * pfile = (char *) GetMem( cpu.ds, cpu.si );
            tracer.Trace( "parse filename '%s'\n", pfile );
            DumpBinaryData( (uint8_t *) pfile, 64, 0 );
    
            DOSFCB * pfcb = (DOSFCB *) GetMem( cpu.es, cpu.di );
            char * pf = pfile;
    
            if ( 0 == pfile[0] )
            {
                if ( 0 == ( cpu.al() & 4 ) )
                    memset( pfcb->name, ' ', _countof( pfcb->name ) );
                if ( 0 == ( cpu.al() & 8 ) )
                    memset( pfcb->ext, ' ', _countof( pfcb->ext ) );
            }
            else
            {
                memset( pfcb->name, ' ', _countof( pfcb->name ) );
                memset( pfcb->ext, ' ', _countof( pfcb->ext ) );
                for ( int i = 0; i < _countof( pfcb->name ) && *pf && *pf != '.'; i++ )
                    pfcb->name[ i ] = *pf++;
                if ( '.' == *pf )
                    pf++;
                for ( int i = 0; i < _countof( pfcb->ext ) && *pf; i++ )
                    pfcb->ext[ i ] = *pf++;
            }
    
            if ( strchr( pfile, '*' ) || strchr( pfile, '?' ) )
                cpu.set_al( 1 );
            else
                cpu.set_al( 0 );
    
            cpu.si += 1 + (uint16_t) ( pf - pfile );
    
            return;
        }
        case 0x2a:
        {
            // get system date. al is day of week 0-6 0=sunday, cx = year 1980-2099, dh = month 1-12, dl = day 1-31
    
            SYSTEMTIME st = {0};
            GetLocalTime( &st );
            cpu.set_al( (uint8_t) st.wDayOfWeek );
            cpu.cx = st.wYear;
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
    
            return;
        }           
        case 0x30:
        {
            // get version number
    
            //cpu.set_al( 2 ); 
            //cpu.set_ah( 11 ); 
            cpu.set_al( 3 ); // It's getting closer
            cpu.set_ah( 0 ); 
    
            tracer.Trace( "returning DOS version %d.%d\n", cpu.al(), cpu.ah() );
            return;
        }
        case 0x33:
        {
            // get/set ctrl-break status
    
            cpu.set_dl( 0 ); // it's off regardless of what is set
            return;
        }
        case 0x35:
        {
            // get interrupt vector. 
    
            uint16_t * pvec = (uint16_t *) GetMem( 0, 4 * (uint16_t) cpu.al() );
            cpu.bx = pvec[ 0 ];
            cpu.es = pvec[ 1 ];
            tracer.Trace( "  getting interrupt vector %02x %s which is %04x:%04x\n", cpu.al(), get_interrupt_string( cpu.al(), 0 ), cpu.es, cpu.bx );
            return;
        }
        case 0x36:
        {
            // get disk space: in: dl code (0 default, 1 = A, 2 = B...
            // output: ax: sectors per cluster, bx = # of available clusters, cx = bytes per sector, dx = total clusters
            // use believable numbers for DOS for lots of disk space free.
    
            cpu.ax = 8;
            cpu.bx = 0x6fff;
            cpu.cx = 512;
            cpu.dx = 0x7fff;
            return;
        }
        case 0x37:
        {
            // query switchchar. Undocumented but legal call in DOS 2.x
    
            cpu.set_dl( '/' );
            return;
        }
        case 0x38:
        {
            // get/set country dependent information.

            // some apps (um, Brief 3.1) call this in a tight loop along with get system time and keyboard status
            SleepEx( 1, FALSE );
    
            cpu.fCarry = false;
            cpu.bx = 1; // USA
            uint8_t * pinfo = GetMem( cpu.ds, cpu.dx );
            memset( pinfo, 0, 0x20 );
            pinfo[ 2 ] = '$';
            pinfo[ 4 ] = ',';
            pinfo[ 6 ] = '.';
            return;
        }
        case 0x39:
        {
            // create directory ds:dx asciz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "create directory '%s'\n", path );

            int ret = mkdir( path );
            if ( 0 == ret )
                cpu.fCarry = false;
            else
            {
                cpu.fCarry = true;
                cpu.ax = 3; // path not found
                tracer.Trace( "ERROR: create directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3a:
        {
            // remove directory ds:dx asciz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "remove directory '%s'\n", path );

            int ret = rmdir( path );
            if ( 0 == ret )
                cpu.fCarry = false;
            else
            {
                cpu.fCarry = true;
                cpu.ax = 3; // path not found
                tracer.Trace( "ERROR: remove directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3b:
        {
            // change directory ds:dx asciz directory name. cf set on error with code in ax
            char * path = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "change directory to '%s'\n", path );

            int ret = chdir( path );
            if ( 0 == ret )
                cpu.fCarry = false;
            else
            {
                cpu.fCarry = true;
                cpu.ax = 3; // path not found
                tracer.Trace( "ERROR: change directory sz failed with error %d = %s\n", errno, strerror( errno ) );
            }

            return;
        }
        case 0x3c:
        {
            // create file. DS:dx pointer to asciiz pathname. al= open mode (dos 2.x ignores). AX=handle
    
            char * path = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "  create file '%s'\n", path );
            cpu.ax = 3;
    
            FILE * fp = fopen( path, "w+b" );
            if ( fp )
            {
                FileEntry fe = {0};
                strcpy( fe.path, path );
                fe.fp = fp;
                fe.handle = FindFirstFreeFileHandle();
                g_fileEntries.push_back( fe );
                cpu.ax = fe.handle;
                cpu.fCarry = false;
                tracer.Trace( "  successfully created file and returning handle %04x\n", cpu.ax );
            }
            else
            {
                tracer.Trace( "ERROR: create file sz failed with error %d = %s\n", errno, strerror( errno ) );
                cpu.ax = 2;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x3d:
        {
            // open file. DS:dx pointer to asciiz pathname. al= open mode (dos 2.x ignores). AX=handle
    
            char * path = (char *) GetMem( cpu.ds, cpu.dx );
            DumpBinaryData( (uint8_t *) path, 0x100, 0 );
            tracer.Trace( "open file '%s'\n", path );
            cpu.ax = 2;
    
            FILE * fp = fopen( path, "r+b" );
            if ( fp )
            {
                FileEntry fe = {0};
                strcpy( fe.path, path );
                fe.fp = fp;
                fe.handle = FindFirstFreeFileHandle();
                g_fileEntries.push_back( fe );
                cpu.ax = fe.handle;
                cpu.fCarry = false;;
                tracer.Trace( "  successfully opened file, using new handle %04x\n", cpu.ax );
            }
            else
            {
                tracer.Trace( "ERROR: open file sz failed with error %d = %s\n", errno, strerror( errno ) );
                cpu.ax = 2;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x3e:
        {
            // close file handle in BX
    
            FILE * fp = RemoveFileEntry( cpu.bx );
            if ( fp )
            {
                tracer.Trace( "  close file handle %04x\n", cpu.bx );
                fclose( fp );
                cpu.fCarry = false;;
            }
            else
            {
                tracer.Trace( "ERROR: close file handle couldn't find handle %04x\n", cpu.bx );
                cpu.ax = 6;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x3f:
        {
            // read from file using handle. BX=handle, CX=num bytes, DS:DX: buffer
            // on output: AX = # of bytes read or if CF is set 5=access denied, 6=invalid handle.
    
            uint16_t h = cpu.bx;
            if ( h <= 4 )
            {
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                if ( 0 == h )
                {
                    // Callers like GWBasic ask for one character at a time but have no idea what a backspace is.
                    // So buffer until a cr, append a lf, and send that one character at a time.
    
                    static char acBuffer[ 128 ] = {0};
    
                    if ( g_use80x25 )
                        UpdateDisplay();
    
                    uint8_t * p = GetMem( cpu.ds, cpu.dx );
                    cpu.fCarry = false;
                    cpu.ax = 1;
    
                    while ( 0 == acBuffer[ 0 ] )
                    {
                        size_t len = _countof( acBuffer );
                        char * result = gets_s( acBuffer, _countof( acBuffer ) );
                        if ( result )
                        {
                            strcat( acBuffer, "\r\n" );
                            break;
                        }
                    }
    
                    *p = acBuffer[0];
                    strcpy( acBuffer, acBuffer + 1 );
                    return;
                }
                else
                {
                    cpu.fCarry = true;
                    tracer.Trace( "attempt to read from handle %04x\n", h );
                }
    
                return;
            }
    
            FILE * fp = FindFileEntry( cpu.bx );
            if ( fp )
            {
                uint16_t len = cpu.cx;
                uint8_t * p = GetMem( cpu.ds, cpu.dx );
                tracer.Trace( "read from file using handle %04x bytes at address %02x:%02x\n", len, cpu.ds, cpu.dx );
    
                uint32_t cur = ftell( fp );
                fseek( fp, 0, SEEK_END );
                uint32_t size = ftell( fp );
                fseek( fp, cur, SEEK_SET );
                cpu.ax = 0;
    
                if ( cur < size )
                {
                    memset( p, 0, len );
                    uint32_t toRead = __min( len, size - cur );
                    size_t numRead = fread( p, toRead, 1, fp );
                    if ( numRead )
                    {
                        cpu.ax = toRead;
                        tracer.Trace( "  successfully read %u bytes\n", toRead );
                        DumpBinaryData( p, toRead, 0 );
                    }
                }
                else
                    tracer.Trace( "ERROR: attempt to read beyond the end of file\n" );
    
                cpu.fCarry = false;;
            }
            else
            {
                tracer.Trace( "ERROR: read from file handle couldn't find handle %04x\n", cpu.bx );
                cpu.ax = 6;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x40:
        {
            // write to file using handle. BX=handle, CX=num bytes, DS:DX: buffer
            // on output: AX = # of bytes read or if CF is set 5=access denied, 6=invalid handle.
    
            uint16_t h = cpu.bx;
            cpu.fCarry = false;
            if ( h <= 4 )
            {
                cpu.ax = cpu.cx;
    
                // reserved handles. 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
    
                uint8_t * p = GetMem( cpu.ds, cpu.dx );
    
                if ( 1 == h || 2 == h )
                {
                    if ( g_use80x25 )
                    {
                        uint8_t * pbuf = GetVideoMem();
                        GetCursorPosition( row, col );
                        tracer.Trace( "  starting to write pbuf %p, %u chars at row %u col %u\n", pbuf, cpu.cx, row, col );
        
                        for ( uint16_t t = 0; t < cpu.cx; t++ )
                        {
                            uint8_t ch = p[ t ];
                            if ( 0x0d == ch )
                            {
                                col = 0;
                                SetCursorPosition( row, col );
                            }
                            else if ( 0x0a == ch )
                            {
                                if ( row >= ScreenRowsM1 )
                                {
                                    int lines = 1;
                                    int rul = 0;
                                    int cul = 0;
                                    int rlr = ScreenRowsM1;
                                    int clr = ScreenColumnsM1;
                
                                    tracer.Trace( "  line feed scrolling up a line\n"  );
                                    scroll_up( pbuf, 1, 0, 0, ScreenRowsM1, ScreenColumnsM1 );
                                }
                                else
                                    SetCursorPosition( row + 1, col );
                            }
                            else
                            {
                                uint32_t offset = row * 2 * ScreenColumns + col * 2;
                                pbuf[ offset ] = printable( ch );
                                tracer.Trace( "  writing %02x '%c' to display offset %u at row %u col %u\n",
                                              ch, printable( ch ), offset, row, col );
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
                        for ( uint16_t x = 0; x < cpu.cx; x++ )
                        {
                            if ( 0x0d != p[ x ] && 0x0b != p[ x ] )
                            {
                                printf( "%c", p[ x ] );
                                tracer.Trace( "writing %02x '%c' to display\n", p[ x ], printable( p[x] ) );
                            }
                        }
                    }
                }
                return;
            }
    
            FILE * fp = FindFileEntry( cpu.bx );
            if ( fp )
            {
                uint16_t len = cpu.cx;
                uint8_t * p = GetMem( cpu.ds, cpu.dx );
                tracer.Trace( "write file using handle, %04x bytes at address %p\n", len, p );
    
                cpu.ax = 0;
    
                size_t numWritten = fwrite( p, len, 1, fp );
                if ( numWritten || ( 0 == len ) )
                {
                    cpu.ax = len;
                    tracer.Trace( "  successfully wrote %u bytes\n", len );
                    DumpBinaryData( p, len, 0 );
                }
                else
                    tracer.Trace( "ERROR: attempt to write to file failed, error %d = %s\n", errno, strerror( errno ) );
    
                cpu.fCarry = false;;
            }
            else
            {
                tracer.Trace( "ERROR: write to file handle couldn't find handle %04x\n", cpu.bx );
                cpu.ax = 6;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x41:
        {
            // delete file: ds:dx has asciiz name of file to delete.
            // return: cf set on error, ax = error code
    
            char * pfile = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "deleting file '%s'\n", pfile );
            int removeok = ( 0 == remove( pfile ) );
            if ( removeok )
                cpu.fCarry = false;
            else
            {
                tracer.Trace( "ERROR: can't delete file '%s' error %d = %s\n", pfile, errno, strerror( errno ) );
                cpu.fCarry = true;
                cpu.ax = 2;
            }
    
            return;
        }
        case 0x42:
        {
            // move file pointer (lseek)
            // bx == handle, cx:dx: 32-bit offset, al=mode. 0=beginning, 1=current. 2=end
    
            uint16_t handle = cpu.bx;
            FILE * fp = FindFileEntry( handle );
            if ( fp )
            {
                uint32_t offset = ( ( (uint32_t) cpu.cx ) << 16 ) | cpu.dx;
                uint8_t origin = cpu.al();
                if ( origin > 2 )
                {
                    tracer.Trace( "ERROR: move file pointer file handle has invalid mode/origin %u\n", origin );
                    cpu.ax = 1;
                    cpu.fCarry = true;
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
                cpu.ax = cur & 0xffff;
                cpu.dx = ( cur >> 16 ) & 0xffff;
    
                cpu.fCarry = false;;
            }
            else
            {
                tracer.Trace( "ERROR: move file pointer file handle couldn't find handle %04x\n", handle );
                cpu.ax = 6;
                cpu.fCarry = true;
            }
    
            return;
        }
        case 0x43:
        {
            // get/put file attributes
            // al: 0 == get file attributes, 1 == put file attributes
            // cx: attributes (bits: 0 ro, 1 hidden, 2 system, 3 volume, 4 subdir, 5 archive)
            // ds:dx: asciz filename
            // returns: ax = error code if CF set. CX = file attributes on get.
    
            char * pfile = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "get/put file attributes on file '%s'\n", pfile );
            cpu.fCarry = true;
    
            if ( 0 == cpu.al() ) // get
            {
                uint32_t attr = GetFileAttributesA( pfile );
                if ( INVALID_FILE_ATTRIBUTES != attr )
                {
                    cpu.fCarry = false;
                    cpu.cx = ( attr & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY |
                                        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE ) );
                }
            }
            else
            {
                BOOL ok = SetFileAttributesA( pfile, cpu.cx );
                cpu.fCarry = !ok;
            }
    
            tracer.Trace( "result of get/put file attributes: %d\n", !cpu.fCarry );
    
            return;
        }
        case 0x44:
        {
            // i/o control for devices (ioctl)
    
            uint8_t subfunction = cpu.al();
            // handles 0-4 are reserved in DOS stdin, stdout, stderr, stdaux, stdprn
            uint16_t handle = cpu.bx;
            uint8_t * pbuf = GetMem( cpu.ds, cpu.dx );
            cpu.fCarry = false;
    
            tracer.Trace( "  ioctl subfunction %u, get device information for handle %04x\n", subfunction, handle );

            switch ( subfunction )
            {
                case 0:
                {
                    if ( handle <= 4 )
                    {
                        // get device information
        
                        cpu.dx = 0;
                        cpu.fCarry = false;
    
                        // this distinction is important for gwbasic and qbasic-generated-apps for both input and output to work in both modes.
                        // The 0x80 bit indicates it's a character device (cursor movement, etc.) vs. a file (or a teletype)
    
                        if ( g_use80x25 )
                        {
                            if ( 0 == handle ) // stdin
                                cpu.dx = 0x81;
                            else if ( 1 == handle ) //stdout
                                cpu.dx = 0x82;
                            else if ( handle < 10 ) // stderr, etc.
                                cpu.dx = 0x80;
                        }
                        else
                        {
                            if ( 0 == handle ) // stdin
                                cpu.dx = 0x1;
                            else if ( 1 == handle ) //stdout
                                cpu.dx = 0x2;
                            else if ( handle < 10 ) // stderr, etc.
                                cpu.dx = 0x0;
                        }
                    }
                    else
                    {
                        FILE * fp = FindFileEntry( handle );
                        if ( !fp )
                        {
                            cpu.fCarry = true;
                            tracer.Trace( "    ERROR: ioctl on handle %04x failed because it's not a valid handle\n", handle );
                        }
                        else
                        {
                            cpu.fCarry = false;
                            cpu.dx = 0x20; // binary (raw) mode
                            int location = ftell( fp );
                            if ( 0 == location )
                                cpu.dx |= 0x40; // file has not been written to
                        }
                    }
    
                    break;
                }
                case 1:
                {
                    // set device information (ignore)
    
                    break;
                }
                default:
                {
                    tracer.Trace( "UNIMPLEMENTED IOCTL subfunction %#x\n", cpu.al() );
                    break;
                }
            }
    
            return;
        }
        case 0x45:
        {
            // create duplicate handle (dup)
            cpu.fCarry = true;
            uint16_t existing_handle = cpu.bx;
            const char * path = FindFileEntryPath( existing_handle );

            if ( path )
            {
                FILE * fp = fopen( path, "r+b" );
                if ( fp )
                {
                    FileEntry fe = {0};
                    strcpy( fe.path, path );
                    fe.fp = fp;
                    fe.handle = FindFirstFreeFileHandle();
                    g_fileEntries.push_back( fe );
                    cpu.ax = fe.handle;
                    cpu.fCarry = false;;
                    tracer.Trace( "  successfully created duplicate handle of %04x as %04x\n", existing_handle, cpu.ax );
                }
                else
                {
                    cpu.ax = 2; // file not found
                    tracer.Trace( "ERROR: attempt to duplicate file handle failed opening file %s error %d: %s\n", path, errno, strerror( errno ) );
                }
            }
            else
            {
                cpu.ax = 2; // file not found
                tracer.Trace( "ERROR: attempt to duplicate non-existent handle %04x\n", existing_handle );
            }

            return;
        }
        case 0x47:
        {
            // get current directory. BX = drive number, DS:SI = pointer to a 64-byte null-terminated buffer.
            // CF set on error. AX=15 for invalid drive
    
            cpu.fCarry = true;
            if ( GetCurrentDirectoryA( sizeof cwd, cwd ) )
            {
                char * paststart = cwd + 3;
                if ( strlen( paststart ) <= 63 )
                {
                    strcpy( (char *) GetMem( cpu.ds, cpu.si ), paststart );
                    cpu.fCarry = false;;
                }
            }
            else
                tracer.Trace( "ERROR: unable to get the current working directory, error %d\n", GetLastError() );
    
            return;
        }
        case 0x48:
        {
            // allocate memory. bx # of paragraphs requested
            // on return, ax = segment of block or error code if cf set
            // bx = size of largest block available if cf set and ax = not enough memory
            // cf clear on success
            // very simplistic first free block strategy here.

            tracer.Trace( "  allocate memory %04x paragraphs\n", cpu.bx );

            // sometimes allocate an extra spaceBetween paragraphs between blocks for overly optimistic resize requests.
            // I'm looking at you link.exe v5.10 from 1990.

            const uint16_t spaceBetween = ( !stricmp( g_acApp, "LINK.EXE" ) ) ? 0x30 : 0;

            size_t cEntries = g_allocEntries.size();
            assert( 0 != cEntries ); // loading the app creates one that shouldn't be freed.
            assert( 0 != cpu.bx ); // not legal to allocate 0 bytes

            tracer.Trace( "  all allocations:\n" );
            for ( size_t i = 0; i < cEntries; i++ )
                tracer.Trace( "      alloc entry %d uses segment %04x, size %04x\n", i, g_allocEntries[i].segment, g_allocEntries[i].para_length );

            uint16_t allocatedSeg = 0;
            size_t insertLocation = 0;
            for ( size_t i = 0; i < cEntries; i++ )
            {
                uint16_t after = g_allocEntries[ i ].segment + g_allocEntries[ i ].para_length;
                if ( i < ( cEntries - 1 ) )
                {
                    uint16_t freePara = g_allocEntries[ i + 1 ].segment - after;
                    if ( freePara >= ( cpu.bx + spaceBetween) )
                    {
                        tracer.Trace( "  using gap from previously freed memory: %02x\n", after );
                        allocatedSeg = after;
                        insertLocation = i + 1;
                        break;
                    }
                }
                else if ( ( after + cpu.bx + spaceBetween ) <= SegmentHardware )
                {
                    tracer.Trace( "  using gap after allocated memory: %02x\n", after );
                    allocatedSeg = after + spaceBetween;
                    insertLocation = i + 1;
                    break;
                }
            }

            if ( 0 == allocatedSeg )
            {
                cpu.fCarry = true;
                cpu.ax = 8; // insufficient memory
                DosAllocation & last = g_allocEntries[ cEntries - 1 ];
                uint16_t firstFreeSeg = last.segment + last.para_length;
                assert( firstFreeSeg <= SegmentHardware );
                cpu.bx = SegmentHardware - firstFreeSeg;
                tracer.Trace( "  ERROR: unable to allocate memory. returning that %02x paragraphs are free\n", cpu.bx );
                return;
            }

            DosAllocation da;
            da.segment = allocatedSeg;
            da.para_length = cpu.bx;
            g_allocEntries.insert( insertLocation + g_allocEntries.begin(), da );
            cpu.fCarry = false;
            cpu.bx = SegmentHardware - allocatedSeg;
            cpu.ax = allocatedSeg;

            return;
        }
        case 0x49:
        {
            // free memory. es = segment to free
            // on return, ax = error if cf is set
            // cf clear on success

            tracer.Trace( "  free memory segment %04x\n", cpu.es );

            size_t entry = FindAllocationEntry( cpu.es );
            if ( -1 == entry )
            {
                // The Microsoft Basic compiler BC.EXE 7.10 attempts to free segment 0x80, which it doesn't own.

                tracer.Trace( "  ERROR: memory corruption; can't find freed segment\n" );
                cpu.fCarry = true;
                cpu.ax = 07; // memory corruption
            }
            else
            {
                cpu.fCarry = false;
                g_allocEntries.erase( g_allocEntries.begin() + entry );
            }

            return;
        }
        case 0x4a:
        {
            // modify memory allocation. VGA and other hardware start at a0000
            // lots of opportunity for improvement here.

            size_t entry = FindAllocationEntry( cpu.es );
            if ( -1 == entry )
            {
                cpu.fCarry = 1;
                cpu.bx = 0;
                tracer.Trace( "ERROR: attempt to modify an allocation that doesn't exist\n" );
                return;
            }

            size_t cEntries = g_allocEntries.size();
            assert( 0 != cEntries ); // loading the app creates 1 shouldn't be freed.
            assert( 0 != cpu.bx ); // not legal to allocate 0 bytes

            tracer.Trace( "  all allocations:\n" );
            for ( size_t i = 0; i < cEntries; i++ )
                tracer.Trace( "      alloc entry %d uses segment %04x, size %04x\n", i, g_allocEntries[i].segment, g_allocEntries[i].para_length );

            uint16_t maxParas;
            if ( entry == ( cEntries - 1 ) )
                maxParas = SegmentHardware - g_allocEntries[ entry ].segment;
            else
                maxParas = g_allocEntries[ entry + 1 ].segment - g_allocEntries[ entry ].segment;

            tracer.Trace( "  maximum reallocation paragraphs: %04x, requested size %04x\n", maxParas, cpu.bx );

            if ( cpu.bx > maxParas )
            {
                cpu.fCarry = true;
                cpu.ax = 8; // insufficient memory
                tracer.Trace( "  insufficient RAM for allocation request of %04x\n", cpu.bx );
                cpu.bx = maxParas;
            }
            else
            {
                cpu.fCarry = false;
                tracer.Trace( "  allocation length changed from %04x to %04x\n", g_allocEntries[ entry ].para_length, cpu.bx );
                g_allocEntries[ entry ].para_length = cpu.bx;
            }

            return;
        }
        case 0x4c:
        {
            // exit app
    
            cpu.end_emulation();
            g_haltExecution = true;
            return;
        }
        case 0x4e:
        {
            // find first asciz
            // in: cx = attribute used during search: 7..0 unused, archive, subdir, volume, system, hidden, read-only
            //     ds:dx pointer to null-terminated ascii string including wildcards
            // out: CF: true on error, false on success
            //      ax: error code if CF is true.
            //      disk transfer address: DosFindFile
    
            cpu.fCarry = true;
            DosFindFile * pff = (DosFindFile* ) g_DiskTransferAddress;
            char * psearch_string = (char *) GetMem( cpu.ds, cpu.dx );
            tracer.Trace( "Find First Asciz for pattern '%s'\n", psearch_string );

            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                FindClose( g_hFindFirst );
                g_hFindFirst = INVALID_HANDLE_VALUE;
            }
    
            WIN32_FIND_DATAA fd = {0};
            g_hFindFirst = FindFirstFileA( psearch_string, &fd );
            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                ProcessFoundFile( pff, fd );
                cpu.fCarry = false;
            }
            else
            {
                cpu.ax = GetLastError(); // interesting errors actually match
                tracer.Trace( "WARNING: find first file failed, error %d\n", GetLastError() );
            }
    
            return;
        }
        case 0x4f:
        {
            // find next asciz
    
            cpu.fCarry = true;
            DosFindFile * pff = (DosFindFile* ) g_DiskTransferAddress;
            tracer.Trace( "Find Next Asciz\n" );
    
            if ( INVALID_HANDLE_VALUE != g_hFindFirst )
            {
                WIN32_FIND_DATAA fd = {0};
                BOOL found = FindNextFileA( g_hFindFirst, &fd );
                if ( found )
                {
                    ProcessFoundFile( pff, fd );
                    cpu.fCarry = false;
                }
                else
                {
                    cpu.ax = 12; // no more files
                    tracer.Trace( "WARNING: find next file found no more, error %d\n", GetLastError() );
                }
            }
            else
            {
                cpu.ax = 12; // no more files
                tracer.Trace( "ERROR: search for next without a prior successful search for first\n" );
            }
    
            return;
        }
        case 0x56:
        {
            // rename file: ds:dx old name, es:di new name
            // CF set on error, AX with error code
    
            char * poldname = (char *) GetMem( cpu.ds, cpu.dx );
            char * pnewname = (char *) GetMem( cpu.es, cpu.di );
    
            tracer.Trace( "renaming file '%s' to '%s'\n", poldname, pnewname );
            int renameok = ( 0 == rename( poldname, pnewname ) );
            if ( renameok )
                cpu.fCarry = false;
            else
            {
                tracer.Trace( "ERROR: can't rename file '%s' as '%s' error %d = %s\n", poldname, pnewname, errno, strerror( errno ) );
                cpu.fCarry = true;
                cpu.ax = 2;
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
    
            cpu.fCarry = true;
            uint16_t handle = cpu.bx;
            const char * path = FindFileEntryPath( handle );
            if ( path )
            {
                if ( 0 == cpu.al() )
                {
                    WIN32_FILE_ATTRIBUTE_DATA fad = {0};
                    if ( GetFileAttributesExA( path, GetFileExInfoStandard, &fad ) )
                    {
                        cpu.ax = 0;
                        cpu.fCarry = false;
                        FileTimeToDos( fad.ftLastWriteTime, cpu.cx, cpu.dx );
                    }
                    else
                    {
                        tracer.Trace( "ERROR: can't get/set file date and time; getfileattributesex failed %d\n", GetLastError() );
                        cpu.ax = 1;
                    }
                }
                else if ( 1 == cpu.al() )
                {
                    // set not implemented...
                    cpu.ax = 0x57;
                }
                else
                {
                    tracer.Trace( "ERROR: can't get/set file date and time; command in al not valid: %d\n", cpu.al() );
                    cpu.ax = 1;
                }
            }
            else
            {
                tracer.Trace( "ERROR: can't get/set file date and time; file handle %04x not valid\n", handle );
                cpu.ax = 6;
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
                cpu.fCarry = false;
            }
            else if ( 1 == cpu.al() )
            {
                tracer.Trace( " set memory allocation strategy to %u\n", cpu.bl() );
                cpu.fCarry = false;
            }
            else
            {
                tracer.Trace( " ERROR: memory allocation has unrecognized al: %u\n", cpu.al() );
                cpu.fCarry = true;
            }

            return;
        }
        case 0x59:
        {
            // get extended error code. stub for now until some app really needs it

            cpu.ax = 2; // last error. file not found
            cpu.set_bh( 1 ); // class. out of resources
            cpu.set_bl( 5 ); // suggestion action code. immediate abort.
            cpu.set_ch( 1 ); // suggestion action code. unknown
            return;
        }
        case 0x63:
        {
            // get lead byte table

            cpu.fCarry = true; // not supported;
            return;
        }
        default:
        {
            tracer.Trace( "unhandled int21 command %02x\n", c );
        }
    }
} //handle_int_21

void i8086_invoke_interrupt( uint8_t interrupt_num )
{
    unsigned char c = cpu.ah();
    tracer.Trace( "int %02x ah %02x al %02x bx %04x cx %04x dx %04x ds %04x cs %04x ss %04x es %04x %s\n",
                  interrupt_num, cpu.ah(), cpu.al(),
                  cpu.bx, cpu.cx, cpu.dx,
                  cpu.ds, cpu.cs, cpu.ss, cpu.es,
                  get_interrupt_string( interrupt_num, c ) );

    if ( 0x16 != interrupt_num || 1 != c )
        g_int16_1_loop = false;

    if ( 0x09 == interrupt_num )
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
        cpu.ax = 0x002c;
        return;
    }
    else if ( 0x12 == interrupt_num )
    {
        // .com apps like Turbo Pascal instead read from the Program Segment Prefix

        cpu.ax = 0x280; // 640K conventional RAM
        return;
    }
    else if ( 0x16 == interrupt_num )
    {
        handle_int_16( c );
        return;
    }
    else if ( 0x1a == interrupt_num )
    {
        if ( 0 == c )
        {
            // real time. get ticks since system boot. 18.2 ticks per second.

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
    }
    else if ( 0x20 == interrupt_num ) // compatibility with CP/M apps for COM executables that jump to address 0 in its data segment
    {
        g_haltExecution = true;
        cpu.end_emulation();
        return;
    }
    else if ( 0x21 == interrupt_num )
    {
        handle_int_21( c );
        return;
    }
    else if ( 0x22 == interrupt_num )
    {
        g_haltExecution = true;
        cpu.end_emulation();
        return;
    }
    else if ( 0x24 == interrupt_num )
    {
        printf( "Abort, Retry, Ignore?\n" );
        exit( 1 );
    }
    else if ( 0x28 == interrupt_num )
    {
        // dos idle loop / scheduler

        SleepEx( 1, FALSE );
        return;
    }
    else if ( 0x2a == interrupt_num )
    {
        // dos network / netbios
        cpu.set_ah( 0 ); // not network installed (for function ah==00 )
        return;
    }
    else if ( 0x2f == interrupt_num )
    {
        // dos multiplex interrupt; get installed state of xml driver and other items.
        // AL = 0 to indicate nothing is installed for the many AX values that invoke this.

        if ( 0x1680 == cpu.ax ) // program idle release timeslice
        {
            UpdateDisplay();
            SleepEx( 1, FALSE );
        }

        cpu.set_al( 0x01 ); // not installed, do NOT install
        return;
    }
    else if ( 0x33 == interrupt_num )
    {
        // mouse

        cpu.ax = 0; // hardware / driver not installed
        return;
    }

    tracer.Trace( "UNIMPLEMENTED pc interrupt: %02u == %#x, ah: %02u == %#x, al: %02u == %#x\n",
                  interrupt_num, interrupt_num, cpu.ah(), cpu.ah(), cpu.al(), cpu.al() );
} //i8086_invoke_interrupt

void InitializePSP( uint16_t segment, char * acAppArgs, const char * pcAPP )
{
    * (uint16_t *) ( GetMem( segment, 0x00 ) ) = 0x20cd;     // int 20 instruction to terminate app like CP/M
    * (uint16_t *) ( GetMem( segment, 0x02 ) ) = 0x9fff;     // top of memorysegment in paragraph form
    * (uint8_t *)  ( GetMem( segment, 0x04 ) ) = 0x00;       // DOS uses 0

    * (uint16_t *) ( GetMem( segment, 0x06 ) ) = 0xffff;     // .com programs bytes available in segment
    * (uint16_t *) ( GetMem( segment, 0x08 ) ) = 0xdead;     // ?? DOS uses this value
    * (uint32_t *) ( GetMem( segment, 0x0a ) ) = 0xf000;     // uint32_t int 22 terminate address

    // 0x80: # of characters following command name at startup
    // 0x81: all characters after program name followed by a CR 0x0d

    uint8_t len = strlen( acAppArgs );
    uint8_t * pargs = GetMem( segment, 0x80 );
    * pargs = len;
    strcpy( (char *) pargs + 1, acAppArgs );
    pargs[ 1 + len ] = 0x0d; // CR ends the args

    // initialize the environment, which (for DOS 3.0+) has the full path of the app afterwards
    // this is a somewhat random location that appears to be free

    const uint16_t EnvironmentSegment = 0x80; 
    * (uint16_t *)  ( GetMem( segment, 0x2c ) ) = EnvironmentSegment;
    char * penvdata = (char *) GetMem( EnvironmentSegment, 0 );
    if ( !stricmp( g_acApp, "B.EXE" ) )
        strcpy( penvdata, "BFLAGS=-kzr -mDJL" ); // Brief: keyboard compat, no ^z at end, fast screen updates, my macros
    else
        strcpy( penvdata, "" );
    len = strlen( penvdata );
    penvdata[ len + 1 ] = 0; // extra 0 for no more environment variables
    * (uint16_t *)  ( penvdata + len + 2 ) = 0x0001; // one more additional item per DOS 3.0+
    GetFullPathNameA( pcAPP, 256, (char *) penvdata + len + 4, 0 );
    tracer.Trace( "wrote full path path to environment: '%s'\n", penvdata + len + 4 );
} //InitializePSP

static void usage( char const * perr )
{
    if ( perr )
        printf( "error: %s\n", perr );

    printf( "NT Virtual DOS Machine: emulates an MS-DOS 3.00 runtime environment enough to run some COM/EXE files on Win64\n" );
    printf( "usage: ntvdm [arguments] <DOS executable> [arg1] [arg2]\n" );
    printf( "  notes:\n" );
    printf( "            -c     don't auto-detect apps that want 80x25 then set window to that size;\n" );
    printf( "                   stay in teletype mode.\n" );
    printf( "            -C     always set window to 80x25; don't use teletype mode.\n" );
    printf( "            -d     don't clear the display when in 80x25 mode on app exit\n" );
    printf( "            -i     trace instructions as they are executed to ntvdm.log (this is verbose!)\n" );
    printf( "            -p     show performance information\n" ); 
#ifdef I8086_TRACK_CYCLES
    printf( "            -s:X   speed in Hz. Default is to run as fast as possible.\n" );
    printf( "                   for 4.77Mhz, use -s:4770000\n" );
#endif
    printf( "            -t     enable debug tracing to ntvdm.log\n" );
    printf( " [arg1] [arg2]     arguments after the .COM/.EXE file are passed to that command\n" );
    printf( "  examples:\n" );
    printf( "      ntvdm -c -t app.com foo bar\n" );
    printf( "      ntvdm turbo.com\n" );
    printf( "      ntvdm s:\\github\\MS-DOS\\v2.0\\bin\\masm small,,,small\n" );
    printf( "      ntvdm s:\\github\\MS-DOS\\v2.0\\bin\\link small,,,small\n" );
    printf( "      ntvdm -t b -k myfile.asm\n" );
    exit( 1 );
} //usage

int main( int argc, char ** argv )
{
    memset( memory, 0, sizeof memory );
    g_hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
    init_blankline( 0x7 ); // light grey text

    char * pcAPP = 0;
    bool trace = FALSE;
    uint64_t clockrate = 0;
    bool showPerformance = false;
    char acAppArgs[80] = {0};
    bool traceInstructions = false;
    bool force80x25 = false;
    bool clearDisplayOnExit = true;

    for ( int i = 1; i < argc; i++ )
    {
        char *parg = argv[i];
        char c = *parg;

        if ( ( 0 == pcAPP ) && ( '-' == c || '/' == c ) )
        {
            char ca = tolower( parg[1] );

            if ( 'd' == ca )
                clearDisplayOnExit = false;
            else if ( 's' == ca )
            {
                if ( ':' == parg[2] )
                    clockrate = _strtoui64( parg+ 3 , 0, 10 );
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
            else
                usage( "invalid argument specified" );
        }
        else
        {
            if ( 0 == pcAPP )
                pcAPP = parg;
            else if ( strlen( acAppArgs ) + 3 + strlen( parg ) < _countof( acAppArgs ) )
            {
                if ( 0 != acAppArgs[0] )
                    strcat( acAppArgs, " " );

                strcat( acAppArgs, parg );
            }
        }
    }

    tracer.Enable( trace, L"ntvdm.log", true );
    tracer.SetQuiet( true );
    cpu.trace_instructions( traceInstructions );

    if ( 0 == pcAPP )
        usage( "no command specified" );

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

    bool isCOM = !strcmp( g_acApp + strlen( g_acApp ) - 4, ".COM" );

    cpu.cs = 0xF000;
    cpu.fTrap = false;

    // Set DL equal to the boot device: 0 for the FD.
    cpu.set_dl( 0 );

    // Set CX:AX equal to the hard disk image size, if present
    cpu.cx = 0;
    cpu.ax = 0;

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

    * (uint8_t *) ( GetMem( 0xffff, 0xe ) ) = 0x69; // machine ID. nice.

    // 256 interrupt vectors at address 0 - 3ff. The first 0x40 are reserved for bios/dos and point to
    // routines starting at 0xc00. The routines are almost all the same -- fake opcode, interrupt #, then a
    // far ret 2 (not iret) so as to not trash the flags used as return codes.
    // The exception is tick tock interrupt 0x1c, which just does an iret for performance.
    // The functions are all 5 bytes long.

    uint32_t * pVectors = (uint32_t *) GetMem( 0, 0 );
    uint8_t * pRoutines = (uint8_t *) GetMem( 0xc0, 0 ); // that's address 0xc00
    for ( uint32_t intx = 0; intx < 0x40; intx++ )
    {
        uint32_t offset = intx * 5;
        pVectors[ intx ] = ( 0xc0 << 16 ) | ( offset );
        uint8_t * routine = pRoutines + offset;
        if ( 0x1c == intx )
            routine[ 0 ] = 0xcf; // iret
        else
        {
            routine[ 0 ] = i8086_opcode_interrupt;
            routine[ 1 ] = (uint8_t) intx;
            routine[ 2 ] = 0xca; // retf 2
            routine[ 3 ] = 2;
            routine[ 4 ] = 0;
        }
    }

    InitializePSP( AppSegment, acAppArgs, g_acApp );

    if ( isCOM )
    {
        // load .com file
        FILE * fp = fopen( g_acApp, "rb" );
        if ( 0 == fp )
            usage( "can't open input com file" );
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        int ok = fread( memory + AppSegmentOffset + 0x100, file_size, 1, fp ) == 1;
        if ( !ok )
            usage( "can't read .com file" );
        fclose( fp );
    
        // prepare to execute the COM file
      
        cpu.cs = AppSegment;
        cpu.ss = AppSegment;
        cpu.ds = AppSegment;
        cpu.es = AppSegment;
        cpu.sp = 0xffff;
        cpu.ip = 0x100;

        tracer.Trace( "loaded %s, app segment %04x, ip %04x\n", g_acApp, cpu.cs, cpu.ip );
    }
    else // EXE
    {
        const uint32_t DataSegmentOffset = AppSegmentOffset;
        const uint16_t DataSegment = AppSegment;

        // Apps own all memory by default. They can realloc this to free space for other allocations

        DosAllocation da;
        da.segment = DataSegment;
        da.para_length = SegmentsAvailable;
        g_allocEntries.push_back( da );
        tracer.Trace( "app given %04x segments, which is %u bytes\n", da.para_length, da.para_length * 16 );

        // load the .exe file
        FILE * fp = fopen( g_acApp, "rb" );
        if ( 0 == fp )
            usage( "can't open input exe file" );
    
        fseek( fp, 0, SEEK_END );
        long file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        vector<uint8_t> theexe( file_size );
        int ok = fread( theexe.data(), file_size, 1, fp ) == 1;
        if ( !ok )
            usage( "can't read .exe file" );
        fclose( fp );

        ExeHeader & head = * (ExeHeader *) theexe.data();
        if ( 0x5a4d != head.signature )
            usage( "exe isn't MZ" );  // he was my hiring manager

        tracer.Trace( "loading app %s\n", g_acApp );
        tracer.Trace( "looks like an MZ exe... size %u, size from blocks %u, bytes in last block %u\n",
                      file_size, ( (uint32_t) head.blocks_in_file ) * 512, head.bytes_in_last_block );
        tracer.Trace( "relocation entry count %u, header paragraphs %u (%u bytes)\n",
                      head.num_relocs, head.header_paragraphs, head.header_paragraphs * 16 );
        tracer.Trace( "relative value of stack segment: %#x, initial sp: %#x, initial ip %#x, initial cs relative to segment: %#x\n",
                      head.ss, head.sp, head.ip, head.cs );
        tracer.Trace( "relocation table offset %u, overlay number %u\n",
                      head.reloc_table_offset, head.overlay_number );

        if ( head.reloc_table_offset > 64 )
            usage( "probably not a 16-bit exe" );

        uint32_t codeStart = 16 * (uint32_t) head.header_paragraphs;
        uint32_t cbUsed = head.blocks_in_file * 512;
        if ( 0 != head.bytes_in_last_block )
            cbUsed -= ( 512 - head.bytes_in_last_block );
        cbUsed -= codeStart; // don't include the header
        tracer.Trace( "bytes used by load module: %u, and code starts at %u\n", cbUsed, codeStart );

        const uint32_t CodeSegmentOffset = DataSegmentOffset + 0x100;   //  data segment + 256 bytes for the psp
        const uint16_t CodeSegment = CodeSegmentOffset / 16;    

        uint8_t * pcode = GetMem( CodeSegment, 0 );
        memcpy( pcode, theexe.data() + codeStart, cbUsed );
        tracer.Trace( "start of the code:\n" );
        DumpBinaryData( pcode, 0x200, 0 );

        // apply relocation entries

        ExeRelocation * pRelocationEntries = (ExeRelocation *) ( theexe.data() + head.reloc_table_offset );
        for ( uint16_t r = 0; r < head.num_relocs; r++ )
        {
            uint32_t offset = pRelocationEntries[ r ].offset + pRelocationEntries[ r ].segment * 16;
            uint16_t * target = (uint16_t *) ( pcode + offset );
            //tracer.TraceQuiet( "relocation %u offset %u, update %#02x to %#02x\n", r, offset, *target, *target + CodeSegment );
            *target += CodeSegment;
        }

        cpu.cs = CodeSegment + head.cs;
        cpu.ss = CodeSegment + head.ss;
        cpu.ds = DataSegment;
        cpu.es = cpu.ds;
        cpu.sp = head.sp;
        cpu.ip = head.ip;
        cpu.ax = 0xffff; // no drives in use

        tracer.Trace( "CS: %#x, SS: %#x, DS: %#x, SP: %#x, IP: %#x\n", cpu.cs, cpu.ss, cpu.ds, cpu.sp, cpu.ip );
    }

    if ( !stricmp( g_acApp, "gwbasic.exe" ) )
    {
        // gwbasic calls ioctrl on stdin and stdout before doing anything that would indicate what mode it wants.

        if ( !g_forceConsole )
            force80x25 = true;
    }

    if ( force80x25 )
        PerhapsFlipTo80x25();

    g_DiskTransferAddress = GetMem( cpu.ds, 0x80 ); // DOS default address
    g_haltExecution = false;

    CPerfTime perfApp;
    uint64_t total_cycles = 0; // this will be instructions if I8086_TRACK_CYCLES isn't defined
    CPUCycleDelay delay( clockrate );
    ULONGLONG ms_last = GetTickCount64();

    do
    {
        total_cycles += cpu.emulate( 1000 ); // 1000 cycles or instructions at a time

        if ( g_haltExecution )
            break;

        delay.Delay( total_cycles );

        if ( !g_KbdIntWaitingForRead && peek_keyboard( true, false, true ) )
        {
            tracer.Trace( "main loop: scheduling an int 9\n" );
            g_KbdIntWaitingForRead = true;
            cpu.external_interrupt( 9 );
            continue;
        }

        // if interrupt 0x1c (tick tock) is hooked by the app, invoke it

        if ( 0x00c0 != ( (uint16_t *) memory )[ 4 * 0x1c + 2 ] ) // optimization since the default handler is just an iret
        {
            // this won't be precise enough to provide a clock, but it's good for delay loops

            ULONGLONG ms_now = GetTickCount64();
            if ( ms_now >= ( ms_last + 55 ) ) // On my machine GetTickCount64() changes every 16ms or so
            {
                // if the app is blocked on keyboard input this interrupt will be delivered late.

                ms_last = ms_now;
                cpu.external_interrupt( 0x1c );
                continue;
            }
        }
    } while ( true );

    if ( g_use80x25 )  // get any last-second screen updates displayed
        UpdateDisplay();

    LONGLONG elapsed = 0;
    FILETIME creationFT, exitFT, kernelFT, userFT;
    if ( showPerformance )
    {
        perfApp.CumulateSince( elapsed );
        GetProcessTimes( GetCurrentProcess(), &creationFT, &exitFT, &kernelFT, &userFT );
    }

    g_consoleConfig.RestoreConsole( clearDisplayOnExit );

    if ( showPerformance )
    {
        #ifdef I8086_TRACK_CYCLES
            printf( "8086 cycles:      %16ws\n", perfApp.RenderLL( (LONGLONG) total_cycles ) );
            printf( "clock rate: " );
            if ( 0 == clockrate )
            {
                printf( "      %16s\n", "unbounded" );
                uint64_t total_ms = total_cycles / 4770;
                printf( "approx ms at 4.77Mhz: %12ws  == ", perfApp.RenderLL( total_ms ) );
                uint16_t days = total_ms / 1000 / 60 / 60 / 24;
                uint16_t hours = ( total_ms % ( 1000 * 60 * 60 * 24 ) ) / 1000 / 60 / 60;
                uint16_t minutes = ( total_ms % ( 1000 * 60 * 60 ) ) / 1000 / 60;
                uint16_t seconds = ( total_ms % ( 1000 * 60 ) ) / 1000;
                uint16_t milliseconds = ( total_ms % 1000 );
                printf( "%u days, %u hours, %u minutes, %u seconds, %u milliseconds\n", days, hours, minutes, seconds, milliseconds );
            }
            else
                printf( "      %16ws Hz\n", perfApp.RenderLL( (LONGLONG ) clockrate ) );
        #endif

        ULARGE_INTEGER ullK, ullU;
        ullK.HighPart = kernelFT.dwHighDateTime;
        ullK.LowPart = kernelFT.dwLowDateTime;
    
        ullU.HighPart = userFT.dwHighDateTime;
        ullU.LowPart = userFT.dwLowDateTime;
    
        printf( "kernel CPU ms:    %16ws\n", perfApp.RenderDurationInMS( ullK.QuadPart ) );
        printf( "user CPU ms:      %16ws\n", perfApp.RenderDurationInMS( ullU.QuadPart ) );
        printf( "total CPU ms:     %16ws\n", perfApp.RenderDurationInMS( ullU.QuadPart + ullK.QuadPart ) );
        printf( "elapsed ms:       %16ws\n", perfApp.RenderDurationInMS( elapsed ) );
    }

    tracer.Shutdown();

    return 0;
} //main

