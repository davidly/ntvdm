/*
   This version builds with Microsoft C Compiler V3.00.
   The syntax is old and reminds me of 7th grade summer vacation.
*/

#define LINT_ARGS
#include <stdio.h>
#include <time.h>
#include <dos.h>

#define true 1
#define false 0
#define bool int

#define ABPrune true
#define WinLosePrune true
#define SCORE_WIN 6
#define SCORE_TIE 5
#define SCORE_LOSE  4
#define SCORE_MAX 9
#define SCORE_MIN 2
#define Iterations 10

#define PieceX 1
#define PieceO 2
#define PieceBlank 0

char g_board[ 9 ];

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

typedef char pfunc_t( void );

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

long g_Moves = 0;

int MinMax( alpha, beta, depth, move ) int alpha; int beta; int depth; int move;
{
    int value;
    char pieceMove;
    int p, score;
    pfunc_t * pf;

    g_Moves++;

    if ( depth >= 4 )
    {
#if 0
        /* 1 iteration takes 3,825 ms with LookForWinner on a 4.77Mhz 8088 */
        p = LookForWinner();
#else
        /* ...compared to 3,242 ms with function pointers */
        pf = winner_functions[ move ];
        p = (*pf)();
#endif

        if ( PieceBlank != p )
        {
            if ( PieceX == p )
                return SCORE_WIN;

            return SCORE_LOSE;
        }

        if ( 8 == depth )
            return SCORE_TIE;
    }

    if ( depth & 1 ) 
    {
        value = SCORE_MIN;
        pieceMove = PieceX;
    }
    else
    {
        value = SCORE_MAX;
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
                if ( WinLosePrune && SCORE_WIN == score )
                    return SCORE_WIN;

                if ( score > value )
                    value = score;

                if ( ABPrune )
                {
                    if ( value > alpha )
                        alpha = value;

                    if ( alpha >= beta )
                        return value;
                }
            }
            else
            {
                if ( WinLosePrune && SCORE_LOSE == score )
                    return SCORE_LOSE;

                if ( score < value )
                    value = score;

                if ( ABPrune )
                {
                    if ( value < beta )
                        beta = value;

                    if ( beta <= alpha )
                        return value;
                }
            }
        }
    }

    return value;
}  /*MinMax*/

int FindSolution( position ) int position;
{
    int times;

    memset( g_board, 0, sizeof g_board );
    g_board[ position ] = PieceX;

    for ( times = 0; times < Iterations; times++ )
        MinMax( SCORE_MIN, SCORE_MAX, 0, position );

    return 0;
} /*FindSolution*/

void print_time_now()
{
    union REGS wrIn, wrOut;
    wrIn.h.ah = 0x2c;
    intdos( &wrIn, &wrOut );
    printf( "current time: %02d:%02d:%02d.%02d\n", wrOut.h.ch, wrOut.h.cl, wrOut.h.dh, wrOut.h.dl );
    fflush( stdout ); /* the c runtime batches output */
} /*print_time_now*/

int main( argc, argv ) int argc; char * argv[];
{
    print_time_now();

    FindSolution( 0 );
    FindSolution( 1 );
    FindSolution( 4 );

    printf( "move count: %ld\n", g_Moves );
    printf( "for %d iterations\n", Iterations );

    print_time_now();
    return 0;
} /*main*/


