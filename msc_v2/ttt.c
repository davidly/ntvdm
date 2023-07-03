/*
   This version builds with old compilers including:
       Aztec C 1.06 for 8080 & Z80 on CP/M.
       Microsoft C Compiler V1.04 for 8086 on DOS. (This is Lattice C)
       Microsoft C Compiler V2.03 for 8086 on DOS. (Still Lattice C)
       Microsoft C Compiler V3.00 for 8086 on DOS.
       QuickC 1.0
       Turbo C 2.0
   The syntax is old and reminds me of 7th grade summer vacation.
   unsigned long isn't supported in many older compilers, so long is used instead.
*/

#define LINT_ARGS

#include <stdio.h>

#ifdef DOSTIME
#include <time.h>
#include <dos.h>
#endif

#define true 1
#define false 0

#define ABPrune true         /* alpha beta pruning */
#define WinLosePrune true    /* stop early on win/lose */
#define WinFunPointers true  /* use function pointers for each move position */
#define ScoreWin 6
#define ScoreTie 5
#define ScoreLose  4
#define ScoreMax 9
#define ScoreMin 2
#define DefaultIterations 100

#define PieceX 1
#define PieceO 2
#define PieceBlank 0

#ifdef CPMTIME
typedef char ttype;  /* 8-bit cpus are about 8% faster with an 8-bit ttype */
#else
typedef int ttype;   /* 16-bit cpus are faster with a 16-bit type */
#endif

int g_Iterations = DefaultIterations;

char g_board[ 9 ];

#if WinFunPointers

char pos0func()
{
    char x = g_board[0];
    
    if ( ( x == g_board[1] && x == g_board[2] ) ||
         ( x == g_board[3] && x == g_board[6] ) ||
         ( x == g_board[4] && x == g_board[8] ) )
        return x;
    return PieceBlank;
}

char pos1func()
{
    char x = g_board[1];
    
    if ( ( x == g_board[0] && x == g_board[2] ) ||
         ( x == g_board[4] && x == g_board[7] ) )
        return x;
    return PieceBlank;
} 

char pos2func()
{
    char x = g_board[2];
    
    if ( ( x == g_board[0] && x == g_board[1] ) ||
         ( x == g_board[5] && x == g_board[8] ) ||
         ( x == g_board[4] && x == g_board[6] ) )
        return x;
    return PieceBlank;
} 

char pos3func()
{
    char x = g_board[3];
    
    if ( ( x == g_board[4] && x == g_board[5] ) ||
         ( x == g_board[0] && x == g_board[6] ) )
        return x;
    return PieceBlank;
} 

char pos4func()
{
    char x = g_board[4];
    
    if ( ( x == g_board[0] && x == g_board[8] ) ||
         ( x == g_board[2] && x == g_board[6] ) ||
         ( x == g_board[1] && x == g_board[7] ) ||
         ( x == g_board[3] && x == g_board[5] ) )
        return x;
    return PieceBlank;
} 

char pos5func()
{
    char x = g_board[5];
    
    if ( ( x == g_board[3] && x == g_board[4] ) ||
         ( x == g_board[2] && x == g_board[8] ) )
        return x;
    return PieceBlank;
} 

char pos6func()
{
    char x = g_board[6];
    
    if ( ( x == g_board[7] && x == g_board[8] ) ||
         ( x == g_board[0] && x == g_board[3] ) ||
         ( x == g_board[4] && x == g_board[2] ) )
        return x;
    return PieceBlank;
} 

char pos7func()
{
    char x = g_board[7];
    
    if ( ( x == g_board[6] && x == g_board[8] ) ||
         ( x == g_board[1] && x == g_board[4] ) )
        return x;
    return PieceBlank;
} 

char pos8func()
{
    char x = g_board[8];
    
    if ( ( x == g_board[6] && x == g_board[7] ) ||
         ( x == g_board[2] && x == g_board[5] ) ||
         ( x == g_board[0] && x == g_board[4] ) )
        return x;
    return PieceBlank;
} 

typedef char pfunc_t();

pfunc_t * winner_functions[9] =
{
    pos0func,
    pos1func,
    pos2func,
    pos3func,
    pos4func,
    pos5func,
    pos6func,
    pos7func,
    pos8func,
};

#else /* WinFunPointers */

char LookForWinner()
{
    char p = g_board[0];
    if ( PieceBlank != p )
    {
        if ( p == g_board[1] && p == g_board[2] )
            return p;

        if ( p == g_board[3] && p == g_board[6] )
            return p;
    }

    p = g_board[3];
    if ( PieceBlank != p && p == g_board[4] && p == g_board[5] )
        return p;

    p = g_board[6];
    if ( PieceBlank != p && p == g_board[7] && p == g_board[8] )
        return p;

    p = g_board[1];
    if ( PieceBlank != p && p == g_board[4] && p == g_board[7] )
        return p;

    p = g_board[2];
    if ( PieceBlank != p && p == g_board[5] && p == g_board[8] )
        return p;

    p = g_board[4];
    if ( PieceBlank != p )
    {
        if ( ( p == g_board[0] ) && ( p == g_board[8] ) )
            return p;

        if ( ( p == g_board[2] ) && ( p == g_board[6] ) )
            return p;
    }

    return PieceBlank;
} /*LookForWinner*/

#endif /* WinFunPointers */

/* older compilers don't support unsigned long */

long g_Moves = 0;

int MinMax( alpha, beta, depth, move ) ttype alpha; ttype beta; ttype depth; ttype move;
{
    ttype value, p, score;
    char pieceMove;
#if WinFunPointers
    pfunc_t * pf;
#endif

    g_Moves++;

    if ( depth >= 4 )
    {
#if WinFunPointers /* function pointers are faster on all platforms by 10-20% */
        pf = winner_functions[ move ];
        p = (*pf)();
#else
        p = LookForWinner();
#endif

        if ( PieceBlank != p )
        {
            if ( PieceX == p )
                return ScoreWin;

            return ScoreLose;
        }

        if ( 8 == depth )
            return ScoreTie;
    }

    if ( depth & 1 ) 
    {
        value = ScoreMin;
        pieceMove = PieceX;
    }
    else
    {
        value = ScoreMax;
        pieceMove = PieceO;
    }

    for ( p = 0; p < 9; p++ )
    {
        if ( PieceBlank == g_board[ p ] )
        {
            g_board[p] = pieceMove;
            score = MinMax( alpha, beta, depth + 1, p );
            g_board[p] = PieceBlank;

            if ( depth & 1 ) 
            {
#if WinLosePrune   /* #if statements must be in first column for MS C 1.0 */
                if ( ScoreWin == score )
                    return ScoreWin;
#endif

                if ( score > value )
                {
                    value = score;

#if ABPrune
                    if ( value >= beta )
                        return value;
                    if ( value > alpha )
                        alpha = value;
#endif
                }
            }
            else
            {
#if WinLosePrune
                if ( ScoreLose == score )
                    return ScoreLose;
#endif

                if ( score < value )
                {
                    value = score;

#if ABPrune
                    if ( value <= alpha )
                        return value;
                    if ( value < beta )
                        beta = value;
#endif
                }
            }
        }
    }

    return value;
}  /*MinMax*/

int FindSolution( position ) ttype position;
{
    ttype i;

    for ( i = 0; i < 9; i++ )
        g_board[ i ] = PieceBlank;

    g_board[ position ] = PieceX;

    for ( i = 0; i < g_Iterations; i++ )
        MinMax( ScoreMin, ScoreMax, 0, position );

    return 0;
} /*FindSolution*/

#ifdef CPMTIME

struct CPMTimeValue
{
    int h, m, s, l;
};

void print_time_now()
{
    /* This CP/M BDOS call of 105 is only implemented in NTVCM -- it's not a standard CP/M 2.2 call */

    struct CPMTimeValue t;
    t.h = t.m = t.s = t.l = 0;

    bdos( 105, &t );
    printf( "current time: %02d:%02d:%02d.%02d\n", t.h, t.m, t.s, t.l );
} /*print_time_now*/

long get_ms()
{
    /* This CP/M BDOS call of 105 is only implemented in NTVCM -- it's not a standard CP/M 2.2 call */

    long h, m, s, l;
    struct CPMTimeValue t;
    t.h = t.m = t.s = t.l = 0;

    bdos( 105, &t );
    h = t.h;
    m = t.m;
    s = t.s;
    l = t.l;

    return h * 3600000 + m * 60000 + s * 1000 + l * 10;
} /*get_ms*/

#else /* no elif on old compilers */

#ifdef DOSTIME

void print_time_now()
{
    /* Make a DOS interrupt call to get the time */

    union REGS wrIn, wrOut;

    wrIn.h.ah = 0x2c;
    intdos( &wrIn, &wrOut );
    printf( "current time: %02d:%02d:%02d.%02d\n", wrOut.h.ch, wrOut.h.cl, wrOut.h.dh, wrOut.h.dl );
    fflush( stdout );
} /*print_time_now*/

long get_ms()
{
    /* this function takes about 3 milliseconds on the original IBM PC */

    long h, m, s, l;
    union REGS wrIn, wrOut;

    wrIn.h.ah = 0x2c;
    intdos( &wrIn, &wrOut );

    h = wrOut.h.ch;
    m = wrOut.h.cl;
    s = wrOut.h.dh;
    l = wrOut.h.dl;

    return h * 3600000 + m * 60000 + s * 1000 + l * 10;
} /*get_ms*/

#else

int print_time_now() { return 0; }
long get_ms() { return 0; }

#endif
#endif

int main( argc, argv ) int argc; char * argv[];
{
    long start_time, end_time;

    if ( 2 == argc )
        sscanf( argv[ 1 ], "%d", &g_Iterations );  /* no atoi in MS C 1.0 */

    start_time = get_ms();

    FindSolution( 0 );
    FindSolution( 1 );
    FindSolution( 4 );

    end_time = get_ms();
    printf( "runtime in ms:   %ld\n", end_time - start_time );

    printf( "move count:      %ld\n", g_Moves );        /* 6493 * g_Iterations */
    printf( "iteration count: %d\n", g_Iterations );
    return 0;
} /*main*/

