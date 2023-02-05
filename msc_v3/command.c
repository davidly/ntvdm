/*
   simplistic command.com replacement for ntvdm.
   written for apps running in ntvdm that shell to command.com, like wordstar and quick basic.
   the actual command.com assumes too much about DOS internals to emulate.
*/

#define LINT_ARGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <dos.h>
#include <process.h>

#define MAX_CMD_LEN 128
#define MAX_ARGUMENTS 30
#define EXTERNAL_CMD_NOT_FOUND -1111
#define INTERNAL_CMD_NOT_FOUND -1
#define EXIT_CMD 1
#define false 0
#define true 1

union REGS inregs, outregs;
struct SREGS sregs;

struct FINDENTRY
{
    char undoc[ 0x15 ];
    unsigned char attr;
    unsigned short filetime;  /* low 5 bits seconds, next 6 bits minutes, high 5 bits hours */
    unsigned short filedate;  /* low 5 bits day, next 4 bits month, high 7 bits year less 1980 */
    unsigned long filesize;
    char filename[ 13 ];
};

/* shared global buffer for various commands to use */
char g_acbuffer[ MAX_CMD_LEN ];
char far * g_acbuf = g_acbuffer;

/* the DOS DiskTransfer address. The C runtime changes this, so poll just before use */
char far * g_transfer = 0;

void update_disk_transfer_buffer()
{
    unsigned long adr;

    inregs.h.ah = 0x2f; /* get disk transfer address */
    segread( &sregs );
    intdosx( &inregs, &outregs, &sregs );

    adr = sregs.es;
    adr <<= 16;
    adr |= outregs.x.bx;
    g_transfer = (char far *) adr;
} /*update_disk_transfer_buffer*/

void show_prompt()
{
    getcwd( g_acbuffer, sizeof g_acbuffer );
    printf( "<%s>", g_acbuffer );
} /*show_prompt*/

int stricmp( a, b ) char * a; char * b;
{
    int diff;

    while ( *a && *b )
    {
        /* can't use ++ operaters in this line as bad code gets generated. compiler bug! */

        diff = toupper( *a ) - toupper( *b );
        if ( 0 != diff )
            return diff;
        a++;
        b++;
    }

    return toupper( *a ) - toupper( *b );
} /*stricmp*/

unsigned long show_find_result( wide, shown ) int wide; int shown;
{
    char far * pfile;
    int day, month, year, minute, hour;
    struct FINDENTRY far * fe = (struct FINDENTRY far *) g_transfer;

    if ( wide )
    {
        if ( ! ( shown % 4 ) )
            printf( "\n" );

        if ( fe->attr & 0x10 )
            printf( "  [%-12s]  ", fe->filename );
        else
            printf( "  %-14s  ", fe->filename );
    }
    else
    {
        if ( 0 == shown )
            printf( "\n" );

        day = fe->filedate & 0x1f;
        month = ( fe->filedate >> 5 ) & 0xf;
        year = ( ( fe->filedate >> 9 ) & 0x7f ) + 1980;
        minute = ( fe->filetime >> 5 ) & 0x3f;
        hour = ( fe->filetime >> 11 ) & 0x1f;

        if ( fe->attr & 0x10 )
            printf( "  [%-12s]  %10s  %02d-%02d-%04d %02d:%02d\n", fe->filename, "<dir>", month, day, year, hour, minute );
        else
            printf( "  %-14s  %10ld  %02d-%02d-%04d %02d:%02d\n", fe->filename, fe->filesize, month, day, year, hour, minute );
    }

    return fe->filesize;
} /*show_find_result*/

void dir_command( argc, argv ) int argc; char * argv[];
{
    int i;
    int wide = 0;
    int shown = 0;
    unsigned long size = 0;

    if ( argc > 3 )
    {
        printf( "invalid arguments\n" );
        return;
    }

    update_disk_transfer_buffer();
    g_acbuf[ 0 ] = 0;

    if ( argc > 1 )
    {
        for ( i = 1; i < argc; i++ )
        {
            if ( '/' == argv[ i ][0] )
            {
                if ( 'W' == toupper( argv[ i ][1] ) )
                    wide = 1;
                else
                {
                    printf( "invalid flag\n" );
                    return;
                }
            }
            else if ( 0 == g_acbuf[ 0 ] )
                strcpy( g_acbuf, argv[ i ] );
            else
            {
                printf( "invalid arguments\n" );
                return;
            }
        }
    }

    if ( 0 == g_acbuf[ 0 ] )
        strcpy( g_acbuf, "*.*" ); /* on real dos, *.* finds files * does not */

    inregs.h.ah = 0x4e; /* find first */
    inregs.x.dx = FP_OFF( g_acbuf );
    segread( &sregs );
    sregs.ds = FP_SEG( g_acbuf );
    intdosx( &inregs, &outregs, &sregs );

    if ( 0 == outregs.x.cflag )
    {
        do
        {
            size += show_find_result( wide, shown );
            shown++;

            inregs.h.ah = 0x4f; /* find next */
            inregs.x.dx = FP_OFF( g_acbuf );
            segread( &sregs );
            sregs.ds = FP_SEG( g_acbuf );
            intdosx( &inregs, &outregs, &sregs );
        } while( 0 == outregs.x.cflag );
    }

    printf( "\n %15d files\n", shown );
    printf( " %15ld bytes\n", size );
} /*dir_command*/

void type_command( argc, argv ) int argc; char * argv[];
{
    int c;
    FILE * fp = fopen( argv[ 1 ], "r" );
    if ( 0 != fp )
    {
        while ( !feof( fp ) )
            printf( "%c", fgetc( fp ) );
        fclose( fp );
    }
    else
        printf( "unable to find file '%s'\n", argv[ 1 ] );
} /*type_command*/

void mem_command( argc, argv ) int argc; char * argv[];
{
    unsigned long freemem;

    inregs.h.ah = 0x48; /* allocate memory */
    inregs.x.bx = 0xffff; /* ask for an impossible amount to see how much is available */
    intdos( &inregs, &outregs );

    freemem = outregs.x.bx;
    freemem <<= 4;
    printf( "%ld Kb free conventional memory\n", freemem / 1024 );
} /*mem_command*/

void cd_command( argc, argv ) int argc; char * argv[];
{
    int ret;
    if ( 2 != argc )
    {
        printf( "usage: cd <directory>\n" );
        return;
    }

    ret = chdir( argv[ 1 ] );
    if ( 0 != ret )
        perror( "unable to change directory" );
} /*cd_command*/

void md_command( argc, argv ) int argc; char * argv[];
{
    int ret;
    if ( 2 != argc )
    {
        printf( "usage: md <directory>\n" );
        return;
    }

    ret = mkdir( argv[ 1 ] );
    if ( 0 != ret )
        perror( "unable to make directory" );
} /*md_command*/

void rd_command( argc, argv ) int argc; char * argv[];
{
    int ret;
    if ( 2 != argc )
    {
        printf( "usage: rd <directory>\n" );
        return;
    }

     ret = rmdir( argv[ 1 ] );
    if ( 0 != ret )
        perror( "unable to remove directory" );
} /*rd_command*/

void ren_command( argc, argv ) int argc; char * argv[];
{
    int ret;
    if ( 3 != argc )
    {
        printf( "usage: ren <old name> <new name>\n" );
        return;
    }

    /* the C runtime for Microsoft C V3.00 reverses the arguments to rename(). So don't use it. */

#if 1

    inregs.h.ah = 0x56; /* rename */
    inregs.x.dx = FP_OFF( argv[1] );
    inregs.x.di = FP_OFF( argv[2] );
    segread( &sregs );
    sregs.ds = FP_SEG( argv[1] );
    sregs.es = FP_SEG( argv[2] );
    intdosx( &inregs, &outregs, &sregs );

    if ( 1 == outregs.x.cflag )
        printf( "unable to rename the file, error %d\n", outregs.x.ax );

#else

    ret = rename( argv[ 1 ], argv[ 2 ] );
    if ( 0 != ret )
        perror( "unable to rename" );

#endif

} /*ren_command*/

void set_command( argc, argv ) int argc; char * argv[];
{
    int i = 0;

    if ( 1 == argc )
    {
        printf( "\n" );
        while ( environ[ i ] )
        {
            printf( "%s\n", environ[ i ] );
            i++;
        }
    }
    else
        printf( "this form of SET is not implemented\n" );
} /*set_command*/

int run_internal( cmd, argc, argv ) char * cmd; int argc; char * argv[];
{
    if ( !stricmp( "cd", cmd ) || !stricmp( "chdir", cmd ) )
    {
        cd_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "dir", cmd ) )
    {
        dir_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "exit", cmd ) )
    {
        return EXIT_CMD;
    }
    else if ( !stricmp( "md", cmd ) || !stricmp( "mkdir", cmd ) )
    {
        md_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "ren", cmd ) || !stricmp( "rename", cmd ) )
    {
        ren_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "rd", cmd ) || !stricmp( "rmdir", cmd ) )
    {
        rd_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "set", cmd ) )
    {
        set_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "type", cmd ) )
    {
        type_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "mem", cmd ) )
    {
        mem_command( argc, argv );
        return 0;
    }

    return INTERNAL_CMD_NOT_FOUND;
} /*run_internal*/

int file_exists( filename ) char * filename;
{
    inregs.h.ah = 0x43; /* get/put file attributes */
    inregs.h.al = 0; /* get */
    inregs.x.dx = FP_OFF( filename );
    segread( &sregs );
    sregs.ds = FP_SEG( filename );
    intdosx( &inregs, &outregs, &sregs );

    return ( 0 == outregs.x.cflag && ( 0 == ( 0x10 & outregs.x.cx ) ) ); /* success and not a directory */
} /*file_exists*/

char * stristr( str, search ) char * str; char * search;
{
    char * p = str;
    char * s;
    char * location;

    while ( *p )
    {
        s = search;
        location = p;

        while ( *p && ( toupper( *s ) == toupper( *p ) ) )
        {
            p++;
            s++;
        }

        if ( 0 == *s )
            return location;

        p = location + 1;
    }

    return 0;
} /*stristr*/

int run_external( cmd, argc, argv ) char * cmd; int argc; char * argv[];
{
    int ret;
    strcpy( g_acbuffer, cmd );
    if ( !stristr( g_acbuffer, ".com" ) && !stristr( g_acbuffer, ".exe" ) )
    {
        strcat( g_acbuffer, ".com" );
        if ( !file_exists( g_acbuffer ) )
        {
            strcpy( g_acbuffer, cmd );
            strcat( g_acbuffer, ".exe" );
            if ( !file_exists( g_acbuffer ) )
                return EXTERNAL_CMD_NOT_FOUND;
        }
    }
    else
    {
        if ( !file_exists( g_acbuffer ) )
            return EXTERNAL_CMD_NOT_FOUND;
    }

    ret = spawnv( P_WAIT, g_acbuffer, argv );
    if ( -1 == ret )
        printf( "failure to start or execute external program '%s', return code %d errno %d\n", g_acbuffer, ret, errno );
    return ret;
} /*run_external*/

int parse_arguments( cmdline, argv ) char * cmdline; char * argv[];
{
    int count = 0;
    int o = 0;

    while ( 0 != cmdline[ o ] ) 
    {
        while ( ' ' == cmdline[ o ] )
        {
            cmdline[ o ] = 0;
            o++;
        }

        if ( 0 == cmdline[ o ] )
            break;

        argv[ count ] = cmdline + o;
        count++;

        while ( ' ' != cmdline[ o ] && 0 != cmdline[ o ] )
            o++;
    }

    argv[ count ] = 0; /* last argument must be null */

    /*
    printf( "there are %d elements\n", count );
    for ( o = 0; o < count; o++ )
        printf( "  item %d: '%s'\n", o, argv[ o ] );
    */

    return count;
} /*parse_arguments*/

int main( argc, argv ) int argc; char * argv[];
{
    static char cmdline[ MAX_CMD_LEN ];
    int j, ret;
    static char * sub_argv[ MAX_ARGUMENTS ];
    int  sub_argc = 0;

    if ( argc > 1 )
    {
        if ( ( '/' == argv[1][0] ) && ( 'C' == toupper( argv[1][1] ) ) )
        {
            /* execute the command then exit */

            for ( j = 2; j < argc; j++ )
                printf( "argument %d: %s\n", j, argv[ j ] );

            if ( argc < 3 )
            {
                printf( "no command following /C\n" );
                return -1;
            }

            for ( j = 2; j < argc; j++ )
            {
                sub_argv[ sub_argc ] = argv[ j ];
                sub_argc++;
            }

            sub_argv[ sub_argc ] = 0; /* last argument must be null */

            ret = run_internal( argv[ 2 ], sub_argc, sub_argv );

            if ( INTERNAL_CMD_NOT_FOUND == ret )
                ret = run_external( argv[ 2 ], sub_argc, sub_argv );

            return ret;
        }
        else
        {
            printf( "unrecognized command line argument\n" );
            return -1;
        }
    }

    printf( "ntvdm command prompt on DOS v%d.%d\n", _osmajor, _osminor );

    do
    {
        show_prompt();
        fflush( stdout ); /* this ancient C runtime requires this or output is batched */

        gets( cmdline );

        sub_argc = parse_arguments( cmdline, sub_argv );

        if ( 0 != sub_argc )
        {
            ret = run_internal( sub_argv[ 0 ], sub_argc, sub_argv );
    
            if ( EXIT_CMD == ret )
                break;
    
            if ( INTERNAL_CMD_NOT_FOUND == ret )
                ret = run_external( sub_argv[ 0 ], sub_argc, sub_argv );

            if ( EXTERNAL_CMD_NOT_FOUND == ret )
                printf( "'%s' not recognized as an internal or external command\n", sub_argv[ 0 ] );
        }

        printf( "\n" );
    } while ( 1 );

    printf( "exiting ntvdm command prompt\n" );
    return 0;
} /*main*/

