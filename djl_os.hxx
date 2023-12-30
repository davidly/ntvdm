/*
    These are some utilities and abstractions for building on Windows and Linux
*/

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32

    #ifndef UNICODE
        #define UNICODE
    #endif
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <conio.h>
    #include <direct.h>
    #include <intrin.h>
    #include <io.h>

    #define not_inlined __declspec(noinline)
    #define force_inlined __forceinline

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

#elif defined( WATCOM )

    #include <io.h>
    #define MAX_PATH 255
    #define not_inlined
    #define force_inlined __inline

    inline void sleep_ms( uint64_t ms ) {}

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

    inline void bump_thread_priority() {}
    inline void set_process_affinity( uint64_t processAffinityMask ) {}
    inline int getpid() { return 0; }
    #define _countof( X ) ( sizeof( X ) / sizeof( X[0] ) )
    inline void swap( uint8_t & a, uint8_t & b ) { uint8_t c = a; a = b; b = c; }

#else // Linux, MacOS, etc.

    #ifndef OLDGCC
        #include <termios.h>
    #endif

    #include <thread>
    #include <sched.h>
    #include <unistd.h>
    #include <ctype.h>

    #define not_inlined __attribute__ ((noinline))
    #define force_inlined inline

    inline void bump_thread_priority() {}

    inline void set_process_affinity( uint64_t processAffinityMask )
    {
#if !defined(__APPLE__) && !defined( OLDGCC )
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

    inline uint64_t _abs64( int64_t x ) { return ( x > 0 ) ? x : -x; }

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

template <class T> inline T round_up( T x, T multiple )
{
    if ( 0 == multiple )
       return x;

    T remainder = x % multiple;
    if ( 0 == remainder )
        return x;

    return x + multiple - remainder;
} //round_up

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
    #elif defined( WATCOM )       // WATCOM for 8086
        return "8086";
    #elif defined( _M_IX86 )      // msft on Windows 32-bit
        return "x86";
    #elif defined( __ARM_32BIT_STATE ) // ARM32 on Raspberry PI (and more)
        return "arm32";
    #else
        return "(other)";
    #endif
} //target_platform

inline const char * build_type()
{
    #ifdef NDEBUG
        return "release";
    #else
       return "debug";
    #endif
} //build_type

inline const char * compiler_used()
{
    static char acver[ 100 ];

    #if defined( __GNUC__ )
        return "g++";
    #elif defined( _MSC_VER )
        sprintf( acver, "msft C++ ver %u", _MSC_VER );
        return acver;
    #elif defined( __clang__ )
        return "clang";
    #elif defined( WATCOM )
        return "watcom";
    #else
        return "unknown";
    #endif
} //compiler_used

inline const char * build_platform()
{
    #if defined( __APPLE__ )
        return "apple";
    #elif defined( __linux )
        return "linux";
    #elif defined( _WIN32 )
        return "windows";
    #elif defined( WATCOM )
        return "windows";
    #else
        return "unknown";
    #endif
} //build_platform

inline const char * build_string()
{
    static char bs[ 320 ];
    sprintf( bs, "Built for %s %s on %c%c %c%c%c %s %s by %s on %s\n",
                 target_platform(), build_type(), __DATE__[4], __DATE__[5],
                 __DATE__[0], __DATE__[1], __DATE__[2], &__DATE__[9], __TIME__, compiler_used(), build_platform() );
    return bs;
} //build_string

#if defined( __GNUC__ ) || defined( __clang__ )
    #define assume_false return( 0 )   // clearly terrible, but this code will never execute. ever.
    #define assume_false_return return // clearly terrible, but this code will never execute. ever.
#elif defined( WATCOM )
    #define assume_false return( 0 )   // clearly terrible, but this code will never execute. ever.
    #define __assume( x )
#else
    #define assume_false __assume( false )
    #define assume_false_return __assume( false )
#endif

inline long portable_filelen( int descriptor )
{
#ifdef _WIN32
    long current = _lseek( descriptor, 0, SEEK_CUR );
    long len = _lseek( descriptor, 0, SEEK_END );
    _lseek( descriptor, current, SEEK_SET );
#else
    long current = lseek( descriptor, 0, SEEK_CUR );
    long len = lseek( descriptor, 0, SEEK_END );
    lseek( descriptor, current, SEEK_SET );
#endif
    return len;
} //portable_filelen

inline long portable_filelen( FILE * fp )
{
    long current = ftell( fp );
    fseek( fp, 0, SEEK_END );
    long len = ftell( fp );
    fseek( fp, current, SEEK_SET );
    return len;
} //portable_filelen

inline long portable_filelen( const char * p )
{
    FILE * fp = fopen( p, "r" );
    if ( 0 != fp )
    {
        long len = portable_filelen( fp );
        fclose( fp );
        return len;
    }

    return 0;
} //portable_filelen

class CFile
{
    private:
        FILE * fp;

    public:
        CFile( FILE * file ) : fp( file ) {}
        ~CFile() { close(); }
        FILE * get() { return fp; }
        void close()
        {
            if ( NULL != fp )
            {
                fclose( fp );
                fp = NULL;
            }
        }
};

