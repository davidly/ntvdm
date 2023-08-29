#pragma once

#include <chrono>
using namespace std;
using namespace std::chrono;

class ConsoleConfiguration
{
    private:
        #ifdef _MSC_VER
            HANDLE consoleOutputHandle;
            HANDLE consoleInputHandle;
            WINDOWPLACEMENT oldWindowPlacement;
            CONSOLE_SCREEN_BUFFER_INFOEX oldScreenInfo;
            DWORD oldOutputConsoleMode, oldInputConsoleMode;
            CONSOLE_CURSOR_INFO oldCursorInfo;
            int16_t setWidth;
            UINT oldOutputCP;
        #else
#ifndef OLDGCC      // the several-years-old Gnu C compiler for the RISC-V development boards
            struct termios orig_termios;
#endif
        #endif

        bool inputEstablished, outputEstablished;

    public:
        #ifdef _MSC_VER
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

        bool IsOutputEstablished() { return outputEstablished; }

        #ifdef _MSC_VER
            HANDLE GetOutputHandle() { return consoleOutputHandle; };
            HANDLE GetInputHandle() { return consoleInputHandle; };
        #endif

        void SetCursorInfo( uint32_t size ) // 0 to 100
        {
            #ifdef _MSC_VER
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
             #endif
        } //SetCursorInfo

        void EstablishConsoleInput( void * pCtrlCRoutine = 0 )
        {
            if ( inputEstablished )
                RestoreConsoleInput();

            #ifdef _MSC_VER
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
                #ifndef OLDGCC // the several-years-old Gnu C compiler for the RISC-V development boards. that machine has no keyboard support
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
            if ( outputEstablished )
                return;

            #ifdef _MSC_VER
    
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

            #endif

                outputEstablished = true;

            if ( 0 != width )
                SendClsSequence();
        } //EstablishConsoleOutput

        void RestoreConsoleInput()
        {
            if ( inputEstablished )
            {
                #ifdef _MSC_VER
                    SetConsoleMode( consoleInputHandle, oldInputConsoleMode );
                #else
                    #ifndef OLDGCC      // the several-years-old Gnu C compiler for the RISC-V development boards
                        tcsetattr( 0, TCSANOW, &orig_termios );
                    #endif
                #endif

                inputEstablished = false;
            }
        } //RestoreConsoleInput
        
        void RestoreConsole( bool clearScreen = true )
        {
            RestoreConsoleInput();

            if ( outputEstablished )
            {
                if ( clearScreen )
                    SendClsSequence();

                #ifdef _MSC_VER
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
        } //RestoreConsole

        void SendClsSequence()
        {
            printf( "\x1b[2J" ); // clear the screen
            printf( "\x1b[1G" ); // cursor to top line
            printf( "\x1b[1d" ); // cursor to left side
        } //SendClsSequence

        void ClearScreen()
        {
            #ifdef _MSC_VER
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

        static int portable_kbhit()
        {
            int result = 0;

            #ifdef _MSC_VER
                result = _kbhit();
            #else
                fd_set set;
                FD_ZERO( &set );
                FD_SET( STDIN_FILENO, &set );
                struct timeval timeout = {0};
                result = ( select( 1, &set, NULL, NULL, &timeout ) > 0 );
            #endif

            return result;
        } //portable_kbhit

        static int portable_getch()
        {
            #ifdef _MSC_VER
                return _getch();
            #else
                int r;
                unsigned char c;

                if ( ( r = read( 0, &c, sizeof( c ) ) ) < 0 )
                    return r;

                return c;
            #endif
        } //portable_getch

        static bool throttled_kbhit()
        {
            // _kbhit() does device I/O in Windows, which sleeps for a tiny amount waiting for a reply, so 
            // compute-bound mbasic.com apps run 10x slower than they should because mbasic polls for keyboard input.
            // Workaround: only call _kbhit() if 50 milliseconds has gone by since the last call.

            static high_resolution_clock::time_point last_call = high_resolution_clock::now();
            high_resolution_clock::time_point tNow = high_resolution_clock::now();
            long long difference = duration_cast<std::chrono::milliseconds>( tNow - last_call ).count();

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

