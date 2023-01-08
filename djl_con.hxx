#pragma once

class ConsoleConfiguration
{
    private:
        WINDOWPLACEMENT windowPlacement;
        CONSOLE_SCREEN_BUFFER_INFOEX oldScreenInfo;
        HANDLE consoleHandle;
        DWORD oldConsoleMode;

    public:
        ConsoleConfiguration()
        {
            windowPlacement = {0};
            oldScreenInfo = {0};
            consoleHandle = 0;
            oldConsoleMode = 0;
        } //ConsoleConfiguration

        ~ConsoleConfiguration()
        {
            RestoreConsole();
        }

        bool IsEstablished() { return ( 0 != consoleHandle ); }

        void EstablishConsole( SHORT width = 80, SHORT height = 24, PHANDLER_ROUTINE handler = NULL )
        {
            if ( 0 != consoleHandle )
                return;
        
            windowPlacement.length = sizeof windowPlacement;
            GetWindowPlacement( GetConsoleWindow(), &windowPlacement );
        
            consoleHandle = GetStdHandle( STD_OUTPUT_HANDLE );
        
            oldScreenInfo.cbSize = sizeof oldScreenInfo;
            GetConsoleScreenBufferInfoEx( consoleHandle, &oldScreenInfo );
        
            CONSOLE_SCREEN_BUFFER_INFOEX newInfo;
            memcpy( &newInfo, &oldScreenInfo, sizeof newInfo );
        
            newInfo.dwSize.X = width;
            newInfo.dwSize.Y = height;
            newInfo.dwMaximumWindowSize.X = width;
            newInfo.dwMaximumWindowSize.Y = height;
            SetConsoleScreenBufferInfoEx( consoleHandle, &newInfo );
        
            COORD newSize = { width, height };
            SetConsoleScreenBufferSize( consoleHandle, newSize );
        
            DWORD dwMode = 0;
            GetConsoleMode( consoleHandle, &dwMode );
            oldConsoleMode = dwMode;
            dwMode |= ( ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WINDOW_INPUT );
            tracer.Trace( "old console mode: %04x, new mode: %04x\n", oldConsoleMode, dwMode );
            SetConsoleMode( consoleHandle, dwMode );
                                                   
            // don't automatically have ^c terminate the app. ^break will still terminate the app
            SetConsoleCtrlHandler( handler, TRUE );

            printf( "\x1b[2J" ); // clear the screen
            printf( "\x1b[1G" ); // cursor to top line
            printf( "\x1b[1d" ); // cursor to left side
        } //EstablishConsole
        
        void RestoreConsole()
        {
            if ( 0 != consoleHandle )
            {
                printf( "\x1b[2J" ); // clear the screen
                SetConsoleScreenBufferInfoEx( consoleHandle, & oldScreenInfo );
                SetWindowPlacement( GetConsoleWindow(), & windowPlacement );
                SetConsoleMode( consoleHandle, oldConsoleMode );
                consoleHandle = 0;
            }
        } //RestoreConsole
}; //ConsoleConfiguration


