#pragma once

#include <intrin.h>

// when this (undefined) opcode is executed, i8086_invoke_interrupt will be called
const uint8_t i8086_opcode_interrupt = 0x69;

extern uint8_t memory[ 1024 * 1024 ];

// tracking cycles slows execution by >13%
#define I8086_TRACK_CYCLES

struct i8086
{
    uint8_t al() { return * (uint8_t *) & ax; }
    uint8_t ah() { return * ( 1 + (uint8_t *) & ax ); }
    uint8_t bl() { return * (uint8_t *) & bx; }
    uint8_t bh() { return * ( 1 + (uint8_t *) & bx ); }
    uint8_t cl() { return * (uint8_t *) & cx; }
    uint8_t ch() { return * ( 1 + (uint8_t *) & cx ); }
    uint8_t dl() { return * (uint8_t *) & dx; }
    uint8_t dh() { return * ( 1 + (uint8_t *) & dx ); }

    void set_al( uint8_t val ) { * (uint8_t *) & ax = val; }
    void set_bl( uint8_t val ) { * (uint8_t *) & bx = val; }
    void set_cl( uint8_t val ) { * (uint8_t *) & cx = val; }
    void set_dl( uint8_t val ) { * (uint8_t *) & dx = val; }
    void set_ah( uint8_t val ) { * ( 1 + (uint8_t *) & ax ) = val; }
    void set_bh( uint8_t val ) { * ( 1 + (uint8_t *) & bx ) = val; }
    void set_ch( uint8_t val ) { * ( 1 + (uint8_t *) & cx ) = val; }
    void set_dh( uint8_t val ) { * ( 1 + (uint8_t *) & dx ) = val; }

    uint16_t get_ax() { return ax; }
    uint16_t get_bx() { return bx; }
    uint16_t get_cx() { return cx; }
    uint16_t get_dx() { return dx; }
    uint16_t get_si() { return si; }
    uint16_t get_di() { return di; }
    uint16_t get_bp() { return bp; }
    uint16_t get_sp() { return sp; }
    uint16_t get_ip() { return ip; }
    uint16_t get_es() { return es; }
    uint16_t get_cs() { return cs; }
    uint16_t get_ss() { return ss; }
    uint16_t get_ds() { return ds; }

    void set_ax( uint16_t val ) { ax = val; }
    void set_bx( uint16_t val ) { bx = val; }
    void set_cx( uint16_t val ) { cx = val; }
    void set_dx( uint16_t val ) { dx = val; }
    void set_si( uint16_t val ) { si = val; }
    void set_di( uint16_t val ) { di = val; }
    void set_bp( uint16_t val ) { bp = val; }
    void set_sp( uint16_t val ) { sp = val; }
    void set_ip( uint16_t val ) { ip = val; }
    void set_es( uint16_t val ) { es = val; }
    void set_cs( uint16_t val ) { cs = val; }
    void set_ss( uint16_t val ) { ss = val; }
    void set_ds( uint16_t val ) { ds = val; }

    void set_carry( bool f ) { fCarry = f; }
    void set_zero( bool f ) { fZero = f; }
    void set_trap( bool f ) { fTrap = f; }

    bool get_carry() { return fCarry; }
    bool get_zero() { return fZero; }

    // emulator API

    uint64_t emulate( uint64_t maxcycles );             // execute up to about maxcycles
    void external_interrupt( uint8_t interrupt_num );   // invoke this simulated hardware/external interrupt immediately
    void trace_instructions( bool trace );              // enable/disable tracing each instruction
    void trace_state( void );                           // trace the registers
    void end_emulation( void );                         // make the emulator return at the start of the next instruction

    i8086()
    {
        reg_pointers[ 0 ] = & ax;  // al
        reg_pointers[ 1 ] = & cx;  // cl
        reg_pointers[ 2 ] = & dx;  // dl
        reg_pointers[ 3 ] = & bx;  // bl
        reg_pointers[ 4 ] = 1 + (uint8_t *) & ax; // ah
        reg_pointers[ 5 ] = 1 + (uint8_t *) & cx; // ch
        reg_pointers[ 6 ] = 1 + (uint8_t *) & dx; // dh
        reg_pointers[ 7 ] = 1 + (uint8_t *) & bx; // bh
        reg_pointers[ 8 ] = & ax;
        reg_pointers[ 9 ] = & cx;
        reg_pointers[ 10 ] = & dx;
        reg_pointers[ 11 ] = & bx;
        reg_pointers[ 12 ] = & sp;
        reg_pointers[ 13 ] = & bp;
        reg_pointers[ 14 ] = & si;
        reg_pointers[ 15 ] = & di;
    } //i8086

  private:

    uint16_t ax, bx, cx, dx;
    uint16_t si, di, bp, sp, ip;
    uint16_t es, cs, ss, ds;
    uint16_t flags;
    uint8_t prefix_segment_override; // 0xff for none, 0..3 for es, cs, ss, ds
    uint8_t prefix_repeat_opcode;
    void * reg_pointers[ 16 ];

    // bits   0,           2,         4,     6,     7,     8,          9,         10,        11
    bool fCarry, fParityEven, fAuxCarry, fZero, fSign, fTrap, fInterrupt, fDirection, fOverflow;

    void materializeFlags()
    {
        flags = 0;
        if ( fCarry ) flags |= ( 1 << 0 );
        if ( fParityEven ) flags |= ( 1 << 2 );
        if ( fAuxCarry ) flags |=  ( 1 << 4 );
        if ( fZero ) flags |= ( 1 << 6 );
        if ( fSign ) flags |= ( 1 << 7 );
        if ( fTrap ) flags |= ( 1 << 8 );
        if ( fInterrupt ) flags |= ( 1 << 9 );
        if ( fDirection ) flags |= ( 1 << 10 );
        if ( fOverflow ) flags |= ( 1 << 11 );
    } //materializeFlags

    void unmaterializeFlags()
    {
        fCarry = ( 0 != ( flags & ( 1 << 0 ) ) );
        fParityEven = ( 0 != ( flags & ( 1 << 2 ) ) );
        fAuxCarry = ( 0 != ( flags & ( 1 << 4 ) ) );
        fZero = ( 0 != ( flags & ( 1 << 6 ) ) );
        fSign = ( 0 != ( flags & ( 1 << 7 ) ) );
        fTrap = ( 0 != ( flags & ( 1 << 8 ) ) );
        fInterrupt = ( 0 != ( flags & ( 1 << 9 ) ) );
        fDirection = ( 0 != ( flags & ( 1 << 10 ) ) );
        fOverflow = ( 0 != ( flags & ( 1 << 11 ) ) );
    } //unmaterializeFlags

    bool is_parity_even8( uint8_t x ) // unused by apps and expensive to compute.
    {
#ifdef _M_AMD64
        return ( ! ( __popcnt16( x ) & 1 ) ); // less portable, but faster
#else
        return ( ( ~ ( x ^= ( x ^= ( x ^= x >> 4 ) >> 2 ) >> 1 ) ) & 1 );
#endif
    } //is_parity_even8

    void set_PSZ16( uint16_t val )
    {
        fParityEven = ( is_parity_even8( val & 0xff ) == is_parity_even8( ( val >> 8 ) & 0xff ) );
        fZero = ( 0 == val );
        fSign = ( 0 != ( 0x8000 & val ) );
    } //set_PSZ16

    void set_PSZ8( uint8_t val )
    {
        fParityEven = is_parity_even8( val );
        fZero = ( 0 == val );
        fSign = ( 0 != ( 0x80 & val ) );
    } //set_PSZ8

    void reset_carry_overflow() { fCarry = false; fOverflow = false; }

    uint16_t * seg_reg( uint8_t val ) { assert( val <= 3 ); return ( ( & es ) + val ); }
    uint8_t * get_preg8( uint8_t reg ) { return (uint8_t *) reg_pointers[ reg ]; }
    uint16_t * get_preg16( uint8_t reg ) { return (uint16_t * ) reg_pointers[ 8 | reg ]; }
    void * get_preg( uint8_t reg ) { if ( _isword ) return get_preg16( reg ); return get_preg8( reg ); }

    uint16_t get_seg_value( uint16_t default_value, uint64_t & cycles )
    {
        if ( 0xff == prefix_segment_override )
            return default_value;

        AddCycles( cycles, 2 );
        return * seg_reg( prefix_segment_override );
    } //get_seg_value

    uint16_t * get_rm16_ptr( uint64_t & cycles ) // overrides width in opcode when needed
    {
        bool old_isword = _isword;
        _isword = true;
        uint16_t * prmw = (uint16_t *) get_rm_ptr( _rm, cycles );
        _isword = old_isword;
        return prmw;
    } //get_rm16_ptr

    uint8_t * get_rm8_ptr( uint64_t & cycles ) // overrides width in opcode when needed
    {
        bool old_isword = _isword;
        _isword = false;
        uint8_t * prmb = (uint8_t *) get_rm_ptr( _rm, cycles );
        _isword = old_isword;
        return prmb;
    } //get_rm8_ptr

    uint16_t get_displacement( uint8_t rm, uint64_t & cycles )
    {
        assert( rm <= 7 );
        switch ( rm )
        {
            case 0: AddCycles( cycles, 7 ); return bx + si;
            case 1: AddCycles( cycles, 7 ); return bx + di;
            case 2: AddCycles( cycles, 8 ); return bp + si;
            case 3: AddCycles( cycles, 8 ); return bp + di;
            case 4: AddCycles( cycles, 6 ); return si;
            case 5: AddCycles( cycles, 6 ); return di;
            case 6: AddCycles( cycles, 6 ); return bp;
            default: AddCycles( cycles, 6 ); return bx;
        }
    } //get_displacement

    uint16_t get_displacement_seg( uint8_t rm, uint64_t & cycles )
    {
        if ( 0xff != prefix_segment_override )
        {
            AddCycles( cycles, 2 );
            return * seg_reg( prefix_segment_override );
        }

        if ( 2 == rm || 3 == rm || 6 == rm ) // bp defaults to ss. see the function directly above for more.
            return ss;

        return ds;
    } // get_displacement_seg

    void * get_rm_ptr( uint8_t rm_to_use, uint64_t & cycles )
    {
        assert( _mod <= 4 );
        if ( 3 == _mod )
            return reg_pointers[ rm_to_use | ( _isword ? 8 : 0 ) ];
        
        rm_to_use &= 0x7; // mask away the higher bit that may be set for register lookups

        if ( 1 == _mod )
        {
            _bc += 1;
            int offset = (int) (char) _pcode[ 2 ];
            uint16_t regval = get_displacement( rm_to_use, cycles );
            uint16_t segment = get_displacement_seg( rm_to_use, cycles );
            return memory + flatten( segment, regval + offset );
        }

        if ( 2 == _mod )
        {
            _bc += 2;
            AddCycles( cycles, 5 );
            uint16_t offset = _pcode[2] + ( (uint16_t) _pcode[3] << 8 );
            uint16_t regval = get_displacement( rm_to_use, cycles );
            uint16_t segment = get_displacement_seg( rm_to_use, cycles );
            return memory + flatten( segment, regval + offset );
        }

        if ( 0x6 == rm_to_use )  // 0 == mod. least frequent
        {
            _bc += 2;
            AddCycles( cycles, 5 );
            return memory + flatten( get_seg_value( ds, cycles ), ( (uint32_t) _pcode[ 2 ] + ( (uint32_t) _pcode[ 3 ] << 8 ) ) );
        }

        uint16_t val = get_displacement( rm_to_use, cycles );
        uint16_t segment = get_displacement_seg( rm_to_use, cycles );
        return memory + flatten( segment, val );
    } //get_rm_ptr

    uint16_t get_rm_ea( uint8_t rm_to_use, uint64_t & cycles ) // effective address. used in 1 place.
    {
        assert( _mod <= 4 );
        if ( 3 == _mod )
        {
            void * pval = reg_pointers[ rm_to_use | ( _isword ? 8 : 0 ) ];
            return _isword ? * (uint16_t *) pval : * (uint8_t *) pval;
        }
        
        rm_to_use &= 0x7; // mask away the higher bit that may be set for register lookups

        if ( 1 == _mod )
        {
            _bc += 1;
            int16_t offset = (int16_t) (int8_t) _pcode[ 2 ]; // cast for sign extension
            uint16_t regval = get_displacement( rm_to_use, cycles );
            return regval + offset;
        }
 
        if  ( 2 == _mod )
        {
            _bc += 2;
            uint16_t offset = _pcode[2] + ( (uint16_t) ( _pcode[3] ) << 8 );
            uint16_t regval = get_displacement( rm_to_use, cycles );
            return regval + offset;
        }

        if ( 0x6 == rm_to_use )  // 0 == mod. least frequent
        {
            _bc += 2;
            return (uint16_t) _pcode[ 2 ] + ( ( (uint16_t) _pcode[ 3 ] ) << 8 );
        }

        return get_displacement( rm_to_use, cycles );
    } //get_rm_ea

    void * get_op_args( bool firstArgReg, uint16_t & rhs, uint64_t & cycles )
    {
        if ( _isword )
            _reg |= 0x8;

        if ( toreg() )
        {
            if ( firstArgReg )
            {
                void * pin = get_rm_ptr( _rm, cycles );
                if ( _isword )
                    rhs = * (uint16_t *) pin;
                else
                    rhs = * (uint8_t *) pin;
                return get_preg( _reg );
            }

            bool secondArgReg = ( 3 == _mod );
            void * pdst = get_rm_ptr( secondArgReg ? _reg : _rm, cycles );
            if ( !secondArgReg )
            {
                bool directAddress = ( 0 == _mod && 6 == _rm );
                int immoffset = 2;
                if ( 1 == _mod )
                    immoffset += 1;
                else if ( 2 == _mod || directAddress )
                    immoffset += 2;
        
                if ( _isword )
                {
                    rhs = _pcode[ immoffset ] | ( ( (uint16_t) _pcode[ 1 + immoffset ] ) << 8 ) ;
                    _bc += 2;
                }
                else
                {
                    rhs = _pcode[ immoffset ];
                    _bc += 1;
                }
            }
            else
                rhs = _isword ? ( * get_preg16( _rm ) ) : ( * get_preg8( _rm ) );

            return pdst;
        }

        if ( _isword )
            rhs = * get_preg16( _reg );
        else
            rhs = * get_preg8( _reg );

        return get_rm_ptr( _rm, cycles );
    } //get_op_args
    
    const char * render_flags() // show the subset actually used with any frequency
    {
        static char acflags[10] = {0};
        size_t next = 0;
        acflags[ next++ ] = fCarry ? 'C' : 'c';
        acflags[ next++ ] = fZero ? 'Z' : 'z';
        acflags[ next++ ] = fSign ? 'S' : 's';
        acflags[ next++ ] = fDirection ? 'D' : 'd';
        acflags[ next++ ] = fOverflow ? 'O' : 'o';
        acflags[ next ] = 0;
        return acflags;
    } //render_flags

    uint8_t * memptr( uint32_t address ) { return memory + address; }
    uint32_t flatten( uint16_t seg, uint16_t offset ) { return ( ( (uint32_t) seg ) << 4 ) + offset; }
    uint32_t flat_ip() { return flatten( cs, ip ); }
    uint8_t * flat_address8( uint16_t seg, uint16_t offset ) { return memory + flatten( seg, offset ); }
    uint16_t * flat_address16( uint16_t seg, uint16_t offset ) { return (uint16_t *) ( memory + flatten( seg, offset ) ); }
    uint16_t mword( uint16_t seg, uint16_t offset ) { return * ( (uint16_t *) & memory[ flatten( seg, offset ) ] ); }
    void setmword( uint16_t seg, uint16_t offset, uint16_t value ) { * (uint16_t *) & memory[ flatten( seg, offset ) ] = value; }

    // state used for instruction decoding

    uint8_t * _pcode; // pointer to the first opcode
    uint8_t _bc;      // # of bytes consumed by currenly running instruction
    uint8_t _b0;      // pcode[ 0 ] -- the first opcode
    uint8_t _b1;      // pcode[ 1 ]
    uint16_t _b12;    // pcode[1] and pcode[2] as a little-endian word
    uint8_t _mod;     // bits 7:6 of _b1
    uint8_t _reg;     // bits 5:3 of _b1
    uint8_t _rm;      // bits 2:0 of _b1
    bool _isword;     // true if bit 0 of _b0 is 1

    void decode_instruction( uint8_t * pcode )
    {
        _bc = 1;
        _pcode = pcode;
        _b0 = pcode[0];
        _b1 = pcode[1];
        _b12 = _b1 | ( (uint16_t) pcode[2] << 8 );
        _mod = ( _b1 >> 6 ) & 3;
        _reg = ( _b1 >> 3 ) & 7;
        _rm = _b1 & 7;
        _isword = ( _b0 & 1 );
    } //decode_instruction

    bool toreg() { return ( 2 == ( _b0 & 2 ) ); } // decode on the fly since it's rarely used

    void trace_decode()
    {
        tracer.Trace( "  decoded as _mod %02x, reg %02x, _rm %02x, _isword %d, toreg %d, segment override %d\n",
                      _mod, _reg, _rm, _isword, toreg(), prefix_segment_override );
    }

    void do_math8( uint8_t math, uint8_t * psrc, uint8_t rhs );
    void do_math16( uint8_t math, uint16_t * psrc, uint16_t rhs );
    uint8_t op_sub8( uint8_t lhs, uint8_t rhs, bool borrow = false );
    uint8_t op_add8( uint8_t lhs, uint8_t rhs, bool carry = false );
    uint8_t op_and8( uint8_t lhs, uint8_t rhs );
    uint8_t op_or8( uint8_t lhs, uint8_t rhs );
    uint8_t op_xor8( uint8_t lhs, uint8_t rhs );
    uint16_t op_sub16( uint16_t lhs, uint16_t rhs, bool borrow = false );
    uint16_t op_add16( uint16_t lhs, uint16_t rhs, bool carry = false );
    uint16_t op_and16( uint16_t lhs, uint16_t rhs );
    uint16_t op_or16( uint16_t lhs, uint16_t rhs );
    uint16_t op_xor16( uint16_t lhs, uint16_t rhs );
    void op_rol8( uint8_t * pval, uint8_t shift );
    void op_ror8( uint8_t * pval, uint8_t shift );
    void op_rcl8( uint8_t * pval, uint8_t shift );
    void op_rcr8( uint8_t * pval, uint8_t shift );
    void op_sal8( uint8_t * pval, uint8_t shift );
    void op_shr8( uint8_t * pval, uint8_t shift );
    void op_sar8( uint8_t * pval, uint8_t shift );
    void op_rol16( uint16_t * pval, uint8_t shift );
    void op_ror16( uint16_t * pval, uint8_t shift );
    void op_rcl16( uint16_t * pval, uint8_t shift );
    void op_rcr16( uint16_t * pval, uint8_t shift );
    void op_sal16( uint16_t * pval, uint8_t shift );
    void op_shr16( uint16_t * pval, uint8_t shift );
    void op_sar16( uint16_t * pval, uint8_t shift );
    void op_cmps8( uint64_t & cycles );
    void op_sto8();
    void op_lods8( uint64_t & cycles );
    void op_scas8( uint64_t & cycles );
    void op_movs8( uint64_t & cycles );
    void op_cmps16( uint64_t & cycles );
    void op_sto16();
    void op_lods16( uint64_t & cycles );
    void op_scas16( uint64_t & cycles );
    void op_movs16( uint64_t & cycles );
    void update_index16( uint16_t & index_register );
    void update_index8( uint16_t & index_register );
    void update_rep_sidi8();
    void update_rep_sidi16();
    uint8_t op_inc8( uint8_t val );
    uint8_t op_dec8( uint8_t val );
    uint16_t op_inc16( uint16_t val );
    uint16_t op_dec16( uint16_t val );
    void op_interrupt( uint8_t instruction_length );
    void op_rotate8( uint8_t * pval, uint8_t operation, uint8_t amount );
    void op_rotate16( uint16_t * pval, uint8_t operation, uint8_t amount );

    void push( uint16_t val )
    {
        sp -= 2;
        setmword( ss, sp, val );
    } //push

    uint16_t pop()
    {
        uint16_t val = mword( ss, sp );
        sp += 2;
        return val;
    } //pop

    #ifdef I8086_TRACK_CYCLES
        void AddCycles( uint64_t & cycles, uint8_t amount ) { cycles += amount; }
        void AddMemCycles( uint64_t & cycles, uint8_t amount ) { if ( 3 != _mod ) cycles += amount; }
    #else
        void AddCycles( uint64_t & cycles, uint8_t amount ) {}
        void AddMemCycles( uint64_t & cycles, uint8_t amount ) {}
    #endif

}; //i8086

extern i8086 cpu;

// callbacks when instructions are executed

extern void i8086_invoke_interrupt( uint8_t interrupt ); // called by default for all interrupts
extern void i8086_invoke_halt();                         // called when the HLT instruction is executed
extern uint8_t i8086_invoke_in_al( uint16_t port );      // called for the instructions: in al, dx and in al, im8
extern uint16_t i8086_invoke_in_ax( uint16_t port );     // called for the instructions: in ax, dx and in ax, im8
