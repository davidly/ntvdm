#pragma once

#include <bitset>
using namespace std;

// when this (undefined) opcode is executed, i8086_invoke_interrupt will be called
const uint8_t i8086_opcode_interrupt = 0x69;

extern uint8_t memory[ 0x10fff0 ]; // the 8086 can address ffff:ffff bytes, which is 0..0x10ffef

// tracking cycles slows execution by >6%
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
    void set_interrupt( bool f ) { fInterrupt = f; }

    bool get_carry() { return fCarry; }
    bool get_zero() { return fZero; }
    bool get_trap() { return fTrap; }
    bool get_interrupt() { return fInterrupt; }

    // emulator API

    uint64_t emulate( uint64_t maxcycles );             // execute up to about maxcycles
    void exit_emulate_early( void );                    // tell the emulator not to wait for maxcycles to return
    bool external_interrupt( uint8_t interrupt_num );   // invoke this simulated hardware/external interrupt immediately
    void trace_instructions( bool trace );              // enable/disable tracing each instruction
    void trace_state( void );                           // trace the registers
    void end_emulation( void );                         // make the emulator return at the start of the next instruction

#ifndef NDEBUG
    uint8_t trace_opcode_usage( void );                    // trace trends in opcode usage
#endif

    i8086() : ax( 0 ), bx( 0 ), cx( 0 ), dx(0 ), si( 0 ), di( 0 ), bp( 0 ), sp( 0 ), ip( 0 ),
              es( 0 ), cs( 0 ), ss( 0 ), ds( 0 ), flags( 0 ),
              prefix_segment_override( 0xff ), prefix_repeat_opcode( 0xff ),
              _pcode( 0 ), _bc( 0 ), _b0( 0 ), _b1( 0 ), _mod( 0 ), _reg( 0 ), _rm( 0 ),
              fCarry( false ), fParityEven( false ), fAuxCarry( false ), fZero( false ), fSign( false ),
              fTrap( false ), fInterrupt( false ), fDirection( false ), fOverflow( false ), fIgnoreTrap( false ), cycles( 0 )
    {
        reg8_pointers[ 0 ] = (uint8_t *) & ax;  // al
        reg8_pointers[ 1 ] = (uint8_t *) & cx;  // cl
        reg8_pointers[ 2 ] = (uint8_t *) & dx;  // dl
        reg8_pointers[ 3 ] = (uint8_t *) & bx;  // bl
        reg8_pointers[ 4 ] = 1 + (uint8_t *) & ax; // ah
        reg8_pointers[ 5 ] = 1 + (uint8_t *) & cx; // ch
        reg8_pointers[ 6 ] = 1 + (uint8_t *) & dx; // dh
        reg8_pointers[ 7 ] = 1 + (uint8_t *) & bx; // bh

        reg16_pointers[ 0 ] = & ax;
        reg16_pointers[ 1 ] = & cx;
        reg16_pointers[ 2 ] = & dx;
        reg16_pointers[ 3 ] = & bx;
        reg16_pointers[ 4 ] = & sp;
        reg16_pointers[ 5 ] = & bp;
        reg16_pointers[ 6 ] = & si;
        reg16_pointers[ 7 ] = & di;
    } //i8086

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

  private:
    // the code assumes relative positions of most of these member variables. they can't easily be moved around.

    uint16_t ax, bx, cx, dx;
    uint16_t si, di, bp, sp, ip;
    uint16_t es, cs, ss, ds;
    uint16_t flags;
    uint8_t prefix_segment_override; // 0xff for none, 0..3 for es, cs, ss, ds
    uint8_t prefix_repeat_opcode;    // 0xff for none, f2 repne/repnz, f3 rep/repe/repz

    // bits   0,           2,         4,     6,     7,     8,          9,         10,        11
    bool fCarry, fParityEven, fAuxCarry, fZero, fSign, fTrap, fInterrupt, fDirection, fOverflow;
    bool fIgnoreTrap;

    // state used for instruction decoding. these start with underscore to differentiate them

    uint8_t _bc;      // # of bytes consumed by the currently running instruction
    uint8_t _b0;      // pcode[ 0 ] -- the first opcode of the currently running instruction
    uint8_t _b1;      // pcode[ 1 ]
    uint8_t _rm;      // bits 2:0 of _b1. register or memory
    uint8_t _reg;     // bits 5:3 of _b1. register (generally, but also math in some cases)
    uint8_t _mod;     // bits 7:6 of _b1
    uint8_t * _pcode; // pointer to the first opcode currently executing

    uint8_t * reg8_pointers[ 8 ];
    uint16_t * reg16_pointers[ 8 ];
    uint64_t cycles;  // # of cycles executed so far during a call to emulate()

    void decode_instruction( uint8_t * pcode )
    {
        _bc = 1;
        _pcode = pcode;
        * (uint16_t *) & _b0 = * (uint16_t *) pcode;
        _rm = ( _b1 & 7 );
        _reg = ( ( _b1 >> 3 ) & 7 );
        _mod = ( _b1 >> 6 );
    } //decode_instruction

    bool isword() { return ( _b0 & 1 ); } // true if the instruction is dealing with a word, not a byte (there are several exceptions)
    bool toreg() { return ( _b0 & 2 ); } // decode on the fly since it's rarely used
    uint16_t b12() { return * (uint16_t *) ( _pcode + 1 ); } // bytes 1 and 2 from the start of the opcode
    uint16_t b34() { return * (uint16_t *) ( _pcode + 3 ); } // bytes 3 and 4 from the start of the opcode

    void trace_decode()
    {
        tracer.Trace( "  decoded as _mod %02x, reg %02x, _rm %02x, isword %d, toreg %d, segment override %d\n",
                      _mod, _reg, _rm, isword(), toreg(), prefix_segment_override );
    } //trace_decode

    void materializeFlags()
    {
        flags = 0xf002; // these bits are meaningless, but always turned on on real hardware
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
        return ( ! ( __popcnt16( x ) & 1 ) ); // less portable, but faster. Not on Q9650 CPU
#elif defined( __APPLE__ )
        return ( ! ( std::bitset<8>( x ).count() & 1 ) );
#else
        return ( ( ~ ( x ^= ( x ^= ( x ^= x >> 4 ) >> 2 ) >> 1 ) ) & 1 );
#endif
    } //is_parity_even8

    void set_PSZ16( uint16_t val )
    {
        fParityEven = is_parity_even8( (uint8_t) val ); // only the lower 8 bits are used to determine parity on the 8086
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
    uint8_t * get_preg8( uint8_t reg ) { assert( reg <= 7 ); return reg8_pointers[ reg ]; }
    uint16_t * get_preg16( uint8_t reg ) { assert( reg <= 7 ); return reg16_pointers[ reg ]; }

    uint16_t get_seg_value()
    {
        if ( 0xff == prefix_segment_override )
            return ds; // the default if there is no override

        AddCycles( 2 );
        return * seg_reg( prefix_segment_override );
    } //get_seg_value

    uint32_t flatten( uint16_t seg, uint16_t offset )
    {
        #ifdef NDEBUG
            return ( ( (uint32_t) seg ) << 4 ) + offset;
        #else
            uint32_t flat = ( ( (uint32_t) seg ) << 4 ) + offset;
            //if ( flat < 0x600 )
            //    tracer.Trace( "referencing low-memory %#x\n", flat );
            //if ( flat < 0xa00 )
            //    tracer.Trace( "referencing lowish-memory %#x\n", flat );
            //if ( flat >= 0xb8000 && flat < 0xb8fa0 )
            //    tracer.Trace( "referencing cga-memory page 0 %#x row %d column %d\n", flat, (flat - 0xb8000) / 160, ( (flat - 0xb8000) % 160 ) / 2 );
            //if ( flat >= 0xb8fa0 && flat < 0xbbe80 )
            //    tracer.Trace( "referencing cga-memory page 1 %#x\n", flat );
            //if ( flat >= 0xbbe80 )
            //    tracer.Trace( "referencing high-memory %#x\n", flat );
            return flat;
        #endif
    } //flatten

    void unhandled_instruction();
    void * flat_address( uint16_t seg, uint16_t offset ) { return memory + flatten( seg, offset ); }
    uint8_t * flat_address8( uint16_t seg, uint16_t offset ) { return (uint8_t *) flat_address( seg, offset ); }
    uint16_t * flat_address16( uint16_t seg, uint16_t offset ) { return (uint16_t *) flat_address( seg, offset ); }
    uint16_t mword( uint16_t seg, uint16_t offset ) { return * flat_address16( seg, offset ); }
    void setmword( uint16_t seg, uint16_t offset, uint16_t value ) { * flat_address16( seg, offset ) = value; }

    uint16_t get_displacement()
    {
        assert( _rm <= 7 );
        switch ( _rm )
        {
            case 0: AddCycles( 7 ); return bx + si;
            case 1: AddCycles( 7 ); return bx + di;
            case 2: AddCycles( 8 ); return bp + si;
            case 3: AddCycles( 8 ); return bp + di;
            case 4: AddCycles( 6 ); return si;
            case 5: AddCycles( 6 ); return di;
            case 6: AddCycles( 6 ); return bp;
            case 7: AddCycles( 6 ); return bx;
        }

        assume_false;
    } //get_displacement

    uint16_t get_displacement_seg()
    {
        if ( 0xff == prefix_segment_override ) // if no segment override
        {
            if ( 2 == _rm || 3 == _rm || 6 == _rm ) // bp defaults to ss. see get_displacement(): 2/3/6 use bp
                return ss;

            return ds;
        }

        AddCycles( 2 );
        return * seg_reg( prefix_segment_override );
    } //get_displacement_seg

    void * get_rm_ptr_common()
    {
        assert( _mod <= 2 );

        if ( 1 == _mod ) // 1-byte signed immediate offset from register(s)
        {
            _bc += 1;
            AddCycles( 4 );
            int16_t offset = (int16_t) (int8_t) _pcode[ 2 ];
            return flat_address( get_displacement_seg(), get_displacement() + offset );
        }

        if ( 2 == _mod ) // 2-byte unsigned immediate offset from register(s)
        {
            _bc += 2;
            AddCycles( 5 );
            uint16_t offset = * (uint16_t *) ( _pcode + 2 );
            return flat_address( get_displacement_seg(), get_displacement() + offset );
        }

        if ( 6 == _rm )  // 0 == mod. least frequent. immediate pointer to offset
        {
            _bc += 2;
            AddCycles( 5 );
            return flat_address( get_seg_value(), * (uint16_t *) ( _pcode + 2 ) );
        }

        return flat_address( get_displacement_seg(), get_displacement() );// no offset; just a value from register(s)
    } //get_rm_ptr_common

    uint16_t * get_rm_ptr16()
    {
        // these instructions are even yet operate on words: mov r16/m16, sreg; mov sreg, reg16/mem16; les reg16, [mem16]
        assert( isword() || ( 0x8c == _b0 ) || ( 0x8e == _b0 ) || ( 0xc4 == _b0 ) );

        if ( 3 == _mod )
            return get_preg16( _rm );
        
        return (uint16_t *) get_rm_ptr_common();
    } //get_rm_ptr16

    uint8_t * get_rm_ptr8()
    {
        assert( !isword() );
        if ( 3 == _mod )
            return get_preg8( _rm );
        
        return (uint8_t *) get_rm_ptr_common();
    } //get_rm_ptr8

    uint16_t get_rm_ea() // effective address. used strictly for lea
    {
        assert( isword() );
        assert( 0x8d == _b0 ); // it's lea
        assert( _mod <= 2 ); // lea specifies that the source operand must be memory, not a register
        
        if ( 1 == _mod )
        {
            _bc += 1;
            int16_t offset = (int16_t) (int8_t) _pcode[ 2 ]; // cast for sign extension
            return get_displacement() + offset;
        }
 
        if  ( 2 == _mod )
        {
            _bc += 2;
            uint16_t offset = * (uint16_t *) ( _pcode + 2 );
            return get_displacement() + offset;
        }

        if ( 6 == _rm )  // 0 == mod. least frequent
        {
            _bc += 2;
            return * (uint16_t *) ( _pcode + 2 );
        }

        return get_displacement();
    } //get_rm_ea

    uint16_t * get_op_args16( uint16_t & rhs )
    {
        assert( isword() );
        if ( toreg() )
        {
            rhs = * get_rm_ptr16();
            return get_preg16( _reg );
        }

        rhs = * get_preg16( _reg );
        return get_rm_ptr16();
    } //get_op_args16
    
    uint8_t * get_op_args8( uint8_t & rhs )
    {
        assert( !isword() );
        if ( toreg() )
        {
            rhs = * get_rm_ptr8();
            return get_preg8( _reg );
        }

        rhs = * get_preg8( _reg );
        return get_rm_ptr8();
    } //get_op_args8
    
    const char * render_flags() // show the subset actually used with any frequency
    {
        static char acflags[13] = {0};
        size_t next = 0;
        acflags[ next++ ] = fOverflow ? 'O' : 'o';
        acflags[ next++ ] = fDirection ? 'D' : 'd';
        acflags[ next++ ] = fInterrupt ? 'I' : 'i';
        acflags[ next++ ] = fTrap ? 'T' : 't';
        acflags[ next++ ] = fSign ? 'S' : 's';
        acflags[ next++ ] = fZero ? 'Z' : 'z';
        acflags[ next++ ] = fAuxCarry ? 'A' : 'a';
        acflags[ next++ ] = fParityEven ? 'P' : 'p';
        acflags[ next++ ] = fCarry ? 'C' : 'c';
        acflags[ next ] = 0;
        return acflags;
    } //render_flags

    bool handle_state();
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
    void op_cmps8();
    void op_sto8();
    void op_lods8();
    void op_scas8();
    void op_movs8();
    void op_cmps16();
    void op_sto16();
    void op_lods16();
    void op_scas16();
    void op_movs16();
    void update_index16( uint16_t & index_register );
    void update_index8( uint16_t & index_register );
    void update_rep_sidi8();
    void update_rep_sidi16();
    uint8_t op_inc8( uint8_t val );
    uint8_t op_dec8( uint8_t val );
    uint16_t op_inc16( uint16_t val );
    uint16_t op_dec16( uint16_t val );
    void op_interrupt( uint8_t interrupt_num, uint8_t instruction_length );
    void op_rotate8( uint8_t * pval, uint8_t operation, uint8_t amount );
    void op_rotate16( uint16_t * pval, uint8_t operation, uint8_t amount );
    void op_daa();
    void op_das();
    void op_aas();
    void op_aaa();
    void op_sahf();
    void op_lahf();
    bool op_f6();
    bool op_f7();
    bool op_ff();

    #ifdef I8086_TRACK_CYCLES
        void AddCycles( uint8_t amount ) { cycles += amount; }
        void AddMemCycles( uint8_t amount ) { if ( 3 != _mod ) cycles += amount; else if ( !toreg() ) cycles += 21; }
    #else
        void AddCycles( uint8_t amount ) {}
        void AddMemCycles( uint8_t amount ) {}
    #endif

}; //i8086

extern i8086 cpu;

// callbacks when instructions are executed

extern void i8086_invoke_interrupt( uint8_t interrupt );          // called by default for all interrupts
extern void i8086_invoke_halt();                                  // called when the HLT instruction is executed
extern uint8_t i8086_invoke_in_al( uint16_t port );               // called for the instructions: in al, dx and in al, im8
extern uint16_t i8086_invoke_in_ax( uint16_t port );              // called for the instructions: in ax, dx and in ax, im8
extern void i8086_invoke_out_al( uint16_t port, uint8_t val );    // called for the instructions: out al, dx and out al, im8
extern void i8086_invoke_out_ax( uint16_t port, uint16_t val );   // called for the instructions: out ax, dx and out ax, im8
extern void i8086_hard_exit( const char * pcerror, uint8_t arg ); // called for fatal errors
