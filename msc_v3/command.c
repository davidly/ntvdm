/*
   simplistic command.com replacement for ntvdm.
   written for apps running in ntvdm that shell to command.com, like wordstar and quick basic.
   the actual command.com assumes too much about DOS internals to emulate without a lot of work.
*/

#define LINT_ARGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <dos.h>
#include <fcntl.h>
#include <process.h>
#include <types.h>
#include <stat.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>

#define MAX_CMD_LEN 128
#define MAX_ARGUMENTS 30
#define EXTERNAL_CMD_NOT_FOUND -1111
#define INTERNAL_CMD_NOT_FOUND -1
#define EXIT_CMD 1111
#define false 0
#define true 1

struct FINDENTRY
{
    char undoc[ 0x15 ];
    unsigned char attr;       /* same bits as Windows */
    unsigned short filetime;  /* low 5 bits seconds, next 6 bits minutes, high 5 bits hours */
    unsigned short filedate;  /* low 5 bits day, next 4 bits month, high 7 bits year less 1980 */
    unsigned long filesize;   /* max 4 gig */
    char filename[ 13 ];      /* asciiz max of 8.3 */
};

int parse_and_run( char * );

union REGS g_regs_in;
union REGS g_regs_out;
struct SREGS g_segregs;

/* shared global buffer for various commands to use */

char g_acbuffer[ MAX_CMD_LEN ];
char far * g_pbuf = g_acbuffer;

/* the DOS DiskTransfer address. The C runtime changes this, so poll just before use */

char far * g_transfer = 0;

void update_disk_transfer_buffer()
{
    unsigned long adr;

    g_regs_in.h.ah = 0x2f; /* get disk transfer address */
    segread( &g_segregs );
    intdosx( &g_regs_in, &g_regs_out, &g_segregs );

    adr = g_segregs.es;
    adr <<= 16;
    adr |= g_regs_out.x.bx;
    g_transfer = (char far *) adr;
} /*update_disk_transfer_buffer*/

void show_prompt()
{
    getcwd( g_acbuffer, sizeof g_acbuffer );
    printf( "<%s>", g_acbuffer );
    fflush( stdout ); /* this ancient C runtime requires this or output is batched */
} /*show_prompt*/

int iswhite( c ) char c;
{
    return ( ' ' == c || '\t' == c || 0xd == c || 0xa == c );
} /*iswhite*/

void rm_leading_and_trailing_space( str ) char * str;
{
    char * p = str;
    int len;
    while ( iswhite( *p ) )
        p++;
    strcpy( str, p );
    len = strlen( str );
    while ( ( len > 0 ) && iswhite( str[ len - 1 ] ) )
    {
        len--;
        str[ len ] = 0;
    }
} /*rm_leading_and_trailing_space*/

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

void print_dir( name ) char far * name;
{
    int i;
    int len = strlen( name );

    printf( "  [%s]  ", name );

    for ( i = 0; i < 12-len; i++ )
        printf( " " );
} /*print_dir*/

unsigned long show_dir_result( wide, shown ) int wide; int shown;
{
    char far * pfile;
    int day, month, year, minute, hour;
    struct FINDENTRY far * fe = (struct FINDENTRY far *) g_transfer;
    char attrib[7]; /* read only, hidden, system, volume, subdirectory, archive */

    if ( wide )
    {
        if ( ! ( shown % 4 ) )
            printf( "\n" );

        if ( fe->attr & 0x10 )
            print_dir( fe->filename );
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

        strcpy( attrib, "      " );
        if ( fe->attr & 1 )
            attrib[ 0 ] = 'r';
        if ( fe->attr & 2 )
            attrib[ 1 ] = 'h';
        if ( fe->attr & 4 )
            attrib[ 2 ] = 's';
        if ( fe->attr & 8 )
            attrib[ 3 ] = 'v';
        if ( fe->attr & 16 )
            attrib[ 4 ] = 'd';
        if ( fe->attr & 32 )
            attrib[ 5 ] = 'a';

        if ( fe->attr & 0x10 )
        {
            print_dir( fe->filename );
            printf( "     <dir>  %02d-%02d-%04d %02d:%02d  %s\n", month, day, year, hour, minute, attrib );
        }
        else
            printf( "  %-14s  %10ld  %02d-%02d-%04d %02d:%02d  %s\n", fe->filename, fe->filesize, month, day, year, hour, minute, attrib );
    }

    fflush( stdout ); /* this ancient C runtime requires this or output is batched */

    if ( fe->attr & 0x10 )
        return 0;

    return fe->filesize;
} /*show_dir_result*/

void invoke_find_next()
{
    g_regs_in.h.ah = 0x4f; /* find next */
    g_regs_in.x.dx = FP_OFF( g_pbuf );
    segread( &g_segregs );
    g_segregs.ds = FP_SEG( g_pbuf );
    intdosx( &g_regs_in, &g_regs_out, &g_segregs );
} /*invoke_find_next*/

void invoke_find_first()
{
    g_regs_in.h.ah = 0x4e; /* find first */
    g_regs_in.x.dx = FP_OFF( g_pbuf );
    segread( &g_segregs );
    g_segregs.ds = FP_SEG( g_pbuf );
    intdosx( &g_regs_in, &g_regs_out, &g_segregs );
} /*invoke_find_first*/

void dir_command( argc, argv ) int argc; char * argv[];
{
    int i;
    int wide = 0;
    int shown = 0;
    unsigned long size = 0;

    if ( argc > 3 )
    {
        printf( "invalid argument count for dir: %d\n", argc );
        return;
    }

    update_disk_transfer_buffer();
    g_acbuffer[ 0 ] = 0;

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
            else if ( 0 == g_acbuffer[ 0 ] )
            {
                strcpy( g_acbuffer, argv[ i ] );

                if ( !strcmp( g_acbuffer, "\\" ) )
                    strcat( g_acbuffer, "*.*" );
                else if ( directory_exists( g_acbuffer ) )
                    strcat( g_acbuffer, "\\*.*" );
            }
            else
            {
                printf( "invalid argument: '%s'\n", argv[i] );
                return;
            }
        }
    }

    if ( 0 == g_acbuffer[ 0 ] )
        strcpy( g_acbuffer, "*.*" ); /* on real dos, *.* finds files * does not */

    invoke_find_first();

    while ( 0 == g_regs_out.x.cflag )
    {
        assert( g_pbuf == g_acbuffer );
        size += show_dir_result( wide, shown );
        shown++;

        invoke_find_next();
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

    g_regs_in.h.ah = 0x48; /* allocate memory */
    g_regs_in.x.bx = 0xffff; /* ask for an impossible amount to see how much is available */
    intdos( &g_regs_in, &g_regs_out );

    freemem = g_regs_out.x.bx;
    freemem <<= 4;
    printf( "\n%ld Kb free conventional memory\n", freemem / 1024 );
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

    g_regs_in.h.ah = 0x56; /* rename */
    g_regs_in.x.dx = FP_OFF( argv[1] );
    g_regs_in.x.di = FP_OFF( argv[2] );
    segread( &g_segregs );
    g_segregs.ds = FP_SEG( argv[1] );
    g_segregs.es = FP_SEG( argv[2] );
    intdosx( &g_regs_in, &g_regs_out, &g_segregs );

    if ( 1 == g_regs_out.x.cflag )
        printf( "unable to rename the file, error %d\n", g_regs_out.x.ax );

#else

    ret = rename( argv[ 1 ], argv[ 2 ] );
    if ( 0 != ret )
        perror( "unable to rename" );

#endif

} /*ren_command*/

char * strupr_env_name( envval ) char * envval;
{
    /* dos apps often expect that environment value names are in upper case (they fail to find values otherwise) */

    char * p = envval;
    char * equal = strchr( envval, '=' );
    if ( equal )
    {
        while ( p < equal )
        {
            *p = toupper( *p );
            p++;
        }
    }

    return envval;
} /*strupr_env_name*/

void set_command( argc, argv ) int argc; char * argv[];
{
    int i = 0;
    int ret;

    if ( 1 == argc )
    {
        printf( "\n" );
        while ( environ[ i ] )
        {
            printf( "%s", environ[ i ] );
            printf( "\n" );
            i++;
        }
    }
    else if ( 2 == argc ) /* set foo=bar */
    {
        if ( strchr( argv[ 1 ], '=' ) )
        {
            ret = putenv( strupr_env_name( strdup( argv[ 1 ] ) ) );
            if ( 0 != ret )
                perror( "unable to set an environment variable" );
        }
        else
            printf( "no equal sign found in SET statement\n" );
    }
    else
        printf( "this form of SET is not implemented\n" );
} /*set_command*/

void del_command( argc, argv ) int argc; char * argv[];
{
    int ret;

    if ( 2 == argc )
    {
        ret = unlink( argv[ 1 ] );
        if ( 0 != ret )
            perror( "unable to delete file" );
    }
    else
        printf( "del expects 1 argument\n" );
} /*del_command*/

void copy_command( argc, argv ) int argc; char * argv[];
{
    int ret;
    int inf = -1, outf = -1;

    if ( 3 == argc )
    {
        if ( strchr( argv[1], '?' ) || strchr( argv[1], '*' ) || strchr( argv[2], '?' ) || strchr( argv[2], '*' ) )
            printf( "wildcard copies are not implemented\n" );
        else
        {
            /* use open() and related functions instead of fopen() just to test a different codepath */

            inf = open( argv[1], O_BINARY );
            if ( -1 == inf )
                printf( "unable to open source file\n" );
            else
            {
                outf = open( argv[2], O_BINARY | O_CREAT | O_RDWR, S_IREAD | S_IWRITE );
                if ( -1 == outf )
                    printf( "unable to open destination file\n" );
                else
                {
                    while ( 0 == eof( inf ) )
                    {
                        ret = read( inf, g_acbuffer, sizeof g_acbuffer );
                        if ( 0 == ret )
                            break;
                        write( outf, g_acbuffer, ret );
                    }
                }
            }
        }

        if ( -1 != inf )
            close( inf );
        if ( -1 != outf )
            close( outf );
    }
    else
        printf( "copy expects 2 arguments\n" );
} /*copy_command*/

void time_command( argc, argv ) int argc; char * argv[];
{
    g_regs_in.h.ah = 0x2c; /* get time */
    intdos( &g_regs_in, &g_regs_out );
    printf( "current time: %02d:%02d:%02d.%02d\n", g_regs_out.h.ch, g_regs_out.h.cl, g_regs_out.h.dh, g_regs_out.h.dl );
    fflush( stdout ); /* the c runtime batches output */
} /*time_command*/

void date_command( argc, argv ) int argc; char * argv[];
{
    g_regs_in.h.ah = 0x2a; /* get date */
    intdos( &g_regs_in, &g_regs_out );
    printf( "current date: %02d/%02d/%04d\n", g_regs_out.h.dh, g_regs_out.h.dl, g_regs_out.x.cx );
    fflush( stdout ); /* the c runtime batches output */
} /*date_command*/

void ver_command( argc, argv ) int argc; char * argv[];
{
    printf( "ntvdm command prompt v0.01 on DOS v%d.%d\n", _osmajor, _osminor );
    fflush( stdout ); /* the c runtime batches output */
} /*ver_command*/

void get_cursor_position( row, col ) unsigned char * row; unsigned char * col;
{
    g_regs_in.h.ah = 0x03; /* read cursor position */
    g_regs_in.h.bh = 0; /* first video page */
    int86( 0x10, &g_regs_in, &g_regs_out );
    *row = g_regs_out.h.dh;
    *col = g_regs_out.h.dl;
} /*get_cursor_position*/

void set_cursor_position( row, col ) unsigned char row; unsigned char col;
{
    g_regs_in.h.ah = 0x02; /* set cursor position */
    g_regs_in.h.bh = 0; /* first video page */
    g_regs_in.h.dh = row;
    g_regs_in.h.dl = col;
    int86( 0x10, &g_regs_in, &g_regs_out );
} /*set_cursor_position*/

void clear_screen()
{
    g_regs_in.h.ah = 6; /* scroll up */
    g_regs_in.h.al = 0;
    g_regs_in.h.ch = 0;
    g_regs_in.h.cl = 0;
    g_regs_in.h.dh = 24;
    g_regs_in.h.dl = 79;
    g_regs_in.h.bh = 7; /* light grey text */
    int86( 0x10, &g_regs_in, &g_regs_out );
} /*clear_screen*/

void cls_command( argc, argv ) int argc; char * argv[];
{
    clear_screen();
    set_cursor_position( 0, 0 );
} /*cls_command*/

void help_command( argc, argv ) int argc; char * argv[];
{
    printf( "ntvdm commands are a tiny subset of command.com\n" );
    printf( "    cd/chdir,    copy,    date,      del/erase,   dir\n" );
    printf( "    exit,        help,    md/mkdir,  rd/rmdir,    rem\n" );
    printf( "    ren/rename,  set,     time,      type,        mem\n" );
} /*help_command*/

int run_internal( cmd, argc, argv ) char * cmd; int argc; char * argv[];
{
    if ( !stricmp( "cd", cmd ) || !stricmp( "chdir", cmd ) )
    {
        cd_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "cls", cmd ) )
    {
        cls_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "copy", cmd ) )
    {
        copy_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "date", cmd ) )
    {
        date_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "del", cmd ) || !stricmp( "erase", cmd ) )
    {
        del_command( argc, argv );
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
    else if ( !stricmp( "help", cmd ) )
    {
        help_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "md", cmd ) || !stricmp( "mkdir", cmd ) )
    {
        md_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "rd", cmd ) || !stricmp( "rmdir", cmd ) )
    {
        rd_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "rem", cmd ) )
    {
        return 0;
    }
    else if ( !stricmp( "ren", cmd ) || !stricmp( "rename", cmd ) )
    {
        ren_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "set", cmd ) )
    {
        set_command( argc, argv );
        return 0;
    }
    else if ( !stricmp( "time", cmd ) )
    {
        time_command( argc, argv );
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
    else if ( !stricmp( "ver", cmd ) )
    {
        ver_command( argc, argv );
        return 0;
    }

    return INTERNAL_CMD_NOT_FOUND;
} /*run_internal*/

int file_or_directory_exists( name, isfile ) char * name; int isfile;
{
    g_regs_in.h.ah = 0x43; /* get/put file attributes */
    g_regs_in.h.al = 0; /* get */
    g_regs_in.x.dx = FP_OFF( name );
    segread( &g_segregs );
    g_segregs.ds = FP_SEG( name );
    intdosx( &g_regs_in, &g_regs_out, &g_segregs );

    if ( 0 == g_regs_out.x.cflag )
    {
        if ( isfile )
            return ( 0 == ( 0x10 & g_regs_out.x.cx ) );

        return ( 0 != ( 0x10 & g_regs_out.x.cx ) );
    }

    return false;
} /*file_or_directory_exists*/

int file_exists( name ) char * name;
{
    file_or_directory_exists( name, true );
} /*file_exists*/

int directory_exists( name ) char * name;
{
    file_or_directory_exists( name, false );
} /*directory_exists*/

void ensure_ends_in_backslash( path ) char * path;
{
    int len = strlen( path );
    if ( len > 0 && '\\' != path[ len - 1 ] )
    {
        path[ len ] = '\\';
        path[ len + 1 ] = 0;
    }
} /*ensure_ends_in_backslash*/

int file_exists_in_path( filename, fullpath ) char * filename; char * fullpath;
{
    char * path;
    char * semi;
    int len;

    if ( file_exists( filename ) )
    {
        strcpy( fullpath, filename );
        return true;
    }

    if ( strchr( filename, '\\' ) ) /* path already specified */
        return false;

    path = getenv( "PATH" );
    if ( !path )                    /* path environment variable exists? */
        return false;

    /* path points to the value after the equals sign */

    while ( *path )
    {
        semi = strchr( path, ';' );
        if ( semi == path )
        {
            path++;
            continue;
        }

        if ( semi )
        {
            len = ( semi - path );
            memcpy( fullpath, path, len );
            fullpath[ len ] = 0;
            path = semi + 1;
        }
        else
        {
            strcpy( fullpath, path );
            path += strlen( path );
        }

        ensure_ends_in_backslash( fullpath );
        strcat( fullpath, filename );

        if ( file_exists( fullpath ) )
            return true;
    }

    return false;
} /*file_exists_in_path*/

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

int ends_with( str, end ) char * str; char * end;
{
    int len = strlen( str );
    int lenend = strlen( end );

    if ( len < lenend )
        return false;

    return !stricmp( str + len - lenend, end );
} /*ends_with*/

int starts_with( str, start ) char * str; char * start;
{
    int len = strlen( str );
    int lenstart = strlen( start );
    int i;

    if ( len < lenstart )
        return false;

    for ( i = 0; i < lenstart; i++ )
        if ( toupper( str[ i ] ) != toupper( start[ i ] ) )
            return false;

    return true;
} /*starts_with*/

int run_batch( cmd, argc, argv ) char * cmd; int argc; char * argv[];
{
    static char acbatch[ MAX_CMD_LEN ];
    static char acgoto[ MAX_CMD_LEN ];
    int ret;
    FILE *fp;
    long file_len, bytes_read;
    char * strret, * pbatch, * pline, * pend, * pastlines, * pgoto;

    if ( 1 == argc )
    {
        /*
            read the whole file because a bug in the MS C 3.00 runtime causes the file's buffer to get
            trashed when subprocesses and other I/O are done while this file is open.
        */

        fp = fopen( cmd, "r" );
        if ( 0 != fp )
        {
            fseek( fp, (long) 0, 2 ); /* seek end */
            file_len = ftell( fp );
            fseek( fp, (long) 0, 0 ); /* rewind */
            pbatch = (char *) malloc( 1 + file_len );

            if ( pbatch )
            {
                /* fewer bytes will be read than the size of the file due to newline processing */
    
                bytes_read = fread( pbatch, 1, file_len, fp );
                pbatch[ bytes_read ] = 0;
                fclose( fp );
                pline = pbatch;
                pastlines = pbatch + bytes_read;

                /* put nulls at the end of each line instead of line feeds */
    
                while ( pline < pastlines && *pline )
                {
                    pend = strchr( pline, 0xa );
                    if ( 0 != pend )
                        *pend = 0;
                    pline += ( 1 + strlen( pline ) );
                }

                /* execute each line */
    
                pline = pbatch;
                while ( pline < pastlines )
                {
next_line:
                    if ( 0 != *pline )
                    {
                        strcpy( acbatch, pline );
                        rm_leading_and_trailing_space( acbatch );
                        printf( "%s\n", acbatch );

                        if ( ':' != acbatch[0] )
                        {
                            if ( starts_with( acbatch, "goto " ) )
                            {
                                pgoto = pbatch;
                                while ( pgoto < pastlines )
                                {
                                    if ( 0 != pgoto )
                                    {
                                        strcpy( acgoto, pgoto );
                                        rm_leading_and_trailing_space( acgoto );
                                        if ( !stricmp( acbatch + 5, acgoto  ) )
                                        {
                                            pline = pgoto;
                                            goto next_line;
                                        }
                                    }

                                    pgoto += ( 1 + strlen( pgoto ) );
                                }

                                printf( "undefined label %s\n", acbatch + 5 );
                            }
    
                            ret = parse_and_run( acbatch );
                            if ( EXIT_CMD == ret )
                                break;
                        }
                    }

                    pline += ( 1 + strlen( pline ) );
                }
    
                free( pbatch );
            }
            else
            {
                fclose( fp );
                printf( "out of memory processing batch file\n" );
            }
        }
        else
            printf( "can't open batch file '%s'\n", cmd );
    }
    else
        printf( "batch file arguments not supported\n" );

    return 0;
} /*run_batch*/

int run_external( cmd, argc, argv ) char * cmd; int argc; char * argv[];
{
    int ret = 0, spawn_errno;
    unsigned char cursor_row, cursor_column;
    char fullpath[ MAX_CMD_LEN ];
    strcpy( g_acbuffer, cmd );

    if ( ! ( ends_with( g_acbuffer, ".com" ) || ends_with( g_acbuffer, ".exe" ) || ends_with( g_acbuffer, ".bat" ) ) )
    {
        strcat( g_acbuffer, ".com" );
        if ( !file_exists_in_path( g_acbuffer, fullpath ) )
        {
            strcpy( g_acbuffer, cmd );
            strcat( g_acbuffer, ".exe" );
            if ( !file_exists_in_path( g_acbuffer, fullpath ) )
            {
                strcpy( g_acbuffer, cmd );
                strcat( g_acbuffer, ".bat" );
                if ( !file_exists_in_path( g_acbuffer, fullpath ) )
                    return EXTERNAL_CMD_NOT_FOUND;
            }
        }
    }
    else
    {
        if ( !file_exists_in_path( g_acbuffer, fullpath ) )
            return EXTERNAL_CMD_NOT_FOUND;
    }

    if ( stristr( fullpath, ".bat" ) )
        ret = run_batch( fullpath, argc, argv );
    else
    {
        printf( "\n" );
        fflush( stdout ); /* this ancient C runtime requires this or output is batched */

        get_cursor_position( &cursor_row, &cursor_column );
        ret = spawnv( P_WAIT, fullpath, argv );
        spawn_errno = errno;
        set_cursor_position( cursor_row, cursor_column );
        if ( -1 == ret )
            printf( "failure to start or execute external program '%s', return code %d errno %d\n", fullpath, ret, spawn_errno );
    }
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

int parse_and_run( cmdline ) char * cmdline;
{
    char * sub_argv[ MAX_ARGUMENTS ];
    int  sub_argc = 0;
    int ret = 0;

    rm_leading_and_trailing_space( cmdline );

    sub_argc = parse_arguments( cmdline, sub_argv );

    if ( 0 != sub_argc )
    {
        ret = run_internal( sub_argv[ 0 ], sub_argc, sub_argv );
    
        if ( INTERNAL_CMD_NOT_FOUND == ret )
            ret = run_external( sub_argv[ 0 ], sub_argc, sub_argv );

        if ( EXTERNAL_CMD_NOT_FOUND == ret )
            printf( "'%s' not recognized as an internal or external command\n", sub_argv[ 0 ] );
    }

    return ret;
} /*parse_and_run*/

int main( argc, argv ) int argc; char * argv[];
{
    static char cmdline[ MAX_CMD_LEN ];
    static char * sub_argv[ MAX_ARGUMENTS ];
    int  sub_argc = 0;
    int j, ret;

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

            if ( argc >= MAX_ARGUMENTS )
            {
                printf( "too many arguments; only %d are supported\n", MAX_ARGUMENTS );
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
            printf( "unrecognized command-line argument\n" );
            return -1;
        }
    }

    ver_command();

    do
    {
        show_prompt();
        gets( cmdline );
        ret = parse_and_run( cmdline );
        if ( EXIT_CMD == ret )
            break;

        printf( "\n" );
    } while ( 1 );

    printf( "\nexiting ntvdm command prompt\n" );
    return 0;
} /*main*/

