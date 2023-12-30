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
#ifndef WATCOM
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
            int64_t offset = 0;
            int64_t beyond = length;
            const int64_t bytesPerRow = 32;
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

                int64_t end_of_row = offset + bytesPerRow;
                int64_t cap = ( end_of_row > beyond ) ? beyond : end_of_row;
                int64_t toread = ( ( offset + bytesPerRow ) > beyond ) ? ( length % bytesPerRow ) : bytesPerRow;
        
                memcpy( buf, pData + offset, toread );
        
                for ( int64_t o = offset; o < cap; o++ )
                {
                    pline = appendHexByte( pline, buf[ o - offset ] );
                    *pline++ = ' ';
                    if ( ( bytesPerRow > 16 ) && ( o == ( offset + 15 ) ) )
                    {
                        *pline++ = ':';
                        *pline++ = ' ';
                    }
                }
        
                uint64_t spaceNeeded = ( bytesPerRow - ( cap - offset ) ) * 3;
        
                for ( uint64_t sp = 0; sp < ( 1 + spaceNeeded ); sp++ )
                    *pline++ = ' ';
        
                for ( int64_t o = offset; o < cap; o++ )
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
#ifndef WATCOM
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

        // Don't prepend the PID to the trace

        void TraceQuiet( const char * format, ... )
        {
            if ( NULL != fp )
            {
#ifndef WATCOM
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

        void TraceDebug( bool condition, const char * format, ... )
        {
            #ifdef DEBUG
            if ( NULL != fp && condition )
            {
#ifndef WATCOM
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
#if !defined( WATCOM ) && !defined( __APPLE__ )
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
}; //CDJLTrace

extern CDJLTrace tracer;

