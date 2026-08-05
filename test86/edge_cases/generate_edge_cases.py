#!/usr/bin/env python3
"""
Generates the intentional edge-case JSON tests in this folder.

Unlike tests/ (10,000 randomly-generated, hardware-validated cases per
opcode from SingleStepTests), these are hand-derived: each one targets a
specific branch or documented quirk in i8086.cxx/i8086.hxx, picked by
reading the emulator source rather than sampling randomly. See README.md
for the list and the reasoning behind each one.

This is a from-scratch, independent reference model of 8086 semantics --
it deliberately does NOT call into the emulator under test, to avoid
circularity. Where the emulator's own logic is already known to match real
hardware (DAA/DAS/AAA/AAS, cited in i8086.cxx as sourced from
righto.com's reverse-engineering), this model mirrors that exact logic
rather than re-deriving BCD rules independently.

Run `python3 generate_edge_cases.py` from this directory to (re)write
every *.json file here.

test86.cxx parses these files with plain strstr/strchr, not a real JSON
parser, so field order matters: it locates the target opcode from the
FILENAME (not the JSON body), and locates each register by searching
forward for literal substrings like "ax", so the `regs` key order below
must exactly match the existing corpus:
  ax,bx,cx,dx,cs,ss,ds,es,sp,bp,si,di,ip,flags
The `cycles` field is never read by test86.cxx. `queue` is only used as a
text anchor to bound the `ram` array and to locate `final.ip`, so an empty
list is safe there.
"""

import json
import os
import re

# ---------------- flag bit positions (must match i8086.hxx materializeFlags) ----------------
CF, PF, AF, ZF, SF, TF, IFB, DF, OF = 0, 2, 4, 6, 7, 8, 9, 10, 11
ALWAYS_ON = 0xF002  # bits that are always set on real hardware, meaningless otherwise


def parity_even(v):
    return bin(v & 0xFF).count("1") % 2 == 0


class Flags:
    def __init__(self, cf=False, pf=False, af=False, zf=False, sf=False,
                 tf=False, iflag=False, df=False, of=False):
        self.cf, self.pf, self.af, self.zf, self.sf = cf, pf, af, zf, sf
        self.tf, self.iflag, self.df, self.of = tf, iflag, df, of

    def to_int(self):
        v = ALWAYS_ON
        if self.cf: v |= 1 << CF
        if self.pf: v |= 1 << PF
        if self.af: v |= 1 << AF
        if self.zf: v |= 1 << ZF
        if self.sf: v |= 1 << SF
        if self.tf: v |= 1 << TF
        if self.iflag: v |= 1 << IFB
        if self.df: v |= 1 << DF
        if self.of: v |= 1 << OF
        return v

    def copy(self):
        return Flags(self.cf, self.pf, self.af, self.zf, self.sf,
                      self.tf, self.iflag, self.df, self.of)


# ---------------- small address/byte helpers ----------------

def lo(v): return v & 0xFF
def hi(v): return (v >> 8) & 0xFF
def w16(v): return v & 0xFFFF
def phys(seg, off): return ((seg << 4) + (off & 0xFFFF)) & 0xFFFFF


# ---------------- reference model: mirrors i8086.cxx's op_* functions exactly ----------------

def set_psz8(f, val):
    val &= 0xFF
    f.pf = parity_even(val)
    f.zf = (val == 0)
    f.sf = bool(val & 0x80)


def set_psz16(f, val):
    val &= 0xFFFF
    f.pf = parity_even(val & 0xFF)
    f.zf = (val == 0)
    f.sf = bool(val & 0x8000)


def add8(f, lhs, rhs, carry=False):
    c = 1 if carry else 0
    res16 = lhs + rhs + c
    res8 = res16 & 0xFF
    f.cf = bool(res16 & 0x100)
    set_psz8(f, res8)
    f.of = (not ((lhs ^ rhs) & 0x80)) and bool((lhs ^ res8) & 0x80)
    f.af = bool(((0xF & lhs) + (0xF & rhs) + c) & 0x10)
    return res8


def add16(f, lhs, rhs, carry=False):
    c = 1 if carry else 0
    res32 = lhs + rhs + c
    res16 = res32 & 0xFFFF
    f.cf = bool(res32 & 0x10000)
    set_psz16(f, res16)
    f.of = (not ((lhs ^ rhs) & 0x8000)) and bool((lhs ^ res16) & 0x8000)
    f.af = bool(((0xF & lhs) + (0xF & rhs) + c) & 0x10)
    return res16


def sub8(f, lhs, rhs, borrow=False):
    com_rhs = (~rhs) & 0xFF
    b = 0 if borrow else 1
    res16 = lhs + com_rhs + b
    res8 = res16 & 0xFF
    f.cf = (res16 & 0x100) == 0
    set_psz8(f, res8)
    f.of = (not ((lhs ^ com_rhs) & 0x80)) and bool((lhs ^ res8) & 0x80)
    # equivalent to the C++ `((lhs&0xf)-(rhs&0xf)-borrow) & ~0xf`: that raw nibble
    # difference is only ever in [-16,15], and is negative exactly when a nibble
    # borrow occurred, which is exactly when the C++ mask comes out non-zero.
    f.af = ((lhs & 0xF) - (rhs & 0xF) - (1 if borrow else 0)) < 0
    return res8


def sub16(f, lhs, rhs, borrow=False):
    com_rhs = (~rhs) & 0xFFFF
    b = 0 if borrow else 1
    res32 = lhs + com_rhs + b
    res16 = res32 & 0xFFFF
    f.cf = (res32 & 0x10000) == 0
    set_psz16(f, res16)
    f.of = (not ((lhs ^ com_rhs) & 0x8000)) and bool((lhs ^ res16) & 0x8000)
    f.af = ((lhs & 0xF) - (rhs & 0xF) - (1 if borrow else 0)) < 0
    return res16


def inc8(f, val):
    f.of = (val == 0x7F)
    val = (val + 1) & 0xFF
    f.af = (val & 0xF) == 0
    set_psz8(f, val)
    return val


def inc16(f, val):
    f.of = (val == 0x7FFF)
    val = (val + 1) & 0xFFFF
    f.af = (val & 0xF) == 0
    set_psz16(f, val)
    return val


def dec8(f, val):
    f.of = (val == 0x80)
    val = (val - 1) & 0xFF
    f.af = (val & 0xF) == 0xF
    set_psz8(f, val)
    return val


# and8/and16/or8/or16/xor8/xor16: CF/OF are unconditionally cleared (reset_carry_overflow()
# in the C++), but AF is never touched at all -- it's left exactly as it was before.

def and8(f, lhs, rhs):
    v = lhs & rhs
    set_psz8(f, v)
    f.cf = f.of = False
    return v


def and16(f, lhs, rhs):
    v = lhs & rhs
    set_psz16(f, v)
    f.cf = f.of = False
    return v


def or8(f, lhs, rhs):
    v = lhs | rhs
    set_psz8(f, v)
    f.cf = f.of = False
    return v


def or16(f, lhs, rhs):
    v = lhs | rhs
    set_psz16(f, v)
    f.cf = f.of = False
    return v


def xor8(f, lhs, rhs):
    v = lhs ^ rhs
    set_psz8(f, v)
    f.cf = f.of = False
    return v


def xor16(f, lhs, rhs):
    v = lhs ^ rhs
    set_psz16(f, v)
    f.cf = f.of = False
    return v


def cmp8(f, lhs, rhs):
    """CMP is SUB with the result discarded -- only flags change."""
    sub8(f, lhs, rhs)


def cmp16(f, lhs, rhs):
    sub16(f, lhs, rhs)


def neg8(f, val):
    return sub8(f, 0, val)


def neg16(f, val):
    return sub16(f, 0, val)


def mul8(f, al, rhs):
    """AL * r/m8 -> AX (unsigned). Mirrors op_f6's reg==4 branch, including the
    undocumented-but-real-hardware quirk where SF reflects bit 7 of the LOW byte
    of the 16-bit product (i.e. bit 7 of the result, not bit 15 of AX)."""
    ax = (al * rhs) & 0xFFFF
    f.cf = f.of = (hi(ax) != 0)
    set_psz16(f, ax)
    f.sf = bool(ax & 0x80)
    return ax


def mul16(f, ax, rhs):
    """AX * r/m16 -> DX:AX (unsigned). Mirrors op_f7's reg==4 branch -- no SF quirk here."""
    result = (ax * rhs) & 0xFFFFFFFF
    dx = (result >> 16) & 0xFFFF
    newax = result & 0xFFFF
    f.cf = f.of = (result > 0xFFFF)
    set_psz16(f, newax)
    return newax, dx


def _to_s8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def _to_s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def imul8(f, al, rhs):
    """AL * r/m8 -> AX (signed). Mirrors op_f6's reg==5 branch."""
    result = _to_s16(_to_s8(al)) * _to_s16(_to_s8(rhs))
    ax = result & 0xFFFF
    masked = result & 0xFFFFFF80  # matches the C++'s 32-bit-wide mask exactly
    f.cf = f.of = (masked != 0 and masked != (0xFFFFFF80))
    set_psz16(f, ax)
    return ax


def imul16(f, ax0, rhs):
    """AX * r/m16 -> DX:AX (signed). Mirrors op_f7's reg==5 branch."""
    result = _to_s16(ax0) * _to_s16(rhs)
    combined = result & 0xFFFFFFFF
    dx = (combined >> 16) & 0xFFFF
    newax = combined & 0xFFFF
    masked = combined & 0xFFFF8000
    f.cf = f.of = (masked != 0 and masked != 0xFFFF8000)
    set_psz16(f, newax)
    return newax, dx


def interrupt_entry(ss, sp, cs, ret_ip, flags_before, isr_cs, isr_ip):
    """Mirrors i8086::op_interrupt exactly: push(flags), push(cs), push(ip+len),
    then clear TF/IF (the pushed flags keep the pre-interrupt values). Returns
    (final_sp, [(addr, byte), ...] for the three pushes, final Flags)."""
    sp = w16(sp)
    pushes = []
    for val in (flags_before.to_int(), cs, ret_ip):
        sp = w16(sp - 1)
        pushes.append((phys(ss, sp), hi(val)))
        sp = w16(sp - 1)
        pushes.append((phys(ss, sp), lo(val)))
    final_flags = flags_before.copy()
    final_flags.tf = False
    final_flags.iflag = False
    return sp, pushes, final_flags


def ivt_ram(vector, isr_cs, isr_ip):
    base = vector * 4
    ip_lo, ip_hi = phys(0, base), phys(0, base + 1)
    cs_lo, cs_hi = phys(0, base + 2), phys(0, base + 3)
    return [(ip_lo, lo(isr_ip)), (ip_hi, hi(isr_ip)),
            (cs_lo, lo(isr_cs)), (cs_hi, hi(isr_cs))]


def sim_push(ss, sp, val):
    """Mirrors i8086::push(): high byte first, then low byte, each after its
    own sp--. Returns (new_sp, [(addr, byte), (addr, byte)])."""
    sp = w16(sp - 1)
    hi_entry = (phys(ss, sp), hi(val))
    sp = w16(sp - 1)
    lo_entry = (phys(ss, sp), lo(val))
    return sp, [hi_entry, lo_entry]


def stack_word_ram(ss, sp, val):
    """Ram entries for a word already sitting on the stack at ss:sp, laid out
    the way i8086::pop() expects to read it (low byte at sp, high at sp+1) --
    for pre-populating a stack pop() or ret/iret will consume."""
    return [(phys(ss, sp), lo(val)), (phys(ss, w16(sp + 1)), hi(val))]


def rol8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    orig = val & 0xFF
    v = orig
    for _ in range(shift):
        high = bool(v & 0x80)
        v = (v << 1) & 0xFF
        if high:
            v |= 1
        f.cf = high
    f.of = bool((v & 0x80) != (orig & 0x80))
    return v


def rcl8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    v = val & 0xFF
    for _ in range(shift):
        new_carry = bool(v & 0x80)
        v = (v << 1) & 0xFF
        if f.cf:
            v |= 1
        f.cf = new_carry
    f.of = bool(v & 0x80) != f.cf
    return v


def sal8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    if shift > 8:
        f.cf = False
        v = 0
    else:
        orig = val & 0xFF
        v = (orig << (shift - 1)) & 0xFF
        f.cf = bool(v & 0x80)
        v = (v << 1) & 0xFF
        f.of = bool((orig & 0x80) != (v & 0x80))
    set_psz8(f, v)
    return v


def sal16(f, val, shift):
    if shift == 0:
        return val & 0xFFFF
    if shift > 16:
        f.cf = False
        v = 0
    else:
        orig = val & 0xFFFF
        v = (orig << (shift - 1)) & 0xFFFF
        f.cf = bool(v & 0x8000)
        v = (v << 1) & 0xFFFF
        f.of = bool((orig & 0x8000) != (v & 0x8000))
    set_psz16(f, v)
    return v


def ror8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    v = val & 0xFF
    for _ in range(shift):
        low = bool(v & 1)
        v >>= 1
        if low:
            v |= 0x80
        f.cf = low
    f.of = bool(v & 0x80) != bool(v & 0x40)
    return v


def rcr8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    v = val & 0xFF
    for _ in range(shift):
        new_carry = bool(v & 1)
        v >>= 1
        if f.cf:
            v |= 0x80
        f.cf = new_carry
    f.of = bool(v & 0x80) != bool(v & 0x40)
    return v


def shr8(f, val, shift):
    if shift == 0:
        return val & 0xFF
    if shift > 8:
        # unlike op_sal8's shift>8 branch, op_shr8's does NOT touch cf/of here --
        # both are simply left as whatever they already were.
        v = 0
    else:
        orig = val & 0xFF
        f.of = bool(orig & 0x80)
        v = orig >> (shift - 1)
        f.cf = bool(v & 1)
        v >>= 1
    set_psz8(f, v)
    return v


def shr16(f, val, shift):
    if shift == 0:
        return val & 0xFFFF
    if shift > 16:
        v = 0  # same as shr8: cf/of untouched here
    else:
        orig = val & 0xFFFF
        f.of = bool(orig & 0x8000)
        v = orig >> (shift - 1)
        f.cf = bool(v & 1)
        v >>= 1
    set_psz16(f, v)
    return v


def daa(f, al):
    old_al = al
    al_check = 0x9F if f.af else 0x99
    if (al & 0xF) > 9 or f.af:
        al = (al + 6) & 0xFF
        f.af = True
    if old_al > al_check or f.cf:
        al = (al + 0x60) & 0xFF
        f.cf = True
    set_psz8(f, al)
    return al


def das(f, al):
    old_al = al
    al_check = 0x9F if f.af else 0x99
    if (al & 0xF) > 9 or f.af:
        al = (al - 6) & 0xFF
        f.af = True
    else:
        f.af = False
    if old_al > al_check or f.cf:
        al = (al - 0x60) & 0xFF
        f.cf = True
    else:
        f.cf = False
    set_psz8(f, al)
    return al


def aaa(f, al, ah):
    if (al & 0xF) > 9 or f.af:
        al = (al + 6) & 0xFF
        ah = (ah + 1) & 0xFF
        f.af = True
        f.cf = True
    else:
        f.af = False
        f.cf = False
    al = al & 0x0F
    return al, ah


def aas(f, al, ah):
    if (al & 0xF) > 9 or f.af:
        new_al = (al - 6) & 0xFF
        ah = (ah - 1) & 0xFF
        f.af = True
        f.cf = True
        al = new_al & 0x0F
    else:
        f.af = False
        f.cf = False
        al = al & 0x0F
    return al, ah


def aam(f, al, imm):
    ah = al // imm
    al2 = al % imm
    set_psz8(f, al2)
    return al2, ah


# ---------------- JSON assembly helpers ----------------

def regs(ax=0, bx=0, cx=0, dx=0, cs=0, ss=0, ds=0, es=0,
         sp=0, bp=0, si=0, di=0, ip=0, flags=ALWAYS_ON):
    return {"ax": ax, "bx": bx, "cx": cx, "dx": dx, "cs": cs, "ss": ss,
            "ds": ds, "es": es, "sp": sp, "bp": bp, "si": si, "di": di,
            "ip": ip, "flags": flags}


TESTS = []  # list of (opcode_hex, slug, test_dict)


def add_test(opcode_hex, slug, name, byte_list, initial_regs, initial_ram,
             final_regs, final_ram):
    # test86.cxx copies "name" into a fixed 100-byte acname[] buffer and fails
    # loudly if it doesn't fit -- see README.md for the full rationale instead.
    if len(name) >= 100:
        raise SystemExit(f"name too long ({len(name)} bytes) for {opcode_hex}_{slug}: {name!r}")
    test_hash = ("manual_" + opcode_hex + "_" + slug).ljust(64, "0")[:64]
    test = {
        "name": name,
        "bytes": byte_list,
        "initial": {
            "regs": initial_regs,
            "ram": [list(e) for e in initial_ram],
            "queue": [],
        },
        "final": {
            "regs": final_regs,
            "ram": [list(e) for e in final_ram],
            "queue": [],
        },
        "cycles": [],
        "test_hash": test_hash,
    }
    TESTS.append((opcode_hex, slug, test))


def code_ram(cs, ip, byte_list):
    """Ram entries for the instruction bytes themselves, at physical cs:ip.."""
    return [(phys(cs, ip + i), b) for i, b in enumerate(byte_list)]


# =====================================================================
# 1. Segment-offset wrap via each get_rm_ptr_common() addressing path
#    (the existing *_wraptest.json files only cover the mod=0,rm=6
#    direct-address special case; these cover the other three).
# =====================================================================

# 1a. mod=1 (8-bit displacement), write: MOV [SI+7Fh], AX  (opcode 89, rm=4/SI)
def t_89_mod1_disp8_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    si = 0xFF90
    disp8 = 0x6F  # si + disp8 == 0xFFFF
    ax = 0x1234
    bytes_ = [0x89, 0x44, disp8]  # modrm: mod=01 reg=000(ax) rm=100(si)
    ir = regs(ax=ax, ds=ds, si=si, cs=cs, ip=ip)
    fr = regs(ax=ax, ds=ds, si=si, cs=cs, ip=ip + len(bytes_))
    lo_addr, hi_addr = phys(ds, 0xFFFF), phys(ds, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, 0), (hi_addr, 0)]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(ax)), (hi_addr, hi(ax))]
    add_test("89", "modrm_disp8_wrap",
              "mov [si+7Fh], ax  mod=1 8-bit-disp address wraps to 0xFFFF",
              bytes_, ir, iram, fr, fram)


# 1b. mod=2 (16-bit displacement), read: MOV AX, [DI+0FFFEh]  (opcode 8B, rm=5/DI)
def t_8b_mod2_disp16_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    di = 0x0001
    disp16 = 0xFFFE  # di + disp16 == 0xFFFF (mod 0x10000)
    memval = 0x5678
    bytes_ = [0x8B, 0x85, lo(disp16), hi(disp16)]  # mod=10 reg=000(ax) rm=101(di)
    ir = regs(ax=0, ds=ds, di=di, cs=cs, ip=ip)
    fr = regs(ax=memval, ds=ds, di=di, cs=cs, ip=ip + len(bytes_))
    lo_addr, hi_addr = phys(ds, 0xFFFF), phys(ds, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(memval)), (hi_addr, hi(memval))]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(memval)), (hi_addr, hi(memval))]
    add_test("8B", "modrm_disp16_wrap",
              "mov ax, [di+0FFFEh]  mod=2 16-bit-disp address wraps to 0xFFFF",
              bytes_, ir, iram, fr, fram)


# 1c. mod=0 register-indirect (no displacement), read+write via XCHG: XCHG AX, [BX]
def t_87_xchg_bx_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    bx = 0xFFFF
    ax0, mem0 = 0xCAFE, 0xBEEF
    bytes_ = [0x87, 0x07]  # mod=00 reg=000(ax) rm=111(bx)
    ir = regs(ax=ax0, ds=ds, bx=bx, cs=cs, ip=ip)
    fr = regs(ax=mem0, ds=ds, bx=bx, cs=cs, ip=ip + len(bytes_))
    lo_addr, hi_addr = phys(ds, 0xFFFF), phys(ds, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(mem0)), (hi_addr, hi(mem0))]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(ax0)), (hi_addr, hi(ax0))]
    add_test("87", "xchg_bx_wrap",
              "xchg ax, [bx]  mod=0 register-indirect [bx] wraps to 0xFFFF",
              bytes_, ir, iram, fr, fram)


# 1d. mod=0 register-indirect, read-modify-write: INC word [BX+SI]  (opcode FF /0)
def t_ff_inc_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    bx, si = 0, 0xFFFF
    memval = 0x00FF
    bytes_ = [0xFF, 0x00]  # mod=00 reg=000(/0 inc) rm=000(bx+si)
    ir = regs(ds=ds, bx=bx, si=si, cs=cs, ip=ip)
    f = Flags()
    newval = inc16(f, memval)
    fr = regs(ds=ds, bx=bx, si=si, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    lo_addr, hi_addr = phys(ds, 0xFFFF), phys(ds, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(memval)), (hi_addr, hi(memval))]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(newval)), (hi_addr, hi(newval))]
    add_test("FF", "inc_wrap",
              "inc word [bx+si]  mod=0 read-modify-write at 0xFFFF",
              bytes_, ir, iram, fr, fram)


# 1e. mod=0 register-indirect, read-then-push: PUSH word [BX+DI]  (opcode FF /6)
def t_ff_push_rm_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    ss, sp = 0x2000, 0x0010
    bx, di = 0, 0xFFFF
    memval = 0x4321
    bytes_ = [0xFF, 0x31]  # mod=00 reg=110(/6 push) rm=001(bx+di)
    ir = regs(ds=ds, ss=ss, sp=sp, bx=bx, di=di, cs=cs, ip=ip)
    new_sp = w16(sp - 2)
    fr = regs(ds=ds, ss=ss, sp=new_sp, bx=bx, di=di, cs=cs, ip=ip + len(bytes_))
    src_lo, src_hi = phys(ds, 0xFFFF), phys(ds, 0x0000)
    dst_lo, dst_hi = phys(ss, new_sp), phys(ss, new_sp + 1)
    iram = code_ram(cs, ip, bytes_) + [(src_lo, lo(memval)), (src_hi, hi(memval))]
    fram = code_ram(cs, ip, bytes_) + [(src_lo, lo(memval)), (src_hi, hi(memval)),
                                        (dst_lo, lo(memval)), (dst_hi, hi(memval))]
    add_test("FF", "push_rm_wrap",
              "push word [bx+di]  reads a wrapped operand, pushes normally",
              bytes_, ir, iram, fr, fram)


# 1f. mod=0 register-indirect using the SS-default segment (rm=bp+si),
#     immediate-to-memory write: ADD word [BP+SI], 10h  (opcode 81 /0)
def t_81_add_imm_wrap():
    cs, ip = 0, 0
    ss = 0x2000  # rm=2 (bp+si) defaults to SS, not DS -- exercises that branch
    bp, si = 0, 0xFFFF
    memval = 0x000A
    imm = 0x0010
    bytes_ = [0x81, 0x02, lo(imm), hi(imm)]  # mod=00 reg=000(/0 add) rm=010(bp+si)
    ir = regs(ss=ss, bp=bp, si=si, cs=cs, ip=ip)
    f = Flags()
    newval = add16(f, memval, imm)
    fr = regs(ss=ss, bp=bp, si=si, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    lo_addr, hi_addr = phys(ss, 0xFFFF), phys(ss, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(memval)), (hi_addr, hi(memval))]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(newval)), (hi_addr, hi(newval))]
    add_test("81", "add_imm_wrap",
              "add word [bp+si], 10h  SS-default-segment address wraps to 0xFFFF",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 2. Instruction fetch across a segment-relative 0xFFFF->0x0000 boundary.
#    EXPLORATORY: decode_instruction()/b12() read the opcode stream via a
#    raw pointer, not through the wrap-aware read_word(), so this is
#    expected to currently FAIL for a non-HMA segment. Encoded with the
#    real hardware-correct expectation; do not "fix" this test to match
#    a wrong result if it fails -- report the bug instead.
# =====================================================================

def t_b8_fetch_wrap_exploratory():
    cs, ip = 0, 0xFFFF
    correct_imm = 0x1234
    buggy_linear_bytes = 0x8899  # what a non-wrap-aware raw pointer read would see
    bytes_ = [0xB8, lo(correct_imm), hi(correct_imm)]  # mov ax, imm16
    ir = regs(cs=cs, ip=ip)
    final_ip = w16(ip + 3)
    fr = regs(ax=correct_imm, cs=cs, ip=final_ip)
    opcode_addr = phys(cs, 0xFFFF)
    wrapped_lo, wrapped_hi = phys(cs, 0x0000), phys(cs, 0x0001)
    linear_lo, linear_hi = opcode_addr + 1, opcode_addr + 2
    iram = [
        (opcode_addr, 0xB8),
        (wrapped_lo, lo(correct_imm)), (wrapped_hi, hi(correct_imm)),
        (linear_lo, lo(buggy_linear_bytes)), (linear_hi, hi(buggy_linear_bytes)),
    ]
    fram = [
        (opcode_addr, 0xB8),
        (wrapped_lo, lo(correct_imm)), (wrapped_hi, hi(correct_imm)),
        (linear_lo, lo(buggy_linear_bytes)), (linear_hi, hi(buggy_linear_bytes)),
    ]
    add_test("B8", "fetch_wrap_exploratory",
              "mov ax, 1234h at ip=0xFFFF: does imm16 fetch wrap the segment offset?",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 3. Stack SP wraparound
# =====================================================================

# 3a. PUSH SP (opcode 54): documented 8086 quirk of pushing sp-2 (post-decrement),
#     combined with SP=1 forcing the pushed value -- and the write itself -- to wrap.
def t_54_push_sp_wrap():
    cs, ip = 0, 0
    ss, sp = 0x2000, 1
    bytes_ = [0x54]
    pushed = w16(sp - 2)  # 0xFFFF
    new_sp = w16(sp - 2)  # push() always leaves sp == the value it pushed here
    ir = regs(ss=ss, sp=sp, cs=cs, ip=ip)
    fr = regs(ss=ss, sp=new_sp, cs=cs, ip=ip + len(bytes_))
    lo_addr, hi_addr = phys(ss, 0xFFFF), phys(ss, 0x0000)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(pushed)), (hi_addr, hi(pushed))]
    add_test("54", "push_sp_wrap",
              "push sp with sp=1: pushes sp-2 (8086 quirk), forcing a stack wrap",
              bytes_, ir, iram, fr, fram)


# 3b. POP BP (opcode 5D) with SP=0xFFFF: symmetric read-side stack wrap.
def t_5d_pop_wrap():
    cs, ip = 0, 0
    ss, sp = 0x2000, 0xFFFF
    val = 0x1234
    bytes_ = [0x5D]
    ir = regs(ss=ss, sp=sp, cs=cs, ip=ip)
    new_sp = w16(sp + 2)
    fr = regs(ss=ss, sp=new_sp, bp=val, cs=cs, ip=ip + len(bytes_))
    lo_addr, hi_addr = phys(ss, 0xFFFF), phys(ss, 0x0000)
    iram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(val)), (hi_addr, hi(val))]
    fram = code_ram(cs, ip, bytes_) + [(lo_addr, lo(val)), (hi_addr, hi(val))]
    add_test("5D", "pop_wrap",
              "pop bp with sp=0xFFFF: high byte read wraps to offset 0x0000",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 4. IP wraparound on branch
# =====================================================================

def t_eb_jmp_short_wrap_backward():
    cs, ip = 0, 2
    rel8 = 0xFA  # -6
    bytes_ = [0xEB, rel8]
    ir = regs(cs=cs, ip=ip)
    final_ip = w16(ip + 2 + (rel8 - 0x100))  # sign-extend rel8
    fr = regs(cs=cs, ip=final_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("EB", "jmp_short_wrap_backward",
              "jmp short -6 from ip=2 wraps ip backward past 0x0000 to 0xFFFE",
              bytes_, ir, iram, fr, fram)


def t_e9_jmp_near_wrap_forward():
    cs, ip = 0, 0xFFF0
    rel16 = 0x0020
    bytes_ = [0xE9, lo(rel16), hi(rel16)]
    ir = regs(cs=cs, ip=ip)
    final_ip = w16(ip + 3 + rel16)
    fr = regs(cs=cs, ip=final_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E9", "jmp_near_wrap_forward",
              "jmp near +0020h from ip=0FFF0h wraps ip forward past 0xFFFF to 0x0013",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 5. LOOP/JCXZ CX edge behavior
# =====================================================================

def t_e2_loop_cx0_wraps_taken():
    cs, ip = 0, 0
    rel8 = 0x10
    bytes_ = [0xE2, rel8]
    ir = regs(cx=0, cs=cs, ip=ip)
    final_ip = w16(ip + 2 + rel8)
    fr = regs(cx=0xFFFF, cs=cs, ip=final_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E2", "loop_cx0_wraps_taken",
              "loop with cx=0: decrements to 0xFFFF (wraps), nonzero, branch taken",
              bytes_, ir, iram, fr, fram)


def t_e2_loop_cx1_not_taken():
    cs, ip = 0, 0
    rel8 = 0x10
    bytes_ = [0xE2, rel8]
    ir = regs(cx=1, cs=cs, ip=ip)
    fr = regs(cx=0, cs=cs, ip=ip + len(bytes_))
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E2", "loop_cx1_not_taken",
              "loop with cx=1: decrements to 0, branch not taken",
              bytes_, ir, iram, fr, fram)


def t_e3_jcxz_cx0_taken():
    cs, ip = 0, 0
    rel8 = 0x10
    bytes_ = [0xE3, rel8]
    ir = regs(cx=0, cs=cs, ip=ip)
    final_ip = w16(ip + 2 + rel8)
    fr = regs(cx=0, cs=cs, ip=final_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E3", "jcxz_cx0_taken",
              "jcxz with cx=0: branch taken, cx left unmodified",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 6. XLAT: AL+BX overflow wraps only the 16-bit offset, not the segment
# =====================================================================

def t_d7_xlat_overflow():
    cs, ip = 0, 0
    ds = 0x2000
    bx, al0 = 0xFFFF, 0x02
    tableval = 0x77
    bytes_ = [0xD7]
    ir = regs(ax=al0, bx=bx, ds=ds, cs=cs, ip=ip)
    fr = regs(ax=tableval, bx=bx, ds=ds, cs=cs, ip=ip + len(bytes_))
    addr = phys(ds, w16(bx + al0))
    iram = code_ram(cs, ip, bytes_) + [(addr, tableval)]
    fram = code_ram(cs, ip, bytes_) + [(addr, tableval)]
    add_test("D7", "xlat_overflow",
              "xlat with bx=0xFFFF, al=2: bx+al truncates to 16 bits, no segment carry",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 7. Arithmetic flag boundaries
# =====================================================================

def t_40_inc_ax_wrap():
    cs, ip = 0, 0
    bytes_ = [0x40]
    f = Flags(cf=True)  # prove INC does not touch CF
    newval = inc16(f, 0xFFFF)
    ir = regs(ax=0xFFFF, cs=cs, ip=ip, flags=Flags(cf=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("40", "inc_ax_wrap",
              "inc ax with ax=0xFFFF: wraps to 0, CF preserved, AF set, OF clear",
              bytes_, ir, iram, fr, fram)


def t_fe_dec_al_signed_overflow():
    cs, ip = 0, 0
    bytes_ = [0xFE, 0xC8]  # mod=11 reg=001(/1 dec) rm=000(al)
    f = Flags()
    newval = dec8(f, 0x80)
    ir = regs(ax=0x0080, cs=cs, ip=ip)
    fr = regs(ax=(0x00 << 8) | newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("FE", "dec_al_signed_overflow",
              "dec al with al=0x80: byte signed-overflow boundary, OF set",
              bytes_, ir, iram, fr, fram)


def t_05_add_ax_signed_overflow():
    cs, ip = 0, 0
    imm = 0x0001
    bytes_ = [0x05, lo(imm), hi(imm)]
    f = Flags()
    newval = add16(f, 0x7FFF, imm)
    ir = regs(ax=0x7FFF, cs=cs, ip=ip)
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("05", "add_ax_signed_overflow",
              "add ax, 1 with ax=0x7FFF: word signed-overflow boundary, OF set, SF set",
              bytes_, ir, iram, fr, fram)


def t_2c_sub_al_nibble_borrow():
    cs, ip = 0, 0
    imm = 0x01
    bytes_ = [0x2C, imm]
    f = Flags()
    newval = sub8(f, 0x10, imm)
    ir = regs(ax=0x0010, cs=cs, ip=ip)
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("2C", "sub_al_nibble_borrow",
              "sub al, 1 with al=0x10: nibble borrow sets AF, no byte borrow, CF clear",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 8. Shift/rotate boundary semantics
# =====================================================================

def t_d2_rcl_bl_cl9_full_period():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xD3]  # mod=11 reg=010(/2 rcl) rm=011(bl)
    bl0 = 0xA5
    f = Flags(cf=False)
    newval = rcl8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip, flags=Flags(cf=False).to_int())
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "rcl_bl_cl9_full_period",
              "rcl bl, cl with cl=9: 9-bit rotate-through-carry period restores value+cf",
              bytes_, ir, iram, fr, fram)


def t_d2_rol_bl_cl9_equals_cl1():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xC3]  # mod=11 reg=000(/0 rol) rm=011(bl)
    bl0 = 0xA5
    f = Flags(cf=False)
    newval = rol8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip, flags=Flags(cf=False).to_int())
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "rol_bl_cl9_equals_cl1",
              "rol bl, cl with cl=9: 8-bit rotate period means this equals cl=1",
              bytes_, ir, iram, fr, fram)


def t_d2_shl_bl_cl9_forced_zero():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xE3]  # mod=11 reg=100(/4 shl) rm=011(bl)
    bl0 = 0xFF
    f = Flags()
    newval = sal8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip)
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "shl_bl_cl9_forced_zero",
              "shl bl, cl with cl=9 (byte, count>8): op_sal8's forced-zero boundary",
              bytes_, ir, iram, fr, fram)


def t_d3_shl_ax_cl17_forced_zero():
    cs, ip = 0, 0
    bytes_ = [0xD3, 0xE0]  # mod=11 reg=100(/4 shl) rm=000(ax)
    ax0 = 0xFFFF
    f = Flags()
    newval = sal16(f, ax0, 17)
    ir = regs(ax=ax0, cx=17, cs=cs, ip=ip)
    fr = regs(ax=newval, cx=17, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D3", "shl_ax_cl17_forced_zero",
              "shl ax, cl with cl=17 (word, count>16): op_sal16's forced-zero boundary",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 9. BCD adjust branch coverage (mirrors op_daa/op_das/op_aaa/op_aas exactly,
#    which are themselves cited as matching real-hardware behavior)
# =====================================================================

def t_27_daa_double_adjust():
    cs, ip = 0, 0
    bytes_ = [0x27]
    f = Flags(cf=False, af=False)
    newval = daa(f, 0x9A)
    ir = regs(ax=0x9A, cs=cs, ip=ip, flags=Flags(cf=False, af=False).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("27", "daa_double_adjust",
              "daa with al=0x9A: both the low-nibble and high-nibble adjustments fire",
              bytes_, ir, iram, fr, fram)


def t_2f_das_double_adjust():
    cs, ip = 0, 0
    bytes_ = [0x2F]
    f = Flags(cf=False, af=False)
    newval = das(f, 0x9A)
    ir = regs(ax=0x9A, cs=cs, ip=ip, flags=Flags(cf=False, af=False).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("2F", "das_double_adjust",
              "das with al=0x9A: both the low-nibble and high-nibble adjustments fire",
              bytes_, ir, iram, fr, fram)


def t_37_aaa_carries_into_ah():
    cs, ip = 0, 0
    bytes_ = [0x37]
    f = Flags(af=False, cf=False, pf=True, zf=True, sf=True)  # prove psz bits untouched
    al2, ah2 = aaa(f, 0x0F, 0x00)
    ir = regs(ax=0x000F, cs=cs, ip=ip,
              flags=Flags(af=False, cf=False, pf=True, zf=True, sf=True).to_int())
    fr = regs(ax=(ah2 << 8) | al2, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("37", "aaa_carries_into_ah",
              "aaa with al=0x0F: carries into ah; unlike daa/das, PF/ZF/SF untouched",
              bytes_, ir, iram, fr, fram)


def t_3f_aas_borrows_from_ah():
    cs, ip = 0, 0
    bytes_ = [0x3F]
    f = Flags(af=False, cf=False, pf=True, zf=True, sf=True)  # prove psz bits untouched
    al2, ah2 = aas(f, 0x0F, 0x01)
    ir = regs(ax=0x010F, cs=cs, ip=ip,
              flags=Flags(af=False, cf=False, pf=True, zf=True, sf=True).to_int())
    fr = regs(ax=(ah2 << 8) | al2, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("3F", "aas_borrows_from_ah",
              "aas with al=0x0F, ah=1: low nibble > 9 borrows from ah; PF/ZF/SF untouched",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 10. AAM with a non-default immediate divisor (0xD4 supports any imm8,
#     not just the conventional 0x0A)
# =====================================================================

def t_d4_aam_nondefault_divisor():
    cs, ip = 0, 0
    imm = 0x10
    al0 = 0x22  # 34 decimal
    bytes_ = [0xD4, imm]
    f = Flags()
    al2, ah2 = aam(f, al0, imm)
    ir = regs(ax=al0, cs=cs, ip=ip)
    fr = regs(ax=(ah2 << 8) | al2, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D4", "aam_nondefault_divisor",
              "aam 10h with al=34: D4 supports any immediate divisor, not just 0Ah",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 11. DIV-by-zero interrupt entry with SP wraparound mid-push-sequence
# =====================================================================

def t_f7_div_by_zero_sp_wrap():
    cs, ip = 0, 0x0010
    ss, sp0 = 0x3000, 5
    isr_cs, isr_ip = 0x9999, 0x8888
    ax0, dx0 = 0x1111, 0x2222
    bytes_ = [0xF7, 0xF3]  # mod=11 reg=110(/6 div) rm=011(bx) -- div bx, bx=0
    initial_flags = Flags(tf=True, iflag=True)  # prove both get cleared on entry
    bc = len(bytes_)  # op_f7's own instruction-length count for the pushed return ip
    return_ip = w16(ip + bc)

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ax=ax0, dx=dx0, bx=0, ss=ss, sp=sp0, cs=cs, ip=ip,
              flags=initial_flags.to_int())
    fr = regs(ax=ax0, dx=dx0, bx=0, ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip,
              flags=final_flags.to_int())

    ivt = ivt_ram(0, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("F7", "div_by_zero_sp_wrap",
              "div bx=0, sp=5: 3rd interrupt-entry push wraps sp; TF/IF cleared on entry",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 12. CMP: like SUB, but the operand must be left UNCHANGED -- only flags update
# =====================================================================

def t_3c_cmp_al_signed_overflow():
    cs, ip = 0, 0
    imm = 0x01
    bytes_ = [0x3C, imm]
    f = Flags()
    al0 = 0x80
    cmp8(f, al0, imm)
    ir = regs(ax=al0, cs=cs, ip=ip)
    fr = regs(ax=al0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())  # al UNCHANGED
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("3C", "cmp_al_signed_overflow",
              "cmp al, 1 with al=0x80: signed-overflow flags, al itself untouched",
              bytes_, ir, iram, fr, fram)


def t_3d_cmp_ax_signed_overflow():
    cs, ip = 0, 0
    imm = 0x0001
    bytes_ = [0x3D, lo(imm), hi(imm)]
    f = Flags()
    ax0 = 0x8000
    cmp16(f, ax0, imm)
    ir = regs(ax=ax0, cs=cs, ip=ip)
    fr = regs(ax=ax0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())  # ax UNCHANGED
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("3D", "cmp_ax_signed_overflow",
              "cmp ax, 1 with ax=0x8000: signed-overflow flags, ax itself untouched",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 13. ADC/SBB: carry-in propagation, distinct from plain ADD/SUB
# =====================================================================

def t_14_adc_al_carry_in_wraps():
    cs, ip = 0, 0
    imm = 0x00
    bytes_ = [0x14, imm]
    f = Flags(cf=True)
    newval = add8(f, 0xFF, imm, carry=True)
    ir = regs(ax=0xFF, cs=cs, ip=ip, flags=Flags(cf=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("14", "adc_al_carry_in_wraps",
              "adc al, 0 with al=0xFF, cf=1: carry-in alone wraps al to 0, cf stays set",
              bytes_, ir, iram, fr, fram)


def t_15_adc_ax_carry_in_wraps():
    cs, ip = 0, 0
    imm = 0x0000
    bytes_ = [0x15, lo(imm), hi(imm)]
    f = Flags(cf=True)
    newval = add16(f, 0xFFFF, imm, carry=True)
    ir = regs(ax=0xFFFF, cs=cs, ip=ip, flags=Flags(cf=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("15", "adc_ax_carry_in_wraps",
              "adc ax, 0 with ax=0xFFFF, cf=1: carry-in alone wraps ax to 0",
              bytes_, ir, iram, fr, fram)


def t_1c_sbb_al_borrow_in_wraps():
    cs, ip = 0, 0
    imm = 0x00
    bytes_ = [0x1C, imm]
    f = Flags(cf=True)
    newval = sub8(f, 0x00, imm, borrow=True)
    ir = regs(ax=0x00, cs=cs, ip=ip, flags=Flags(cf=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("1C", "sbb_al_borrow_in_wraps",
              "sbb al, 0 with al=0, cf=1: borrow-in alone wraps al to 0xFF",
              bytes_, ir, iram, fr, fram)


def t_1d_sbb_ax_borrow_in_wraps():
    cs, ip = 0, 0
    imm = 0x0000
    bytes_ = [0x1D, lo(imm), hi(imm)]
    f = Flags(cf=True)
    newval = sub16(f, 0x0000, imm, borrow=True)
    ir = regs(ax=0x0000, cs=cs, ip=ip, flags=Flags(cf=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("1D", "sbb_ax_borrow_in_wraps",
              "sbb ax, 0 with ax=0, cf=1: borrow-in alone wraps ax to 0xFFFF",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 14. AND/OR/XOR: CF/OF are unconditionally cleared, but AF is left untouched
#     (neither documented, but that's what op_and8/or8/xor8 actually do)
# =====================================================================

def t_24_and_al_clears_cf_of():
    cs, ip = 0, 0
    imm = 0x0F
    bytes_ = [0x24, imm]
    f = Flags(cf=True, of=True, af=True)
    newval = and8(f, 0xFF, imm)
    ir = regs(ax=0xFF, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("24", "and_al_clears_cf_of",
              "and al, 0Fh: cf/of forced clear even though set beforehand, af untouched",
              bytes_, ir, iram, fr, fram)


def t_25_and_ax_clears_cf_of():
    cs, ip = 0, 0
    imm = 0x00FF
    bytes_ = [0x25, lo(imm), hi(imm)]
    f = Flags(cf=True, of=True, af=True)
    newval = and16(f, 0xFFFF, imm)
    ir = regs(ax=0xFFFF, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("25", "and_ax_clears_cf_of",
              "and ax, 00FFh: cf/of forced clear even though set beforehand, af untouched",
              bytes_, ir, iram, fr, fram)


def t_0c_or_al_zero_result():
    cs, ip = 0, 0
    imm = 0x00
    bytes_ = [0x0C, imm]
    f = Flags(cf=True, of=True, af=True)
    newval = or8(f, 0x00, imm)
    ir = regs(ax=0x00, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("0C", "or_al_zero_result",
              "or al, 0 with al=0: zf set, cf/of forced clear despite being set before",
              bytes_, ir, iram, fr, fram)


def t_0d_or_ax_zero_result():
    cs, ip = 0, 0
    imm = 0x0000
    bytes_ = [0x0D, lo(imm), hi(imm)]
    f = Flags(cf=True, of=True, af=True)
    newval = or16(f, 0x0000, imm)
    ir = regs(ax=0x0000, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("0D", "or_ax_zero_result",
              "or ax, 0 with ax=0: zf set, cf/of forced clear despite being set before",
              bytes_, ir, iram, fr, fram)


def t_34_xor_al_self_cancel():
    cs, ip = 0, 0
    imm = 0xFF
    bytes_ = [0x34, imm]
    f = Flags(cf=True, of=True, af=True)
    newval = xor8(f, 0xFF, imm)
    ir = regs(ax=0xFF, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("34", "xor_al_self_cancel",
              "xor al, 0FFh with al=0xFF: cancels to 0, cf/of forced clear",
              bytes_, ir, iram, fr, fram)


def t_35_xor_ax_self_cancel():
    cs, ip = 0, 0
    imm = 0xFFFF
    bytes_ = [0x35, lo(imm), hi(imm)]
    f = Flags(cf=True, of=True, af=True)
    newval = xor16(f, 0xFFFF, imm)
    ir = regs(ax=0xFFFF, cs=cs, ip=ip, flags=Flags(cf=True, of=True, af=True).to_int())
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("35", "xor_ax_self_cancel",
              "xor ax, 0FFFFh with ax=0xFFFF: cancels to 0, cf/of forced clear",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 15. NEG: the classic "can't negate the most negative value" boundary
# =====================================================================

def t_f6_neg_al_cant_negate():
    cs, ip = 0, 0
    bytes_ = [0xF6, 0xD8]  # mod=11 reg=011(/3 neg) rm=000(al)
    f = Flags()
    newval = neg8(f, 0x80)
    ir = regs(ax=0x80, cs=cs, ip=ip)
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F6", "neg_al_cant_negate",
              "neg al with al=0x80: most-negative byte can't be negated, of set",
              bytes_, ir, iram, fr, fram)


def t_f7_neg_ax_cant_negate():
    cs, ip = 0, 0
    bytes_ = [0xF7, 0xD8]  # mod=11 reg=011(/3 neg) rm=000(ax)
    f = Flags()
    newval = neg16(f, 0x8000)
    ir = regs(ax=0x8000, cs=cs, ip=ip)
    fr = regs(ax=newval, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F7", "neg_ax_cant_negate",
              "neg ax with ax=0x8000: most-negative word can't be negated, of set",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 16. MUL/IMUL boundary behavior
# =====================================================================

def t_f6_mul_al_sf_from_low_byte():
    cs, ip = 0, 0
    bytes_ = [0xF6, 0xE3]  # mod=11 reg=100(/4 mul) rm=011(bl)
    al0, bl0 = 0x81, 0x01
    f = Flags()
    newax = mul8(f, al0, bl0)
    ir = regs(ax=al0, bx=bl0, cs=cs, ip=ip)
    fr = regs(ax=newax, bx=bl0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F6", "mul_al_sf_from_low_byte",
              "mul bl, al=0x81 bl=1: sf quirk reflects bit7 of low byte, not ax bit15",
              bytes_, ir, iram, fr, fram)


def t_f7_mul_ax_overflow():
    cs, ip = 0, 0
    bytes_ = [0xF7, 0xE3]  # mod=11 reg=100(/4 mul) rm=011(bx)
    ax0, bx0 = 0xFFFF, 0xFFFF
    f = Flags()
    newax, newdx = mul16(f, ax0, bx0)
    ir = regs(ax=ax0, bx=bx0, cs=cs, ip=ip)
    fr = regs(ax=newax, dx=newdx, bx=bx0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F7", "mul_ax_overflow",
              "mul bx with ax=bx=0xFFFF: product needs dx, cf/of set",
              bytes_, ir, iram, fr, fram)


def t_f6_imul_al_negate_overflow():
    cs, ip = 0, 0
    bytes_ = [0xF6, 0xEB]  # mod=11 reg=101(/5 imul) rm=011(bl)
    al0, bl0 = 0x80, 0xFF  # -128 * -1 = +128, doesn't fit back in a signed byte
    f = Flags()
    newax = imul8(f, al0, bl0)
    ir = regs(ax=al0, bx=bl0, cs=cs, ip=ip)
    fr = regs(ax=newax, bx=bl0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F6", "imul_al_negate_overflow",
              "imul bl with al=-128, bl=-1: +128 doesn't fit in a signed byte, cf/of set",
              bytes_, ir, iram, fr, fram)


def t_f7_imul_ax_negate_overflow():
    cs, ip = 0, 0
    bytes_ = [0xF7, 0xEB]  # mod=11 reg=101(/5 imul) rm=011(bx)
    ax0, bx0 = 0x8000, 0xFFFF  # -32768 * -1 = +32768, doesn't fit back in a signed word
    f = Flags()
    newax, newdx = imul16(f, ax0, bx0)
    ir = regs(ax=ax0, bx=bx0, cs=cs, ip=ip)
    fr = regs(ax=newax, dx=newdx, bx=bx0, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("F7", "imul_ax_negate_overflow",
              "imul bx with ax=-32768, bx=-1: +32768 doesn't fit in a signed word, cf/of set",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 17. DIV/IDIV: quotient-overflow (not divide-by-zero) triggers the same
#     interrupt-0 entry, and IDIV has the documented "one more negative value
#     than positive" asymmetry (see the op_f6/op_f7 "odd that -Nh is invalid"
#     comments) -- overflow even though the mathematically-true quotient would
#     otherwise fit in a signed byte/word.
# =====================================================================

def t_f6_div_al_quotient_overflow():
    cs, ip = 0, 0x0010
    ss, sp0 = 0x3000, 0x0020
    isr_cs, isr_ip = 0x9999, 0x8888
    bytes_ = [0xF6, 0xF3]  # mod=11 reg=110(/6 div) rm=011(bl) -- div bl
    ax0, bl0 = 0xFF00, 0x01  # quotient 0xFF00 > 0xff, overflows an 8-bit result
    initial_flags = Flags()
    return_ip = w16(ip + len(bytes_))

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ax=ax0, bx=bl0, ss=ss, sp=sp0, cs=cs, ip=ip, flags=initial_flags.to_int())
    fr = regs(ax=ax0, bx=bl0, ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip,
              flags=final_flags.to_int())

    ivt = ivt_ram(0, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("F6", "div_al_quotient_overflow",
              "div bl with ax=0xFF00, bl=1: quotient > 0xff overflows, triggers int0",
              bytes_, ir, iram, fr, fram)


def t_f7_div_ax_quotient_overflow():
    cs, ip = 0, 0x0010
    ss, sp0 = 0x3000, 0x0020
    isr_cs, isr_ip = 0x9999, 0x8888
    bytes_ = [0xF7, 0xF3]  # mod=11 reg=110(/6 div) rm=011(bx) -- div bx
    dx0, ax0, bx0 = 0x0001, 0x0000, 0x0001  # dividend 0x10000, quotient overflows 16 bits
    initial_flags = Flags()
    return_ip = w16(ip + len(bytes_))

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ax=ax0, dx=dx0, bx=bx0, ss=ss, sp=sp0, cs=cs, ip=ip,
              flags=initial_flags.to_int())
    fr = regs(ax=ax0, dx=dx0, bx=bx0, ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip,
              flags=final_flags.to_int())

    ivt = ivt_ram(0, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("F7", "div_ax_quotient_overflow",
              "div bx with dx:ax=0x10000, bx=1: quotient > 0xffff overflows, triggers int0",
              bytes_, ir, iram, fr, fram)


def t_f6_idiv_al_negative_128_quirk():
    cs, ip = 0, 0x0010
    ss, sp0 = 0x3000, 0x0020
    isr_cs, isr_ip = 0x9999, 0x8888
    bytes_ = [0xF6, 0xFB]  # mod=11 reg=111(/7 idiv) rm=011(bl) -- idiv bl
    ax0, bl0 = 0x0080, 0x01  # 128/1=128, negated to -128 -- rejected (see op_f6 comment)
    initial_flags = Flags()
    return_ip = w16(ip + len(bytes_))

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ax=ax0, bx=bl0, ss=ss, sp=sp0, cs=cs, ip=ip, flags=initial_flags.to_int())
    fr = regs(ax=ax0, bx=bl0, ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip,
              flags=final_flags.to_int())

    ivt = ivt_ram(0, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("F6", "idiv_al_negative_128_quirk",
              "idiv bl with ax=128, bl=1: quotient -128 is rejected, one-off asymmetry",
              bytes_, ir, iram, fr, fram)


def t_f7_idiv_ax_negative_32768_quirk():
    cs, ip = 0, 0x0010
    ss, sp0 = 0x3000, 0x0020
    isr_cs, isr_ip = 0x9999, 0x8888
    bytes_ = [0xF7, 0xFB]  # mod=11 reg=111(/7 idiv) rm=011(bx) -- idiv bx
    dx0, ax0, bx0 = 0x0000, 0x8000, 0x0001  # 32768/1=32768, negated to -32768 -- rejected
    initial_flags = Flags()
    return_ip = w16(ip + len(bytes_))

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ax=ax0, dx=dx0, bx=bx0, ss=ss, sp=sp0, cs=cs, ip=ip,
              flags=initial_flags.to_int())
    fr = regs(ax=ax0, dx=dx0, bx=bx0, ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip,
              flags=final_flags.to_int())

    ivt = ivt_ram(0, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("F7", "idiv_ax_negative_32768_quirk",
              "idiv bx with dx:ax=32768, bx=1: result -32768 is rejected, same quirk",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 18. IRET's one-instruction trap deferral (fIgnoreTrap in handle_state()).
#     When IRET pops a flags word with TF newly set (was 0, now 1), the very
#     NEXT instruction runs without trapping; the one after THAT traps before
#     it executes. This needs three instruction-slots in one test: IRET, a
#     NOP that must run normally, and a second NOP that must be preempted by
#     the resulting INT1 before it ever executes.
# =====================================================================

def t_cf_iret_trap_deferred_one_instruction():
    cs = 0
    ss, sp0 = 0x2000, 0x0010
    iret_target_ip = 0x0020
    isr_cs, isr_ip = 0x9999, 0x7777
    popped_flags = Flags(tf=True)  # previous TF was 0 (see initial regs) -- a 0->1 edge

    bytes_ = [0xCF]  # iret; the two NOPs live at iret_target_ip, not here
    initial_flags = Flags()  # tf=False: this is the "previousTrap" IRET compares against

    # Stack layout iret pops from: ip, then cs, then flags (mirrors op_interrupt's
    # push order flags/cs/ip in reverse).
    stack_ram = (stack_word_ram(ss, sp0, iret_target_ip) +
                 stack_word_ram(ss, w16(sp0 + 2), cs) +
                 stack_word_ram(ss, w16(sp0 + 4), popped_flags.to_int()))
    sp_after_iret = w16(sp0 + 6)

    # Instruction #2 (right after iret): a NOP that must execute normally
    # (fIgnoreTrap suppresses the trap check just this once).
    nop1_ip = iret_target_ip
    # Instruction #3: a second NOP whose bytes are present but must NEVER be
    # decoded/executed -- the trap fires in handle_state() before it does.
    nop2_ip = w16(iret_target_ip + 1)

    # The trap's own interrupt entry: return address is nop2_ip with length 0
    # (op_interrupt(1, 0) -- there's nothing to skip past, it never ran).
    final_sp, trap_pushes, final_flags = interrupt_entry(
        ss, sp_after_iret, cs, nop2_ip, popped_flags, isr_cs, isr_ip)

    # handle_state() falls through into decoding the ISR's own first byte
    # within the SAME emulate(1) call that injects the trap -- a NOP there
    # advances ip by one more, to isr_ip+1.
    isr_nop_ip = w16(isr_ip + 1)

    ir = regs(ss=ss, sp=sp0, cs=cs, ip=0, flags=initial_flags.to_int())
    fr = regs(ss=ss, sp=final_sp, cs=isr_cs, ip=isr_nop_ip, flags=final_flags.to_int())

    ivt = ivt_ram(1, isr_cs, isr_ip)
    iram = (code_ram(cs, 0, bytes_) + code_ram(cs, nop1_ip, [0x90]) +
            code_ram(cs, nop2_ip, [0x90]) + code_ram(isr_cs, isr_ip, [0x90]) +
            stack_ram + ivt)
    fram = (code_ram(cs, 0, bytes_) + code_ram(cs, nop1_ip, [0x90]) +
            code_ram(cs, nop2_ip, [0x90]) + code_ram(isr_cs, isr_ip, [0x90]) +
            ivt + trap_pushes)
    add_test("CF", "iret_trap_deferred_one_instruction",
              "iret setting tf=1 (was 0): next instr runs, the one after traps first",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 19. LOOPZ/LOOPNZ: like LOOP (already covered), CX is decremented and
#     tested first, but the branch also requires ZF to match -- these two
#     tests show ZF alone blocking the branch even though CX is nonzero.
# =====================================================================

def t_e1_loopz_zf_blocks_branch():
    cs, ip = 0, 0
    rel8 = 0x10
    bytes_ = [0xE1, rel8]
    f = Flags(zf=False)
    ir = regs(cx=5, cs=cs, ip=ip, flags=f.to_int())
    fr = regs(cx=4, cs=cs, ip=ip + len(bytes_), flags=f.to_int())  # not taken
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E1", "loopz_zf_blocks_branch",
              "loopz with cx=5, zf=0: cx decrements to 4 (nonzero) but zf=0 blocks it",
              bytes_, ir, iram, fr, fram)


def t_e0_loopnz_zf_blocks_branch():
    cs, ip = 0, 0
    rel8 = 0x10
    bytes_ = [0xE0, rel8]
    f = Flags(zf=True)
    ir = regs(cx=5, cs=cs, ip=ip, flags=f.to_int())
    fr = regs(cx=4, cs=cs, ip=ip + len(bytes_), flags=f.to_int())  # not taken
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("E0", "loopnz_zf_blocks_branch",
              "loopnz with cx=5, zf=1: cx decrements to 4 (nonzero) but zf=1 blocks it",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 20. String instructions: REP with cx=0 runs zero iterations (not one), and
#     the direction flag's decrement path can wrap si/di just like the
#     increment path can overflow -- both untested by the wrap-address suite
#     above, which only exercised get_rm_ptr_common()'s mechanism, not the
#     string opcodes' own separate byte-at-a-time index update.
# =====================================================================

def t_f3a4_rep_movsb_cx0_zero_iterations():
    cs, ip = 0, 0
    ds, es = 0x1000, 0x3000
    si0, di0 = 0x0050, 0x0060
    bytes_ = [0xF3, 0xA4]  # rep movsb
    ir = regs(cx=0, ds=ds, es=es, si=si0, di=di0, cs=cs, ip=ip)
    fr = regs(cx=0, ds=ds, es=es, si=si0, di=di0, cs=cs, ip=ip + len(bytes_))
    src, dst = phys(ds, si0), phys(es, di0)
    iram = code_ram(cs, ip, bytes_) + [(src, 0xAA), (dst, 0xBB)]
    fram = code_ram(cs, ip, bytes_) + [(src, 0xAA), (dst, 0xBB)]  # untouched
    add_test("A4", "rep_movsb_cx0_zero_iterations",
              "rep movsb with cx=0: zero iterations, si/di/memory all untouched",
              bytes_, ir, iram, fr, fram)


def t_a4_movsb_df1_wraps_si_di():
    cs, ip = 0, 0
    ds, es = 0x1000, 0x3000
    bytes_ = [0xA4]  # movsb, no rep
    val = 0x42
    f = Flags(df=True)
    ir = regs(ds=ds, es=es, si=0, di=0, cs=cs, ip=ip, flags=f.to_int())
    fr = regs(ds=ds, es=es, si=0xFFFF, di=0xFFFF, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    src, dst = phys(ds, 0), phys(es, 0)
    iram = code_ram(cs, ip, bytes_) + [(src, val), (dst, 0)]
    fram = code_ram(cs, ip, bytes_) + [(src, val), (dst, val)]
    add_test("A4", "movsb_df1_wraps_si_di",
              "movsb with df=1, si=di=0: both decrement to 0xFFFF (wrap) after the copy",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 21. INTO: conditionally interrupts only when OF=1 -- an all-or-nothing
#     branch (unlike the jcc opcodes, this one either fully no-ops or fully
#     enters an interrupt, both worth locking in explicitly).
# =====================================================================

def t_ce_into_of0_noop():
    cs, ip = 0, 0
    bytes_ = [0xCE]
    f = Flags(of=False)
    ir = regs(cs=cs, ip=ip, flags=f.to_int())
    fr = regs(cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("CE", "into_of0_noop",
              "into with of=0: no-op, ip just advances past it",
              bytes_, ir, iram, fr, fram)


def t_ce_into_of1_interrupts():
    cs, ip = 0, 0x0100  # clear of IVT vector 4's own entry at physical 0x10-0x13
    ss, sp0 = 0x2000, 0x0020
    isr_cs, isr_ip = 0x9999, 0x8888
    bytes_ = [0xCE]
    initial_flags = Flags(of=True)
    return_ip = w16(ip + len(bytes_))  # op_interrupt(4, 1) -- length 1

    final_sp, pushes, final_flags = interrupt_entry(
        ss, sp0, cs, return_ip, initial_flags, isr_cs, isr_ip)

    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip, flags=initial_flags.to_int())
    fr = regs(ss=ss, sp=final_sp, cs=isr_cs, ip=isr_ip, flags=final_flags.to_int())

    ivt = ivt_ram(4, isr_cs, isr_ip)
    iram = code_ram(cs, ip, bytes_) + ivt
    fram = code_ram(cs, ip, bytes_) + ivt + pushes
    add_test("CE", "into_of1_interrupts",
              "into with of=1: triggers int4 via the same op_interrupt entry path",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 22. Far jump instruction-fetch wrap regression: a second, harder case for
#     the decode_instruction() fix -- here the wrap boundary falls INSIDE a
#     2-byte immediate field (the destination ip's own low/high bytes are on
#     opposite sides of the 0xffff/0x0000 split), not just between the
#     opcode byte and an immediate field like the original B8 regression test.
# =====================================================================

def t_ea_jmp_far_fetch_wrap():
    cs, ip = 0, 0xFFFE
    new_ip, new_cs = 0x1234, 0x5678
    bytes_ = [0xEA, lo(new_ip), hi(new_ip), lo(new_cs), hi(new_cs)]
    ir = regs(cs=cs, ip=ip)
    fr = regs(cs=new_cs, ip=new_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("EA", "jmp_far_fetch_wrap",
              "jmp far 5678:1234 from ip=0xFFFE: wrap falls inside the ip immediate itself",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 23. POP r/m16 (to memory, not a register): a different code path
#     (get_rm_ptr16()+write_word()) than the register-form pops already
#     covered -- SP wrap on the read side, ordinary write on the other end.
# =====================================================================

def t_8f_pop_mem_sp_wrap():
    cs, ip = 0, 0
    ds = 0x1000
    ss, sp0 = 0x2000, 0xFFFF
    popped = 0xBEEF
    bytes_ = [0x8F, 0x00]  # mod=00 reg=000 rm=000(bx+si) -- pop [bx+si]
    ir = regs(ds=ds, ss=ss, sp=sp0, bx=0, si=0, cs=cs, ip=ip)
    new_sp = w16(sp0 + 2)
    fr = regs(ds=ds, ss=ss, sp=new_sp, bx=0, si=0, cs=cs, ip=ip + len(bytes_))
    src = stack_word_ram(ss, sp0, popped)  # low byte at 0xFFFF, high wraps to 0x0000
    dst_addr = phys(ds, 0)
    iram = code_ram(cs, ip, bytes_) + src + [(dst_addr, 0), (dst_addr + 1, 0)]
    fram = code_ram(cs, ip, bytes_) + src + [(dst_addr, lo(popped)), (dst_addr + 1, hi(popped))]
    add_test("8F", "pop_mem_sp_wrap",
              "pop [bx+si] with sp=0xFFFF: read side wraps like the register pop tests",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 24. LEA: get_rm_ea() (i8086.hxx:541-568) never calls get_seg_value() or
#     get_displacement_seg() -- a segment override prefix is parsed (and
#     costs its bytes/cycles) but has zero effect on the loaded value, since
#     LEA only ever produces a bare offset, never a physical address.
# =====================================================================

def t_8d_lea_ignores_segment_override():
    cs, ip = 0, 0
    bx0, si0 = 0x1234, 0x0001
    bytes_ = [0x26, 0x8D, 0x00]  # es: prefix, then lea ax, [bx+si]
    ir = regs(bx=bx0, si=si0, es=0xABCD, cs=cs, ip=ip)
    ea = w16(bx0 + si0)
    fr = regs(ax=ea, bx=bx0, si=si0, es=0xABCD, cs=cs, ip=ip + len(bytes_))
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("26", "lea_ignores_segment_override",
              "lea ax, [es:bx+si]: es prefix consumed but has no effect on the result",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 25. CBW/CWD: sign extension, both directions of the boundary
# =====================================================================

def t_98_cbw_sign_extends():
    cs, ip = 0, 0
    bytes_ = [0x98]
    ir = regs(ax=0x0080, cs=cs, ip=ip)  # al=0x80 (negative)
    fr = regs(ax=0xFF80, cs=cs, ip=ip + len(bytes_))
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("98", "cbw_sign_extends",
              "cbw with al=0x80 (negative): ah becomes 0xFF",
              bytes_, ir, iram, fr, fram)


def t_99_cwd_sign_extends():
    cs, ip = 0, 0
    bytes_ = [0x99]
    ir = regs(ax=0x8000, cs=cs, ip=ip)  # ax negative
    fr = regs(ax=0x8000, dx=0xFFFF, cs=cs, ip=ip + len(bytes_))
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("99", "cwd_sign_extends",
              "cwd with ax=0x8000 (negative): dx becomes 0xFFFF",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 26. AAD with a non-default base -- the read counterpart of the AAM test
# =====================================================================

def t_d5_aad_nondefault_base():
    cs, ip = 0, 0
    imm = 0x10
    ah0, al0 = 0x02, 0x05  # ah*imm + al = 2*16 + 5 = 37
    bytes_ = [0xD5, imm]
    f = Flags()
    result = (al0 + ah0 * imm) & 0xFF
    set_psz8(f, result)
    ir = regs(ax=(ah0 << 8) | al0, cs=cs, ip=ip)
    fr = regs(ax=result, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D5", "aad_nondefault_base",
              "aad 10h with ah=2, al=5: D5 supports any base, not just the conventional 0Ah",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 27. Far call/return: push/pop order and value correctness, previously
#     entirely untested opcodes
# =====================================================================

def t_9a_call_far_push_order():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0x0010
    new_cs, new_ip = 0x1234, 0x5678
    bytes_ = [0x9A, lo(new_ip), hi(new_ip), lo(new_cs), hi(new_cs)]
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip)
    return_ip = w16(ip + len(bytes_))
    sp1, push_cs = sim_push(ss, sp0, cs)
    sp2, push_ip = sim_push(ss, sp1, return_ip)
    fr = regs(ss=ss, sp=sp2, cs=new_cs, ip=new_ip)
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_) + push_cs + push_ip
    add_test("9A", "call_far_push_order",
              "call far 1234:5678: pushes old cs then return ip, in that order",
              bytes_, ir, iram, fr, fram)


def t_cb_retf_pop_order():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0x0010
    target_ip, target_cs = 0x4321, 0x8765
    bytes_ = [0xCB]
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip)
    src = stack_word_ram(ss, sp0, target_ip) + stack_word_ram(ss, w16(sp0 + 2), target_cs)
    new_sp = w16(sp0 + 4)
    fr = regs(ss=ss, sp=new_sp, cs=target_cs, ip=target_ip)
    iram = code_ram(cs, ip, bytes_) + src
    fram = code_ram(cs, ip, bytes_) + src
    add_test("CB", "retf_pop_order",
              "retf: pops ip then cs, in that order (reverse of call far's pushes)",
              bytes_, ir, iram, fr, fram)


def t_c2_ret_imm16_sp_wrap():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0xFFFC
    target_ip = 0x0055
    imm = 0x0004
    bytes_ = [0xC2, lo(imm), hi(imm)]
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip)
    src = stack_word_ram(ss, sp0, target_ip)
    new_sp = w16(w16(sp0 + 2) + imm)  # pop() advances sp by 2, then += imm16 wraps
    fr = regs(ss=ss, sp=new_sp, cs=cs, ip=target_ip)
    iram = code_ram(cs, ip, bytes_) + src
    fram = code_ram(cs, ip, bytes_) + src
    add_test("C2", "ret_imm16_sp_wrap",
              "ret 4 with sp=0xFFFC: pop() plus the imm16 sp adjustment wraps sp",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 28. PUSHF/POPF: previously untested. get_flags()/materializeFlags() always
#     normalize the reserved bits regardless of what a popped value contains,
#     so the interesting property here is straightforward round-trip fidelity
#     of the 9 real flag bits, not the reserved-bit handling.
# =====================================================================

def t_9c_pushf_value():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0x0010
    bytes_ = [0x9C]
    f = Flags(cf=True, pf=False, af=True, zf=False, sf=True,
              tf=False, iflag=True, df=True, of=True)
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip, flags=f.to_int())
    new_sp, pushed = sim_push(ss, sp0, f.to_int())
    fr = regs(ss=ss, sp=new_sp, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_) + pushed
    add_test("9C", "pushf_value",
              "pushf: pushes the exact current flags word",
              bytes_, ir, iram, fr, fram)


def t_9d_popf_value():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0x0010
    bytes_ = [0x9D]
    f = Flags(cf=True, pf=False, af=True, zf=False, sf=True,
              tf=True, iflag=True, df=True, of=True)
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip)  # initial flags irrelevant, popf overwrites all
    src = stack_word_ram(ss, sp0, f.to_int())
    new_sp = w16(sp0 + 2)
    fr = regs(ss=ss, sp=new_sp, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_) + src
    fram = code_ram(cs, ip, bytes_) + src
    add_test("9D", "popf_value",
              "popf: restores all 9 real flag bits from the stack exactly",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 29. SAHF/LAHF: only SF/ZF/AF/PF/CF transfer via AH -- OF/DF/IF/TF never do
# =====================================================================

def t_9e_sahf_selective_bits():
    cs, ip = 0, 0
    bytes_ = [0x9E]
    ah0 = 0x00  # sf=zf=af=pf=cf all 0 via ah
    initial = Flags(of=True, df=True, tf=True, iflag=True,  # must survive untouched
                     cf=True, pf=True, af=True, zf=True, sf=True)  # must all clear
    ir = regs(ax=ah0 << 8, cs=cs, ip=ip, flags=initial.to_int())
    final = Flags(of=True, df=True, tf=True, iflag=True,
                  cf=False, pf=False, af=False, zf=False, sf=False)
    fr = regs(ax=ah0 << 8, cs=cs, ip=ip + len(bytes_), flags=final.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("9E", "sahf_selective_bits",
              "sahf with ah=0: clears sf/zf/af/pf/cf only, of/df/if/tf survive untouched",
              bytes_, ir, iram, fr, fram)


def t_9f_lahf_packs_bits():
    cs, ip = 0, 0
    bytes_ = [0x9F]
    f = Flags(cf=True, pf=True, af=True, zf=True, sf=True, of=True, df=True, iflag=True)
    expected_ah = 0x02 | 0x80 | 0x40 | 0x10 | 0x04 | 0x01  # 0xD7
    ir = regs(ax=0, cs=cs, ip=ip, flags=f.to_int())
    fr = regs(ax=expected_ah << 8, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("9F", "lahf_packs_bits",
              "lahf: packs sf/zf/af/pf/cf into ah (0xD7), leaves of/df/if untouched",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 30. Rounding out the shift/rotate group: SHR mirrors SHL's forced-zero
#     boundary but with a real asymmetry (op_shr8/16's shift>N branch never
#     touches cf/of, unlike op_sal8/16's, which explicitly clears cf), and
#     ROR/RCR get the same period tests ROL/RCL already got.
# =====================================================================

def t_d2_shr_bl_cl9_preserves_cf_of():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xEB]  # mod=11 reg=101(/5 shr) rm=011(bl)
    bl0 = 0xFF
    f = Flags(cf=True, of=True)
    newval = shr8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip, flags=Flags(cf=True, of=True).to_int())
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "shr_bl_cl9_preserves_cf_of",
              "shr bl, cl=9 (count>8): unlike shl's boundary, cf/of are left untouched",
              bytes_, ir, iram, fr, fram)


def t_d3_shr_ax_cl17_preserves_cf_of():
    cs, ip = 0, 0
    bytes_ = [0xD3, 0xE8]  # mod=11 reg=101(/5 shr) rm=000(ax)
    ax0 = 0xFFFF
    f = Flags(cf=True, of=True)
    newval = shr16(f, ax0, 17)
    ir = regs(ax=ax0, cx=17, cs=cs, ip=ip, flags=Flags(cf=True, of=True).to_int())
    fr = regs(ax=newval, cx=17, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D3", "shr_ax_cl17_preserves_cf_of",
              "shr ax, cl with cl=17 (word, count>16): same cf/of-preserved asymmetry",
              bytes_, ir, iram, fr, fram)


def t_d2_ror_bl_cl9_equals_cl1():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xCB]  # mod=11 reg=001(/1 ror) rm=011(bl)
    bl0 = 0xA5
    f = Flags(cf=False)
    newval = ror8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip, flags=Flags(cf=False).to_int())
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "ror_bl_cl9_equals_cl1",
              "ror bl, cl with cl=9: 8-bit rotate period means this equals cl=1",
              bytes_, ir, iram, fr, fram)


def t_d2_rcr_bl_cl9_full_period():
    cs, ip = 0, 0
    bytes_ = [0xD2, 0xDB]  # mod=11 reg=011(/3 rcr) rm=011(bl)
    bl0 = 0xA5
    f = Flags(cf=False)
    newval = rcr8(f, bl0, 9)
    ir = regs(bx=bl0, cx=9, cs=cs, ip=ip, flags=Flags(cf=False).to_int())
    fr = regs(bx=newval, cx=9, cs=cs, ip=ip + len(bytes_), flags=f.to_int())
    iram = code_ram(cs, ip, bytes_)
    fram = code_ram(cs, ip, bytes_)
    add_test("D2", "rcr_bl_cl9_full_period",
              "rcr bl, cl with cl=9: 9-bit rotate-through-carry period restores value+cf",
              bytes_, ir, iram, fr, fram)


# =====================================================================
# 31. 0xC0: undefined in Intel's docs (the immediate-shift-count encoding is
#     an 80186+ addition), but real 8086/8088 silicon decodes it identically
#     to 0xC2 (RET imm16) since the CPU only examines specific opcode bits --
#     this emulator reproduces that alias (i8086.cxx:1706-1708) rather than
#     implementing a shift instruction there. An easy thing to get backwards
#     if "fixed" by someone expecting 80186+ behavior.
# =====================================================================

def t_c0_aliases_ret_imm16():
    cs, ip = 0, 0
    ss, sp0 = 0x2000, 0x0010
    target_ip = 0x0050
    imm = 0x0004
    bytes_ = [0xC0, lo(imm), hi(imm)]
    ir = regs(ss=ss, sp=sp0, cs=cs, ip=ip)
    src = stack_word_ram(ss, sp0, target_ip)
    new_sp = w16(w16(sp0 + 2) + imm)
    fr = regs(ss=ss, sp=new_sp, cs=cs, ip=target_ip)
    iram = code_ram(cs, ip, bytes_) + src
    fram = code_ram(cs, ip, bytes_) + src
    add_test("C0", "aliases_ret_imm16",
              "0xC0 on real 8086 is not a shift -- it decodes identically to ret imm16",
              bytes_, ir, iram, fr, fram)


# =====================================================================

def main():
    for fn in [
        t_89_mod1_disp8_wrap, t_8b_mod2_disp16_wrap, t_87_xchg_bx_wrap,
        t_ff_inc_wrap, t_ff_push_rm_wrap, t_81_add_imm_wrap,
        t_b8_fetch_wrap_exploratory,
        t_54_push_sp_wrap, t_5d_pop_wrap,
        t_eb_jmp_short_wrap_backward, t_e9_jmp_near_wrap_forward,
        t_e2_loop_cx0_wraps_taken, t_e2_loop_cx1_not_taken, t_e3_jcxz_cx0_taken,
        t_d7_xlat_overflow,
        t_40_inc_ax_wrap, t_fe_dec_al_signed_overflow,
        t_05_add_ax_signed_overflow, t_2c_sub_al_nibble_borrow,
        t_d2_rcl_bl_cl9_full_period, t_d2_rol_bl_cl9_equals_cl1,
        t_d2_shl_bl_cl9_forced_zero, t_d3_shl_ax_cl17_forced_zero,
        t_27_daa_double_adjust, t_2f_das_double_adjust,
        t_37_aaa_carries_into_ah, t_3f_aas_borrows_from_ah,
        t_d4_aam_nondefault_divisor,
        t_f7_div_by_zero_sp_wrap,
        t_3c_cmp_al_signed_overflow, t_3d_cmp_ax_signed_overflow,
        t_14_adc_al_carry_in_wraps, t_15_adc_ax_carry_in_wraps,
        t_1c_sbb_al_borrow_in_wraps, t_1d_sbb_ax_borrow_in_wraps,
        t_24_and_al_clears_cf_of, t_25_and_ax_clears_cf_of,
        t_0c_or_al_zero_result, t_0d_or_ax_zero_result,
        t_34_xor_al_self_cancel, t_35_xor_ax_self_cancel,
        t_f6_neg_al_cant_negate, t_f7_neg_ax_cant_negate,
        t_f6_mul_al_sf_from_low_byte, t_f7_mul_ax_overflow,
        t_f6_imul_al_negate_overflow, t_f7_imul_ax_negate_overflow,
        t_f6_div_al_quotient_overflow, t_f7_div_ax_quotient_overflow,
        t_f6_idiv_al_negative_128_quirk, t_f7_idiv_ax_negative_32768_quirk,
        t_cf_iret_trap_deferred_one_instruction,
        t_e1_loopz_zf_blocks_branch, t_e0_loopnz_zf_blocks_branch,
        t_f3a4_rep_movsb_cx0_zero_iterations, t_a4_movsb_df1_wraps_si_di,
        t_ce_into_of0_noop, t_ce_into_of1_interrupts,
        t_ea_jmp_far_fetch_wrap,
        t_8f_pop_mem_sp_wrap,
        t_8d_lea_ignores_segment_override,
        t_98_cbw_sign_extends, t_99_cwd_sign_extends,
        t_d5_aad_nondefault_base,
        t_9a_call_far_push_order, t_cb_retf_pop_order, t_c2_ret_imm16_sp_wrap,
        t_9c_pushf_value, t_9d_popf_value,
        t_9e_sahf_selective_bits, t_9f_lahf_packs_bits,
        t_d2_shr_bl_cl9_preserves_cf_of, t_d3_shr_ax_cl17_preserves_cf_of,
        t_d2_ror_bl_cl9_equals_cl1, t_d2_rcr_bl_cl9_full_period,
        t_c0_aliases_ret_imm16,
    ]:
        fn()

    out_dir = os.path.dirname(os.path.abspath(__file__))
    seen = {}
    for opcode_hex, slug, test in TESTS:
        fname = f"{opcode_hex}_{slug}.json"
        seen[fname] = seen.get(fname, 0) + 1
        path = os.path.join(out_dir, fname)
        text = json.dumps([test], indent=2)
        # test86.cxx's hand-rolled parser locates each "ram" entry's value by
        # scanning forward from '[' for a literal space character, which only
        # lands in the right place if each [addr, val] pair is one one line
        # (matching the existing corpus's format) rather than json.dumps's
        # default one-number-per-line expansion of nested lists.
        text = re.sub(r"\[\s*(-?\d+),\s*(-?\d+)\s*\]", r"[\1, \2]", text)
        with open(path, "w") as f:
            f.write(text)
            f.write("\n")
    dupes = [k for k, v in seen.items() if v > 1]
    if dupes:
        raise SystemExit(f"duplicate output filenames: {dupes}")
    print(f"wrote {len(TESTS)} test files to {out_dir}")


if __name__ == "__main__":
    main()
