# Intentional edge-case tests

`tests/` (and `tests/undocumented/`) hold the SingleStepTests corpus: 10,000
randomly-generated, hardware-validated cases per opcode. Excellent for broad
correctness, but being random it rarely lands on the specific boundary
values (segment-offset 0xFFFF, SP/CX/IP wraparound, shift-count thresholds,
BCD-adjust branches) where bugs actually cluster.

The tests here are the opposite: each one is hand-derived by reading the
emulator source (`i8086.cxx`/`i8086.hxx`) and picking initial state to hit
one specific branch or documented quirk. Run them with:

    pwsh ../runall.ps1 -TestDir edge_cases

Regenerate all of them from the reference model with:

    python3 generate_edge_cases.py

The generator is an independent, from-scratch model of 8086 semantics --
it does not call into the emulator under test, to avoid validating the
emulator against itself. Where the emulator's own logic is already known to
match real hardware (DAA/DAS/AAA/AAS, cited in `i8086.cxx` as sourced from
Ken Shirriff's righto.com reverse-engineering), the model mirrors that exact
logic rather than re-deriving BCD rules independently.

## Status: 76/76 pass

`B8_fetch_wrap_exploratory.json` found a real bug and has since been fixed.
`decode_instruction()` (i8086.hxx:243-252) and the byte reads throughout the
decoder that are relative to it (`_pcode[n]`, `read_iword(_pcode+n)`,
`b12()`/`b34()`) all read through a raw pointer, **not** through the
wrap-aware `read_word()` that data reads use. So while
`read_word`/`write_word`/`add_two_wrap` correctly special-case a *data*
operand landing at segment offset 0xFFFF (see the six wrap tests below), an
*instruction* whose bytes straddled that same boundary read the wrong,
linearly-adjacent physical byte instead of wrapping back to offset 0x0000 of
the same segment. Confirmed by running it before the fix: the emulator
returned `AX=0x8899` (the byte pattern planted at the buggy linear-read
location) instead of the real-hardware-correct `AX=0x1234` (planted at the
correctly-wrapped location).

Fixed in `decode_instruction()` (i8086.hxx): when `ip` is close enough to the
top of the segment that a maximal 6-byte instruction (opcode + modrm + disp16
+ imm16, the longest form this decoder produces) could cross 0xFFFF, the
bytes are copied through a small wrap-aware scratch buffer (`_wrap_scratch`,
filled via `mbyte()`, which does wrap correctly) before decoding, so every
existing `_pcode`-relative read downstream keeps working unchanged. All 51
tests here pass, and the full `tests/`/`tests/undocumented/` corpus (328
files) still passes after the fix.

The math/compare tests below (`3C_cmp_al_signed_overflow` through
`F7_idiv_ax_negative_32768_quirk`) were added in a second pass, covering
every flag-setting mathematical operation -- ADD, ADC, SUB, SBB, CMP, AND,
OR, XOR, NEG, MUL, IMUL, DIV, IDIV -- each with both an 8-bit and 16-bit
case, picked the same way: by reading the corresponding `op_*` function and
choosing operands that land on its specific boundary or quirk.

A third pass (`CF_iret_trap_deferred_one_instruction` through
`9F_lahf_packs_bits`) covers every remaining opcode this emulator
implements that hadn't been touched yet: string instructions, flag/segment
instructions, far call/jmp/ret, and a couple of previously-completely-untested
groups (`8F` pop-to-memory, `LEA`). Most of these are correctness locks for
opcodes that simply had zero coverage before, but a few are genuine quirks
found by reading the source -- most notably the IRET one-instruction trap
deferral, which had no test at all despite being real, deliberately-coded
logic (`fIgnoreTrap` in `handle_state()`).

A fourth, small pass (`D2_shr_bl_cl9_preserves_cf_of` through
`C0_aliases_ret_imm16`) rounds out the shift/rotate group -- `SHR`/`ROR`/`RCR`
had no coverage at all even though `ROL`/`RCL`/`SHL` did -- and found one
more real quirk along the way: `op_shr8`/`op_shr16`'s large-count boundary
does *not* clear CF/OF the way `op_sal8`/`op_sal16`'s equivalent branch
does, and `0xC0` is not an 80186-style immediate-count shift on real 8086 at
all -- it aliases `0xC2` (`RET imm16`).

## What each test targets

| File | Targets |
|---|---|
| `89_modrm_disp8_wrap` | `get_rm_ptr_common()`'s `mod=1` (8-bit disp) path landing on offset 0xFFFF -- the existing wraptests only cover the `mod=0,rm=6` direct-address case. |
| `8B_modrm_disp16_wrap` | Same, for the `mod=2` (16-bit disp) path. |
| `87_xchg_bx_wrap` | Same, for the `mod=0` register-indirect path (`[BX]`), read+write via XCHG. |
| `FF_inc_wrap` | Read-modify-write (`INC word [BX+SI]`) at the wrap boundary. |
| `FF_push_rm_wrap` | Reads a wrapped memory operand, then pushes it onto an unrelated, non-wrapped stack -- two independent mechanisms in one instruction. |
| `81_add_imm_wrap` | `[BP+SI]` defaults to the **SS** segment, not DS (`get_displacement_seg()`) -- the other five wrap tests all default to DS. |
| `B8_fetch_wrap_exploratory` | See "Status" above -- code/immediate fetch across a segment-relative wrap. Found and fixed a real bug; kept as a regression lock. |
| `54_push_sp_wrap` | `PUSH SP` (i8086.cxx:1286-1289) pushes `sp-2` (the post-decrement value, an 8086-specific quirk vs. 80286+), combined with SP=1 so that value itself wraps the stack write. |
| `5D_pop_wrap` | Symmetric read-side stack wrap: `POP` with SP=0xFFFF. |
| `EB_jmp_short_wrap_backward` | `JMP short` IP arithmetic wrapping below 0x0000. |
| `E9_jmp_near_wrap_forward` | `JMP near` IP arithmetic wrapping past 0xFFFF. |
| `E2_loop_cx0_wraps_taken` | `LOOP` decrements *then* tests CX -- CX=0 wraps to 0xFFFF, which is nonzero, so the branch **is** taken. |
| `E2_loop_cx1_not_taken` | CX=1 decrements to 0 -- branch not taken. Paired with the above so the two outcomes are both explicit. |
| `E3_jcxz_cx0_taken` | `JCXZ` with CX=0, the trivial edge JCXZ exists for. |
| `D7_xlat_overflow` | `XLAT`'s `bx+al` computation overflows 16 bits without carrying into the segment. |
| `40_inc_ax_wrap` | `INC` at 0xFFFF->0x0000: CF must be *preserved* (INC never touches it, unlike ADD), AF set (nibble carry), OF clear. |
| `FE_dec_al_signed_overflow` | Byte `DEC` at the 0x80->0x7F signed-overflow boundary. |
| `05_add_ax_signed_overflow` | Word `ADD` at the 0x7FFF->0x8000 signed-overflow boundary. |
| `2C_sub_al_nibble_borrow` | `SUB` producing a nibble borrow (AF) without an overall byte borrow (CF). |
| `D2_rcl_bl_cl9_full_period` | `RCL` (rotate-through-carry) has a **9-bit** period for a byte (8 value bits + carry) -- CL=9 must restore both the value and the original CF. Distinct from plain rotate's 8-bit period; easy to conflate. |
| `D2_rol_bl_cl9_equals_cl1` | Plain `ROL` has an 8-bit period, so CL=9 must equal CL=1. 8086 does not mask the shift count like 80286+ does. |
| `D2_shl_bl_cl9_forced_zero` | `op_sal8`'s hand-coded `shift > 8` boundary (i8086.cxx:465-485) forces the byte result to 0 -- not something a generic per-bit loop bound would produce on its own. |
| `D3_shl_ax_cl17_forced_zero` | The word counterpart (`op_sal16`, `shift > 16`) -- a separately hand-coded boundary that could drift out of sync with the byte version. |
| `27_daa_double_adjust` | `DAA` with AL=0x9A: both the low-nibble and high-nibble adjustment branches fire in the same instruction. |
| `2F_das_double_adjust` | Same, for `DAS`. |
| `37_aaa_carries_into_ah` | `AAA`: low-nibble-over-9 carries into AH; unlike DAA/DAS, `op_aaa` never calls `set_PSZ8`, so PF/ZF/SF must be left exactly as they were. |
| `3F_aas_borrows_from_ah` | Same, for `AAS` (borrows from AH instead of carrying). |
| `D4_aam_nondefault_divisor` | `AAM` supports an arbitrary immediate divisor (i8086.cxx:1853-1869), not just the conventional 0x0A. |
| `F7_div_by_zero_sp_wrap` | `DIV` by zero triggers `op_interrupt()`, which pushes flags/CS/IP in that order (i8086.cxx:717-739); SP=5 makes the 3rd push (of IP) land exactly at SP=0xFFFF. Also confirms IF and TF are cleared in the *final* flags register while the *pushed* flags still carry the pre-interrupt values -- a scenario the random corpus could only hit by roughly a 1-in-10,000 coincidence. |
| `3C_cmp_al_signed_overflow` | `CMP` at the byte signed-overflow boundary; confirms AL itself is left unmodified (only `do_math8`'s default/discard-result branch runs). |
| `3D_cmp_ax_signed_overflow` | Word counterpart -- AX unmodified. |
| `14_adc_al_carry_in_wraps` | `ADC` with AL=0xFF and CF=1 in: the carry-in alone wraps AL to 0, distinct from plain ADD which would leave AL=0xFF unchanged for `+0`. |
| `15_adc_ax_carry_in_wraps` | Word counterpart. |
| `1C_sbb_al_borrow_in_wraps` | `SBB` with AL=0 and CF=1 in: the borrow-in alone wraps AL to 0xFF. |
| `1D_sbb_ax_borrow_in_wraps` | Word counterpart. |
| `24_and_al_clears_cf_of` | `AND`/`OR`/`XOR` all call `reset_carry_overflow()` unconditionally but never touch AF (i8086.cxx:188-234) -- CF/OF are set beforehand and must come back clear, while AF must survive untouched. |
| `25_and_ax_clears_cf_of` | Word counterpart. |
| `0C_or_al_zero_result` | `OR` producing a zero result (ZF=1) while also exercising the same forced-CF/OF-clear as the AND tests. |
| `0D_or_ax_zero_result` | Word counterpart. |
| `34_xor_al_self_cancel` | `XOR` of a value with itself (self-cancelling to 0), same forced-clear behavior. |
| `35_xor_ax_self_cancel` | Word counterpart. |
| `F6_neg_al_cant_negate` | `NEG` of the most-negative byte (0x80): the C library idiom "can't negate INT_MIN" applies here too -- OF set, value unchanged. |
| `F7_neg_ax_cant_negate` | Word counterpart (0x8000). |
| `F6_mul_al_sf_from_low_byte` | `MUL` (byte): an undocumented-but-real-hardware quirk (i8086.cxx:878, "documentation says undefined, but real hardware does this") where SF reflects bit 7 of the product's *low byte*, not bit 15 of the full 16-bit AX result -- operands chosen so the two would disagree if the quirk weren't implemented. |
| `F7_mul_ax_overflow` | `MUL` (word): result needs DX, CF/OF set. No SF quirk at this width -- `op_f7`'s MUL branch has no equivalent override. |
| `F6_imul_al_negate_overflow` | `IMUL` (byte): -128 * -1 = +128, which cannot be represented as a signed byte -- same "can't represent the negation" theme as NEG, one level up. |
| `F7_imul_ax_negate_overflow` | Word counterpart: -32768 * -1 = +32768. |
| `F6_div_al_quotient_overflow` | `DIV` (byte) with a nonzero divisor but a quotient too large for AL -- triggers the same `op_interrupt()` entry as divide-by-zero, via `op_f6`'s separate `result <= 0xff` check. |
| `F7_div_ax_quotient_overflow` | Word counterpart (`result <= 0xffff` in `op_f7`). |
| `F6_idiv_al_negative_128_quirk` | `IDIV` (byte): a quotient that mathematically comes out to exactly -128 is rejected (i8086.cxx:918, "odd that -128 is invalid, but that's how it works") -- one more negative quotient value than positive is invalid, a real hardware quirk this emulator deliberately reproduces. |
| `F7_idiv_ax_negative_32768_quirk` | Word counterpart: a quotient of exactly -32768 is rejected the same way (i8086.cxx:1019). |
| `CF_iret_trap_deferred_one_instruction` | `IRET` popping a flags word with TF newly 0->1: `handle_state()`'s `fIgnoreTrap` (i8086.cxx:1135-1145) suppresses the trap check for exactly the next instruction; the one after that traps *before* it executes. A three-instruction-slot sequence (IRET, a NOP that must run, a second NOP that must never be decoded) -- previously zero coverage despite being real, deliberate logic. |
| `E1_loopz_zf_blocks_branch` | `LOOPZ` with CX=5 (decrements to a nonzero 4) but ZF=0: the branch is blocked by ZF alone, distinct from the pure-CX gating the existing `E2_loop_*` tests cover. |
| `E0_loopnz_zf_blocks_branch` | Same, for `LOOPNZ` with ZF=1. |
| `A4_rep_movsb_cx0_zero_iterations` | `REP MOVSB` with CX=0 runs *zero* iterations, not one -- SI/DI/memory all untouched, only IP advances past the 2-byte prefixed instruction. |
| `A4_movsb_df1_wraps_si_di` | `MOVSB` with DF=1, SI=DI=0: both decrement to 0xFFFF after the copy -- the string opcodes' own byte-at-a-time index update (`update_index8`, i8086.cxx:111-117), a separate mechanism from the `get_rm_ptr_common()` wrap already covered above. |
| `CE_into_of0_noop` | `INTO` with OF=0 is a pure no-op. |
| `CE_into_of1_interrupts` | `INTO` with OF=1 triggers INT4 through the same `op_interrupt()` entry path as DIV-by-zero. |
| `EA_jmp_far_fetch_wrap` | A second, harder regression test for the `decode_instruction()` fetch-wrap fix (see "Status" above): the wrap boundary here falls *inside* the destination IP's own 2-byte field, splitting its low/high bytes across 0xFFFF/0x0000, not just between the opcode byte and an immediate field like the original `B8` case. |
| `8F_pop_mem_sp_wrap` | `POP r/m16` (to memory, not a register) with SP=0xFFFF: a different code path (`get_rm_ptr16()`+`write_word()`) from the register-form pops already covered, with the same read-side stack wrap. |
| `26_lea_ignores_segment_override` | `LEA` with an ES: prefix: `get_rm_ea()` (i8086.hxx:541-568) never calls `get_seg_value()`/`get_displacement_seg()`, so the override is parsed (and costs a byte) but has zero effect on the loaded offset. |
| `98_cbw_sign_extends` | `CBW` with AL=0x80 (negative): AH becomes 0xFF. |
| `99_cwd_sign_extends` | `CWD` with AX=0x8000 (negative): DX becomes 0xFFFF. |
| `D5_aad_nondefault_base` | `AAD` supports an arbitrary immediate base (i8086.cxx:1871-1878), the read-side counterpart of the existing `D4_aam_nondefault_divisor` test. |
| `9A_call_far_push_order` | `CALL FAR`: pushes old CS, then the return IP, in that order -- a previously entirely untested opcode. |
| `CB_retf_pop_order` | `RETF`: pops IP then CS, the reverse of CALL FAR's push order. |
| `C2_ret_imm16_sp_wrap` | `RET imm16` with SP=0xFFFC: `pop()`'s own SP advance plus the immediate SP adjustment (`sp += b12()`) together wrap SP. |
| `9C_pushf_value` | `PUSHF`: pushes the exact current flags word -- untested opcode. |
| `9D_popf_value` | `POPF`: restores all 9 real flag bits from the stack exactly. |
| `9E_sahf_selective_bits` | `SAHF` only transfers SF/ZF/AF/PF/CF from AH (i8086.cxx:827-835) -- OF/DF/IF/TF must survive completely untouched, proven by setting them all beforehand. |
| `9F_lahf_packs_bits` | `LAHF`'s counterpart: packs the same five bits into AH, leaving OF/DF/IF unread. |
| `D2_shr_bl_cl9_preserves_cf_of` | `SHR` (byte) at its count>8 forced-zero boundary: unlike `SHL`'s equivalent branch, `op_shr8` never touches CF/OF there -- they're left exactly as they were, a real asymmetry between the two that's easy to assume is symmetric. |
| `D3_shr_ax_cl17_preserves_cf_of` | Word counterpart. |
| `D2_ror_bl_cl9_equals_cl1` | `ROR`'s missing counterpart to the existing `ROL` period test -- CL=9 must equal CL=1 (8-bit period). |
| `D2_rcr_bl_cl9_full_period` | `RCR`'s missing counterpart to `RCL` -- CL=9 (9-bit rotate-through-carry period) restores both the value and the original CF. |
| `C0_aliases_ret_imm16` | `0xC0` is undefined in Intel's docs (immediate-count shifts are an 80186+ addition), but this emulator reproduces real 8086/8088 silicon's actual behavior: it decodes identically to `0xC2` (`RET imm16`), not as a shift (i8086.cxx:1706-1708). |
