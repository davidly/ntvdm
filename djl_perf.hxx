#pragma once

class CPerfTime
{
    private:
        LARGE_INTEGER liLastCall;
        LARGE_INTEGER liFrequency;
        NUMBERFMT NumberFormat;
        WCHAR awcRender[ 100 ];

    public:
        CPerfTime()
        {
            ZeroMemory( &NumberFormat, sizeof NumberFormat );
            NumberFormat.NumDigits = 0;
            NumberFormat.Grouping = 3;
            NumberFormat.lpDecimalSep = (LPWSTR) L".";
            NumberFormat.lpThousandSep = (LPWSTR) L",";

            Baseline();
            QueryPerformanceFrequency( &liFrequency );
        }

        void Baseline()
        {
            QueryPerformanceCounter( &liLastCall );
        }
    
        int RenderLL( LONGLONG ll, WCHAR * pwcBuf, ULONG cwcBuf )
        {
            WCHAR awc[100];
            swprintf( awc, L"%I64u", ll );

            if ( 0 != cwcBuf )
                *pwcBuf = 0;

            return GetNumberFormat( LOCALE_USER_DEFAULT, 0, awc, &NumberFormat, pwcBuf, cwcBuf );
        } //RenderLL

        WCHAR * RenderLL( LONGLONG ll )
        {
            WCHAR awc[100];
            swprintf( awc, L"%I64u", ll );

            awcRender[0] = 0;
            GetNumberFormat( LOCALE_USER_DEFAULT, 0, awc, &NumberFormat, awcRender, sizeof awcRender / sizeof WCHAR );
            return awcRender;
        } //RenderLL

        void CumulateSince( LONGLONG & running )
        {
            LARGE_INTEGER liNow;
            QueryPerformanceCounter( &liNow );
            LONGLONG since = liNow.QuadPart - liLastCall.QuadPart;
            liLastCall = liNow;

            InterlockedExchangeAdd64( &running, since );
        }
    
        LONGLONG TimeNow()
        {
            LARGE_INTEGER liNow;
            QueryPerformanceCounter( &liNow );
            return liNow.QuadPart;
        }

        LONGLONG DurationToMS( LONGLONG duration )
        {
            duration *= 1000000;
            return ( duration / liFrequency.QuadPart ) / 1000;
        }

        WCHAR * RenderDurationInMS( LONGLONG duration )
        {
            LONGLONG x = DurationToMS( duration );

            RenderLL( x, awcRender, sizeof awcRender / sizeof WCHAR );

            return awcRender;
        }
}; //CPerfTime


