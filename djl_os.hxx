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

    #define not_inlined __declspec(noinline)

    inline void sleep_ms( uint64_t ms ) { SleepEx( (DWORD) ms, FALSE ); }

    inline bool file_exists( char const * pfile )
    {
        uint32_t attr = GetFileAttributesA( pfile );
        return ( ( INVALID_FILE_ATTRIBUTES != attr ) && ( ! ( FILE_ATTRIBUTE_DIRECTORY & attr ) ) );
    } //file_exists

    inline void bump_thread_priority() { SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST ); }

    inline void set_process_affinity( uint64_t processAffinityMask )
    {
        SetProcessAffinityMask( (HANDLE) -1, processAffinityMask );
    }

#else // Linux, MacOS, etc.

    #ifndef OLDGCC
        #include <termios.h>
    #endif

    #include <thread>
    #include <sched.h>
    #include <unistd.h>
    #include <ctype.h>

    #define not_inlined

    inline void bump_thread_priority() {}

    inline void set_process_affinity( uint64_t processAffinityMask )
    {
#if !defined(__APPLE__)
        cpu_set_t mask;
        CPU_ZERO( &mask );

        for ( long l = 0; l < 32; l++ )
        {
            int b = ( 1 << l );
            if ( 0 != ( b & processAffinityMask ) )
                CPU_SET( l, &mask );
        }

        // this does nothing on WSL 1 or 2 except make you believe it might work until you actually check
        int status = sched_setaffinity( 0, sizeof( mask ), &mask );
#endif
    } //set_process_affinity

    template < typename T, size_t N > size_t _countof( T ( & arr )[ N ] ) { return std::extent< T[ N ] >::value; }    
    #define _stricmp strcasecmp
    #define MAX_PATH 1024

    inline char * strupr( char * s )
    {
        for ( char * t = s; *t; t++ )
            *t = toupper( *t );
        return s;
    } //strupr

    inline char * _strupr( char * s ) { return strupr( s ); }

    inline char * strlwr( char * s )
    {
        for ( char * t = s; *t; t++ )
            *t = tolower( *t );
        return s;
    } //strlwr

    inline char * _strlwr( char * s ) { return strlwr( s ); }

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

inline const char * target_platform()
{
    #if defined( __riscv )        // g++ on linux
        return "riscv";
    #elif defined( __amd64 )      // g++ on linux
        return "amd64";
    #elif defined( __aarch64__ )  // g++ on linux
        return "arm64";
    #elif defined( _M_AMD64 )     // msft on Windows
        return "amd64";
    #elif defined( _M_ARM64 )     // msft on Windows
        return "arm64";
    #endif

    return "(other)";
} //target_platform

inline const char * build_type()
{
    #ifdef NDEBUG
        return "release";
    #endif

    return "debug";
} //build_type

inline const char * compiler_used()
{
    #if defined( __GNUC__ )
        return "g++";
    #elif defined( _MSC_VER )
        return "msft C++";
    #elif defined( __clang__ )
        return "clang";
    #endif

    return "unknown";
} //compiler_used

inline const char * build_platform()
{
    #if defined( __APPLE__ )
        return "apple";
    #elif defined( __linux )
        return "linux";
    #elif defined( _MSC_VER )
        return "windows";
    #endif

    return "unknown";
} //build_platform


