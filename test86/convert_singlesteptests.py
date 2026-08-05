#!/usr/bin/env python3
"""
Converts the SingleStepTests/8088 v1 JSON test corpus into the format
test86.cxx expects in tests/ and tests/undocumented/.

Background
----------
tests/ and tests/undocumented/ hold ~10,000 hardware-validated before/after
test cases per opcode. That data is not checked into this repo (see
.gitignore -- it's several GB) and has to be fetched and converted
separately; this script does the conversion.

The original source of this data is Tom Harte's ProcessorTests project
(github.com/TomHarte/ProcessorTests -- now archived, redirects to
github.com/SingleStepTests/ProcessorTests), later split into per-CPU repos
under the SingleStepTests GitHub organization. The V1 8088 test set used
here was generated with Folkert van Heusden's assistance. Its current,
actively-maintained standalone home is:

    https://github.com/SingleStepTests/8088   (the `v1` folder)

V2, in the same repo, is a different and INCOMPATIBLE format (produced later
with different hardware, a sparse per-cycle bus-activity encoding, etc.) --
this script and test86.cxx's parser only understand v1.

What needs to change
---------------------
The upstream v1 files are gzipped and use a *sparse* diff format: `final`
only lists registers/memory that actually changed, plus a `hash`/`idx` pair
per test. test86.cxx's hand-rolled parser (see run_test() in test86.cxx)
expects a *complete* snapshot instead: `final.regs` must list every
register (unchanged ones repeated from `initial.regs`) and `final.ram` must
list every address `initial.ram` touched (unchanged ones repeated). It also
looks for a `test_hash` field, not `hash`/`idx`.

This also drops the upstream `cycles` bus-activity trace (never read by
test86.cxx) to keep the converted files far smaller and faster to produce --
with it, the 10,000-tests-per-opcode files run tens of megabytes each. Pass
--keep-cycles to preserve it byte-for-byte instead, if you want that.

Usage
-----
    python3 convert_singlesteptests.py <v1-checkout-dir> [output-dir]

<v1-checkout-dir> should contain the upstream *.json.gz (or already
decompressed *.json) files directly -- i.e. point it at the `v1` folder from
a clone of https://github.com/SingleStepTests/8088. output-dir defaults to
`tests` next to this script.

Every converted opcode file is written flat into output-dir; this repo's
own split into tests/undocumented/ (see UNDOCUMENTED_OPCODES below) is
purely this project's own organizational choice for readability, not an
upstream distinction -- runall.sh/runall.ps1 glob both tests/*.json and
tests/undocumented/*.json, so a single flat directory works identically as
far as the test runner is concerned. Pass --split to reproduce the same
split this repo uses.
"""
import gzip
import hashlib
import json
import os
import shutil
import sys

# The subset of opcode files this repo happens to keep separate in
# tests/undocumented/ purely for readability -- not an upstream distinction.
UNDOCUMENTED_OPCODES = {
    "0F", "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6A",
    "6B", "6C", "6D", "6E", "6F", "C0", "C1", "C8", "C9", "D0.6", "D1.6",
    "D2.6", "D3.1", "D3.5", "D3.6", "D6", "F6.1", "F7.1", "FF.7",
}

REG_ORDER = ["ax", "bx", "cx", "dx", "cs", "ss", "ds", "es",
             "sp", "bp", "si", "di", "ip", "flags"]


def expand_test(test, keep_cycles):
    initial = test["initial"]
    final = test["final"]

    full_regs = {k: initial["regs"][k] for k in REG_ORDER}
    full_regs.update(final["regs"])

    ram_values = dict(initial["ram"])
    ram_values.update(final["ram"])
    full_ram = [[addr, ram_values[addr]] for addr, _ in initial["ram"]]

    out = {
        "name": test["name"],
        "bytes": test["bytes"],
        "initial": initial,
        "final": {
            "regs": full_regs,
            "ram": full_ram,
            "queue": final["queue"],
        },
        "cycles": test["cycles"] if keep_cycles else [],
        # test86.cxx never verifies this value against anything -- it's only
        # ever printed in failure/log messages -- so a fresh hash derived
        # from the upstream one is fine; it doesn't need to match upstream's
        # own sha1 "hash" byte-for-byte.
        "test_hash": hashlib.sha256(test["hash"].encode()).hexdigest(),
    }
    return out


def convert_file(src_path, dst_path, keep_cycles):
    opener = gzip.open if src_path.endswith(".gz") else open
    with opener(src_path, "rt") as f:
        tests = json.load(f)
    converted = [expand_test(t, keep_cycles) for t in tests]
    with open(dst_path, "w") as f:
        json.dump(converted, f)  # no indent: test86.cxx's parser needs each
        # [addr, val] ram pair on one line (a single space after the comma),
        # which json.dump's compact default separators produce naturally.


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        sys.exit(1)
    src_dir = args[0]
    dst_dir = args[1] if len(args) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "tests")
    keep_cycles = "--keep-cycles" in sys.argv
    split_undocumented = "--split" in sys.argv

    os.makedirs(dst_dir, exist_ok=True)
    undoc_dir = os.path.join(dst_dir, "undocumented")
    if split_undocumented:
        os.makedirs(undoc_dir, exist_ok=True)

    src_files = sorted(f for f in os.listdir(src_dir)
                        if f.endswith(".json") or f.endswith(".json.gz"))
    if not src_files:
        print(f"no .json/.json.gz files found in {src_dir}")
        sys.exit(1)

    for i, fname in enumerate(src_files, 1):
        base = fname[:-len(".json.gz")] if fname.endswith(".json.gz") else fname[:-len(".json")]
        target_dir = undoc_dir if (split_undocumented and base in UNDOCUMENTED_OPCODES) else dst_dir
        dst_path = os.path.join(target_dir, base + ".json")
        print(f"[{i}/{len(src_files)}] {fname} -> {dst_path}")
        convert_file(os.path.join(src_dir, fname), dst_path, keep_cycles)

    print(f"done: {len(src_files)} files written under {dst_dir}")


if __name__ == "__main__":
    main()
