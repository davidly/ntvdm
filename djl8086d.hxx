#pragma once

// 8086 disassembler.
// this has not been systematically tested for every instruction.
// it has been tested for a handful of apps, so most instructions have been validated.
// usage:
//    CDisassemble8086 dis;
//    const char * p = dis.Disassemble( uint8_t * pcode );
//    printf( "next instruction: %s\n", p );
//    printf( "  it was %d bytes long\n", dis.BytesConsumed() );
//
// first byte: (for those opcodes where this applies)
//   bits 7:2  shortened opcode, referred to as top6 below
//          1  D. direction: 1 is destination and 0 is source
//          0  W. word if 1 and byte if 0. There are numerous exceptions to this rule.
//
// second byte: (for those opcodes where this applies)
//  bits  7:6  mod. mode
//        5:3  reg. register or /X modifier of byte 1
//        2:0  rm. register/memory
//  meaning
//    mod=11: r/m specifies a register modified by W
//    mod=00: index register(s) or direct address (if rm is 110)
//    mod=01: index register(s) plus signed 8-bit immediate offset
//    mod=02: index register(s) plus signed 16-bit immediate offset
//    
// registers: w=0 means 8 bit, w=1 means 16 bit
//   rb 0..7: al, cl, dl, bl, ah, ch, dh, bh
//   rw 0..7: ax, cx, dx, bx, sp, bp, si, di
// 

// display assembler shortcuts: display and append

#define _da( ... ) sprintf( acOut, __VA_ARGS__ )
#define _daa( ... ) sprintf( acOut + strlen( acOut ), __VA_ARGS__ )

class CDisassemble8086
{
    private:
        uint8_t * _pcode;    // pointer to stream of bytes to disassemble
        uint8_t _bc;         // # of bytes consumed by most recent instruction disassembeled
        uint8_t _b0;         // pcode[ 0 ]
        uint8_t _b1;         // pcode[ 1 ]
        uint8_t _b2;         // pcode[ 2 ];
        uint8_t _b3;         // pcode[ 3 ];
        uint8_t _b4;         // pcode[ 4 ];
        uint16_t _b12;        // b1 and b2 as a little-endian word
        uint16_t _b23;        // b2 and b3 as a little-endian word
        uint16_t _b34;        // b3 and b4 as a little-endian word
        uint16_t _reg;        // bits 5:3 of _b1
        uint8_t _rm;         // bits 2:0 of _b1
        uint8_t _mod;        // bits 7:6 of _b1
        bool _isword;     // true if bit 0 of _b0 is 1
        bool _toreg;      // true if bit 1 of _b0 is 1

        // wish I could make these static without requiring an initialization elsewhere
        
        const char * reg_strings[16];
        const char * rm_strings[8];
        const char * sr_strings[4];
        const char * jmp_strings[16];
        const char * i_opBits[16]; // bitwise/add/sub
        const char * i_opRot[8];  // rotates/shifts
        const char * i_opMath[8];  // test/not/neg/mul/div
        const char * i_opMix[8];  // inc/dec/call/jmp
        
        const char * getrm( uint8_t rm_to_use, int immediateOffset = 0 )
        {
            static char acOut[80];

            //tracer.TraceQuiet( "{rm_to_use %#x, isword %d, mod %#x}", rm_to_use, _isword, _mod );

            if ( 3 == _mod )
                return reg_strings[ rm_to_use | ( _isword ? 8 : 0 ) ];
        
            rm_to_use &= 0x7; // mask away the higher bit that my be set for register lookups

            _da( "%s ", _isword ? "word ptr" : "byte ptr" );
        
            if ( 0 == _mod )
            {
                if ( 0x6 == rm_to_use )
                {
                    _daa( "[%04xh]", (uint32_t) _pcode[ 2 + immediateOffset ] + ( (uint32_t) _pcode[ 3 + immediateOffset ] << 8 ) );
                    _bc += 2;
                }
                else
                    _daa( "[%s]", rm_strings[ rm_to_use ] );
            }
            else if ( 1 == _mod )
            {
                int offset = (int) (char) _pcode[2];
                _daa( "[%s%s%d]", rm_strings[ rm_to_use ], ( offset >= 0 ) ? "+" : "", offset );
                _bc += 1;
            }
            else if ( 2 == _mod )
            {
                _daa( "[%s+%04xh]", rm_strings[ rm_to_use ], _pcode[2] + ( (uint32_t) _pcode[3] << 8 ) );
                _bc += 2;
            }
        
            return acOut;
        } //getrm

        const char * getrmAsWord()
        {
            bool old_isword = _isword;
            _isword = true;
            const char * pc = getrm( _rm );
            _isword = old_isword;
            return pc;
        } //getrmAsWord
        
        const char * getrmAsByte()
        {
            bool old_isword = _isword;
            _isword = false;
            const char * pc = getrm( _rm );
            _isword = old_isword;
            return pc;
        } //getrmAsByte
        
        const char * opargs( bool firstArgReg )
        {
            static char ac[80];
            ac[0] = 0;

            if ( _isword )
                _reg |= 0x8;

            //tracer.TraceQuiet( "{toreg %d, isword %d, mod %d, reg %d, rm %d}", _toreg, _isword, _mod, _reg, _rm );
        
            if ( _toreg )
            {
                bool directAddress = ( 0 == _mod && 6 == _rm );
                bool secondArgReg = ( 3 == _mod );
                if ( firstArgReg )
                {
                    strcpy( ac, reg_strings[ _reg ] );
                    strcat( ac, ", " );
                    strcat( ac, getrm( _rm ) );
                }
                else
                {
                    sprintf( ac, "%s, ", getrm( secondArgReg ? _reg : _rm ) );
    
                    // output any immediate data
            
                    if ( !secondArgReg )
                    {
                        // destination is a memory address, so this must be an immediate
    
                        int immoffset = 2;
                        if ( 1 == _mod )
                            immoffset += 1;
                        else if ( 2 == _mod || directAddress )
                            immoffset += 2;
            
                        char acI[ 10 ]; // immediate
                        if ( _isword )
                        {
                            sprintf( acI, "%04xh", _pcode[ immoffset ] | ( (uint16_t) _pcode[ 1 + immoffset ] << 8 )  );
                            _bc += 2;
                        }
                        else
                        {
                            sprintf( acI, "%02xh", _pcode[ immoffset ] );
                            _bc += 1;
                        }
                        strcat( ac, acI );
                    }
                    else
                        strcat( ac, getrm( _rm ) );
                }
            }
            else
            {
                strcpy( ac, getrm( _rm ) );
                strcat( ac, ", " );
                strcat( ac, reg_strings[ _reg ] );
            }
        
            return ac;
        } //opargs
        
        const char * opBits()
        {
            uint8_t register_val = ( _b1 >> 3 ) & 0x7;
            return i_opBits[ register_val | ( _isword ? 8 : 0 ) ];
        } //opBits

        void DecodeInstruction( uint8_t * pcode )
        {
            _bc = 1;
            _pcode = pcode;
            _b0 = pcode[0];
            _b1 = pcode[1];
            _b2 = pcode[2];
            _b3 = pcode[3];
            _b4 = pcode[4];
            _b12 = _b1 | ( (uint16_t) _b2 << 8 );
            _b23 = _b2 | ( (uint16_t) _b3 << 8 );
            _b34 = _b3 | ( (uint16_t) _b4 << 8 );
            _reg = ( _b1 >> 3 ) & 0x7;
            _rm = _b1 & 0x7;
            _isword = ( 1 == ( _b0 & 0x1 ) );
            _toreg = ( 2 == ( _b0 & 0x2 ) );
            _mod = ( _b1 >> 6 ) & 0x3;
        } //DecodeInstruction
        
    public:
        CDisassemble8086() : _pcode( 0 ), _bc( 0 ), _b0( 0 ), _b1( 0 ), _b2( 0 ), _b3( 0 ), _b4( 0 ),
                             _b12( 0 ), _b23( 0 ), _b34( 0 ), _reg( 0 ), _rm( 0 ), _mod( 0 ), _isword( false ), _toreg( false )
        {
            // older versions of C++ don't allow class static initializers in their declarations.

            reg_strings[0]  = "al"; reg_strings[1]  = "cl"; reg_strings[2]  = "dl"; reg_strings[3]  = "bl";
            reg_strings[4]  = "ah"; reg_strings[5]  = "ch"; reg_strings[6]  = "dh"; reg_strings[7]  = "bh";
            reg_strings[8]  = "ax"; reg_strings[9]  = "cx"; reg_strings[10] = "dx"; reg_strings[11] = "bx";
            reg_strings[12] = "sp"; reg_strings[13] = "bp"; reg_strings[14] = "si"; reg_strings[15] = "di";

            rm_strings[0] = "bx+si"; rm_strings[1] = "bx+di"; rm_strings[2] = "bp+si"; rm_strings[3] = "bp+di";
            rm_strings[4] = "si";    rm_strings[5] = "di";    rm_strings[6] = "bp";    rm_strings[7] = "bx";

            sr_strings[0] = "es"; sr_strings[1] = "cs"; sr_strings[2] = "ss"; sr_strings[3] = "ds";

            jmp_strings[0]  = "jo "; jmp_strings[1]  = "jno"; jmp_strings[2]  = "jc "; jmp_strings[3]  = "jnc";
            jmp_strings[4]  = "je "; jmp_strings[5]  = "jne"; jmp_strings[6]  = "jbe"; jmp_strings[7]  = "ja ";
            jmp_strings[8]  = "js "; jmp_strings[9]  = "jns"; jmp_strings[10] = "jp "; jmp_strings[11] = "jnp";
            jmp_strings[12] = "jl "; jmp_strings[13] = "jge"; jmp_strings[14] = "jae"; jmp_strings[15] = "jg ";

            i_opBits[0]  = "addb"; i_opBits[1]  = "orb "; i_opBits[2]  = "adcb"; i_opBits[3]  = "sbbb";
            i_opBits[4]  = "andb"; i_opBits[5]  = "subb"; i_opBits[6]  = "xorb"; i_opBits[7]  = "cmpb";
            i_opBits[8]  = "add "; i_opBits[9]  = "or  "; i_opBits[10] = "adc "; i_opBits[11] = "sbb ";
            i_opBits[12] = "and "; i_opBits[13] = "sub "; i_opBits[14] = "xor "; i_opBits[15] = "cmp ";

            i_opRot[0] = "rol"; i_opRot[1] = "ror"; i_opRot[2] = "rcl"; i_opRot[3] = "rcr";
            i_opRot[4] = "sal"; i_opRot[5] = "shr"; i_opRot[6] = "NYI"; i_opRot[7] = "sar";

            i_opMath[0] = "test"; i_opMath[1] = "NYI "; i_opMath[2] = "not "; i_opMath[3] = "neg ";
            i_opMath[4] = "mul "; i_opMath[5] = "imul"; i_opMath[6] = "div "; i_opMath[7] = "idiv";

            i_opMix[0] = "inc "; i_opMix[1] = "dec ";          i_opMix[2] = "call"; i_opMix[3] = "call dword ptr";
            i_opMix[4] = "jmp "; i_opMix[5] = "jmp dword ptr"; i_opMix[6] = "push"; i_opMix[7] = "NYI";
        }

         ~CDisassemble8086() {}
        uint8_t BytesConsumed() { return _bc; } // can be called after Disassemble

        const char * Disassemble( uint8_t * pcode )
        {
            if ( 0 != _pcode && ( _abs64( (uint64_t) pcode - (uint64_t) _pcode ) < 8 ) )
            {
                if ( pcode != ( _pcode + _bc ) )
                    tracer.Trace( "pcode %p, _pcode %p, _bc %02x\n", pcode, _pcode, _bc );
                assert( pcode == ( _pcode + _bc ) );
            }

            static char acOut[ 80 ];
            strcpy( acOut, "NYI" );
            DecodeInstruction( pcode );
            bool handled = true;

            //tracer.TraceQuiet( "{reg %#02x, rm %#02x, isword %d, b12 %04xh}", _reg, _rm, _isword, _b12 );
        
            switch ( _b0 )
            {
                case 0x04: _da( "add    al, %02xh", _b1 ); _bc = 2; break;
                case 0x05: _da( "add    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x06: _da( "push   es" ); break;
                case 0x07: _da( "pop    es" ); break;
                case 0x0c: _da( "or     al, %02xh", _b1 ); _bc = 2; break;
                case 0x0d: _da( "or     ax, %04xh", _b12 ); _bc = 3; break;
                case 0x0e: _da( "push   cs" ); break;
                case 0x14: _da( "adc    al, %02xh", _b1 ); _bc = 2; break;
                case 0x15: _da( "adc    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x16: _da( "push   ss" ); break;
                case 0x17: _da( "pop    ss" ); break;
                case 0x1c: _da( "sbb    al, %02xh", _b1 ); _bc = 2; break;
                case 0x1d: _da( "sbb    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x1e: _da( "push   ds" ); break;
                case 0x1f: _da( "pop    ds" ); break;
                case 0x24: _da( "and    al, %02xh", _b1 ); _bc = 2; break;
                case 0x25: _da( "and    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x26: _da( "es segment override" ); break;
                case 0x27: _da( "daa" ); break;
                case 0x2c: _da( "sub    al, %02xh", _b1 ); _bc = 2; break;
                case 0x2d: _da( "sub    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x2e: _da( "cs segment override" ); break;
                case 0x2f: _da( "das" ); break;
                case 0x34: _da( "xor    al, %02xh", _b1 ); _bc = 2; break;
                case 0x35: _da( "xor    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x36: _da( "ss segment override" ); break;
                case 0x37: _da( "aaa" ); break;
                case 0x3c: _da( "cmp    al, %02xh", _b1 ); _bc = 2; break;
                case 0x3d: _da( "cmp    ax, %04xh", _b12 ); _bc = 3; break;
                case 0x3e: _da( "ds segment override" ); break;
                case 0x3f: _da( "aas" ); break;
                case 0x69: _da( "fint   %02xh", _b1 ); _bc = 2; break; // fake interrupt
                case 0x84: _da( "test   %s", opargs( true ) ); _bc++; break;
                case 0x85: _da( "test   %s", opargs( true ) ); _bc++; break;
                case 0x86: _da( "xchg   %s", opargs( true ) ); _bc++; break;
                case 0x87: _da( "xchg   %s", opargs( true ) ); _bc++; break;
                case 0x8c: _da( "mov    %s, %s", getrmAsWord(), sr_strings[ _reg & 0x3 ] ); _bc++; break;
                case 0x8d: _da( "lea    %s, %s", reg_strings[ 8 | _reg ], getrmAsWord() ); _bc++; break;
                case 0x8e: _da( "mov    %s, %s", sr_strings[ _reg & 0x3 ], getrmAsWord() ); _bc++; break;
                case 0x8f: _da( "pop    %s", getrm( _rm ) ); _bc++; break;
                case 0x90: _da( "nop" ); break;
                case 0x98: _da( "cbw" ); break;
                case 0x99: _da( "cwd" ); break;
                case 0x9a: _da( "call   far ptr  %04xh:%04xh", pcode[3] | ( (uint16_t) pcode[4] << 8 ), _b12 ); _bc += 4; _pcode = 0; break;
                case 0x9b: _da( "wait" ); break;
                case 0x9c: _da( "pushf" ); break;
                case 0x9d: _da( "popf" ); break;
                case 0x9e: _da( "sahf" ); break;
                case 0x9f: _da( "lahf" ); break;
                case 0xa0: _da( "mov    al, byte ptr [%04xh]", _b12 ); _bc = 3; break;
                case 0xa1: _da( "mov    ax, word ptr [%04xh]", _b12 ); _bc = 3; break;
                case 0xa2: _da( "mov    byte ptr [%04xh], al", _b12 ); _bc = 3; break;
                case 0xa3: _da( "mov    word ptr [%04xh], ax", _b12 ); _bc = 3; break;
                case 0xa4: _da( "movsb" ); break;
                case 0xa5: _da( "movsw" ); break;
                case 0xa6: _da( "cmpsb" ); break;
                case 0xa7: _da( "cmpsw" ); break;
                case 0xa8: _da( "test   al, %02xh", _b1 ); _bc = 2; break;
                case 0xa9: _da( "test   ax, %04xh", _b12 ); _bc = 3; break;
                case 0xaa: _da( "stosb" ); break;
                case 0xab: _da( "stosw" ); break;
                case 0xac: _da( "lodsb" ); break;
                case 0xad: _da( "lodsw" ); break;
                case 0xae: _da( "scasb" ); break;
                case 0xaf: _da( "scasw" ); break;
                case 0xc2: _da( "ret    %04xh", _b12 ); _bc = 3; _pcode = 0; break;
                case 0xc3: _da( "ret" ); _pcode = 0; break;
                case 0xc4: _da( "les    %s, [%04xh]", reg_strings[ 8 | _reg ], _b23 ); getrmAsWord(); _bc++; break;
                case 0xc5: _da( "lds    %s, [%04xh]", reg_strings[ 8 | _reg ], _b23 ); getrmAsWord(); _bc++; break;
                case 0xc6: _da( "mov    %s", opargs( false ) ); _bc++; break;
                case 0xc7: _da( "mov    %s", opargs( false ) ); _bc++; break;
                case 0xca: _da( "retf   %04xh", _b12 ); _bc = 3; _pcode = 0; break;
                case 0xcb: _da( "retf"); _pcode = 0; break;
                case 0xcc: _da( "int 3" ); break;
                case 0xcd: _da( "int    %02xh", _b1 ); _bc = 2; break;
                case 0xce: _da( "into" ); break;
                case 0xcf: _da( "iret" ); _pcode = 0; break;
                case 0xd4: _da( "aam" ); _bc = 2; break;
                case 0xd5: _da( "aad" ); _bc = 2; break;
                case 0xd7: _da( "xlat" ); break;
                case 0xe0: _da( "loopnz %02xh", _b1 ); _bc = 2; _pcode = 0; break;
                case 0xe1: _da( "loopz %02xh", _b1 ); _bc = 2; _pcode = 0; break;
                case 0xe2: _da( "loop   %02xh", _b1 ); _bc = 2; _pcode = 0; break;
                case 0xe3: _da( "jcxz   %02xh", _b1 ); _bc = 2; _pcode = 0; break;
                case 0xe4: _da( "in     al, %02xh", _b1 ); _bc = 2; break;
                case 0xe5: _da( "in     ax, %02xh", _b1 ); _bc = 2; break;
                case 0xe6: _da( "out    al, %02xh", _b1 ); _bc = 2; break;
                case 0xe7: _da( "out    ax, %02xh", _b1 ); _bc = 2; break;
                case 0xe8: _da( "call   %04xh", _b12 ); _bc = 3; _pcode = 0; break;
                case 0xe9: _da( "jmp    far %04xh", _b12 ); _bc = 3; _pcode = 0; break;
                case 0xea: _da( "jmp    far %04xh:%04xh", _b12, _b34 ); _bc = 5; _pcode = 0; break;
                case 0xeb: _da( "jmp    short %d", (int) (char ) _b1 ); _bc = 2; _pcode = 0; break;
                case 0xec: _da( "in     al, dx" ); break;
                case 0xed: _da( "in     ax, dx" ); break;
                case 0xf0: _da( "lock" ); break;
                case 0xf2: _da( "repne" ); break;
                case 0xf3: _da( "repe" ); break;
                case 0xf4: _da( "hlt" ); break;
                case 0xf5: _da( "cmc" ); break;
                case 0xf8: _da( "clc    ; (clear carry flag)" ); break;
                case 0xf9: _da( "stc    ; (set carry)" ); break;
                case 0xfa: _da( "cli    ; (clear interrupt flag)" ); break;
                case 0xfb: _da( "sti    ; (set interrupt flag)" ); break;
                case 0xfc: _da( "cld    ; (clear direction flag)" ); break;
                case 0xfd: _da( "std    ; (set direction flag)" ); break;
                default: handled = false; break;
            }
        
            if ( handled )
                return acOut;
        
            if ( _b0 >= 0x40 && _b0 <= 0x4f )
            {
                _da( "%s    %s", ( _b0 <= 0x47 ) ? "inc" : "dec", reg_strings[ 8 + ( _b0 & 0x7 ) ] );
                return acOut;
            }
        
            if ( _b0 >= 0x50 && _b0 <= 0x5f )
            {
                _da( "%s   %s", ( _b0 <= 0x57 ) ? "push" : "pop ", reg_strings[ 8 + ( _b0 & 0x7 ) ] );
                return acOut;
            }

            if ( _b0 >= 0x70 && _b0 <= 0x7f )
            {
                _bc = 2;
                _da( "%s    %d", jmp_strings[ _b0 & 0xf ], (int) (char) _b1 );
                _pcode = 0;
                return acOut;
            }
        
            if ( _b0 >= 0xb0 && _b0 <= 0xbf )
            {
                if ( _b0 <= 0xb7 )
                {
                    _da( "mov    %s, %02xh", reg_strings[ _b0 & 0x7 ], _b1 );
                    _bc = 2;
                }
                else
                {
                    _da( "mov    %s, %04xh", reg_strings[ 8 + ( _b0 & 0x7 ) ], _b12 );
                    _bc = 3;
                }
                return acOut;
            }
        
            if ( _b0 >= 0x91 && _b0 <= 0x97 )
            {
                _da( "xchg   ax, %s", reg_strings[ 8 + ( _b0 & 0x7 ) ] );
                return acOut;
            }

            if ( _b0 >= 0xd8 && _b0 <= 0xde ) // esc
            {
                _bc++;
                _da( "esc    %s", getrm( _rm ) );
                return acOut;
            }
        
            uint8_t top6 = _b0 & 0xfc;
            _bc = 2;
        
            switch( top6 )
            {
                case 0x00: _da( "add    %s", opargs( true ) ); break;
                case 0x08: _da( "or     %s", opargs( true ) ); break;
                case 0x10: _da( "adc    %s", opargs( true ) ); break;
                case 0x18: _da( "sbb    %s", opargs( true ) ); break;
                case 0x20: _da( "and    %s", opargs( true ) ); break;
                case 0x28: _da( "sub    %s", opargs( true ) ); break;
                case 0x30: _da( "xor    %s", opargs( true ) ); break;
                case 0x38: _da( "cmp    %s", opargs( true ) ); break;
                case 0x80: // bitwise/add/sub
                {
                    _da( "%s   ", opBits() );
        
                    bool directAddress = ( 0 == _mod && 6 == _rm );
                    int immoffset = 2;
                    if ( 1 == _mod )
                        immoffset += 1;
                    else if ( 2 == _mod || directAddress )
                        immoffset += 2;

                    if ( _isword )
                    {
                        _bc++;
                        _daa( "%s, ", getrm( _rm ) );
                        if ( 0x83 == _b0 ) // low bit is on, but it's an 8-bit immediate value
                            _daa( "%02xh", pcode[ immoffset ] );
                        else
                        {
                            _daa( "%04xh", ( (uint32_t) pcode[ immoffset ] + ( pcode[ 1 + immoffset ] << 8 ) ) );
                            _bc ++;
                        }
                    }
                    else
                    {
                        _bc += 1;
                        _daa( "%s, ", getrmAsByte() );
                        _daa( "%02xh", pcode[ immoffset ] );
                    }
                    break;
                }
                case 0x88: // mov
                {
                    bool firstArgReg = ( 0x8a == _b0 || 0x8b == _b0 );
                    _da( "mov    %s", opargs( firstArgReg ) );
                    break;
                }
                case 0xd0: // rotates
                {
                    _da( "%s    %s", i_opRot[ _reg ], getrm( _rm ) );
                    _daa( ", %s", _toreg ? "cl" : "1" );
                    break;
                }
                case 0xf4: // mul, imul, div (unsigned), and idiv (signed) have more implicit arguments
                {
                     if ( _reg >= 4 && _reg <= 7 ) // mul, imul, div, idiv
                         _da( "%s   %s", i_opMath[ _reg ], getrm( _rm ) );
                     else if ( 0 == _reg ) // test instructions f6 and f7
                     {
                         _da( "%s   %s, ", i_opMath[ _reg ], getrm( _rm ) );
                         if ( _isword )
                         {
                             uint16_t rhs = pcode[ _bc++ ];
                             rhs |= ( (uint16_t) ( pcode[ _bc++ ] ) << 8 );
                             _daa( "%04xh", rhs );
                         }
                         else
                             _daa( "%02xh", pcode[ _bc++ ] );
                     }
                     else
                         _da( "%s   %s", i_opMath[ _reg ], getrm( _rm ) );
                     break;
                }
                case 0xfc: _da( "%s   %s", i_opMix[ _reg ], getrm( _rm ) ); _pcode = 0; break;
            }

            return acOut;
        }
};          

#undef _da
#undef _daa

