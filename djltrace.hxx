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

using namespace std;

#ifdef _MSC_VER

#ifndef _WINDOWS_
extern "C" uint32_t GetCurrentThreadId(void);
#endif

#define do_gettid() GetCurrentThreadId()

#else

#include <sys/unistd.h>

#define do_gettid() 0

#endif

class CDJLTrace
{
    private:
        FILE * fp;
        std::mutex mtx;
        bool quiet; // no pid and tid
        bool flush; // flush after each write

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

                    strcat( tempPath.data(), "tracer.txt" );

                    fp = fopen( tempPath.data(), mode );
                }
                else
                {
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

        void Trace( const char * format, ... )
        {
            if ( NULL != fp )
            {
                lock_guard<mutex> lock( mtx );

                va_list args;
                va_start( args, format );
                if ( !quiet )
                    fprintf( fp, "PID %6u TID %6u -- ", getpid(), do_gettid() );
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
        } //Trace

        // Don't prepend the PID and TID to the trace

        void TraceQuiet( const char * format, ... )
        {
            if ( NULL != fp )
            {
                lock_guard<mutex> lock( mtx );

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
                lock_guard<mutex> lock( mtx );

                va_list args;
                va_start( args, format );
                if ( !quiet )
                    fprintf( fp, "PID %6u TID %6u -- ", getpid(), do_gettid() );
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
            #endif
        } //TraceDebug
}; //CDJLTrace

extern CDJLTrace tracer;

