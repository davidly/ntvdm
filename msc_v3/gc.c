/*
    tests getting characters from int 0x16 function 0
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

/* use these registers for DOS interrupts */

union REGS g_regs_in;
union REGS g_regs_out;
struct SREGS g_segregs;

unsigned short gc()
{
    g_regs_in.h.ah = 0; /* wait for and get character */
    int86( 0x16, &g_regs_in, &g_regs_out );
    return g_regs_out.x.ax;
} /*gc*/

int main( argc, argv ) int argc; char * argv[];
{
    do
    {
        unsigned short ax = gc();
        unsigned short scancode = ax >> 8;
        unsigned short ascii = ax & 0xff;
        printf( "ax (sc/asc): %#04x, scancode %d, ascii %d == '%c'\n", ax, scancode, ascii, ascii );
        fflush( stdout );
        if ( 'z' == ( ax & 0xff ) )
            break;
    } while ( 1 );

    printf( "exiting\n" );
    return 0;
} /*main*/



