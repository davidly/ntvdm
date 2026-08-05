#!/bin/bash
# Runs every test86 JSON test file in tests/ and tests/undocumented/, one process per
# core, and prints a summary. Exits with the number of failed files (0 if all passed).
#
# tests/*.json (~10,000 hardware-validated cases per opcode) are NOT checked into this
# repo (several GB -- see .gitignore) and must be fetched and converted separately.
# That corpus originates from Tom Harte's ProcessorTests project
# (github.com/TomHarte/ProcessorTests, now archived, redirects to
# github.com/SingleStepTests/ProcessorTests), generated with Folkert van Heusden's
# assistance; its current, actively-maintained standalone home is
# github.com/SingleStepTests/8088, the `v1` folder specifically (NOT `v2`, which is a
# different, incompatible format from newer/different hardware). See
# convert_singlesteptests.py in this directory for the exact conversion required and a
# clone-to-running-tests example.

set -u
cd "$(dirname "$0")"

nproc_count=$(nproc)
outdir=$(mktemp -d)
trap 'rm -rf "$outdir"' EXIT

start=$(date +%s.%N)

ls tests/*.json tests/undocumented/*.json 2>/dev/null | xargs -P "$nproc_count" -I{} sh -c \
    './test86 "{}" > "'"$outdir"'/$(basename "{}").out" 2>&1'

end=$(date +%s.%N)
elapsed=$(awk "BEGIN { printf \"%.1f\", $end - $start }")

total=0
failed=0
for f in "$outdir"/*.out; do
    total=$((total + 1))
    if ! grep -q "great success" "$f"; then
        failed=$((failed + 1))
        echo "=== $(basename "$f" .out) ==="
        tail -5 "$f"
    fi
done

echo "----------------------------------------"
echo "$total files, $((total - failed)) passed, $failed failed, ${elapsed}s wall clock, $nproc_count workers"

exit "$failed"
