#pragma once

class CSimpleThread
{
    private:
        HANDLE heventStop;
        HANDLE hthread;

    public:
        CSimpleThread( LPTHREAD_START_ROUTINE func ) : heventStop( INVALID_HANDLE_VALUE ), hthread( INVALID_HANDLE_VALUE )
        {
            HANDLE heventStop = CreateEvent( 0, FALSE, FALSE, 0 );
            HANDLE hthread = CreateThread( 0, 0, func, heventStop, 0, 0 );
        }

        ~CSimpleThread() { EndThread(); }

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
}; //CSimpleThread
       
