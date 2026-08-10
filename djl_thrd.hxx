#pragma once

#if !defined( _WIN32 ) && !defined( sparc ) && !defined( __mc68000__ )

#include <atomic>
#include <pthread.h>

class C_pthread_mutex_t_lock
{
    private:
        pthread_mutex_t & _mtx;
    public:
        C_pthread_mutex_t_lock( pthread_mutex_t & m ) : _mtx( m ) { pthread_mutex_lock( & _mtx ); }
        ~C_pthread_mutex_t_lock() { pthread_mutex_unlock( & _mtx ); }
};

#endif

class CSimpleThread
{
#ifdef _WIN32
    private:
        HANDLE heventStop;
        HANDLE hthread;
    #elif defined( sparc ) || defined( __mc68000__ )
    // no member state -- this target never actually starts a thread (see below)
#else
    public:
        pthread_t the_thread;
        pthread_cond_t end_condition;
        pthread_mutex_t the_mutex;
        std::atomic<bool> stop_running;

#endif

    public:
#ifdef _WIN32
        CSimpleThread( LPTHREAD_START_ROUTINE func ) : heventStop( INVALID_HANDLE_VALUE ), hthread( INVALID_HANDLE_VALUE )
        {
            heventStop = CreateEvent( 0, FALSE, FALSE, 0 );
            hthread = CreateThread( 0, 0, func, heventStop, 0, 0 );
        }

        void EndThread()
        {
            if ( INVALID_HANDLE_VALUE != hthread )
            {
                SetEvent( heventStop );
                WaitForSingleObject( hthread, INFINITE );
                CloseHandle( hthread );
                hthread = INVALID_HANDLE_VALUE;
                CloseHandle( heventStop );
                heventStop = INVALID_HANDLE_VALUE;
            }
        }
#elif defined( sparc ) || defined( __mc68000__ )
        // SPARCOS/M68: avoid pthread entirely -- their emulators don't yet support all the syscalls
        // glibc/uclibc need for it. g_UseOneThread is forced true on these targets (see main()), so
        // this constructor is declared to match the call site but is never actually invoked.
        CSimpleThread( void * ( * start_routine )( void * ) ) {}
        void EndThread() {}
#else
        CSimpleThread( void * ( * start_routine )( void * ) ) : the_thread( 0 )
        {
            stop_running = false;
            // Explicit init (rather than assigning PTHREAD_MUTEX_INITIALIZER/PTHREAD_COND_INITIALIZER)
            // for both: assigning one of those macros to an already-declared variable and then also
            // calling pthread_*_init on it -- which the cond var used to do -- is undefined behavior
            // per POSIX (initializing an already-initialized primitive), even though it's harmless in
            // practice with glibc/macOS's actual implementations.
            pthread_mutex_init( & the_mutex, 0 );
            pthread_cond_init( & end_condition, 0 );
            int ret = pthread_create( & the_thread, 0, start_routine, (void *) this );
            tracer.Trace( "simplethread: return value from pthread_create: %d\n", ret );
        }

        void EndThread()
        {
            if ( 0 != the_thread )
            {
                {
                    // stop_running is set inside the same lock as the signal (rather than before it,
                    // as this used to do) so the happens-before relationship the waiting thread relies
                    // on to see it is established by this lock/unlock pair directly, not by incidental
                    // reasoning about pthread_mutex_lock/unlock acting as a fence around a write that
                    // precedes it in program order. stop_running is also atomic<bool> now (not a plain
                    // bool) so this is correct independent of that argument too -- belt and suspenders,
                    // since the thread proc below reads it outside any lock, right after pthread_cond_
                    // timedwait's internal re-lock/unlock has already returned.

                    tracer.Trace( "simplethread: signaling a thread to check if it's time to complete\n" );
                    C_pthread_mutex_t_lock mtx_lock( the_mutex );
                    stop_running = true;
                    pthread_cond_signal( & end_condition );
                }

                tracer.Trace( "simplethread: joining the thread\n" );
                pthread_join( the_thread, 0 );
                the_thread = 0;
                tracer.Trace( "simplethread: destroying the thread resources\n" );
                pthread_cond_destroy( & end_condition );
                pthread_mutex_destroy( & the_mutex );
            }
        }
#endif

        ~CSimpleThread() { EndThread(); }
}; //CSimpleThread

