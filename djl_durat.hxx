#pragma once

#include <chrono>

using namespace std;
using namespace std::chrono;

class CDuration
{
    private:
        high_resolution_clock::time_point tLast;

    public:
        CDuration()
        {
            tLast = high_resolution_clock::now();
        }

        ~CDuration() {}

        bool HasTimeElapsed( long long target )
        {
            high_resolution_clock::time_point tNow = high_resolution_clock::now();
            long long duration = duration_cast<std::chrono::nanoseconds>( tNow - tLast ).count();
            if ( duration >= target )
            {
                tLast = tNow;
                return true;
            }

            return false;
        }

        bool HasTimeElapsedMS( long long target )
        {
            high_resolution_clock::time_point tNow = high_resolution_clock::now();
            long long duration = duration_cast<std::chrono::milliseconds>( tNow - tLast ).count();
            if ( duration >= target )
            {
                tLast = tNow;
                return true;
            }

            return false;
        }
};


