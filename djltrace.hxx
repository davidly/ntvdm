#pragma once

// In one source file, declare the CDJLTrace named tracer like this:
//    CDJLTrace tracer;
// When the app starts, enable tracing like this:
//    tracer.Enable( true );
// By default the tracing file is placed in %temp%\tracer.txt
// Arguments to Trace() are just like printf. e.g.:
//    tracer.Trace( "what to log with an integer argument %d and a wide string %ws\n", 10, pwcHello );
//

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <mutex>
#include <memory>
#include <vector>
#include <cstring>
#include <djl_os.hxx>

#if defined( __GNUC__ ) && !defined( __APPLE__) && !defined( __clang__ )
#pragma GCC diagnostic ignored "-Wformat="
#endif

#ifdef _WIN32
#include <process.h>
#endif

#if !defined(_WIN32) && !defined(WATCOM)
    #include <sys/unistd.h>
    #ifdef __APPLE__
        #include <unistd.h>
    #endif
#endif

using namespace std;

class CDJLTrace
{
    private:
        FILE * fp;
#if !defined( WATCOM ) && !defined( OLDGCC ) && !defined( __mc68000__ )
        std::mutex mtx;
#endif
        bool quiet; // no pid
        bool flush; // flush after each write

        static char * appendHexNibble( char * p, uint8_t val )
        {
            *p++ = ( val <= 9 ) ? val + '0' : val - 10 + 'a';
            return p;
        } //appendHexNibble
        
        static char * appendHexByte( char * p, uint8_t val )
        {
            p = appendHexNibble( p, ( val >> 4 ) & 0xf );
            p = appendHexNibble( p, val & 0xf );
            return p;
        } //appendBexByte
        
        static char * appendHexWord( char * p, uint16_t val )
        {
            p = appendHexByte( p, ( val >> 8 ) & 0xff );
            p = appendHexByte( p, val & 0xff );
            return p;
        } //appendHexWord

        void ShowBinaryData( uint8_t * pData, uint32_t length, uint32_t indent, bool trace )
        {
            int32_t offset = 0;
            int32_t beyond = length;
            const int32_t bytesPerRow = 32;
            uint8_t buf[ bytesPerRow ];
            char acLine[ 200 ];
        
            while ( offset < beyond )
            {
                char * pline = acLine;
        
                for ( uint32_t i = 0; i < indent; i++ )
                    *pline++ = ' ';
        
                pline = appendHexWord( pline, (uint16_t) offset );
                *pline++ = ' ';
                *pline++ = ' ';

                int32_t end_of_row = offset + bytesPerRow;
                int32_t cap = ( end_of_row > beyond ) ? beyond : end_of_row;
                int32_t toread = ( ( offset + bytesPerRow ) > beyond ) ? ( length % bytesPerRow ) : bytesPerRow;
        
                memcpy( buf, pData + offset, toread );

                uint32_t extraSpace = 2;
        
                for ( int32_t o = offset; o < cap; o++ )
                {
                    pline = appendHexByte( pline, buf[ o - offset ] );
                    *pline++ = ' ';
                    if ( ( bytesPerRow > 16 ) && ( o == ( offset + 15 ) ) )
                    {
                        *pline++ = ':';
                        *pline++ = ' ';
                        extraSpace = 0;
                    }
                }
        
                uint32_t spaceNeeded = extraSpace + ( ( bytesPerRow - ( cap - offset ) ) * 3 );
        
                for ( uint32_t sp = 0; sp < ( 1 + spaceNeeded ); sp++ )
                    *pline++ = ' ';
        
                for ( int32_t o = offset; o < cap; o++ )
                {
                    char ch = buf[ o - offset ];
        
                    if ( (int8_t) ch < ' ' || 127 == ch )
                        ch = '.';
        
                    *pline++ = ch;
                }
        
                offset += bytesPerRow;
                *pline = 0;

                if ( trace )
                    TraceQuiet( "%s\n", acLine );
                else
                    printf( "%s\n", acLine );
            }
        } //ShowBinaryData

    public:
        CDJLTrace() : fp( NULL ), quiet( false ), flush( true ) {}

        bool Enable( bool enable, const wchar_t * pcLogFile = NULL, bool destroyContents = false )
        {
            if ( 0 != pcLogFile )
            {
                size_t len = wcslen( pcLogFile );
                vector<char> narrow( 1 + len );
                wcstombs( narrow.data(), pcLogFile, 1 + len );
                return Enable( enable, narrow.data(), destroyContents );
            }

            return Enable( enable, (const char *) 0, destroyContents );
        } // Enable

        bool Enable( bool enable, const char * pcLogFile = NULL, bool destroyContents = false )
        {
            Shutdown();

            if ( enable )
            {
                const char * mode = destroyContents ? "w+t" : "a+t";

                if ( NULL == pcLogFile )
                {
                    const char * tracefile = "tracer.txt";
                    size_t len = strlen( tracefile );
                    vector<char> tempPath( 1 + len );
                    tempPath[0] = 0;
                   
                    const char * ptmp = getenv( "TEMP" );
                    if ( ptmp )
                    {
                        tempPath.resize( 1 + len + strlen( ptmp ) );
                        strcpy( tempPath.data(), ptmp );
                        strcat( tempPath.data(), "/" );
                    }

                    strcat( tempPath.data(), tracefile );

                    fp = fopen( tempPath.data(), mode );
                }
                else
                {
#ifdef WATCOM // workaround for WATCOM, which doesn't delete the file with "w+t" in spite of its documentation claiming otherwise
                    if ( !strcmp( mode, "w+t" ) )
                        remove( pcLogFile );
#endif

                    fp = fopen( pcLogFile, mode );
                }
            }

            return ( NULL != fp );
        } //Enable

        void Shutdown()
        {
            if ( NULL != fp )
            {
                fflush( fp );
                fclose( fp );
                fp = NULL;
            }
        } //Shutdown

        ~CDJLTrace()
        {
            Shutdown();
        } //~CDJLTrace

        bool IsEnabled() { return ( 0 != fp ); }

        void SetQuiet( bool q ) { quiet = q; }

        void SetFlushEachTrace( bool f ) { flush = f; }

        void Flush() { if ( 0 != fp ) fflush( fp ); }

        void Trace( const char * format, ... )
        {
            if ( NULL != fp )
            {
#if !defined( WATCOM ) && !defined( OLDGCC ) && !defined( __mc68000__ )
                lock_guard<mutex> lock( mtx );
#endif

                va_list args;
                va_start( args, format );
                if ( !quiet )
                    fprintf( fp, "PID %6u -- ",
#ifdef _WIN32
                             (unsigned) _getpid() );
#else
                             getpid() );
#endif
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
        } //Trace

        void TraceVA( const char * format, va_list args )
        {
            if ( NULL != fp )
            {
                vfprintf( fp, format, args );
                if ( flush )
                    fflush( fp );
            }
        } //TraceVA

        // Don't prepend the PID to the trace

        void TraceQuiet( const char * format, ... )
        {
            if ( NULL != fp )
            {
#if !defined( WATCOM ) && !defined( OLDGCC ) && !defined( __mc68000__ )
                lock_guard<mutex> lock( mtx );
#endif
                va_list args;
                va_start( args, format );
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
        } //TraceQuiet

        void TraceIt( const char * format, ... )
        {
#if !defined( WATCOM ) && !defined( OLDGCC ) && !defined( __mc68000__ )
            lock_guard<mutex> lock( mtx );
#endif
            va_list args;
            va_start( args, format );
            vfprintf( fp, format, args );
            va_end( args );
            if ( flush )
                fflush( fp );
        } //TraceIt

        void TraceDebug( bool condition, const char * format, ... )
        {
            #ifdef DEBUG
            if ( NULL != fp && condition )
            {
#if !defined( WATCOM ) && !defined( OLDGCC ) && !defined( __mc68000__ )
                lock_guard<mutex> lock( mtx );
#endif

                va_list args;
                va_start( args, format );
                if ( !quiet )
                    fprintf( fp, "PID %6u -- ",
#ifdef _WIN32
                             _getpid() );
#else
                             getpid() );
#endif
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
            #else
#if !defined( WATCOM ) && !defined( __APPLE__ ) && !defined( __clang__ ) && !defined (OLDGCC)
            condition; // unused
            format; // unused
#endif
            #endif
        } //TraceDebug

        void TraceBinaryData( uint8_t * pData, uint32_t length, uint32_t indent )
        {
            if ( NULL != fp )
                ShowBinaryData( pData, length, indent, true );
        } //TraceBinaryData

        void PrintBinaryData( uint8_t * pData, uint32_t length, uint32_t indent )
        {
            ShowBinaryData( pData, length, indent, false );
        } //PrintBinaryData

        static char * RenderNumberWithCommas( int64_t n, char * pc )
        {
            char actmp[ 32 ];
            int64_t orig = n;

            if ( 0 == n )
            {
                strcpy( pc, "0" );
                return pc;
            }
            else if ( n < 0 )
                n = -n;

            pc[ 0 ] = 0;

            while ( 0 != n )
            {
                strcpy( actmp, pc );
                if ( n >= 1000 )
                    snprintf( pc, 5, ",%03lld", n % 1000 );
                else
                    snprintf( pc, 4, "%lld", n );
                strcat( pc, actmp );
                n /= 1000;
            }

            if ( orig < 0 )
            {
                strcpy( actmp, pc );
                strcpy( pc, "-" );
                strcat( pc, actmp );
            }

            return pc;
        } //RenderNumberWithCommas
}; //CDJLTrace

extern CDJLTrace tracer;
