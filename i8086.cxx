// 8086 emulator
// Written by David Lee in late 2022.
// Useful: http://bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf
//         https://www.eeeguide.com/8086-instruction-format/
//         https://www.felixcloutier.com/x86
//         https://onlinedisassembler.com/odaweb/
//         https://www2.math.uni-wuppertal.de/~fpf/Uebungen/GdR-SS02/opcode_i.html
//         https://www.pcjs.org/documents/manuals/intel/8086/
// Cycle counts are approximate -- within 25% of actual values. It doesn't account for misalignment,
// ignores some immediate vs. reg cases where the difference is 1 cycle, gets div/mult approximately,
// and doesn't handle many other cases. Also, various 8086 tech documents don't have consistent counts.
// I tested cycle counts against physical 80186 and 8088 machines. It's somewhere in between.

#include <djl_os.hxx>

#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <djltrace.hxx>
#include <djl8086d.hxx>

using namespace std;

#include "i8086.hxx"

uint8_t memory[ 1024 * 1024 ];
i8086 cpu;
static CDisassemble8086 g_Disassembler;
static uint32_t g_State = 0;

const uint32_t stateTraceInstructions = 1;
const uint32_t stateEndEmulation = 2;
const uint32_t stateExitEmulateEarly = 4;
const uint32_t stateTrapSet = 8;

void i8086::trace_instructions( bool t ) { if ( t ) g_State |= stateTraceInstructions; else g_State &= ~stateTraceInstructions; }
void i8086::end_emulation() { g_State |= stateEndEmulation; }
void i8086::exit_emulate_early() { g_State |= stateExitEmulateEarly; }
static void trap_set() { g_State |= stateTrapSet; }

bool i8086::external_interrupt( uint8_t interrupt_num )
{
    if ( fInterrupt && !fTrap )
    {
        op_interrupt( interrupt_num, 0 ); // 0 since it's not in the instruction stream
        return true;
    }

    return false;
} //external_interrupt

void i8086::unhandled_instruction()
{
    i8086_hard_exit( "unhandled 8086 instruction %02x\n", _b0 );
} //unhandled_instruction

void i8086::trace_state()
{
//    tracer.Trace( "es: [di+8] 309c: [8] " ); tracer.TraceBinaryData( memory + flatten( 0x309c, 8 ), 2, 0 );
//    tracer.Trace( "bp-2 1f13:ad04-2 " ); tracer.TraceBinaryData( memory + flatten( 0x1f13, 0xad04 - 2 ), 2, 0 );
//    tracer.Trace( "x594 + 24: " ); tracer.TraceBinaryData( memory + flatten( 0x3c9b, 0x594 + 24 ), 4, 0 );
//    tracer.TraceBinaryData( memory + flatten( 0, 4 * 0x1c ), 4, 0 );

    uint8_t * pcode = flat_address8( cs, ip );
    const char * pdisassemble = g_Disassembler.Disassemble( pcode );
    tracer.TraceQuiet( "ip %4x, opc %02x %02x %02x %02x %02x, ax %04x, bx %04x, cx %04x, dx %04x, di %04x, "
                       "si %04x, ds %04x, es %04x, cs %04x, ss %04x, bp %04x, sp %04x, %s, %s ; %u\n",
                       ip, pcode[0], pcode[1], pcode[2], pcode[3], pcode[4],
                       ax, bx, cx, dx, di, si, ds, es, cs, ss, bp, sp,
                       render_flags(), pdisassemble, g_Disassembler.BytesConsumed() );
} //trace_state

// base cycle count per opcode; will be higher for multi-byte instructions, memory references,
// ea calculations, jumps taken, loops taken, rotate cl-times, and reps
const uint8_t i8086_cycles[ 256 ] =
{
    /*00*/     3,  3,  3,  3,  4,  4, 14, 12,    3,  3,  3,  3,  4,  4, 14,  0,
    /*10*/     3,  3,  3,  3,  4,  4, 14, 12,    3,  3,  3,  3,  4,  4, 14, 12,
    /*20*/     3,  3,  3,  3,  4,  4,  2,  4,    3,  3,  3,  3,  4,  4,  2,  4,
    /*30*/     3,  3,  3,  3,  4,  4,  2,  4,    3,  3,  3,  3,  4,  4,  2,  4,
    /*40*/     3,  3,  3,  3,  3,  3,  3,  3,    3,  3,  3,  3,  3,  3,  3,  3,
    /*50*/    15, 15, 15, 15, 15, 15, 15, 15,   12, 12, 12, 12, 12, 12, 12, 12,
    /*60*/     0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,  0,  0,  0,  0,  0,
    /*70*/     4,  4,  4,  4,  4,  4,  4,  4,    4,  4,  4,  4,  4,  4,  4,  4,
    /*80*/     4,  4,  4,  4,  5,  5,  4,  4,    2,  2,  2,  2,  2,  4,  2, 12, // lea as 4, not 2; docs can't be right
    /*90*/     4,  4,  4,  4,  4,  4,  4,  4,    2,  5, 36,  4, 14, 12,  4,  4,
    /*a0*/    14, 14, 14, 14, 18, 26, 30, 30,    5,  5, 11, 15, 16, 16, 19, 19,
    /*b0*/     4,  4,  4,  4,  4,  4,  4,  4,    4,  4,  4,  4,  4,  4,  4,  4,
    /*c0*/     0,  0, 24, 20, 24, 24, 14, 14,    0,  0, 33, 34, 72, 71,  4, 44,
    /*d0*/     2,  2,  8,  8, 83, 60, 11,  0,    0,  0,  0,  0,  0,  0,  0,  0,
    /*e0*/     6,  5,  5,  6, 14, 14, 14, 14,   23, 15, 15, 15, 12, 12, 12, 12,
    /*f0*/     1,  0,  9,  9,  2,  3,  5,  5,    2,  2,  2,  2,  2,  2,  3,  2,
};

void i8086::update_index8( uint16_t & index_register ) // si or di
{
    if ( fDirection )
        index_register--;
    else
        index_register++;
} //update_index16

void i8086::update_index16( uint16_t & index_register ) // si or di
{
    if ( fDirection )
        index_register -= 2;
    else
        index_register += 2;
} //update_index16

void i8086::update_rep_sidi8()
{
    update_index8( si );
    update_index8( di );
} //update_rep_sidi8

void i8086::update_rep_sidi16()
{
    update_index16( si );
    update_index16( di );
} //update_rep_sidi16

force_inlined uint8_t i8086::op_sub8( uint8_t lhs, uint8_t rhs, bool borrow )
{
    uint8_t com_rhs = ~rhs; // com == ones-complement
    uint8_t borrow_int = borrow ? 0 : 1;
    uint16_t res16 = (uint16_t) lhs + (uint16_t) com_rhs + (uint16_t) borrow_int;
    uint8_t res8 = res16 & 0xff;
    fCarry = ( 0 == ( res16 & 0x100 ) );
    set_PSZ8( res8 );
    fOverflow = ( ! ( ( lhs ^ com_rhs ) & 0x80 ) ) && ( ( lhs ^ res8 ) & 0x80 );
    fAuxCarry = ( 0 != ( ( ( lhs & 0xf ) - ( rhs & 0xf ) - ( borrow ? 1 : 0 ) ) & ~0xf ) );
    return res8;
} //op_sub8

force_inlined uint16_t i8086::op_sub16( uint16_t lhs, uint16_t rhs, bool borrow )
{
    uint16_t com_rhs = ~rhs; // com == ones-complement
    uint16_t borrow_int = borrow ? 0 : 1;
    uint32_t res32 = (uint32_t) lhs + (uint32_t) com_rhs + (uint32_t) borrow_int;
    uint16_t res16 = res32 & 0xffff;
    fCarry = ( 0 == ( res32 & 0x10000 ) );
    set_PSZ16( res16 );
    fOverflow = ( ! ( ( lhs ^ com_rhs ) & 0x8000 ) ) && ( ( lhs ^ res16 ) & 0x8000 );
    fAuxCarry = ( 0 != ( ( ( lhs & 0xf ) - ( rhs & 0xf ) - ( borrow ? 1 : 0 ) ) & ~0xf ) );
    return res16;
} //op_sub16

force_inlined uint8_t i8086::op_add8( uint8_t lhs, uint8_t rhs, bool carry )
{
    uint16_t carry_int = carry ? 1 : 0;
    uint16_t res16 = (uint16_t) lhs + (uint16_t) rhs + carry_int;
    uint8_t res8 = res16 & 0xff;
    fCarry = ( 0 != ( res16 & 0x100 ) );
    set_PSZ8( res8 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x80 ) ) && ( ( lhs ^ res8 ) & 0x80 );
    fAuxCarry = ( 0 != ( ( ( 0xf & lhs ) + ( 0xf & rhs ) + carry_int ) & 0x10 ) );
    return res8;
} //op_add8

force_inlined uint16_t i8086::op_add16( uint16_t lhs, uint16_t rhs, bool carry )
{
    uint32_t carry_int = carry ? 1 : 0;
    uint32_t res32 = (uint32_t) lhs + (uint32_t) rhs + carry_int;
    uint16_t res16 = res32 & 0xffff;
    fCarry = ( 0 != ( res32 & 0x10000 ) );
    set_PSZ16( res16 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x8000 ) ) && ( ( lhs ^ res16 ) & 0x8000 );
    fAuxCarry = ( 0 != ( ( ( 0xf & lhs ) + ( 0xf & rhs ) + carry_int ) & 0x10 ) );
    return res16;
} //op_add16

uint8_t i8086::op_and8( uint8_t lhs, uint8_t rhs )
{
    lhs &= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and8

uint16_t i8086::op_and16( uint16_t lhs, uint16_t rhs )
{
    lhs &= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and16

uint8_t i8086::op_or8( uint8_t lhs, uint8_t rhs )
{
    lhs |= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or8

uint16_t i8086::op_or16( uint16_t lhs, uint16_t rhs )
{
    lhs |= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or16

uint8_t i8086::op_xor8( uint8_t lhs, uint8_t rhs )
{
    lhs ^= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor8

uint16_t i8086::op_xor16( uint16_t lhs, uint16_t rhs )
{
    lhs ^= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor16

force_inlined void i8086::do_math8( uint8_t math, uint8_t * psrc, uint8_t rhs )
{
    assert( math <= 7 );
    switch ( math )
    {
        case 0: *psrc = op_add8( *psrc, rhs ); break;
        case 1: *psrc = op_or8( *psrc, rhs ); break;
        case 2: *psrc = op_add8( *psrc, rhs, fCarry ); break;
        case 3: *psrc = op_sub8( *psrc, rhs, fCarry ); break;
        case 4: *psrc = op_and8( *psrc, rhs ); break;
        case 5: *psrc = op_sub8( *psrc, rhs ); break;
        case 6: *psrc = op_xor8( *psrc, rhs ); break;
        default: op_sub8( *psrc, rhs ); break; // 7 is cmp
    }
} //do_math8

force_inlined void i8086::do_math16( uint8_t math, uint16_t * psrc, uint16_t rhs )
{
    assert( math <= 7 );
    switch( math )
    {
        case 0: *psrc = op_add16( *psrc, rhs ); break;
        case 1: *psrc = op_or16( *psrc, rhs ); break;
        case 2: *psrc = op_add16( *psrc, rhs, fCarry ); break;
        case 3: *psrc = op_sub16( *psrc, rhs, fCarry ); break;
        case 4: *psrc = op_and16( *psrc, rhs ); break;
        case 5: *psrc = op_sub16( *psrc, rhs ); break;
        case 6: *psrc = op_xor16( *psrc, rhs ); break;
        default: op_sub16( *psrc, rhs ); break; // 7 is cmp
    }
} //do_math16

uint8_t i8086::op_inc8( uint8_t val )
{
   fOverflow = ( 0x7f == val );
   val++;
   fAuxCarry = ( 0 == ( val & 0xf ) );
   set_PSZ8( val );
   return val;
} //op_inc8

uint16_t i8086::op_inc16( uint16_t val )
{
   fOverflow = ( 0x7fff == val );
   val++;
   fAuxCarry = ( 0 == ( val & 0xf ) );
   set_PSZ16( val );
   return val;
} //op_inc16

uint8_t i8086::op_dec8( uint8_t val )
{
   fOverflow = ( 0x80 == val );
   val--;
   fAuxCarry = ( 0xf == ( val & 0xf ) );
   set_PSZ8( val );
   return val;
} //op_dec8

uint16_t i8086::op_dec16( uint16_t val )
{
   fOverflow = ( 0x8000 == val );
   val--;
   fAuxCarry = ( 0xf == ( val & 0xf ) );
   set_PSZ16( val );
   return val;
} //op_dec16

void i8086::op_rol8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool highBit = ( 0 != ( 0x80 & val ) );
        val <<= 1;
        if ( highBit )
            val |= 1;
        else
            val &= 0xfe;
        fCarry = highBit;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ fCarry );

    *pval = val;
} //op_rol8

void i8086::op_rol16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool highBit = ( 0 != ( 0x8000 & val ) );
        val <<= 1;
        if ( highBit )
            val |= 1;
        else
            val &= 0xfffe;
        fCarry = highBit;
    }

    // Overflow only defined for 1-bit shifts, but actual hardware and emulators do this
    //if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ fCarry );

    *pval = val;
} //op_rol16

void i8086::op_ror8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool lowBit = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( lowBit )
            val |= 0x80;
        else
            val &= 0x7f;
        fCarry = lowBit;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ ( 0 != ( val & 0x40 ) ) );

    *pval = val;
} //op_ror8

void i8086::op_ror16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool lowBit = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( lowBit )
            val |= 0x8000;
        else
            val &= 0x7fff;
        fCarry = lowBit;
    }

    // Overflow only defined for 1-bit shifts, but actual hardware and emulators do this
    //if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ ( 0 != ( val & 0x4000 ) ) );
    //else
    //    fOverflow = true;

    *pval = val;
} //op_ror16

not_inlined void i8086::op_rcl8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 0x80 & val ) );
        val <<= 1;
        if ( fCarry )
            val |= 1;
        else
            val &= 0xfe;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ fCarry );

    *pval = val;
} //op_rcl8

not_inlined void i8086::op_rcl16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 0x8000 & val ) );
        val <<= 1;
        if ( fCarry )
            val |= 1;
        else
            val &= 0xfffe;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ fCarry );

    *pval = val;
} //op_rcl16

not_inlined void i8086::op_rcr8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( fCarry )
            val |= 0x80;
        else
            val &= 0x7f;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ ( 0 != ( val & 0x40 ) ) );

    *pval = val;
} //op_rcr8

not_inlined void i8086::op_rcr16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( fCarry )
            val |= 0x8000;
        else
            val &= 0x7fff;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ ( 0 != ( val & 0x4000 ) ) );

    *pval = val;
} //op_rcr16

void i8086::op_sal8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    *pval <<= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 0x80 ) );
    *pval <<= 1;

    //if ( 1 == shift )
        fOverflow = ! ( ( 0 != ( *pval & 0x80 ) ) == fCarry );

    set_PSZ8( *pval );
} //op_sal8

void i8086::op_sal16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    *pval <<= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 0x8000 ) );
    *pval <<= 1;

    // the 8086 doc says that Overflow is only defined when shift == 1.
    // actual 8088 CPUs and some emulators set overflow if shift > 0.
    // so the same is done here for sal16

    //if ( 1 == shift )
        fOverflow = ! ( ( 0 != ( *pval & 0x8000 ) ) == fCarry );

    set_PSZ16( *pval );
} //op_sal16

void i8086::op_shr8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    fOverflow = ( 0 != ( *pval & 0x80 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PSZ8( *pval );
} //op_shr8

void i8086::op_shr16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    fOverflow = ( 0 != ( *pval & 0x8000 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PSZ16( *pval );
} //op_shr16

not_inlined void i8086::op_sar8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    bool highBit = ( 0 != ( val & 0x80 ) );
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        fCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( highBit )
            val |= 0x80;
        else
            val &= 0x7f;
    }

    if ( 1 == shift )
        fOverflow = false;

    set_PSZ16( val );
    *pval = val;
} //op_sar8

void i8086::op_sar16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    bool highBit = ( 0 != ( val & 0x8000 ) );
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        fCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( highBit )
            val |= 0x8000;
        else
            val &= 0x7fff;
    }

    if ( 1 == shift )
        fOverflow = false;

    set_PSZ16( val );
    *pval = val;
} //op_sar16

void i8086::op_cmps8()
{
    op_sub8( * flat_address8( get_seg_value(), si ), * flat_address8( es, di ) ); // es cannot be overridden
    update_rep_sidi8();
} //op_cmps8

void i8086::op_cmps16()
{
    op_sub16( * flat_address16( get_seg_value(), si ), * flat_address16( es, di ) ); // es cannot be overridden
    update_rep_sidi16();
} //op_cmps16

void i8086::op_movs8()
{
    * flat_address8( es, di ) = * flat_address8( get_seg_value(), si );
    update_rep_sidi8();
} //op_movs8

void i8086::op_movs16()
{
    * flat_address16( es, di ) = * flat_address16( get_seg_value(), si );
    update_rep_sidi16();
} //op_movs16

void i8086::op_sto8()
{
    * flat_address8( es, di ) = al();
    update_index8( di );
} //op_sto8

void i8086::op_sto16()
{
    * flat_address16( es, di ) = ax;
    update_index16( di );
} //op_sto16

void i8086::op_lods8()
{
    set_al( * flat_address8( get_seg_value(), si ) );
    update_index8( si );
} //op_lods8

void i8086::op_lods16()
{
    ax = * flat_address16( get_seg_value(), si );
    update_index16( si );
} //op_lods16

void i8086::op_scas8()
{
    op_sub8( al(), * flat_address8( es, di ) ); // es cannot be overridden
    update_index8( di );
} //op_scas8

void i8086::op_scas16()
{
    op_sub16( ax, * flat_address16( es, di ) ); // es cannot be overridden
    update_index16( di );
} //op_scas16

void i8086::op_rotate8( uint8_t * pval, uint8_t operation, uint8_t amount )
{
    switch( operation )
    {
        case 0: op_rol8( pval, amount ); break;
        case 1: op_ror8( pval, amount ); break;
        case 2: op_rcl8( pval, amount ); break;
        case 3: op_rcr8( pval, amount ); break;
        case 4: op_sal8( pval, amount ); break;    // aka shl
        case 5: op_shr8( pval, amount ); break;
        case 7: op_sar8( pval, amount ); break;
        // 6 is illegal
    }
} //op_rotate8

void i8086::op_rotate16( uint16_t * pval, uint8_t operation, uint8_t amount )
{
    switch( operation )
    {
        case 0: op_rol16( pval, amount ); break;
        case 1: op_ror16( pval, amount ); break;
        case 2: op_rcl16( pval, amount ); break;
        case 3: op_rcr16( pval, amount ); break;
        case 4: op_sal16( pval, amount ); break;   // aka shl
        case 5: op_shr16( pval, amount ); break; 
        case 7: op_sar16( pval, amount ); break;
        // 6 is illegal
    }
} //op_rotate16

not_inlined void i8086::op_interrupt( uint8_t interrupt_num, uint8_t instruction_length )
{
    if ( g_State & stateTraceInstructions )
        tracer.Trace( "op_interrupt num %#x, length %d\n", interrupt_num, instruction_length );

    materializeFlags();
    push( flags );
    fInterrupt = false; // will be set again if/when flags are popped on iret
    fTrap = false;
    fAuxCarry = false;
    push( cs );
    push( ip + instruction_length );

    uint16_t * vectorItem = flat_address16( 0, 4 * interrupt_num );
    ip = vectorItem[ 0 ];
    cs = vectorItem[ 1 ];
} //op_interrupt

not_inlined void i8086::op_daa()
{
    uint8_t old_al = al();
    bool oldCarry = fCarry;
    fCarry = false;

    if ( ( ( al() & 0xf ) > 9 ) || fAuxCarry )
    {
        fCarry = oldCarry || ( al() > 9 );
        set_al( al() + 6 );
        fAuxCarry = true;
    }
    else
        fAuxCarry = false;

    if ( ( old_al > 0x99 ) || oldCarry )
    {
        set_al( al() + 0x60 );
        fCarry = true;
    }
    else
        fCarry = false;

    set_PSZ8( al() );
} //op_daa

not_inlined void i8086::op_das()
{
    uint8_t old_al = al();
    bool oldCarry = fCarry;
    fCarry = false;
    if ( ( ( al() & 0xf ) > 9 ) || ( fAuxCarry ) )
    {
        fCarry = ( oldCarry || ( al() < 6 ) );
        set_al( al() - 6 );
        fAuxCarry = true;
    }
    else
        fAuxCarry = false;

    if ( ( old_al > 0x99 ) || ( oldCarry ) )
    {
        set_al( al() - 0x60 );
        fCarry = true;
    }
    set_PSZ8( al() );
} //op_das

not_inlined void i8086::op_aas()
{
    if ( ( ( al() & 0x0f ) > 0 ) || fAuxCarry )
    {
        ax = ax - 6;
        set_ah( ah() - 1 );
        fAuxCarry = 1;
        fCarry = 1;
        set_al( al() & 0x0f );
    }
    else
    {
        fAuxCarry = false;
        fCarry = false;
        set_al( al() & 0x0f );
    }
} //op_aas

not_inlined void i8086::op_aaa()
{
    if ( ( ( al() & 0xf ) > 9 ) || fAuxCarry )
    {
        ax = ax + 0x106;
        fAuxCarry = true;
        fCarry = true;
    }
    else
    {
        fAuxCarry = false;
        fCarry = false;
    }

    set_al( al() & 0x0f );
} //op_aaa

not_inlined void i8086::op_sahf()
{
    uint8_t fl = ah();
    fSign = ( 0 != ( fl & 0x80 ) );
    fZero = ( 0 != ( fl & 0x40 ) );
    fAuxCarry = ( 0 != ( fl & 0x20 ) );
    fParityEven = ( 0 != ( fl & 0x04 ) );
    fCarry = ( 0 != ( fl & 1 ) );
} //op_sahf

not_inlined void i8086::op_lahf()
{
    uint8_t fl = 0x02;
    if ( fSign ) fl |= 0x80;
    if ( fZero ) fl |= 0x40;
    if ( fAuxCarry ) fl |= 0x10;
    if ( fParityEven ) fl |= 0x04;
    if ( fCarry ) fl |= 1;
    set_ah( fl );
} //op_lahf

not_inlined bool i8086::op_f6()
{
    _bc++;

    if ( 0 == _reg ) // test reg8/mem8, immed8
    {
        AddMemCycles( 10 );
        uint8_t lhs = * get_rm_ptr8();
        uint8_t rhs = _pcode[ _bc++ ];
        op_and8( lhs, rhs );
    }
    else if ( 2 == _reg ) // not reg8/mem8 -- no flags updated
    {
        AddMemCycles( 19 );
        uint8_t * pval = get_rm_ptr8();
        *pval = ~ ( *pval );
    }
    else if ( 3 == _reg ) // neg reg8/mem8 (subtract from 0)
    {
        AddMemCycles( 19 );
        uint8_t * pval = get_rm_ptr8();
        *pval = op_sub8( 0, *pval );
    }
    else if ( 4 == _reg ) // mul. ax = al * r/m8
    {
        AddCycles( 77 ); // assume worst-case
        uint8_t rhs = * get_rm_ptr8();
        ax = (uint16_t) al() * (uint16_t) rhs;
        fCarry = fOverflow = ( 0 != ah() );
        //fAuxCarry = ( ax > 0xfff ); // documentation says that aux carry is undefined, but real hardware does this
        set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
        fSign = ( 0 != ( 0x80 & al() ) ); // documentation says these bits are undefined, but real hardware does this
    }
    else if ( 5 == _reg ) // imul. ax = al * r/m8
    {
        AddCycles( 98 ); // assume worst-case
        uint8_t rhs = * get_rm_ptr8();
        uint32_t result = (int16_t) (int8_t) al() * (int16_t) (int8_t) rhs;
        ax = result & 0xffff;
        result &= 0xffff8000;
        fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
        //fAuxCarry = ( ( 0 != result ) && ( 0xfffff800 != result ) ); // documentation says that aux carry is undefined, but real hardware does this
        set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
    }
    else if ( 6 == _reg ) // div m, r8 / src. al = result, ah = remainder
    {
        AddCycles( 90 ); // assume worst-case
        uint8_t rhs = * get_rm_ptr8();
        if ( 0 != rhs )
        {
            uint16_t lhs = ax;
            set_al( (uint8_t) ( lhs / (uint16_t) rhs ) );
            set_ah( lhs % rhs );

            // documentation says these bits are undefined, but real hardware does this.
            //bool oldZero = fZero;
            //set_PSZ16( ax );
            //fZero = oldZero;
        }
        else
            return true;
    }
    else if ( 7 == _reg ) // idiv r/m8
    {
        AddCycles( 112 ); // assume worst-case
        uint8_t rhs = * get_rm_ptr8();
        if ( 0 != rhs )
        {
            int16_t lhs = ax;
            set_al( ( lhs / (int16_t) (int8_t) rhs ) & 0xff );
            set_ah( lhs % (int16_t) (int8_t) rhs );

            // documentation says these bits are undefined, but real hardware does this.
            //bool oldZero = fZero;
            //set_PSZ16( ax );
            //fZero = oldZero;
        }
        else
            return true;
    }
    else
        unhandled_instruction();

    return false;
} //op_f6

not_inlined bool i8086::op_f7()
{
    _bc++;

    if ( 0 == _reg ) // test reg16/mem16, immed16
    {
        AddMemCycles( 10 );
        uint16_t lhs = * get_rm_ptr16();
        uint16_t rhs = * (uint16_t *) ( _pcode + _bc );
        _bc += 2;
        op_and16( lhs, rhs );
    }
    else if ( 2 == _reg ) // not reg16/mem16 -- no flags updated
    {
        AddMemCycles( 19 );
        uint16_t * pval = get_rm_ptr16();
        *pval = ~ ( *pval );
    }
    else if ( 3 == _reg ) // neg reg16/mem16 (subtract from 0)
    {
        AddMemCycles( 19 );
        uint16_t * pval = get_rm_ptr16();
        *pval = op_sub16( 0, *pval );
    }
    else if ( 4 == _reg ) // mul. dx:ax = ax * src
    {
        AddCycles( 133 ); // assume worst-case
        uint16_t rhs = * get_rm_ptr16();
        uint32_t result = (uint32_t) ax * (uint32_t) rhs;
        dx = result >> 16;
        ax = result & 0xffff;
        fCarry = fOverflow = ( result > 0xffff );
        //fAuxCarry = ( result > 0xfff ); // documentation says that aux carry is undefined, but real hardware does this
        set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
    }
    else if ( 5 == _reg ) // imul. dx:ax = ax * src
    {
        AddCycles( 154 ); // assume worst-case
        uint16_t rhs = * get_rm_ptr16();
        uint32_t result = (int32_t) (int16_t) ax * (int32_t) (int16_t) rhs;
        dx = result >> 16;
        ax = result & 0xffff;
        result &= 0xffff8000;
        fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
        //fAuxCarry = ( ( 0 != result ) && ( 0xfffff800 != result ) ); // documentation says that aux carry is undefined, but real hardware does this
        set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
    }
    else if ( 6 == _reg ) // div dx:ax / src. ax = result, dx = remainder
    {
        AddCycles( 162 ); // assume worst-case
        uint16_t rhs = * get_rm_ptr16();
        if ( 0 != rhs )
        {
            uint32_t lhs = ( (uint32_t) dx << 16 ) + (uint32_t) ax;
            ax = (uint16_t) ( lhs / (uint32_t) rhs );
            dx = lhs % rhs;

            // documentation says these bits are undefined, but real hardware does this.
            //bool oldZero = fZero;
            //set_PSZ16( ax );
            //fZero = oldZero;
        }
        else
            return true;
    }
    else if ( 7 == _reg ) // idiv dx:ax / src. ax = result, dx = remainder
    {
        AddCycles( 184 ); // assume worst-case
        uint16_t rhs = * get_rm_ptr16();
        if ( 0 != rhs )
        {
            uint32_t lhs = ( (uint32_t) dx << 16 ) + (uint32_t) ax;
            ax = (uint16_t) ( (int32_t) (int16_t) lhs / (int32_t) (int16_t) rhs );
            dx = (int32_t) lhs % (int32_t) rhs;

            // documentation says these bits are undefined, but real hardware does this.
            //bool oldZero = fZero;
            //set_PSZ16( ax );
            //fZero = oldZero;
        }
        else
            return true;
    }
    else
        unhandled_instruction();
    return false;
} //op_f7

not_inlined bool i8086::op_ff()
{
    if ( 0 == _reg ) // inc mem16
    {
        AddCycles( 21 );
        uint16_t * pval = get_rm_ptr16();
        *pval = op_inc16( *pval );
        _bc++;
    }
    else if ( 1 == _reg ) // dec mem16
    {
        AddCycles( 21 );
        uint16_t * pval = get_rm_ptr16();
        *pval = op_dec16( *pval );
        _bc++;
    }
    else if ( 2 == _reg ) // call reg16/mem16 (intra segment)
    {
        AddCycles( 18 );
        AddMemCycles( 9 );
        uint16_t * pfunc = get_rm_ptr16();
        uint16_t return_address = ip + _bc + 1;
        push( return_address );
        ip = *pfunc;
        return true;
    }
    else if ( 3 == _reg ) // call mem16:16 (inter segment)
    {
        AddCycles( 34 );
        AddMemCycles( 17 );
        uint16_t * pdata = get_rm_ptr16();
        push( cs );
        push( ip + _bc + 1 );
        ip = pdata[ 0 ];
        cs = pdata[ 1 ];
        return true;
    }
    else if ( 4 == _reg ) // jmp reg16/mem16 (intra segment)
    {
        AddCycles( 13 );
        AddMemCycles( 3 );
        ip = * get_rm_ptr16();
        return true;
    }
    else if ( 5 == _reg ) // jmp mem16 (inter segment)
    {
        AddCycles( 16 );
        AddMemCycles( 9 );
        uint16_t * pdata = get_rm_ptr16();
        ip = pdata[ 0 ];
        cs = pdata[ 1 ];
        return true;
    }
    else if ( 6 == _reg ) // push mem16
    {
        AddCycles( 22 );
        uint16_t * pval = get_rm_ptr16();
        push( *pval );
        _bc++;
    }
    else
        unhandled_instruction();
    return false;
} //op_ff

not_inlined bool i8086::handle_state()
{
    // all of this code exists to reduce what would be multiple checks per instruction loop to just one check

    if ( g_State & stateEndEmulation )
    {
        g_State &= ~stateEndEmulation;
        return true;
    }

    if ( ( g_State & stateExitEmulateEarly ) &&
         ( 0xff == prefix_segment_override ) &&    // wait until there is no prefix to exit
         ( 0xff == prefix_repeat_opcode ) )
    {
        g_State &= ~stateExitEmulateEarly;
        return true;
    }

    if ( g_State & stateTraceInstructions )
        trace_state();

    if ( g_State & stateTrapSet )
    {
        assert( fTrap );
        if ( fIgnoreTrap )  // set just after the iret that enables fTrap or an int3; actually trap after the following instruction
            fIgnoreTrap = false;
        else
        {
            g_State &= ~stateTrapSet;
            op_interrupt( 1, 0 );
        }
    }
    return false;
} //handle_state

#ifndef NDEBUG
static uint64_t opcode_usage[ 256 ] = {0};

uint8_t i8086::trace_opcode_usage()
{
    uint8_t used = 0;
    for ( size_t i = 0; i < 256; i++ )
        if ( opcode_usage[ i ] )
        {
            tracer.Trace( "%02zx: %llu\n", i, opcode_usage[ i ] );
            used++;
        }
    tracer.Trace( "number of unique first opcodes: %zd\n", used );
    return used;
} //trace_opcode_usage
#endif //DEBUG

uint64_t i8086::emulate( uint64_t maxcycles )
{
    cycles = 0;

    while ( cycles < maxcycles )                           // 4.8% of runtime
    {
        prefix_segment_override = 0xff;                    // .69% of runtime though both are updated at once
        prefix_repeat_opcode = 0xff;

_prefix_set:
        if ( 0 != g_State )                                // 1.6% of runtime
            if ( handle_state() )
                break;

        #ifndef NDEBUG
            opcode_usage[ _b0 ]++;
            assert( 0 != cs || 0 != ip );                  // almost certainly an app bug.
        #endif

        decode_instruction( flat_address8( cs, ip ) );     // 23% of runtime

        #ifdef I8086_TRACK_CYCLES
            cycles += i8086_cycles[ _b0 ];                 // 2% of runtime
        #else
            cycles += 18; // average for the mips.com benchmark
        #endif

        // 30% of runtime setting up for use of the jumptables because they are in TEXT and
        // the LEA instruction has a terrible interaction with L1/L2 instruction cache misses
        // for each of the lookups (the 1-byte element table with ~251 entries and the 4-byte
        // element table with ~115 entries).
        // Update: newer versions of the compiler no longer use lea, but the tables are still
        // in TEXT even with the /jumptablerdata flag set. It's a frustration, Microsoft.

        switch( _b0 )
        {
            case 0x00: case 0x01: case 0x02: case 0x03: case 0x08: case 0x09: case 0x0a: case 0x0b:  // add, or, adc, sbb, and, sub, xor, cmp
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1a: case 0x1b: 
            case 0x20: case 0x21: case 0x22: case 0x23: case 0x28: case 0x29: case 0x2a: case 0x2b: 
            case 0x30: case 0x31: case 0x32: case 0x33: case 0x38: case 0x39: case 0x3a: case 0x3b: 
            {
                _bc = 2;
                if ( toreg() )
                    AddMemCycles( 10 );
                else
                    AddCycles( 21 );

                uint8_t math = ( _b0 >> 3 ) & 7;
                if ( isword() )
                {
                    uint16_t src;
                    uint16_t * pdst = get_op_args16( src );
                    do_math16( math, pdst, src );
                }
                else
                {
                    uint8_t src;
                    uint8_t * pdst = get_op_args8( src );
                    do_math8( math, pdst, src );
                }
                break;
            }
            case 0x04: case 0x05: case 0x0c: case 0x0d: case 0x14: case 0x15: case 0x1c: case 0x1d: // add al, immed8. add ax, immed16. etc.
            case 0x24: case 0x25: case 0x2c: case 0x2d: case 0x34: case 0x35: case 0x3c: case 0x3d:
            {
                uint8_t math = ( _b0 >> 3 ) & 7;
                if ( isword() )
                {
                    do_math16( math, &ax, b12() );
                    _bc += 2;
                }
                else
                {
                    do_math8( math, get_preg8( 0 ), _b1 );
                    _bc++;
                }
                break;
            }
            case 0x06: { push( es ); break; } // push es
            case 0x07: { es = pop(); break; } // pop es
            case 0x0e: { push( cs ); break; } // push cs
            case 0x16: { push( ss ); break; } // push ss
            case 0x17: { ss = pop(); break; } // pop ss
            case 0x1e: { push( ds ); break; } // push ds
            case 0x1f: { ds = pop(); break; } // pop ds
            case 0x26: { prefix_segment_override = 0; ip++; goto _prefix_set; } // es segment override
            case 0x27: { op_daa(); break; } // daa
            case 0x2e: { prefix_segment_override = 1; ip++; goto _prefix_set; } // cs segment override
            case 0x2f: { op_das(); break; } // das
            case 0x36: { prefix_segment_override = 2; ip++; goto _prefix_set; } // ss segment override
            case 0x37: { op_aaa(); break; } // aaa. ascii adjust after addition
            case 0x3e: { prefix_segment_override = 3; ip++; goto _prefix_set; } // ds segment override
            case 0x3f: { op_aas(); break; } // aas. ascii adjust al after subtraction
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: // inc ax..di
            {
                uint16_t *pval = get_preg16( _b0 & 7 );
                *pval = op_inc16( *pval );
                break;
            }
            case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: // dec ax..di
            {
                uint16_t *pval = get_preg16( _b0 & 7 );
                *pval = op_dec16( *pval );
                break;
            }
            case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: // push
            case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: // pop
            {
                uint16_t * preg = get_preg16( _b0 & 7 );
                if ( _b0 <= 0x57 )
                    push( *preg );
                else 
                    *preg = pop();
                break;
            }
            case 0x69: // fint FAKE Opcode: i8086_opcode_interrupt. default interrupt routines execute this to get to C++ code
            {
                uint16_t old_ip = ip;
                uint16_t old_cs = cs;

                i8086_invoke_interrupt( _b1 );

                // if ip or cs changed, it's likely the interrupt loaded or ended an app via int21 4b execute program or int21 4c exit app
                // the ip/cs now point to the new app or old parent app.
                
                 if ( old_ip != ip || old_cs != cs )
                    continue;

                _bc++;
                break;
            }
            case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77: // jcc
            case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            {
                bool takejmp;
                switch( _b0 & 0xf )
                {                                                                //                   hints:
                    case 0:  takejmp = fOverflow; break;                         // jo                o = overflow
                    case 1:  takejmp = !fOverflow; break;                        // jno               n = not
                    case 2:  takejmp = fCarry; break;                            // jb / jnae / jc    b = below, ae = above or equal, c = carry
                    case 3:  takejmp = !fCarry; break;                           // jnb / jae / jnc
                    case 4:  takejmp = fZero; break;                             // je / jz           e = equal, z = zero
                    case 5:  takejmp = !fZero; break;                            // jne / jnz
                    case 6:  takejmp = fCarry || fZero; break;                   // jbe / jna         be = below or equal, na = not above
                    case 7:  takejmp = !fCarry && !fZero; break;                 // jnbe / ja
                    case 8:  takejmp = fSign; break;                             // js                s = signed
                    case 9:  takejmp = !fSign; break;                            // jns
                    case 10: takejmp = fParityEven; break;                       // jp / jpe          p / pe = parity even
                    case 11: takejmp = !fParityEven; break;                      // jnp / jpo         po = parity odd
                    case 12: takejmp = ( fSign != fOverflow ); break;            // jl / jnge         l = less than, nge = not greater than or equal
                    case 13: takejmp = ( fSign == fOverflow ); break;            // jnl / jge
                    case 14: takejmp = fZero || ( fSign != fOverflow ); break;   // jle / jng         le = less than or equal, ng = not greather than
                    default: takejmp = !fZero && ( fSign == fOverflow  ); break; // jnle / jg   must be 15, but to work around a bogus compiler warning
                }
    
                if ( takejmp )
                {
                    ip += ( 2 + (int) (int8_t) _b1 );
                    AddCycles( 12 );
                    continue;
                }

                _bc = 2;
                break;
            }
            case 0x80: case 0x81: case 0x82: case 0x83: // math: reg8/mem8, imm8; reg16/mem16, imm16; reg16/mem16, imm8
            {
                uint8_t math = _reg; // the _reg field is the math operator, not a register
                _bc = 3;

                // mod=00: index register(s) or direct address (if rm is 110)
                // mod=01: index register(s) plus signed 8-bit immediate offset
                // mod=10: index register(s) plus signed 16-bit immediate offset
                // mod=11: r/m specifies a register modified by W
        
                bool directAddress = ( 0 == _mod && 6 == _rm );
                AddCycles( directAddress ? 13 : 6 );
                int imm_offset;
                if ( 1 == _mod )
                    imm_offset = 3;
                else if ( ( 2 == _mod ) || directAddress )
                    imm_offset = 4;
                else
                    imm_offset = 2;

                if ( isword() )
                {
                    uint16_t rhs;
                    if ( 0x83 == _b0 ) // one byte signed immediate, word math. (add sp, imm8)
                        rhs = (int8_t) _pcode[ imm_offset ]; // cast for sign extension from byte to word
                    else
                    {
                        _bc++;
                        rhs = * (uint16_t *) ( _pcode + imm_offset );
                    }
        
                    do_math16( math, get_rm_ptr16(), rhs );
                }
                else
                {
                    uint8_t rhs = _pcode[ imm_offset ];
                    do_math8( math, get_rm_ptr8(), rhs );
                }
                break;
            }
            case 0x84: // test reg8/mem8, reg8
            {
                _bc++;
                AddMemCycles( 8 );
                uint8_t src;
                uint8_t * pleft = get_op_args8( src );
                op_and8( *pleft, src );
                break;
            }
            case 0x85: // test reg16/mem16, reg16
            {
                _bc++;
                AddMemCycles( 8 );
                uint16_t src;
                uint16_t * pleft = get_op_args16( src );
                op_and16( *pleft, src );
                break;
            }
            case 0x86: // xchg reg8, reg8/mem8
            {
                AddMemCycles( 21 );
                swap( * get_preg8( _reg ), * get_rm_ptr8() );
                _bc++;
                break;
            }
            case 0x87: // xchg reg16, reg16/mem16
            {
                AddMemCycles( 21 );
                swap( * get_preg16( _reg ), * get_rm_ptr16() );
                _bc++;
                break;
            }
            case 0x88: // mov reg8/mem8, reg8
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                uint8_t src;
                uint8_t * pdst = get_op_args8( src );
                * pdst = src;
                break;
            }
            case 0x89: // mov reg16/mem16, reg16
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                uint16_t src;
                uint16_t * pdst = get_op_args16( src );
                * pdst = src;
                break;
            }
            case 0x8a: // mov reg8, r/m8
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                * get_preg8( _reg ) = * get_rm_ptr8();
                break;
            }
            case 0x8b: // mov reg16, r/m16
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                * get_preg16( _reg ) = * get_rm_ptr16();
                break;
            } 
            case 0x8c: // mov reg16/m16, sreg
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                * get_rm_ptr16() = * seg_reg( _reg ); // 0x8c is even, but it's a word instruction not byte
                break;
            }
            case 0x8d: { _bc++; * get_preg16( _reg ) = get_rm_ea(); break; } // lea reg16, mem16
            case 0x8e: // mov sreg, reg16/mem16
            {
                _bc++;
                AddMemCycles( 11 ); // 10/11/12 possible
                * seg_reg( _reg ) = * get_rm_ptr16();
                break;
            }
            case 0x8f: // pop reg16/mem16
            {
                AddMemCycles( 14 );
                * get_rm_ptr16() = pop();
                _bc++;
                break;
            }
            case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: // nop + xchg ax, cx/dx/bx/sp/bp/si/di
            {
                swap( ax, * get_preg16( _b0 & 7 ) );
                break;
            }
            case 0x98: { set_ah( ( al() & 0x80 ) ? 0xff : 0 ); break; } // cbw -- covert byte in al to word in ax. sign extend
            case 0x99: { dx = ( ax & 0x8000 ) ? 0xffff : 0; break; } // cwd -- convert word in ax to to double-word in dx:ax. sign extend
            case 0x9a: // call far proc
            {
                push( cs );
                push( ip + 5 );
                ip = b12();
                cs = b34();
                continue;
            }
            case 0x9b: break; // wait for pending floating point exceptions
            case 0x9c: { materializeFlags(); push( flags ); break; } // pushf
            case 0x9d: { flags = pop(); unmaterializeFlags(); break; } // popf
            case 0x9e: { op_sahf(); break; } // sahf -- stores a subset of flags from ah
            case 0x9f: { op_lahf(); break; } // lahf -- loads a subset of flags to ah
            case 0xa0: // mov al, mem8
            {
                set_al( * flat_address8( get_seg_value(), b12() ) );
                _bc += 2;
                break;
            }
            case 0xa1: // mov ax, mem16
            {
                ax = * flat_address16( get_seg_value(), b12() );
                _bc += 2;
                break;
            }
            case 0xa2: // mov mem8, al
            {
                * flat_address8( get_seg_value(), b12() ) = al();
                _bc += 2;
                break;
            }
            case 0xa3: // mov mem16, ax
            {
                * flat_address16( get_seg_value(), b12() ) = ax;
                _bc += 2;
                break;
            }
            case 0xa4: // movs dst-str8, src-str8.  movsb
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is legal, but f2 is used here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 17 );
                        op_movs8();
                        cx--;
                    }
                }
                else
                    op_movs8();
                break;
            }
            case 0xa5: // movs dest-str16, src-str16.  movsw
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is legal, but f2 is used here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 17 );
                        op_movs16();
                        cx--;
                    }
                }
                else
                    op_movs16();
                break;
            }
            case 0xa6: // cmps m8, m8. cmpsb
            {
                if ( 0xff != prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 30 );
                        op_cmps8();
                        cx--;
                        if ( (  fZero && ( 0xf2 == prefix_repeat_opcode ) ) ||
                             ( !fZero && ( 0xf3 == prefix_repeat_opcode ) ) )
                            break;
                    }
                }
                else
                    op_cmps8();
                break;
            }
            case 0xa7: // cmps dest-str16, src-str16. cmpsw
            {
                if ( 0xff != prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 30 );
                        op_cmps16();
                        cx--;
                        if ( (  fZero && ( 0xf2 == prefix_repeat_opcode ) ) ||
                             ( !fZero && ( 0xf3 == prefix_repeat_opcode ) ) )
                            break;
                    }
                }
                else
                    op_cmps16();
                break;
            }
            case 0xa8: { _bc++; op_and8( al(), _b1 ); break; } // test al, immed8
            case 0xa9: // test ax, immed16
            {
                _bc += 2;
                op_and16( ax, b12() );
                break;
            }
            case 0xaa: // stos8 -- fill bytes with al. stosb
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is legal, but f2 is used here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 10 );
                        op_sto8();
                        cx--;
                    }
                }
                else
                    op_sto8();
                break;
            }
            case 0xab: // stos16 -- fill words with ax. stosw
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is legal, but f2 is used here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 14 );
                        op_sto16();
                        cx--;
                    }
                }
                else
                    op_sto16();
                break;
            }
            case 0xac: // lods8 src-str8. lodsb
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is odd but supported. f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 10 ); // a guess
                        op_lods8();
                        cx--;
                    }
                }
                else
                    op_lods8();
                break;
            }
            case 0xad: // lods16 src-str16. lodsw
            {
                if ( 0xff != prefix_repeat_opcode ) // f3 is odd but supported. f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 10 ); // a guess
                        op_lods16();
                        cx--;
                    }
                }
                else
                    op_lods16();
                break;
            }
            case 0xae: // scas8 compare al with byte at es:di. scasb
            {
                if ( 0xff != prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 15 ); // a guess
                        op_scas8();
                        cx--;
                        if ( (  fZero && ( 0xf2 == prefix_repeat_opcode ) ) ||
                             ( !fZero && ( 0xf3 == prefix_repeat_opcode ) ) )
                            break;
                    }
                }
                else
                    op_scas8();
                break;
            }
            case 0xaf: // scas16 compare ax with word at es:di. scasw
            {
                if ( 0xff != prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( 19 ); // a guess
                        op_scas16();
                        cx--;
                        if ( (  fZero && ( 0xf2 == prefix_repeat_opcode ) ) ||
                             ( !fZero && ( 0xf3 == prefix_repeat_opcode ) ) )
                            break;
                    }
                }
                else
                    op_scas16();
                break;
            }
            case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7: // mov r8, immed
            {
                * get_preg8( _b0 & 7 ) = _b1;
                _bc = 2;
                break;
            }
            case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf: // mov r16, immed
            {
                * get_preg16( _b0 & 7 ) = b12();
                _bc = 3;
                break;
            }
            case 0xc2: { ip = pop(); sp += b12(); continue; } // ret immed16 intrasegment
            case 0xc3: { ip = pop(); continue; } // ret intrasegment
            case 0xc4: // les reg16, [mem16]
            {
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = get_rm_ptr16();
                *preg = pvalue[ 0 ];
                es = pvalue[ 1 ];
                break;
            }
            case 0xc5: // lds reg16, [mem16]
            {
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = get_rm_ptr16();
                *preg = pvalue[ 0 ];
                ds = pvalue[ 1 ];
                break;
            }
            case 0xc6: // mov mem8, immed8
            {
                if ( 0 != _reg )
                    unhandled_instruction();
                _bc++;
                uint8_t * pdst = get_rm_ptr8();
                *pdst = _pcode[ _bc ];
                _bc++;
                break;
            }
            case 0xc7: // mov mem16, immed16
            {
                if ( 0 != _reg )
                    unhandled_instruction();
                _bc++;
                uint16_t * pdst = get_rm_ptr16();
                *pdst = * (uint16_t *) & _pcode[ _bc ];
                _bc += 2;
                break;
            }
            case 0xca: { ip = pop(); cs = pop(); sp += b12(); continue; } // retf immed16
            case 0xcb: { ip = pop(); cs = pop(); continue; } // retf
            case 0xcc:  // int3
            {
                op_interrupt( 3, 1 );
                fIgnoreTrap = true; // don't trap after an int3
                continue;
            }
            case 0xcd: // int
            {
                op_interrupt( _b1, 2 );
                continue;
            }
            case 0xce: // into
            {
                if ( fOverflow )
                {
                    AddCycles( 69 );
                    op_interrupt( 4, 1 ); // overflow
                    continue;
                }
                break;
            }
            case 0xcf: // iret
            {
                bool previousTrap = fTrap;
                ip = pop();
                cs = pop();
                flags = pop();
                unmaterializeFlags();

                // don't trap if it's just now set until after the next instruction

                if ( fTrap )
                {
                    trap_set();
                    if ( !previousTrap )
                        fIgnoreTrap = true;
                }
                continue;
            }
            case 0xd0: // bit shift reg8/mem8, 1
            {
                _bc++;
                AddMemCycles( 13 );
                uint8_t *pval = get_rm_ptr8();
                op_rotate8( pval, _reg, 1 );
                break;
            }
            case 0xd1: // bit shift reg16/mem16, 1
            {
                _bc++;
                AddMemCycles( 13 );
                uint16_t *pval = get_rm_ptr16();
                op_rotate16( pval, _reg, 1 );
                break;
            }
            case 0xd2: // bit shift reg8/mem8, cl
            {
                _bc++;
                AddMemCycles( 12 );
                uint8_t *pval = get_rm_ptr8();
                uint8_t amount = cl() & 0x1f;
                AddCycles( 4 * amount );
                op_rotate8( pval, _reg, amount );
                break;
            }
            case 0xd3: // bit shift reg16/mem16, cl
            {
                _bc++;
                AddMemCycles( 12 );
                uint16_t *pval = get_rm_ptr16();
                uint8_t amount = cl() & 0x1f;
                AddCycles( 4 * amount );
                op_rotate16( pval, _reg, amount );
                break;
            }
            case 0xd4: // aam
            {
                _bc++;
                if ( 0 != _b1 )
                {
                    uint8_t tempal = al();
                    set_ah( tempal / _b1 );
                    set_al( tempal % _b1 );
                    set_PSZ16( ax );
                }
                else
                {
                    op_interrupt( 0, _bc );
                    continue;
                }
                break;
            }
            case 0xd5: // aad
            {
                set_al( ( al() + ( ah() * _b1 ) ) & 0xff );
                set_ah( 0 );
                _bc++;
                break;
            }
            case 0xd6: { set_al( fCarry ? 0xff : 0 ); break; } // salc (undocumented. IP protection scheme?)
            case 0xd7: // xlat
            {
                uint8_t * ptable = flat_address8( get_seg_value(), bx );
                set_al( ptable[ al() ] );
                break;
            }
            case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde:  // esc (8087 instructions)
            {
                _bc++;
                if ( isword() )
                    get_rm_ptr16();
                else
                    get_rm_ptr8();
                break;
            }
            case 0xe0: // loopne/loopnz short-label
            {
                cx--;
                if ( 0 != cx && !fZero )
                {
                    AddCycles( 14 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                _bc++;
                break;
            }
            case 0xe1: // loope/loopz short-label
            {
                cx--;
                if ( 0 != cx && fZero )
                {
                    AddCycles( 12 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                _bc++;
                break;
            }
            case 0xe2: // loop short-label
            {
                cx--;
                if ( 0 != cx )
                {
                    AddCycles( 12 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                _bc++;
                break;
            }
            case 0xe3: // jcxz rel8  jump if cx is 0
            {
                if ( 0 == cx )
                {
                    AddCycles( 12 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                _bc++;
                break;
            }
            case 0xe4: { set_al( i8086_invoke_in_al( _b1 ) ); _bc++; break; } // in al, immed8
            case 0xe5: { ax = i8086_invoke_in_ax( _b1 ); _bc++; break; } // in ax, immed8
            case 0xe6: { i8086_invoke_out_al( _b1, al() ); _bc++; break; } // out al, immed8
            case 0xe7: { i8086_invoke_out_ax( _b1, ax ); _bc++; break; } // out ax, immed8
            case 0xe8: // call rel16
            {
                uint16_t return_address = ip + 3;
                push( return_address );
                ip = return_address + b12();
                continue;
            }
            case 0xe9: { ip += ( 3 + (int16_t) b12() ); continue; } // jmp near
            case 0xea: { ip = b12(); cs = b34(); continue; } // jmp far
            case 0xeb: { ip += ( 2 + (int16_t) (int8_t) _b1 ); continue; } // jmp short i8
            case 0xec: { set_al( i8086_invoke_in_al( dx ) ); break; } // in al, dx
            case 0xed: { ax = i8086_invoke_in_ax( dx ); break; } // in ax, dx
            case 0xee: { break; } // out al, dx
            case 0xef: { break; } // out ax, dx
            case 0xf0: { break; } // lock prefix. ignore since interrupts won't happen
            case 0xf2: // repne/repnz -- fall through to the f3 code
            case 0xf3: { prefix_repeat_opcode = _b0; ip++; goto _prefix_set; } // rep/repe/repz
            case 0xf4: { i8086_invoke_halt(); goto _all_done; } // hlt
            case 0xf5: { fCarry = !fCarry; break; } //cmc
            case 0xf6: // test/UNUSED/not/neg/mul/imul/div/idiv r/m8
            {
                if ( op_f6() )
                {
                    op_interrupt( 0, _bc ); // divide by 0
                    continue;
                }
                break;
            }
            case 0xf7: // test/UNUSED/not/neg/mul/imul/div/idiv r/m16
            {
                if ( op_f7() )
                {
                    op_interrupt( 0, _bc ); // divide by 0
                    continue;
                }
                break;
            }
            case 0xf8: { fCarry = false; break; } // clc
            case 0xf9: { fCarry = true; break; } // stc
            case 0xfa: { fInterrupt = false; break; } // cli
            case 0xfb: { fInterrupt = true; break; } // sti
            case 0xfc: { fDirection = false; break; } // cld
            case 0xfd: { fDirection = true; break; } // std
            case 0xfe: // inc/dec reg8/mem8
            {
                _bc++;
                AddMemCycles( 12 );
                uint8_t * pdst = get_rm_ptr8();

                if ( 0 == _reg ) // inc
                    *pdst = op_inc8( *pdst );
                else
                    *pdst = op_dec8( *pdst );
                break;
            }
            case 0xff: { if ( op_ff() ) continue; break; } // many
            default:
                unhandled_instruction();
        } //switch
  
        ip += _bc;                                         // 8.7% of runtime (includes while check above)
    } //while

_all_done:
    return cycles;
} //emulate
