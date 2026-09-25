#!/usr/bin/env bash
# Baseline throughput figures for BENCHMARKS.md.
#
# Every configuration is run twice, at a short and a long generation count,
# and the rate is taken from the *difference*. That cancels the fixed cost of
# starting a process, making a window, compiling the rule and seeding the
# grid, which would otherwise be charged to the first few hundred generations
# and flatter or damn a configuration depending on how long it ran.
#
# Usage:  scripts/benchmark.sh [path-to-aether]
# Set AETHER_PRIME=1 to run on the discrete GPU of an Optimus laptop. Figures
# taken without it are the integrated GPU's and are not comparable to SPEC 12,
# which is a T1200 document.

set -u
AETHER=${1:-./build/aether}
REPEATS=${REPEATS:-3}

# Somewhere real to save to: a grid above 4 MB writes a sidecar beside the
# session, so /dev/null is not a valid destination for the larger
# configurations. The write is the same on both runs of a pair and cancels
# out of the difference.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$AETHER" ]; then echo "no aether binary at $AETHER" >&2; exit 2; fi

PREFIX=()
if [ "${AETHER_PRIME:-0}" = "1" ]; then
    export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    WHICH="discrete GPU (PRIME offload)"
else
    WHICH="default GPU"
fi

# Milliseconds for one run, or empty if it failed.
run_ms() {
    local start end
    start=$(date +%s%N)
    if ! "$AETHER" "$@" >/dev/null 2>&1; then echo ""; return; fi
    end=$(date +%s%N)
    echo $(( (end - start) / 1000000 ))
}

# name, short generations, long generations, then the aether arguments.
# Prints the best of REPEATS, best being the least disturbed by other load.
measure() {
    local name=$1 short=$2 long=$3; shift 3
    local best=""
    for _ in $(seq "$REPEATS"); do
        local a b
        a=$(run_ms headless --generations "$short" --save "$WORK/a.aether" "$@")
        b=$(run_ms headless --generations "$long"  --save "$WORK/b.aether" "$@")
        if [ -z "$a" ] || [ -z "$b" ]; then printf '%-46s  FAILED\n' "$name"; return; fi
        local ms=$(( b - a ))
        if [ "$ms" -le 0 ]; then ms=1; fi
        local rate
        rate=$(awk -v g="$(( long - short ))" -v ms="$ms" 'BEGIN { printf "%.0f", g * 1000 / ms }')
        if [ -z "$best" ] || [ "$rate" -gt "$best" ]; then best=$rate; fi
    done
    printf '%-46s  %8s gen/s\n' "$name" "$best"
}

echo "aether benchmark — $WHICH — $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "binary: $AETHER   commit: $(git -C "$(dirname "$AETHER")/.." rev-parse --short HEAD 2>/dev/null || echo '?')"
echo "best of $REPEATS, rates from the difference between two run lengths"
echo

measure "2D binary 1024^2, table backend"        2000 12000 --rule B3/S23 --size 1024x1024
measure "2D binary 1024^2, CPU reference"           4    24 --rule B3/S23 --size 1024x1024 --cpu
measure "2D binary 1024^2, cell mutation p=0"     2000 12000 --rule B3/S23 --size 1024x1024 --cell-mutation 0
measure "2D binary 1024^2, cell mutation p=0.02"  2000 12000 --rule B3/S23 --size 1024x1024 --cell-mutation 0.02
measure "2D 16-state 1024^2, codegen backend"     1000  6000 --rule "$(cat "$(dirname "$0")/bench16.rule" 2>/dev/null || echo B3/S23)" --size 1024x1024
measure "3D binary 256^3, table backend"            50   250 --rule B5/S45 --size 256x256x256
# A 1D grid is a texture one row tall, so its width is bounded by
# GL_MAX_TEXTURE_SIZE: 32768 on the T1200 and 16384 on the Intel part of the
# same machine. 16384 is measured so the two are comparable and neither
# refuses it.
measure "1D elementary, 16384 cells"              5000 30000 --rule W110 --size 16384
measure "continuous 512^2, kernel radius 4"       1000  6000 --lua "$(dirname "$0")/bench-r4.lua" --size 512x512
measure "continuous 512^2, kernel radius 13"       200  1200 --lua "$(dirname "$0")/bench-r13.lua" --size 512x512
