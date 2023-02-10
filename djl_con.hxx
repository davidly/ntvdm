#pragma once

class ConsoleConfiguration
{
    private:
        HANDLE consoleOutputHandle;
        HANDLE consoleInputHandle;
        WINDOWPLACEMENT oldWindowPlacement;
        CONSOLE_SCREEN_BUFFER_INFOEX oldScreenInfo;
        DWORD oldConsoleMode;
        CONSOLE_CURSOR_INFO oldCursorInfo;

    public:
        ConsoleConfiguration()
        {
            consoleOutputHandle = 0;
            consoleInputHandle = 0;
            oldWindowPlacement = {0};
            oldScreenInfo = {0};
            oldConsoleMode = 0;
            oldCursorInfo = {0};
        } //ConsoleConfiguration

        ~ConsoleConfiguration()
        {
            RestoreConsole();
        }

        bool IsEstablished() { return ( 0 != consoleOutputHandle ); }
        HANDLE GetOutputHandle() { return consoleOutputHandle; };
        HANDLE GetInputHandle() { return consoleInputHandle; };

        void SetCursorInfo( DWORD size ) // 0 to 100
        {
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
        } //SetCursorInfo

        void EstablishConsole( SHORT width = 80, SHORT height = 24, PHANDLER_ROUTINE handler = NULL )
        {
            if ( 0 != consoleOutputHandle )
                return;
        
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
        } //EstablishConsole
        
        void RestoreConsole( bool clearScreen = true )
        {
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
        } //RestoreConsole

        void SendClsSequence()
        {
            printf( "\x1b[2J" ); // clear the screen
            printf( "\x1b[1G" ); // cursor to top line
            printf( "\x1b[1d" ); // cursor to left side
        }

        void ClearScreen()
        {
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
        }
}; //ConsoleConfiguration

