#pragma once

// In one source file, declare the CDJLTrace named tracer like this:
//    CDJLTrace tracer;
// When the app starts, enable tracing like this:
//    tracer.Enable( true );
// By default the tracing file is placed in %temp%\tracer.txt
// Arguments to Trace() are just like printf. e.g.:
//    tracer.Trace( "what to log with an integer argument %d and a wide string %ws\n", 10, pwcHello );
//

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <stdio.h>
#include <mutex>

using namespace std;

class CDJLTrace
{
    private:
        FILE * fp;
        std::mutex mtx;
        bool quiet; // no pid and tid
        bool flush; // flush after each write

    public:
        CDJLTrace() : fp( NULL ), quiet( false ), flush( true ) {}

        bool Enable( bool enable, const WCHAR * pwcLogFile = NULL, bool destroyContents = false )
        {
            Shutdown();

            if ( enable )
            {
                const WCHAR * mode = destroyContents ? L"w+t" : L"a+t";

                if ( NULL == pwcLogFile )
                {
                    const WCHAR * pwcFile = L"tracer.txt";
                    size_t len = wcslen( pwcFile );

                    unique_ptr<WCHAR> tempPath( new WCHAR[ MAX_PATH + 1 ] );
                    size_t available = MAX_PATH - len;

                    size_t result = GetTempPath( (DWORD) available , tempPath.get() );
                    if ( result > available || 0 == result )
                        return false;

                    wcscat_s( tempPath.get(), MAX_PATH - result, pwcFile );
                    fp = _wfsopen( tempPath.get(), mode, _SH_DENYWR );
                }
                else
                {
                    fp = _wfsopen( pwcLogFile, mode, _SH_DENYWR );
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
                    fprintf( fp, "PID %6d TID %6d -- ", GetCurrentProcessId(), GetCurrentThreadId() );
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
                    fprintf( fp, "PID %6d TID %6d -- ", GetCurrentProcessId(), GetCurrentThreadId() );
                vfprintf( fp, format, args );
                va_end( args );
                if ( flush )
                    fflush( fp );
            }
            #endif
        } //TraceDebug
}; //CDJLTrace

extern CDJLTrace tracer;

