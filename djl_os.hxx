/*
    These are some utilities and abstractions for building on Windows and Linux
*/

#pragma once

#include <stdint.h>
#include <time.h>

#ifdef _MSC_VER

    #define UNICODE
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <conio.h>
    #include <direct.h>
    #include <intrin.h>

    inline void sleep_ms( uint64_t ms ) { SleepEx( ms, FALSE ); }

    inline bool file_exists( char const * pfile )
    {
        uint32_t attr = GetFileAttributesA( pfile );
        return ( ( INVALID_FILE_ATTRIBUTES != attr ) && ( ! ( FILE_ATTRIBUTE_DIRECTORY & attr ) ) );
    } //file_exists

#else // Linux, MacOS, etc.

    #ifndef OLDGCC
        #include <termios.h>
    #endif

    #include <ctype.h>

    template < typename T, size_t N > size_t _countof( T ( & arr )[ N ] ) { return std::extent< T[ N ] >::value; }    
    #define _stricmp strcasecmp
    #define MAX_PATH 1024

    inline char * strupr( char * s )
    {
        for ( char * t = s; *t; t++ )
            *t = toupper( *t );
        return s;
    } //strupr

    inline char * strlwr( char * s )
    {
        for ( char * t = s; *t; t++ )
            *t = tolower( *t );
        return s;
    } //strlwr

    inline void sleep_ms( uint64_t ms )
    {
        uint64_t total_ns = ms * 1000000;
        long ns = (long) ( total_ns % 1000000000 );
        long sec = (long) ( total_ns / 1000000000 );
        struct timespec ts = { sec, ns };

        #ifndef OLDGCC
            nanosleep( &ts, 0 );
        #endif
    } //sleep_ms

    inline bool file_exists( char const * pfile )
    {
        FILE * fp = fopen( pfile, "r" );
        bool exists = false;
        if ( fp )
        {
            fclose( fp );
            exists = true;
        }
        return exists;
    } //file_exists

#endif

template <class T> inline T get_max( T a, T b )
{
    if ( a > b )
        return a;
    return b;
} //get_max

template <class T> inline T get_min( T a, T b )
{
    if ( a < b )
        return a;
    return b;
} //get_min
