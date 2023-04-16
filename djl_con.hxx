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
            DWORD oldConsoleMode;
            CONSOLE_CURSOR_INFO oldCursorInfo;
        #else
            bool initialized;
            bool established;
            struct termios orig_termios;
        #endif

    public:
        #ifdef _MSC_VER
            ConsoleConfiguration() : consoleOutputHandle( 0 ), consoleInputHandle( 0 ), oldConsoleMode( 0 )
            {
                    oldWindowPlacement = {0};
                    oldScreenInfo = {0};
                    oldCursorInfo = {0};
            } //ConsoleConfiguration
        #else
            ConsoleConfiguration() : initialized( false ), established( false )
            {
                tcgetattr( 0, &orig_termios );
                struct termios new_termios;
                memcpy( &new_termios, &orig_termios, sizeof( new_termios ) );

                // make input raw so it's possible to peek to see if a keystroke is available

                cfmakeraw( &new_termios );
                new_termios.c_oflag = orig_termios.c_oflag;
                tcsetattr( 0, TCSANOW, &new_termios );

                initialized = true;
            } //ConsoleConfiguration
        #endif

        ~ConsoleConfiguration()
        {
            RestoreConsole();
        }

        #ifdef _MSC_VER
            bool IsEstablished() { return ( 0 != consoleOutputHandle ); }
            HANDLE GetOutputHandle() { return consoleOutputHandle; };
            HANDLE GetInputHandle() { return consoleInputHandle; };
        #else
            bool IsEstablished() { return established; }
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

        void EstablishConsole( int16_t width = 80, int16_t height = 24, void * proutine = 0 )
        {
            #ifdef _MSC_VER
                if ( 0 != consoleOutputHandle )
                    return;
    
                PHANDLER_ROUTINE handler = (PHANDLER_ROUTINE) proutine;
            
                oldWindowPlacement.length = sizeof oldWindowPlacement;
                GetWindowPlacement( GetConsoleWindow(), &oldWindowPlacement );
            
                consoleOutputHandle = GetStdHandle( STD_OUTPUT_HANDLE );
                consoleInputHandle = GetStdHandle( STD_INPUT_HANDLE );
    
                GetConsoleCursorInfo( consoleOutputHandle, &oldCursorInfo );
            
                oldScreenInfo.cbSize = sizeof oldScreenInfo;
                GetConsoleScreenBufferInfoEx( consoleOutputHandle, &oldScreenInfo );
            
                CONSOLE_SCREEN_BUFFER_INFOEX newInfo;
                memcpy( &newInfo, &oldScreenInfo, sizeof newInfo );
            
                newInfo.dwSize.X = width;
                newInfo.dwSize.Y = height;
                newInfo.dwMaximumWindowSize.X = width;
                newInfo.dwMaximumWindowSize.Y = height;
                SetConsoleScreenBufferInfoEx( consoleOutputHandle, &newInfo );
            
                COORD newSize = { width, height };
                SetConsoleScreenBufferSize( consoleOutputHandle, newSize );
            
                DWORD dwMode = 0;
                GetConsoleMode( consoleOutputHandle, &dwMode );
                oldConsoleMode = dwMode;
                dwMode |= ( ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WINDOW_INPUT );
                tracer.Trace( "old console mode: %04x, new mode: %04x\n", oldConsoleMode, dwMode );
                SetConsoleMode( consoleOutputHandle, dwMode );
                                                       
                // don't automatically have ^c terminate the app. ^break will still terminate the app
                SetConsoleCtrlHandler( handler, TRUE );
    
                SendClsSequence();
            #endif
        } //EstablishConsole
        
        void RestoreConsole( bool clearScreen = true )
        {
            #ifdef _MSC_VER
                if ( 0 != consoleOutputHandle )
                {
                    if ( clearScreen )
                        SendClsSequence();
                    SetConsoleScreenBufferInfoEx( consoleOutputHandle, & oldScreenInfo );
                    SetWindowPlacement( GetConsoleWindow(), & oldWindowPlacement );
                    SetConsoleMode( consoleOutputHandle, oldConsoleMode );
                    SetConsoleCursorInfo( consoleOutputHandle, & oldCursorInfo );
                    consoleOutputHandle = 0;
                }
            #else
                if ( initialized )
                {
                    tcsetattr( 0, TCSANOW, &orig_termios );
                    initialized = false;
                }
            #endif
        } //RestoreConsole

        void SendClsSequence()
        {
            #ifdef _MSC_VER
                printf( "\x1b[2J" ); // clear the screen
                printf( "\x1b[1G" ); // cursor to top line
                printf( "\x1b[1d" ); // cursor to left side
            #endif
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
                    SetConsoleMode( hcon, dwMode );
                }
            #endif
        } //ClearScreen

        static int portable_kbhit()
        {
            #ifdef _MSC_VER
                return _kbhit();
            #else
                struct timeval tv = { 0L, 0L };
                fd_set fds;
                FD_ZERO( &fds );
                FD_SET( 0, &fds );
                return ( select( 1, &fds, NULL, NULL, &tv ) > 0 );
            #endif
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
        } // portable_getch

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
                char ch = portable_getch();
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
        }
}; //ConsoleConfiguration

