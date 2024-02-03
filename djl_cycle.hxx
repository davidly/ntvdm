#pragma once

#include <time.h>
using namespace std;

#ifndef WATCOM
using namespace std::chrono;
#endif

class CPUCycleDelay
{
#ifdef WATCOM // no implementation on DOS
    public:
        CPUCycleDelay( uint64_t clockRate ) {}
        void Reset() {}
        void Delay( uint64_t cycles_total ) {}
#else
    private:
        high_resolution_clock::time_point start_execution;
        uint64_t clock_rate;

    public:
        CPUCycleDelay( uint64_t clockRate ) : clock_rate( clockRate )
        {
            Reset();
        } //CPUCycleDelay

        void Reset()
        {
            if ( 0 != clock_rate )
                start_execution = high_resolution_clock::now();
        } //Reset

        void Delay( uint64_t cycles_total )
        {
            if ( 0 != clock_rate )
            {
                uint64_t targetMicroseconds = ( 1000000 * cycles_total ) / clock_rate;

                do
                {
                    high_resolution_clock::time_point right_now = high_resolution_clock::now();
                    uint64_t sofar = duration_cast<std::chrono::microseconds>( right_now - start_execution ).count();
             
                    if ( sofar >= targetMicroseconds )
                        break;

                    // sleep in a slightly less than busy loop.
                    // this is slightly slower than running in a busy loop, but it's pretty close

                    #ifdef _WIN32
                        SleepEx( 1, FALSE );
                    #else
                        struct timespec ts = { 0, 1000000 };
                        nanosleep( &ts, 0 );
                    #endif
                } while ( true );
            }
        } //Delay
#endif //WATCOM
}; //CPUCycleDelay
