#pragma once

#ifdef __mc68000__
extern "C" int clock_gettime( clockid_t id, struct timespec * res );
#endif

static bool s_convert_redirected_LF_to_CR = false;

#ifdef WATCOM

#include <conio.h>
#include <graph.h>
#include <dos.h>

class ConsoleConfiguration
{
    private:
        bool outputEstablished;
        void (__interrupt __far * prev_int_23)();
        static void __interrupt __far controlc_routine() {}
        
    public:
        ConsoleConfiguration() : outputEstablished( false ), prev_int_23( 0 ) {}
        ~ConsoleConfiguration() {}

        static void ConvertRedirectedLFToCR( bool c ) { s_convert_redirected_LF_to_CR = c; }

        void EstablishConsoleOutput( int16_t width = 80, int16_t height = 24 )
        {
            if ( outputEstablished )
                return;

            outputEstablished = true;
            _setvideomode( 3 ); // 80x25 color text

            // hook the ^C interrupt so the default handler doesn't terminate the app

            prev_int_23 = _dos_getvect( 0x23 );
            _dos_setvect( 0x23, controlc_routine );
        } //EstablishConsoleOutput

        static int portable_kbhit() { return kbhit(); }
        static int throttled_kbhit() { return kbhit(); }
        static int portable_getch() { return getch(); }
        static char * portable_gets_s( char * buf, size_t bufsize ) { return gets( buf ); }

        void RestoreConsoleInput()
        {
            if ( 0 != prev_int_23 )
            {
                _dos_setvect( 0x23, prev_int_23 );
                prev_int_23 = 0;
            }
        } //RestoreConsoleInput

        void RestoreConsoleOutput( bool clearScreen = true )
        {
            outputEstablished = false;
        } //RestoreConsoleOutput

        void RestoreConsole( bool clearScreen = true )
        {
            RestoreConsoleInput();
            RestoreConsoleOutput( clearScreen );
        } //RestoreConsole

        bool IsOutputEstablished() { return outputEstablished; }
};

#else

#include <chrono>
using namespace std;
using namespace std::chrono;

class ConsoleConfiguration
{
    private:
        #ifdef _WIN32
            HANDLE consoleOutputHandle;
            HANDLE consoleInputHandle;
            WINDOWPLACEMENT oldWindowPlacement;
            CONSOLE_SCREEN_BUFFER_INFOEX oldScreenInfo;
            DWORD oldOutputConsoleMode, oldInputConsoleMode;
            CONSOLE_CURSOR_INFO oldCursorInfo;
            int16_t setWidth;
            UINT oldOutputCP;
            static const size_t longestEscapeSequence = 10; // probably overkill
            char aReady[ 1 + longestEscapeSequence ];
        #else
#if !defined( OLDGCC ) && !defined( __mc68000__ )
            struct termios orig_termios;
#endif
        #endif

        bool inputEstablished, outputEstablished;

#ifdef _WIN32
        // this function takes Windows keyboard input and produces Linux keyboard input
        bool process_key_event( INPUT_RECORD & rec, char * pout )
        {
            *pout = 0;

            if ( KEY_EVENT != rec.EventType )
                return false;
        
            if ( !rec.Event.KeyEvent.bKeyDown )
                return false;
        
            const uint8_t asc = rec.Event.KeyEvent.uChar.AsciiChar;
            const uint8_t sc = (uint8_t) rec.Event.KeyEvent.wVirtualScanCode;
        
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

            pout[ 0 ] = asc;
            pout[ 1 ] = 0;

            if ( falt && asc >= ' ' && asc <= 0x7f )
            {
                sprintf( pout, "\033%c", asc );
            }
            else if ( 0x01 == sc ) // ESC
            {
                // can't test on Windows if ( falt ) ;
            }
            else if ( 0x0e == sc ) // Backspace
            {
                if ( falt )
                {
                    pout[ 0 ] = 0x1b;
                    pout[ 1 ] = 0x7f;
                    pout[ 2 ] = 0;
                }
                else if ( fctrl )
                    pout[ 0 ] = 8;
                else
                    pout[ 0 ] = 0x7f; // shift and no shift
            }
            else if ( 0x0f == sc ) // Tab
            {
                // can't test on Windows if ( falt ) ;
                // can't test on Windows else if ( fctrl ) ;
                if ( fshift ) strcpy( pout, "\033[Z" );
            }
            else if ( 0x35 == sc ) // keypad /
            {
                if ( falt ) strcpy( pout, "\033/" );
                else if ( fctrl ) *pout = 31;
            }
            else if ( 0x37 == sc ) // keypad *
            {
                if ( falt ) strcpy( pout, "\033*" );
                else if ( fctrl ) *pout = 10;
            }
            else if ( sc >= 0x3b && sc <= 0x44 ) // F1 - F10
            {
                if ( falt )
                {
                    if ( sc >= 0x3b && sc <= 0x3e )
                        sprintf( pout, "\033[1;3%c", sc - 0x3b + 80 );
                    else if ( sc == 0x3f )
                        strcpy( pout, "\033[15;3~" );
                    else if ( sc >= 0x40 && sc <= 0x42 )
                        sprintf( pout, "\033[1%c;3~", 55 + sc - 0x40 );
                    else if ( sc >= 0x43 && sc <= 0x44 )
                        sprintf( pout, "\033[2%c;3~", 48 + sc - 0x43 );
                }
                else if ( fctrl )
                {
                    if ( sc >= 0x3b && sc <= 0x3e )
                        sprintf( pout, "\033[1;5%c", sc - 0x3b + 80 );
                    else if ( sc == 0x3f )
                        strcpy( pout, "\033[15;5~" );
                    else if ( sc >= 0x40 && sc <= 0x42 )
                        sprintf( pout, "\033[2%c;5~", 55 + sc - 0x40 );
                    else if ( sc >= 0x43 && sc <= 0x44 )
                        sprintf( pout, "\033[2%c;5~", 48 + sc - 0x43 );
                }
                else if ( fshift )
                {
                    if ( sc >= 0x3b && sc <= 0x3e )
                        sprintf( pout, "\033[1;2%c", sc - 0x3b + 80 );
                    else if ( sc == 0x3f )
                        strcpy( pout, "\033[15;2~" );
                    else if ( sc >= 0x40 && sc <= 0x42 )
                        sprintf( pout, "\033[1%c;2~", 55 + sc - 0x40 );
                    else if ( sc >= 0x43 && sc <= 0x44 )
                        sprintf( pout, "\033[2%c;2~", 48 + sc - 0x43 );
                }
                else
                {
                    if ( sc >= 0x3b && sc <= 0x3e )
                        sprintf( pout, "\033O%c", 80 + sc - 0x3b );
                    else if ( sc == 0x3f )
                        strcpy( pout, "\033[15~" );
                    else if ( sc >= 0x40 && sc <= 0x42 )
                        sprintf( pout, "\033[1%c~", 55 + sc - 0x40 );
                    else
                        sprintf( pout, "\033[2%c~", 48 + sc - 0x43 );
                }
            }
            else if ( 0x47 == sc ) // Home
            {
                if ( falt ) strcpy( pout, "\033[1;3H" );
                else if ( fctrl ) strcpy( pout, "\033[1;5H" );
                else if ( fshift ) strcpy( pout, "\033[1;2H" );
                else strcpy( pout, "\033[H" );
            }
            else if ( 0x48 == sc ) // up arrow
            {
                if ( falt ) strcpy( pout, "\033[1;3A" );
                else if ( fctrl ) strcpy( pout, "\033[1;5A" );
                else if ( fshift ) strcpy( pout, "\033[1;2A" );
                else strcpy( pout, "\033[A" );
            }
            else if ( 0x49 == sc ) // page up
            {
                if ( falt ) strcpy( pout, "\033[5;3~" );
                else if ( fctrl ) strcpy( pout, "\033[5;5~" );
                else if ( fshift ) strcpy( pout, "\033[5;2~" );
                else strcpy( pout, "\033[5~" );
            }
            else if ( 0x4a == sc ) // keypad -
            {
                if ( falt ) strcpy( pout, "\033-" );
                // can't test on Windows else if ( fctrl ) ;
            }
            else if ( 0x4b == sc ) // left arrow
            {
                if ( falt ) strcpy( pout, "\033[1;3D" );
                else if ( fctrl ) strcpy( pout, "\033[1;5D" );
                else if ( fshift ) strcpy( pout, "\033[1;2D" );
                else strcpy( pout, "\033[D" );
            }
            else if ( 0x4c == sc ) // keypad 5
            {
                // no output on Linux if ( fctrl ) ;
                // no output on Linux else if ( fshift ) ;
            }
            else if ( 0x4d == sc ) // right arrow
            {
                if ( falt ) strcpy( pout, "\033[1;3C" );
                else if ( fctrl ) strcpy( pout, "\033[1;5C" );
                else if ( fshift ) strcpy( pout, "\033[1;2C" );
                else strcpy( pout, "\033[C" );
            }
            else if ( 0x4e == sc ) // keypad +
            {
                if ( falt ) strcpy( pout, "\033+" );
                else if ( fshift ) *pout = 43;
                else if ( fctrl ) *pout = 43;
                else *pout = 43;
            }
            else if ( 0x4f == sc ) // End
            {
                if ( falt ) strcpy( pout, "\033[1;3F" );
                else if ( fctrl ) strcpy( pout, "\033[1;5F" );
                else if ( fshift ) strcpy( pout, "\033[1;2F" );
                else strcpy( pout, "\033[F" );
            }
            else if ( 0x50 == sc ) // down arrow
            {
                if ( falt ) strcpy( pout, "\033[1;3B" );
                else if ( fctrl ) strcpy( pout, "\033[1;5B" );
                else if ( fshift ) strcpy( pout, "\033[1;2B" );
                else strcpy( pout, "\033[B" );
            }
            else if ( 0x51 == sc ) // page down
            {
                if ( falt ) strcpy( pout, "\033[6;3~" );
                else if ( fctrl ) strcpy( pout, "\033[6;5~" );
                else if ( fshift ) strcpy( pout, "\033[6;2~" );
                else strcpy( pout, "\033[6~" );
            }
            else if ( 0x52 == sc ) // INS
            {
                if ( falt ) strcpy( pout, "\033[2;3~" );
                else if ( fctrl ) strcpy( pout, "\033[2;5~" );
                else if ( fshift ) ; // can't repro on windows
                else strcpy( pout, "\033[2~" );
            }
            else if ( 0x53 == sc ) // Del
            {
                if ( falt ) strcpy( pout, "\033[3;3~" );
                else if ( fctrl ) strcpy( pout, "\033[3;5~" );
                else if ( fshift ) strcpy( pout, "\033[3;2~" );
                else strcpy( pout, "\033[3~" );
            }
            else if ( 0x57 == sc || 0x58 == sc ) // F11 - F12
            {
                if ( falt ) sprintf( pout, "\033[2%c;3~", sc - 0x57 + 51 );
                else if ( fctrl ) sprintf( pout, "\033[2%c;5~", sc - 0x57 + 51 );
                else if ( fshift ) sprintf( pout, "\033[2%c;2~", sc - 0x57 + 51 );
                else sprintf( pout, "\033[2%c~", 51 + sc - 0x57 );
            }

            return true;
        } //process_key_event
#endif // _WIN32

    public:
        #ifdef _WIN32
            ConsoleConfiguration() : oldOutputConsoleMode( 0 ), oldInputConsoleMode( 0 ),
                                     setWidth( 0 ), oldOutputCP( 0 ),
                                     inputEstablished( false ), outputEstablished( false )
            {
                oldWindowPlacement = {0};
                oldScreenInfo = {0};
                oldCursorInfo = {0};

                consoleOutputHandle = GetStdHandle( STD_OUTPUT_HANDLE );
                consoleInputHandle = GetStdHandle( STD_INPUT_HANDLE );

                EstablishConsoleInput();

                memset( aReady, 0, sizeof( aReady ) );
            } //ConsoleConfiguration
        #else
            ConsoleConfiguration() : inputEstablished( false ), outputEstablished( false )
            {
                EstablishConsoleInput();
            } //ConsoleConfiguration
        #endif

        ~ConsoleConfiguration()
        {
            RestoreConsole();
        }

        static void ConvertRedirectedLFToCR( bool c ) { s_convert_redirected_LF_to_CR = c; }

        bool IsOutputEstablished() { return outputEstablished; }

        #ifdef _WIN32
            HANDLE GetOutputHandle() { return consoleOutputHandle; };
            HANDLE GetInputHandle() { return consoleInputHandle; };
        #endif

        void SetCursorInfo( uint32_t size ) // 0 to 100
        {
            #ifdef _WIN32
                if ( 0 != consoleOutputHandle )
                {
                    CONSOLE_CURSOR_INFO info = {0};
                    if ( 0 == size )
                    {
                        info.dwSize = 1;
                        info.bVisible = FALSE;
                    }
                    else
                    {
                        info.dwSize = size;
                        info.bVisible = TRUE;
                    }
        
                    SetConsoleCursorInfo( consoleOutputHandle, &info );
                }
             #else
                if ( outputEstablished )
                {
                    if ( 0 == size )
                        printf( "\x1b[?25l" );
                    else
                        printf( "\x1b[?25h" );
                    fflush( stdout );
                }
             #endif
        } //SetCursorInfo

        void EstablishConsoleInput( void * pCtrlCRoutine = 0 )
        {
            if ( !isatty( fileno( stdin ) ) )
                return;

            if ( inputEstablished )
                RestoreConsoleInput();

            #ifdef _WIN32
                GetConsoleMode( consoleOutputHandle, &oldInputConsoleMode );

                if ( 0 == pCtrlCRoutine )
                {
                    DWORD dwMode = oldInputConsoleMode;
                    dwMode &= ~ENABLE_PROCESSED_INPUT;
                    SetConsoleMode( consoleInputHandle, dwMode );
                    tracer.Trace( "old and new console input mode: %#x, %#x\n", oldInputConsoleMode, dwMode );
                }
                else
                {
                    // don't automatically have ^c terminate the app. ^break will still terminate the app
                 
                    PHANDLER_ROUTINE handler = (PHANDLER_ROUTINE) pCtrlCRoutine;
                    SetConsoleCtrlHandler( handler, TRUE );
                }
            #else
                #if !defined( OLDGCC ) && !defined( __mc68000__ ) // these will never run on actual Linux and the emulators or platform already are configured for raw keystrokes
                    tcgetattr( 0, &orig_termios );
    
                    // make input raw so it's possible to peek to see if a keystroke is available
    
                    struct termios new_termios;
                    memcpy( &new_termios, &orig_termios, sizeof( new_termios ) );
    
                    cfmakeraw( &new_termios );
                    new_termios.c_oflag = orig_termios.c_oflag;
                    tcsetattr( 0, TCSANOW, &new_termios );
                #endif
            #endif

            inputEstablished = true;
        } //EstablishConsoleInput

        void EstablishConsoleOutput( int16_t width = 80, int16_t height = 24 )
        {
            tracer.Trace( "  EstablishConsoleOutput width %u height %u, outputEstablished %d\n", width, height, outputEstablished );
            if ( outputEstablished )
                return;

            #ifdef _WIN32
                GetConsoleCursorInfo( consoleOutputHandle, &oldCursorInfo );
        
                if ( 0 != width )
                {
                    oldWindowPlacement.length = sizeof oldWindowPlacement;
                    GetWindowPlacement( GetConsoleWindow(), &oldWindowPlacement );

                    oldOutputCP = GetConsoleOutputCP();
                    SetConsoleOutputCP( 437 );
                
                    oldScreenInfo.cbSize = sizeof oldScreenInfo;
                    GetConsoleScreenBufferInfoEx( consoleOutputHandle, &oldScreenInfo );
                
                    CONSOLE_SCREEN_BUFFER_INFOEX newInfo;
                    memcpy( &newInfo, &oldScreenInfo, sizeof newInfo );

                    setWidth = width;
                    newInfo.dwSize.X = width;
                    newInfo.dwSize.Y = height;
                    newInfo.dwMaximumWindowSize.X = width;
                    newInfo.dwMaximumWindowSize.Y = height;

                    newInfo.wAttributes = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN;

                    // legacy DOS RGB values
                    newInfo.ColorTable[ 0 ] = 0;
                    newInfo.ColorTable[ 1 ] = 0x800000;
                    newInfo.ColorTable[ 2 ] = 0x008000;
                    newInfo.ColorTable[ 3 ] = 0x808000;
                    newInfo.ColorTable[ 4 ] = 0x000080;
                    newInfo.ColorTable[ 5 ] = 0x800080;
                    newInfo.ColorTable[ 6 ] = 0x008080;
                    newInfo.ColorTable[ 7 ] = 0xc0c0c0;
                    newInfo.ColorTable[ 8 ] = 0x808080;
                    newInfo.ColorTable[ 9 ] = 0xff0000;
                    newInfo.ColorTable[ 10 ] = 0x00ff00;
                    newInfo.ColorTable[ 11 ] = 0xffff00;
                    newInfo.ColorTable[ 12 ] = 0x0000ff;
                    newInfo.ColorTable[ 13 ] = 0xff00ff;
                    newInfo.ColorTable[ 14 ] = 0x00ffff;
                    newInfo.ColorTable[ 15 ] = 0xffffff;
                    SetConsoleScreenBufferInfoEx( consoleOutputHandle, &newInfo );

                    COORD newSize = { width, height };
                    SetConsoleScreenBufferSize( consoleOutputHandle, newSize );
                }
            
                DWORD dwMode = 0;
                GetConsoleMode( consoleOutputHandle, &dwMode );
                oldOutputConsoleMode = dwMode;
                dwMode |= ( ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WINDOW_INPUT );
                tracer.Trace( "old and new console output mode: %04x, %04x\n", oldOutputConsoleMode, dwMode );
                SetConsoleMode( consoleOutputHandle, dwMode );
            #else
                #ifndef __mc68000__
                    if ( isatty( fileno( stdout ) ) )
                    {
                        printf( "%c[1 q", 27 ); // 1 == cursor blinking block. 
                        fflush( stdout );
                    }
                #endif
            #endif

                outputEstablished = true;

            if ( 0 != width )
                SendClsSequence();
        } //EstablishConsoleOutput

        void RestoreConsoleInput()
        {
            if ( inputEstablished )
            {
                #ifdef _WIN32
                    SetConsoleMode( consoleInputHandle, oldInputConsoleMode );
                #else
                    #if !defined( OLDGCC ) && !defined( __mc68000__ )
                        tcsetattr( 0, TCSANOW, &orig_termios );
                    #endif
                #endif

                inputEstablished = false;
            }
        } //RestoreConsoleInput
        
        void RestoreConsoleOutput( bool clearScreen = true )
        {
            if ( outputEstablished )
            {
                #if !defined( _WIN32 ) && !defined( __mc68000__ )
                    if ( isatty( fileno( stdout ) ) )
                    {
                        printf( "%c[0m", 27 ); // turn off display attributes
                        fflush( stdout );
                    }
                #endif

                if ( clearScreen )
                    SendClsSequence();

                #ifdef _WIN32
                    SetConsoleOutputCP( oldOutputCP );
                    SetConsoleCursorInfo( consoleOutputHandle, & oldCursorInfo );
    
                    if ( 0 != setWidth )
                    {
                        SetConsoleScreenBufferInfoEx( consoleOutputHandle, & oldScreenInfo );
                        SetWindowPlacement( GetConsoleWindow(), & oldWindowPlacement );
                    }
    
                    SetConsoleMode( consoleOutputHandle, oldOutputConsoleMode );
                #endif

                outputEstablished = false;
            }
        } //RestoreConsoleOutput

        void RestoreConsole( bool clearScreen = true )
        {
            RestoreConsoleInput();
            RestoreConsoleOutput( clearScreen );
        } //RestoreConsole

        void SendClsSequence()
        {
            if ( isatty( fileno( stdout ) ) )
            {
                #if !defined( __mc68000__ )
                    printf( "\x1b[2J" ); // clear the screen
                    printf( "\x1b[1G" ); // cursor to top line
                    printf( "\x1b[1d" ); // cursor to left side
                    fflush( stdout );
                #endif
            }
        } //SendClsSequence

        void ClearScreen()
        {
            #ifdef _WIN32
                if ( 0 != consoleOutputHandle )
                    SendClsSequence();
                else
                {
                    HANDLE hcon = GetStdHandle( STD_OUTPUT_HANDLE );
                    DWORD dwMode = 0;
                    GetConsoleMode( hcon, &dwMode );
                    DWORD oldMode = dwMode;
                    dwMode |= ( ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WINDOW_INPUT );
                    SetConsoleMode( hcon, dwMode );
                    SendClsSequence();
                    SetConsoleMode( hcon, oldMode );
                }
            #else
                SendClsSequence();
            #endif
        } //ClearScreen

        int portable_kbhit()
        {
            if ( !isatty( fileno( stdin ) ) )
                return ( 0 == feof( stdin) );

            #ifdef _WIN32
                if ( 0 != aReady[ 0 ] )
                    return true;
                return _kbhit();
            #else
                fd_set set;
                FD_ZERO( &set );
                FD_SET( STDIN_FILENO, &set );
                struct timeval timeout = {0};
                return ( select( 1, &set, NULL, NULL, &timeout ) > 0 );
            #endif
        } //portable_kbhit

        static int redirected_getch()
        {
            assert( !isatty( fileno( stdin ) ) );
            static bool look_ahead_available = false;
            static char look_ahead = 0;

            if ( look_ahead_available )
            {
                look_ahead_available = false;
                return look_ahead;
            }

            char data;
            if ( 1 == read( 0, &data, 1 ) )
            {
                // for files with CR/LF, skip the CR and turn the LF into a CR
                // for files with LF, turn the LF into a CR

#ifndef _WIN32
                if ( ( 13 == data ) && ( !feof( stdin ) ) )
                {
                    if ( 0 == read( 0, &look_ahead, 1 ) ) // make gcc not complain by checking return code
                        look_ahead = 13;

                    if ( 10 == look_ahead )
                        data = 10;
                    else
                        look_ahead_available = true;
                }
#endif

                if ( s_convert_redirected_LF_to_CR && ( 10 == data ) )
                    data = 13;

                return data;
            }
            return EOF;
        } //redirected_getch()

#ifdef _WIN32
        // behave like getch() on linux -- extended characters have escape sequences

        int linux_getch()
        {
            if ( !isatty( fileno( stdin ) ) )
                return redirected_getch();

            size_t cReady = strlen( aReady );
            if ( 0 != cReady )
            {
                int result = aReady[ 0 ];
                memmove( & aReady[ 0 ], & aReady[ 1 ], cReady ); // may just be the null termination
                return result;
            }

            HANDLE hConsoleInput = GetStdHandle( STD_INPUT_HANDLE );

            do
            {
                DWORD available = 0;
                BOOL ok = GetNumberOfConsoleInputEvents( hConsoleInput, &available );
                if ( ok && ( 0 != available ) )
                {
                    INPUT_RECORD records[ 1 ];
                    DWORD numRead = 0;
                    ok = ReadConsoleInput( hConsoleInput, records, 1, &numRead );
                    if ( ok )
                    {
                        for ( DWORD x = 0; x < numRead; x++ )
                        {
                            char acSequence[ longestEscapeSequence ] = {0};
                            bool used = process_key_event( records[ x ], acSequence );
                            if ( !used )
                                continue;
                    
                            strcat( aReady, acSequence );
                            tracer.Trace( "    consumed sequence of length %zd, total cached len %zd\n", strlen( acSequence ), strlen( aReady ) );
                        }
                    }
                }

                if ( 0 != aReady[ 0 ] )
                    return linux_getch();
            } while( true );

            tracer.Trace( "  bug in linux_getch; returning nothing\n" );
            assert( false );
            return 0;
        } //linux_getch
#endif // _WIN32

        static int portable_getch()
        {
            if ( !isatty( fileno( stdin ) ) )
                return redirected_getch();

            #ifdef _WIN32
                return _getch();
            #else
                int r;
                unsigned char c;

                do
                {
                    if ( ( r = read( 0, &c, sizeof( c ) ) ) < 0 )
                        return r;
                    if ( 0 != r )  // Linux platforms I tested wait for a char to be available. MacOS returns 0 immediately.
                        break;
                    //tracer.Trace( "  sleeping in portable_getch()\n" );
                    sleep_ms( 1 );
                } while( true );

                return c;
            #endif
        } //portable_getch

        bool throttled_kbhit()
        {
            // _kbhit() does device I/O in Windows, which sleeps for a tiny amount waiting for a reply, so 
            // compute-bound mbasic.com apps run 10x slower than they should because mbasic polls for keyboard input.
            // Workaround: only call _kbhit() if 50 milliseconds has gone by since the last call.

#ifdef __mc68000__ // newlib for embedded only has second-level granularity for high_resolution_clock, so use a different codepath for that
            static struct timespec last_call;
            static int static_result = clock_gettime( CLOCK_REALTIME, &last_call );
            struct timespec tNow;
            int result = clock_gettime( CLOCK_REALTIME, &tNow );
            uint32_t difference = (uint32_t) ( ( ( tNow.tv_sec - last_call.tv_sec ) * 1000 ) + ( ( tNow.tv_nsec - last_call.tv_nsec ) / 1000000 ) );

#else
            static high_resolution_clock::time_point last_call = high_resolution_clock::now();
            high_resolution_clock::time_point tNow = high_resolution_clock::now();
            long long difference = duration_cast<std::chrono::milliseconds>( tNow - last_call ).count();
#endif

            if ( difference > 50 )
            {
                last_call = tNow;
                return portable_kbhit();
            }

            return false;
        } //throttled_kbhit

        static char * portable_gets_s( char * buf, size_t bufsize )
        {
            size_t len = 0;
            do
            {
                char ch = (char) portable_getch();
                if ( '\n' == ch || '\r' == ch )
                {
                    printf( "\n" );
                    fflush( stdout ); // fflush is required on linux or it'll be buffered not seen until the app ends.
                    break;
                }
    
                if ( len >= ( bufsize - 1 ) )                
                    break;
    
                if ( 0x7f == ch || 8 == ch ) // backspace (it's not 8 for some reason)
                {
                    if ( len > 0 )
                    {
                        printf( "\x8 \x8" );
                        fflush( stdout );
                        len--;
                    }
                }
                else
                {
                    printf( "%c", ch );
                    fflush( stdout );
                    buf[ len++ ] = ch;
                }
            } while( true );
    
            buf[ len ] = 0;
            return buf;
        } //portable_gets_s
}; //ConsoleConfiguration

#endif // WATCOM
