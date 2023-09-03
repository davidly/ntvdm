// writes and reads keystrokes to and from a file

#pragma once

class CKeyStrokes
{
    public:
        enum KeystrokeMode { ksm_None, ksm_Write, ksm_Read };

        CKeyStrokes() : ksmode( ksm_None ) {}

        ~CKeyStrokes()
        {
            if ( ksm_Write == ksmode )
                Persist();
        } //~CKeyStrokes

        void SetMode( KeystrokeMode ksm )
        {
            ksmode = ksm;
            if ( ksm_Read == ksm )
                Restore();
        } //SetMode

        bool KeystrokeAvailable() { return ( ( ksm_Read == ksmode ) && keys.size() ); }
        bool InReadMode() { return ( ksm_Read == ksmode ); }

        uint16_t Peek()
        {
            assert( KeystrokeAvailable() );
            tracer.Trace( "peeked keystroke %04x\n", keys[ 0 ] );
            return keys[ 0 ];
        } //Peek

        uint16_t ConsumeNext()
        {
            assert( KeystrokeAvailable() );
            tracer.Trace( "keystrokes in read buffer available: %zd\n", keys.size() );

            uint16_t x = keys[ 0 ];
            keys.erase( keys.begin() ); // expensive, but not very frequent
            return x;
        } //ConsumeNext

        void Append( uint16_t x )
        {
            if ( ksm_Write == ksmode )
            {
                tracer.Trace( "pushing char %04x onto the keystroke log\n", x );
                keys.push_back( x );
            }
        } //Append

        bool Persist()
        {
            // save what's in the vector to kslog.txt

            tracer.Trace( "persisting %zd keystrokes\n", keys.size() );

            FILE * fp = fopen( "kslog.txt", "w" );
            if ( 0 != fp )
            {
                for ( size_t i = 0; i < keys.size(); i++ )
                    fprintf( fp, "%04x", keys[ i ] );
                fclose( fp );
                return true;
            }

            tracer.Trace( "error: can't create file kslog.txt\n" );
            return false;
        } //Persist

        bool Restore()
        {
            // read what's in kslog.txt to the vector

            tracer.Trace( "restoring kslog.txt\n" );
            FILE * fp = fopen( "kslog.txt", "r" );
            if ( 0 != fp )
            {
                long l = portable_filelen( fp );
                size_t count = l / 4;
                tracer.Trace( "keystroke file length: %ld, count %zd\n", l, count );
                for ( size_t i = 0; i < count; i++ )
                {
                    char ac[ 5 ] = {0};
                    if ( fread( ac, 1, 4, fp ) )
                    {
                        uint16_t x = (uint16_t) _strtoui64( ac, 0, 16 );
                        tracer.Trace( "read key %04x\n", x );
                        keys.push_back( x );
                    }
                }

                fclose( fp );
                return true;
            }

            tracer.Trace( "error: can't find kslog.txt to read it\n" );
            return false;
        } //Restore

    private:
        vector<uint16_t> keys; // high part = scancode, lowpart = ascii char
        KeystrokeMode ksmode;
};
