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

void i8086::trace_state()
{
//    tracer.Trace( "es: [di+8] 309c: [8] " ); tracer.TraceBinaryData( memory + flatten( 0x309c, 8 ), 2, 0 );
//    tracer.Trace( "bp-2 1f13:ad04-2 " ); tracer.TraceBinaryData( memory + flatten( 0x1f13, 0xad04 - 2 ), 2, 0 );
//    tracer.Trace( "x594 + 24: " ); tracer.TraceBinaryData( memory + flatten( 0x3c9b, 0x594 + 24 ), 4, 0 );
//    tracer.TraceBinaryData( memory + flatten( 0, 4 * 0x1c ), 4, 0 );

    uint8_t * pcode = memptr( flat_ip() );
    const char * pdisassemble = g_Disassembler.Disassemble( pcode );
    tracer.TraceQuiet( "ip %4x, opc %02x %02x %02x %02x %02x, ax %04x, bx %04x, cx %04x, dx %04x, di %04x, "
                       "si %04x, ds %04x, es %04x, cs %04x, ss %04x, bp %04x, sp %04x, %s, %s ; %u\n",
                       ip, pcode[0], pcode[1], pcode[2], pcode[3], pcode[4],
                       ax, bx, cx, dx, di, si, ds, es, cs, ss, bp, sp,
                       render_flags(), pdisassemble, g_Disassembler.BytesConsumed() );
} //trace_state

// base cycle count per opcode; will be higher for multi-byte instructions, memory references,
// ea calculations, jumps taken, loops taken, rotate cl-times, and reps
const uint8_t i8086_cycles[ 256 ]
{
    /*00*/     3,  3,  3,  3,  4,  4, 15, 12,    3,  3,  3,  3,  4,  4, 15,  0,
    /*10*/     3,  3,  3,  3,  4,  4, 15, 12,    3,  3,  3,  3,  4,  4, 15, 12,
    /*20*/     3,  3,  3,  3,  4,  4,  2,  4,    3,  3,  3,  3,  4,  4,  2,  4,
    /*30*/     3,  3,  3,  3,  4,  4,  2,  4,    3,  3,  3,  3,  4,  4,  2,  4,
    /*40*/     3,  3,  3,  3,  3,  3,  3,  3,    3,  3,  3,  3,  3,  3,  3,  3,
    /*50*/    15, 15, 15, 15, 15, 15, 15, 15,   12, 12, 12, 12, 12, 12, 12, 12,
    /*60*/     0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,  0,  0,  0,  0,  0,
    /*70*/     4,  4,  4,  4,  4,  4,  4,  4,    4,  4,  4,  4,  4,  4,  4,  4,
    /*80*/     4,  4,  4,  4,  5,  5,  4,  4,    2,  2,  2,  2,  2,  4,  2, 12, // lea as 4, not 2; docs can't be right
    /*90*/     4,  4,  4,  4,  4,  4,  4,  4,    2,  5, 36,  4, 14, 12,  4,  4,
    /*a0*/    12, 12, 13, 13, 18, 26, 30, 30,    5,  5, 11, 15, 16, 16, 19, 19,
    /*b0*/     4,  4,  4,  4,  4,  4,  4,  4,    4,  4,  4,  4,  4,  4,  4,  4,
    /*c0*/     0,  0, 24, 20, 24, 24, 14, 14,    0,  0, 33, 34, 72, 71,  4, 44,
    /*d0*/     2,  2,  8,  8, 83, 60, 11,  0,    0,  0,  0,  0,  0,  0,  0,  0,
    /*e0*/     6,  5,  5,  6, 14, 14, 14, 14,   23, 15, 15, 15, 12, 12, 12, 12,
    /*f0*/     1,  0,  9,  9,  2,  3,  5,  5,    2,  2,  2,  2,  2,  3,  2,  2,
};

void i8086::update_index16( uint16_t & index_register ) // si or di
{
    if ( fDirection )
        index_register -= 2;
    else
        index_register += 2;
} //update_index16

void i8086::update_index8( uint16_t & index_register ) // si or di
{
    if ( fDirection )
        index_register--;
    else
        index_register++;
} //update_index16

void i8086::update_rep_sidi16()
{
    update_index16( si );
    update_index16( di );
} //update_rep_sidi16

void i8086::update_rep_sidi8()
{
    update_index8( si );
    update_index8( di );
} //update_rep_sidi8

force_inlined uint8_t i8086::op_sub8( uint8_t lhs, uint8_t rhs, bool borrow )
{
    // com == ones-complement
    uint8_t com_rhs = ~rhs;
    uint8_t borrow_int = borrow ? 0 : 1;
    uint16_t res16 =  (uint16_t) lhs + (uint16_t) com_rhs + (uint16_t) borrow_int;
    uint8_t res8 = res16 & 0xff;
    fCarry = ( 0 == ( res16 & 0x100 ) );
    set_PSZ8( res8 );

    // if not ( ( one of lhs and com_x are negative ) and ( one of lhs and result are negative ) )
    fOverflow = ! ( ( lhs ^ com_rhs ) & 0x80 ) && ( ( lhs ^ res8 ) & 0x80 );
    uint8_t l_nibble = lhs & 0xf;
    uint8_t r_nibble = rhs & 0xf;
    fAuxCarry = ( 0 != ( ( l_nibble - r_nibble - ( borrow ? 1 : 0 ) ) & ~0xf ) );
    return res8;
} //op_sub8

force_inlined uint16_t i8086::op_sub16( uint16_t lhs, uint16_t rhs, bool borrow )
{
    // com == ones-complement
    uint16_t com_rhs = ~rhs;
    uint16_t borrow_int = borrow ? 0 : 1;
    uint32_t res32 =  (uint32_t) lhs + (uint32_t) com_rhs + (uint32_t) borrow_int;
    uint16_t res16 = res32 & 0xffff;
    fCarry = ( 0 == ( res32 & 0x10000 ) );
    set_PSZ16( res16 );
    fOverflow = ( ! ( ( lhs ^ com_rhs ) & 0x8000 ) ) && ( ( lhs ^ res16 ) & 0x8000 );
    uint8_t l_nibble = lhs & 0xf;
    uint8_t r_nibble = rhs & 0xf;
    fAuxCarry = ( 0 != ( ( l_nibble - r_nibble - ( borrow ? 1 : 0 ) ) & ~0xf ) );
    return res16;
} //op_sub16

force_inlined uint16_t i8086::op_add16( uint16_t lhs, uint16_t rhs, bool carry )
{
    uint32_t carry_int = carry ? 1 : 0;
    uint32_t r32 = (uint32_t) lhs + (uint32_t) rhs + carry_int;
    uint16_t r16 = r32 & 0xffff;
    fCarry = ( 0 != ( r32 & 0x010000 ) );
    fAuxCarry = ( 0 != ( ( ( 0xf & lhs ) + ( 0xf & rhs ) + carry_int ) & 0x10 ) );
    set_PSZ16( r16 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x8000 ) ) && ( ( lhs ^ r16 ) & 0x8000 );
    return r16;
} //op_add16

force_inlined uint8_t i8086::op_add8( uint8_t lhs, uint8_t rhs, bool carry )
{
    uint16_t carry_int = carry ? 1 : 0;
    uint16_t r16 = (uint16_t) lhs + (uint16_t) rhs + carry_int;
    uint8_t r8 = r16 & 0xff;
    fCarry = ( 0 != ( r16 & 0x0100 ) );
    fAuxCarry = ( 0 != ( ( ( 0xf & lhs ) + ( 0xf & rhs ) + carry_int ) & 0x10 ) );
    set_PSZ8( r8 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x80 ) ) && ( ( lhs ^ r8 ) & 0x80 );
    return r8;
} //op_add8

uint16_t i8086::op_and16( uint16_t lhs, uint16_t rhs )
{
    lhs &= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and16

uint8_t i8086::op_and8( uint8_t lhs, uint8_t rhs )
{
    lhs &= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and8

uint16_t i8086::op_or16( uint16_t lhs, uint16_t rhs )
{
    lhs |= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or16

uint16_t i8086::op_xor16( uint16_t lhs, uint16_t rhs )
{
    lhs ^= rhs;
    set_PSZ16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor16

uint8_t i8086::op_or8( uint8_t lhs, uint8_t rhs )
{
    lhs |= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or8

uint8_t i8086::op_xor8( uint8_t lhs, uint8_t rhs )
{
    lhs ^= rhs;
    set_PSZ8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor8

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

uint8_t i8086::op_dec8( uint8_t val )
{
   fOverflow = ( 0x80 == val );
   val--;
   fAuxCarry = ( 0xf == ( val & 0xf ) );
   set_PSZ8( val );
   return val;
} //op_dec8

uint16_t i8086::op_inc16( uint16_t val )
{
   fOverflow = ( 0x7fff == val );
   val++;
   fAuxCarry = ( 0 == ( val & 0xf ) );
   set_PSZ16( val );
   return val;
} //op_inc16

uint16_t i8086::op_dec16( uint16_t val )
{
   fOverflow = ( 0x8000 == val );
   val--;
   fAuxCarry = ( 0xf == ( val & 0xf ) );
   set_PSZ16( val );
   return val;
} //op_dec16

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
} //rol16

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
} //ror16

void i8086::op_rcl16( uint16_t * pval, uint8_t shift )
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
} //rcl16

void i8086::op_rcr16( uint16_t * pval, uint8_t shift )
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
} //rcr16

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
} //sal16

void i8086::op_shr16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    fOverflow = ( 0 != ( *pval & 0x8000 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PSZ16( *pval );
} //shr16

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
} //sar16

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
} //rol8

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
} //ror8

void i8086::op_rcl8( uint8_t * pval, uint8_t shift )
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
} //rcl8

void i8086::op_rcr8( uint8_t * pval, uint8_t shift )
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
} //rcr8

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
} //sal8

void i8086::op_shr8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    fOverflow = ( 0 != ( *pval & 0x80 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PSZ8( *pval );
} //shr8

void i8086::op_sar8( uint8_t * pval, uint8_t shift )
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
} //sar8

void i8086::op_cmps16( uint64_t & cycles )
{
    op_sub16( * flat_address16( get_seg_value( ds, cycles ), si ), * flat_address16( es, di ) );
    update_rep_sidi16();
} //op_cmps16

void i8086::op_cmps8( uint64_t & cycles )
{
    op_sub8( * flat_address8( get_seg_value( ds, cycles ), si ), * flat_address8( es, di ) );
    update_rep_sidi8();
} //op_cmps8

void i8086::op_movs16( uint64_t & cycles )
{
    * flat_address16( es, di ) = * flat_address16( get_seg_value( ds, cycles ), si );
    update_rep_sidi16();
} //op_movs16

void i8086::op_movs8( uint64_t & cycles )
{
    * flat_address8( es, di ) = * flat_address8( get_seg_value( ds, cycles ), si );
    update_rep_sidi8();
} //op_movs8

void i8086::op_sto16()
{
    * flat_address16( es, di ) = ax;
    update_index16( di );
} //op_sto16

void i8086::op_sto8()
{
    * flat_address8( es, di ) = al();
    update_index8( di );
} //op_sto8

void i8086::op_lods16( uint64_t & cycles )
{
    ax = * flat_address16( get_seg_value( ds, cycles ), si );
    update_index16( si );
} //op_lods16

void i8086::op_lods8( uint64_t & cycles )
{
    set_al( * flat_address8( get_seg_value( ds, cycles ), si ) );
    update_index8( si );
} //op_lods8

void i8086::op_scas16( uint64_t & cycles )
{
    op_sub16( ax, * flat_address16( get_seg_value( es, cycles ), di ) );
    update_index16( di );
} //op_scas16

void i8086::op_scas8( uint64_t & cycles )
{
    op_sub8( al(), * flat_address8( get_seg_value( es, cycles ), di ) );
    update_index8( di );
} //op_scas8

void i8086::op_rotate8( uint8_t * pval, uint8_t operation, uint8_t amount )
{
    switch( operation )
    {
        case 0: op_rol8( pval, amount ); break;
        case 1: op_ror8( pval, amount ); break;
        case 2: op_rcl8( pval, amount ); break;
        case 3: op_rcr8( pval, amount ); break;
        case 4: op_sal8( pval, amount ); break;    // aka shr
        case 5: op_shr8( pval, amount ); break;
        case 7: op_sar8( pval, amount ); break;
        default: { assert( false );  break; }      // 6 is illegal
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
        default: { assert( false ); break; }       // 6 is illegal
    }
} //op_rotate16

not_inlined void i8086::op_interrupt( uint8_t interrupt_num, uint8_t instruction_length )
{
    if ( g_State & stateTraceInstructions )
        tracer.Trace( "op_interrupt num %#x, length %d\n", interrupt_num, instruction_length );

    uint32_t offset = 4 * interrupt_num;
    uint16_t * vectorItem = (uint16_t *) ( memory + offset );
    materializeFlags();
    push( flags );
    fInterrupt = false; // will perhaps be set again when flags are popped on iret
    fTrap = false;
    fAuxCarry = false;
    push( cs );
    push( ip + instruction_length );

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
        tracer.Trace( "trapset is set. ignore trap %d, ftrap %d\n", fIgnoreTrap, fTrap );
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

uint64_t i8086::emulate( uint64_t maxcycles )
{
    uint64_t cycles = 0;
    while ( cycles < maxcycles )                           // .91% of runtime
    {
        prefix_segment_override = 0xff;                    // .66% of runtime though both are updated at once
        prefix_repeat_opcode = 0xff;

_prefix_set:
        if ( 0 != g_State )                                // 2.8% of runtime
            if ( handle_state() )
                break;

        assert( 0 != cs || 0 != ip );                      // almost certainly an app bug.
        decode_instruction( memptr( flat_ip() ) );         // 29 % of runtime

        #ifdef I8086_TRACK_CYCLES
            cycles += i8086_cycles[ _b0 ];                 // .25% of runtime
        #else
            cycles += 18; // average for the mips.com benchmark
        #endif

        switch( _b0 )                                      // 21.2% of runtime setting up for jumptable
        {
            case 0x04: { set_al( op_add8( al(), _b1 ) ); _bc++; break; } // add al, immed8
            case 0x05: { ax = op_add16( ax, b12() ); _bc += 2; break; } // add ax, immed16
            case 0x06: { push( es ); break; } // push es
            case 0x07: { es = pop(); break; } // pop es
            case 0x0c: { _bc++; set_al( op_or8( al(), _b1 ) ); break; } // or al, immed8
            case 0x0d: { _bc += 2; ax = op_or16( ax, b12() ); break; } // or ax, immed16
            case 0x0e: { push( cs ); break; } // push cs
            case 0x14: { _bc++; set_al( op_add8( al(), _b1, fCarry ) ); break; } // adc al, immed8
            case 0x15: { _bc += 2; ax = op_add16( ax, b12(), fCarry ); break; } // adc ax, immed16
            case 0x16: { push( ss ); break; } // push ss
            case 0x17: { ss = pop(); break; } // pop ss
            case 0x1c: { _bc++; set_al( op_sub8( al(), _b1, fCarry ) ); break; } // sbb al, immed8
            case 0x1d: { _bc += 2; ax = op_sub16( ax, b12(), fCarry ); break; } // sbb ax, immed16
            case 0x1e: { push( ds ); break; } // push ds
            case 0x1f: { ds = pop(); break; } // pop ds
            case 0x24: { _bc++; set_al( op_and8( al(), _b1 ) ); break; } // and al, immed8
            case 0x25: { _bc += 2; ax = op_and16( ax, b12() ); break; } // and ax, immed16
            case 0x26: { prefix_segment_override = 0; ip++; goto _prefix_set; } // es segment override
            case 0x27: { op_daa(); break; } // daa
            case 0x2c: { _bc++; set_al( op_sub8( al(), _b1 ) ); break; } // sub al, immed8
            case 0x2d: { _bc += 2; ax = op_sub16( ax, b12() ); break; } // sub ax, immed16
            case 0x2e: { prefix_segment_override = 1; ip++; goto _prefix_set; } // cs segment override
            case 0x2f: { op_das(); break; } // das
            case 0x34: { _bc++; set_al( op_xor8( al(), _b1 ) ); break; } // xor al, immed8
            case 0x35: { _bc += 2; ax = op_xor16( ax, b12() ); break; } // xor ax, immed16
            case 0x36: { prefix_segment_override = 2; ip++; goto _prefix_set; } // ss segment override
            case 0x37: { op_aaa(); break; } // aaa. ascii adjust after addition
            case 0x3c: { _bc++; op_sub8( al(), _b1 ); break; } // cmp al, i8
            case 0x3d: { _bc += 2; op_sub16( ax, b12() ); break; } // cmp ax, i16
            case 0x3e: { prefix_segment_override = 3; ip++; goto _prefix_set; } // ds segment override
            case 0x3f: { op_aas(); break; } // aas. ascii adjust al after subtraction
            case 0x69: // fint FAKE Opcode: i8086_opcode_interrupt. default interrupt routines execute this to get to C++ code
            {
                _bc++;
                uint16_t old_ip = ip;
                uint16_t old_cs = cs;

                i8086_invoke_interrupt( _b1 );

                // if ip or cs changed, it's likely the interrupt loaded or ended an app via int21 4b execute program or int21 4c exit app
                // the ip/cs now point to the new app or old parent app.
                
                 if ( old_ip != ip || old_cs != cs )
                    continue;

                break;
            }
            case 0x84: // test reg8/mem8, reg8
            {
                _bc++;
                AddMemCycles( cycles, 8 );
                uint8_t src;
                uint8_t * pleft = get_op_args8( toreg(), src, cycles );
                op_and8( *pleft, src );
                break;
            }
            case 0x85: // test reg16/mem16, reg16
            {
                _bc++;
                AddMemCycles( cycles, 8 );
                uint16_t src;
                uint16_t * pleft = get_op_args16( toreg(), src, cycles );
                op_and16( *pleft, src );
                break;
            }
            case 0x86: // xchg reg8, reg8/mem8
            {
                AddMemCycles( cycles, 21 );
                uint8_t * pA = get_preg8( _reg );
                uint8_t * pB = get_rm_ptr8( _rm, cycles );
                uint8_t tmp = *pB;
                *pB = *pA;
                *pA = tmp;
                _bc++;
                break;
            }
            case 0x87: // xchg reg16, reg16/mem16
            {
                AddMemCycles( cycles, 21 );
                uint16_t * pA = get_preg16( _reg );
                uint16_t * pB = get_rm_ptr16( _rm, cycles );
                uint16_t tmp = *pB;
                *pB = *pA;
                *pA = tmp;
                _bc++;
                break;
            }
            case 0x88: // mov reg8/mem8, reg8
            {
                _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                uint8_t src;
                uint8_t * pdst = get_op_args8( toreg(), src, cycles );
                * pdst = src;
                break;
            }
            case 0x89: // mov reg16/mem16, reg16
            {
                _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                uint16_t src;
                uint16_t * pdst = get_op_args16( toreg(), src, cycles );
                * pdst = src;
                break;
            }
            case 0x8a: // mov reg8, r/m8
            {
                _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                * get_preg8( _reg ) = * get_rm_ptr8( _rm, cycles );
                break;
            }
            case 0x8b: // mov reg16, r/m16
            {
                _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                * get_preg16( _reg ) = * get_rm_ptr16( _rm, cycles );
                break;
            } 
            case 0x8c: // mov r/m16, sreg
            {
                _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                * get_rm_ptr16_override( _rm, cycles ) = * seg_reg( _reg ); // 0x8c is even, but it's a word instruction not byte
                break;
            }
            case 0x8d: { _bc++; * get_preg16( _reg ) = get_rm_ea( _rm, cycles ); break; } // lea reg16, mem16
            case 0x8e: // mov sreg, reg16/mem16
            {
                 _bc++;
                AddMemCycles( cycles, 11 ); // 10/11/12 possible
                 _isword = true; // the opcode indicates it's a byte instruction, but it's not
                 * seg_reg( _reg ) = * get_rm_ptr16( _rm, cycles );
                 break;
            }
            case 0x8f: // pop reg16/mem16
            {
                AddMemCycles( cycles, 14 );
                uint16_t * pdst = get_rm_ptr16( _rm, cycles );
                *pdst = pop();
                _bc++;
                break;
            }
            case 0x90: { break; } // nop
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
            case 0x9e: // sahf -- stores a subset of flags from ah
            {
                uint8_t fl = ah();
                fSign = ( 0 != ( fl & 0x80 ) );
                fZero = ( 0 != ( fl & 0x40 ) );
                fAuxCarry = ( 0 != ( fl & 0x20 ) );
                fParityEven = ( 0 != ( fl & 0x04 ) );
                fCarry = ( 0 != ( fl & 1 ) );
                break;
            }
            case 0x9f: // lahf -- loads a subset of flags to ah
            {
                uint8_t fl = 0x02;
                if ( fSign ) fl |= 0x80;
                if ( fZero ) fl |= 0x40;
                if ( fAuxCarry ) fl |= 0x10;
                if ( fParityEven ) fl |= 0x04;
                if ( fCarry ) fl |= 1;
                set_ah( fl );
                break;
            }
            case 0xa0: // mov al, mem8
            {
                uint32_t flat = flatten( get_seg_value( ds, cycles ), b12() );
                set_al( * (uint8_t *) ( memory + flat ) );
                _bc += 2;
                break;
            }
            case 0xa1: // mov ax, mem16
            {
                uint32_t flat = flatten( get_seg_value( ds, cycles ), b12() );
                ax = * (uint16_t *) ( memory + flat );
                _bc += 2;
                break;
            }
            case 0xa2: // mov mem8, al
            {
                uint8_t * pdst = (uint8_t *) ( memory + flatten( get_seg_value( ds, cycles ), b12() ) );
                *pdst = al();
                _bc += 2;
                break;
            }
            case 0xa3: // mov mem16, ax
            {
                uint16_t * pdst = (uint16_t *) ( memory + flatten( get_seg_value( ds, cycles ), b12() ) );
                *pdst = ax;
                _bc += 2;
                break;
            }
            case 0xa4: // movs dst-str8, src-str8.  movsb
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 17 );
                        op_movs8( cycles );
                        cx--;
                    }
                }
                else
                    op_movs8( cycles );
                break;
            }
            case 0xa5: // movs dest-str16, src-str16.  movsw
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 17 );
                        op_movs16( cycles );
                        cx--;
                    }
                }
                else
                    op_movs16( cycles );
                break;
            }
            case 0xa6: // cmps m8, m8. cmpsb
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 30 );
                        op_cmps8( cycles );
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 30 );
                        op_cmps8( cycles );
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_cmps8( cycles );
                break;
            }
            case 0xa7: // cmps dest-str15, src-str16. cmpsw
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 30 );
                        op_cmps16( cycles );
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 30 );
                        op_cmps16( cycles );
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_cmps16( cycles );
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
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 10 );
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
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 14 );
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
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 10 ); // a guess
                        op_lods8( cycles );
                        cx--;
                    }
                }
                else
                    op_lods8( cycles );
                break;
            }
            case 0xad: // lods16 src-str16. lodsw
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 10 ); // a guess
                        op_lods16( cycles );
                        cx--;
                    }
                }
                else
                    op_lods16( cycles );
                break;
            }
            case 0xae: // scas8 compare al with byte at es:di. scasb
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 15 ); // a guess
                        op_scas8( cycles );
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 15 ); // a guess
                        op_scas8( cycles );
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_scas8( cycles );
                break;
            }
            case 0xaf: // scas16 compare ax with word at es:di. scasw
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 19 ); // a guess
                        op_scas16( cycles );
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        AddCycles( cycles, 19 ); // a guess
                        op_scas16( cycles );
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_scas16( cycles );
                break;
            }
            case 0xc2: { ip = pop(); sp += b12(); continue; } // ret immed16 intrasegment
            case 0xc3: { ip = pop(); continue; } // ret intrasegment
            case 0xc4: // les reg16, [mem16]
            {
                _isword = true; // opcode is even, but it's a word.
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = get_rm_ptr16( _rm, cycles );
                *preg = pvalue[ 0 ];
                es = pvalue[ 1 ];
                break;
            }
            case 0xc5: // lds reg16, [mem16]
            {
                _isword = true; // opcode is even, but it's a word.
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = get_rm_ptr16( _rm, cycles );
                *preg = pvalue[ 0 ];
                ds = pvalue[ 1 ];
                break;
            }
            case 0xc6: // mov mem8, immed8
            {
                _bc++;
                uint8_t * pdst = get_rm_ptr8( _rm, cycles );
                *pdst = _pcode[ _bc ];
                _bc++;
                break;
            }
            case 0xc7: // mov mem16, immed16
            {
                _bc++;
                uint16_t src;
                uint16_t * pdst = get_op_args16( false, src, cycles );
                *pdst = src;
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
                    AddCycles( cycles, 69 );
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
                    {
                        tracer.Trace( "iret setting ignore trap\n" );
                        fIgnoreTrap = true;
                    }
                }

                continue;
            }
            case 0xd0: // bit shift reg8/mem8, 1
            {
                _bc++;
                AddMemCycles( cycles, 13 );
                uint8_t *pval = get_rm_ptr8( _rm, cycles );
                op_rotate8( pval, _reg, 1 );
                break;
            }
            case 0xd1: // bit shift reg16/mem16, 1
            {
                _bc++;
                AddMemCycles( cycles, 13 );
                uint16_t *pval = get_rm_ptr16( _rm, cycles );
                op_rotate16( pval, _reg, 1 );
                break;
            }
            case 0xd2: // bit shift reg8/mem8, cl
            {
                _bc++;
                AddMemCycles( cycles, 12 );
                uint8_t *pval = get_rm_ptr8( _rm, cycles );
                uint8_t amount = cl() & 0x1f;
                AddCycles( cycles, 4 * amount );
                op_rotate8( pval, _reg, amount );
                break;
            }
            case 0xd3: // bit shift reg16/mem16, cl
            {
                _bc++;
                AddMemCycles( cycles, 12 );
                uint16_t *pval = get_rm_ptr16( _rm, cycles );
                uint8_t amount = cl() & 0x1f;
                AddCycles( cycles, 4 * amount );
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
            case 0xd6: // salc (undocumented. IP protection scheme?)
            {
                set_al( fCarry ? 0xff : 0 );
                break;
            }
            case 0xd7: // xlat
            {
                uint8_t * ptable = flat_address8( get_seg_value( ds, cycles ), bx );
                set_al( ptable[ al() ] );
                break;
            }
            case 0xe0: // loopne/loopnz short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx && !fZero )
                {
                    AddCycles( cycles, 14 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe1: // loope/loopz short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx && fZero )
                {
                    AddCycles( cycles, 12 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe2: // loop short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx )
                {
                    AddCycles( cycles, 12 );
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe3: // jcxz rel8  jump if cx is 0
            {
                if ( 0 == cx )
                {
                    AddCycles( cycles, 12 );
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
            case 0xe8: // call a8
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
                _bc++;

                if ( 0 == _reg ) // test reg8/mem8, immed8
                {
                    AddMemCycles( cycles, 8 );
                    uint8_t lhs = * get_rm_ptr8( _rm, cycles );
                    uint8_t rhs = _pcode[ _bc++ ];
                    op_and8( lhs, rhs );
                }
                else if ( 2 == _reg ) // not reg8/mem8 -- no flags updated
                {
                    AddMemCycles( cycles, 13 );
                    uint8_t * pval = get_rm_ptr8( _rm, cycles );
                    *pval = ~ ( *pval );
                }
                else if ( 3 == _reg ) // neg reg8/mem8 (subtract from 0)
                {
                    AddMemCycles( cycles, 13 );
                    uint8_t * pval = get_rm_ptr8( _rm, cycles );
                    *pval = op_sub8( 0, *pval );
                }
                else if ( 4 == _reg ) // mul. ax = al * r/m8
                {
                    AddCycles( cycles, 77 ); // assume worst-case
                    uint8_t rhs = * get_rm_ptr8( _rm, cycles );
                    ax = (uint16_t) al() * (uint16_t) rhs;
                    fCarry = fOverflow = ( 0 != ah() );
                    //fAuxCarry = ( ax > 0xfff ); // documentation says that aux carry is undefined, but real hardware does this
                    set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
                    fSign = ( 0 != ( 0x80 & al() ) ); // documentation says these bits are undefined, but real hardware does this
                }
                else if ( 5 == _reg ) // imul. ax = al * r/m8
                {
                    AddCycles( cycles, 98 ); // assume worst-case
                    uint8_t rhs = * get_rm_ptr8( _rm, cycles );
                    uint32_t result = (int16_t) al() * (int16_t) rhs;
                    ax = result & 0xffff;
                    result &= 0xffff8000;
                    fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
                    //fAuxCarry = ( ( 0 != result ) && ( 0xfffff800 != result ) ); // documentation says that aux carry is undefined, but real hardware does this
                    set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
                }
                else if ( 6 == _reg ) // div m, r8 / src. al = result, ah = remainder
                {
                    AddCycles( cycles, 90 ); // assume worst-case
                    uint8_t rhs = * get_rm_ptr8( _rm, cycles );
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
                    {
                        tracer.Trace( "divide by zero in div m, r8\n" );
                        op_interrupt( 0, _bc );
                        continue;
                    }
                }
                else if ( 7 == _reg ) // idiv r/m8
                {
                    AddCycles( cycles, 112 ); // assume worst-case
                    uint8_t rhs = * get_rm_ptr8( _rm, cycles );
                    if ( 0 != rhs )
                    {
                        int16_t lhs = ax;
                        set_al( ( lhs / (int16_t) rhs ) & 0xff );
                        set_ah( lhs % (int16_t) rhs );

                        // documentation says these bits are undefined, but real hardware does this.
                        //bool oldZero = fZero;
                        //set_PSZ16( ax );
                        //fZero = oldZero;
                    }
                    else
                    {
                        tracer.Trace( "divide by zero in idiv r/m8\n" );
                        op_interrupt( 0, _bc );
                        continue;
                    }
                }
                else
                    assert( false );

                break;
            }
            case 0xf7: // test/UNUSED/not/neg/mul/imul/div/idiv r/m16
            {
                _bc++;

                if ( 0 == _reg ) // test reg16/mem16, immed16
                {
                    AddMemCycles( cycles, 8 );
                    uint16_t lhs = * get_rm_ptr16( _rm, cycles );
                    uint16_t rhs = * (uint16_t *) ( _pcode + _bc );
                    _bc += 2;
                    op_and16( lhs, rhs );
                }
                else if ( 2 == _reg ) // not reg16/mem16 -- no flags updated
                {
                    AddMemCycles( cycles, 13 );
                    uint16_t * pval = get_rm_ptr16( _rm, cycles );
                    *pval = ~ ( *pval );
                }
                else if ( 3 == _reg ) // neg reg16/mem16 (subtract from 0)
                {
                    AddMemCycles( cycles, 13 );
                    uint16_t * pval = get_rm_ptr16( _rm, cycles );
                    *pval = op_sub16( 0, *pval );
                }
                else if ( 4 == _reg ) // mul. dx:ax = ax * src
                {
                    AddCycles( cycles, 133 ); // assume worst-case
                    uint16_t rhs = * get_rm_ptr16( _rm, cycles );
                    uint32_t result = (uint32_t) ax * (uint32_t) rhs;
                    dx = result >> 16;
                    ax = result & 0xffff;
                    fCarry = fOverflow = ( result > 0xffff );
                    //fAuxCarry = ( result > 0xfff ); // documentation says that aux carry is undefined, but real hardware does this
                    set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
                }
                else if ( 5 == _reg ) // imul. dx:ax = ax * src
                {
                    AddCycles( cycles, 154 ); // assume worst-case
                    uint16_t rhs = * get_rm_ptr16( _rm, cycles );
                    uint32_t result = (int32_t) ax * (int32_t) rhs;
                    dx = result >> 16;
                    ax = result & 0xffff;
                    result &= 0xffff8000;
                    fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
                    //fAuxCarry = ( ( 0 != result ) && ( 0xfffff800 != result ) ); // documentation says that aux carry is undefined, but real hardware does this
                    set_PSZ16( ax ); // documentation says these bits are undefined, but real hardware does this
                }
                else if ( 6 == _reg ) // div dx:ax / src. ax = result, dx = remainder
                {
                    AddCycles( cycles, 162 ); // assume worst-case
                    uint16_t rhs = * get_rm_ptr16( _rm, cycles );
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
                    {
                        tracer.Trace( "divide by zero in div dx:ax / src\n" );
                        op_interrupt( 0, _bc );
                        continue;
                    }
                }
                else if ( 7 == _reg ) // idiv dx:ax / src. ax = result, dx = remainder
                {
                    AddCycles( cycles, 184 ); // assume worst-case
                    uint16_t rhs = * get_rm_ptr16( _rm, cycles );
                    if ( 0 != rhs )
                    {
                        uint32_t lhs = ( (uint32_t) dx << 16 ) + (uint32_t) ax;
                        ax = (uint16_t) ( (int32_t) lhs / (int32_t) (int16_t) rhs );
                        dx = (int32_t) lhs % (int32_t) rhs;

                        // documentation says these bits are undefined, but real hardware does this.
                        //bool oldZero = fZero;
                        //set_PSZ16( ax );
                        //fZero = oldZero;
                    }
                    else
                    {
                        tracer.Trace( "divide by zero in idiv dx:ax / src\n" );
                        op_interrupt( 0, _bc );
                        continue;
                    }
                }
                else
                    assert( false );

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
                AddMemCycles( cycles, 12 );
                uint8_t * pdst = get_rm_ptr8( _rm, cycles );

                if ( 0 == _reg ) // inc
                    *pdst = op_inc8( *pdst );
                else
                    *pdst = op_dec8( *pdst );
                break;
            }
            case 0xff: // many
            {
                if ( 0 == _reg ) // inc mem16
                {
                    AddCycles( cycles, 21 );
                    uint16_t * pval = get_rm_ptr16( _rm, cycles );
                    *pval = op_inc16( *pval );
                    _bc++;
                }
                else if ( 1 == _reg ) // dec mem16
                {
                    AddCycles( cycles, 21 );
                    uint16_t * pval = get_rm_ptr16( _rm, cycles );
                    *pval = op_dec16( *pval );
                    _bc++;
                }
                else if ( 2 == _reg ) // call reg16/mem16 (intra segment)
                {
                    AddCycles( cycles, 18 );
                    AddMemCycles( cycles, 9 );
                    uint16_t * pfunc = get_rm_ptr16( _rm, cycles );
                    uint16_t return_address = ip + _bc + 1;
                    push( return_address );
                    ip = *pfunc;
                    continue;
                }
                else if ( 3 == _reg ) // call mem16:16 (inter segment)
                {
                    AddCycles( cycles, 35 );
                    uint16_t * pdata = get_rm_ptr16( _rm, cycles );
                    push( cs );
                    push( ip + _bc + 1 );
                    ip = pdata[ 0 ];
                    cs = pdata[ 1 ];
                    continue;
                }
                else if ( 4 == _reg ) // jmp reg16/mem16 (intra segment)
                {
                    AddCycles( cycles, 13 );
                    AddMemCycles( cycles, 3 );
                    ip = * get_rm_ptr16( _rm, cycles );
                    continue;
                }
                else if ( 5 == _reg ) // jmp mem16 (inter segment)
                {
                    AddCycles( cycles, 16 );
                    uint16_t * pdata = get_rm_ptr16( _rm, cycles );
                    ip = pdata[ 0 ];
                    cs = pdata[ 1 ];
                    continue;
                }
                else if ( 6 == _reg ) // push mem16
                {
                    AddCycles( cycles, 14 );
                    uint16_t * pval = get_rm_ptr16( _rm, cycles );
                    push( *pval );
                    _bc++;
                }

                break;
            }
            default: // check for ranges of opcodes
            {
                if ( _b0 <= 0x47 && _b0 >= 0x40 ) // inc ax..di
                {
                    uint16_t *pval = get_preg16( _b0 - 0x40 );
                    *pval = op_inc16( *pval );
                }
                else if ( _b0 <= 0x4f && _b0 >= 0x48 ) // dec ax..di
                {
                    uint16_t *pval = get_preg16( _b0 - 0x48 );
                    *pval = op_dec16( *pval );
                }
                else if ( _b0 <= 0x5f && _b0 >= 0x50 ) // push / pop
                {
                    uint16_t * preg = get_preg16( _b0 & 7 );
                    if ( _b0 <= 0x57 )
                        push( *preg );
                    else 
                        *preg = pop();
                }
                else if ( _b0 <= 0x7f && _b0 >= 0x70 ) // jcc
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
                        case 15: takejmp = !fZero && ( fSign == fOverflow  ); break; // jnle / jg
                    }
    
                    if ( takejmp )
                    {
                        ip += ( 2 + (int) (int8_t) _b1 );
                        AddCycles( cycles, 12 );
                        continue;
                    }
                    else
                        _bc = 2;
                }
                else if ( _b0 <= 0x97 && _b0 >= 0x91 ) // xchg ax, cx/dx/bx/sp/bp/si/di  0x90 is nop
                {
                    uint16_t * preg = get_preg16( _b0 & 7 );
                    swap( ax, * preg );
                }
                else if ( _b0 <= 0xbf && _b0 >= 0xb0 ) // mov r, immed
                {
                    if ( _b0 <= 0xb7 )
                    {
                        * get_preg8( _b0 & 7 ) = _b1;
                        _bc = 2;
                    }
                    else
                    {
                        * get_preg16( _b0 & 7 ) = b12();
                        _bc = 3;
                    }
                }
                else if ( _b0 <= 0xde && _b0 >= 0xd8 ) // esc (8087 instructions)
                {
                    _bc++;
                    if ( _isword )
                        void * p = get_rm_ptr16( _rm, cycles );
                    else
                        void * p = get_rm_ptr8( _rm, cycles );
                    // ignore p -- just consume the correct number of opcodes
                }
                else if ( 0 == ( 0xc4 & _b0 ) ) // add, or, adc, sbb, and, sub, xor, cmp
                {
                    _bc = 2;
                    AddMemCycles( cycles, 10 );
                    uint8_t bits5to3 = ( _b0 >> 3 ) & 7;
                    if ( _isword )
                    {
                        uint16_t src;
                        uint16_t * pdst = get_op_args16( true, src, cycles );
                        do_math16( bits5to3, pdst, src );
                    }
                    else
                    {
                        uint8_t src;
                        uint8_t * pdst = get_op_args8( true, src, cycles );
                        do_math8( bits5to3, pdst, src );
                    }
                }
                else if ( 0x80 == ( 0xfc & _b0 ) ) // math
                {
                    uint8_t math = _reg; // the _reg field is the math operator, not a register
                    _bc = 3;
        
                    bool directAddress = ( 0 == _mod && 6 == _rm );
                    int immoffset;
                    if ( 1 == _mod )
                        immoffset = 3;
                    else if ( 2 == _mod || directAddress )
                        immoffset = 4;
                    else
                        immoffset = 2;
        
                    AddCycles( cycles, directAddress ? 13 : 6 );
        
                    if ( _isword )
                    {
                        uint16_t rhs;
                        if ( 0x83 == _b0 ) // one byte immediate, word math. (add sp, imm8)
                            rhs = (int8_t) _pcode[ immoffset ]; // cast for sign extension from byte to word
                        else
                        {
                            _bc++;
                            rhs = * (uint16_t *) ( _pcode + immoffset );
                        }
        
                        do_math16( math, get_rm_ptr16( _rm, cycles ), rhs );
                    }
                    else
                    {
                        uint8_t rhs = _pcode[ immoffset ];
                        do_math8( math, get_rm_ptr8( _rm, cycles ), rhs );
                    }
                }
                else
                    i8086_hard_exit( "unhandled 8086 instruction %02x\n", _b0 );
            } //default
        } //switch
  
        ip += _bc;                                         // 4.4% of runtime
    } //while

_all_done:
    return cycles;
} //emulate
