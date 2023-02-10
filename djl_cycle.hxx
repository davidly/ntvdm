#pragma once

class CPUCycleDelay
{
    private:
        LARGE_INTEGER liFreq;
        ULONGLONG clockRate;
        LARGE_INTEGER liStartExecution;

    public:
        CPUCycleDelay( ULONGLONG clock_rate ) : clockRate( clock_rate )
        {
            liStartExecution.QuadPart = 0;
            liFreq.QuadPart = 0;

            if ( 0 != clockRate )
            {
                QueryPerformanceFrequency( & liFreq );
                QueryPerformanceCounter( & liStartExecution );
            }
        } //CPUCycleDelay

        void Reset()
        {
            if ( 0 != clockRate )
                QueryPerformanceCounter( & liStartExecution );
        } //Reset

        void Delay( ULONGLONG cyclesTotal )
        {
            if ( 0 != clockRate )
            {
                ULONGLONG targetMicroseconds = ( 1000000 * cyclesTotal ) / clockRate;

                do
                {
                    LARGE_INTEGER li;
                    QueryPerformanceCounter( & li );
                    ULONGLONG diff = li.QuadPart - liStartExecution.QuadPart;
                    ULONGLONG sofar = ( 1000000 * diff ) / liFreq.QuadPart;

                    if ( sofar >= targetMicroseconds )
                        break;

                    // sleep in a slightly less than busy loop.
                    // this is slightly slower than running in a busy loop, but it's pretty close

                    SleepEx( 1, FALSE );
                } while (TRUE);
            }
        } //Delay
}; //CPUCycleDelay
