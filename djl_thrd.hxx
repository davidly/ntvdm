#pragma once

#ifndef _WIN32
#include <pthread.h>
#endif

class CSimpleThread
{
#ifdef _WIN32    
    private:
        HANDLE heventStop;
        HANDLE hthread;
#else
    public: 
        pthread_t the_thread;
        pthread_cond_t the_condition;
        pthread_mutex_t the_mutex;
        bool exit_now;
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
#else        
        CSimpleThread( void * ( * start_routine )( void * ) ) : the_thread( 0 ), exit_now( false )
        {
            the_condition = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
            the_mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
            int ret = pthread_create( & the_thread, 0, start_routine, (void *) this );  
            tracer.Trace( "return value from pthread_create: %d\n", ret );
            pthread_cond_init( & the_condition, 0 );   
        }

        void EndThread()
        {
            if ( 0 != the_thread )
            {
                tracer.Trace( "signaling the keyboard thread to complete\n" );
                pthread_cond_signal( & the_condition );
                exit_now = true;
                tracer.Trace( "joining the keyboard thread\n" );
                pthread_join( the_thread, 0 );
                the_thread = 0; 
                tracer.Trace( "destroying keyboard thread resources\n" );
                pthread_cond_destroy( & the_condition );
                pthread_mutex_destroy( & the_mutex );
            }
        }
#endif

        ~CSimpleThread() { EndThread(); }
}; //CSimpleThread
       
