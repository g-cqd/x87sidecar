#!/usr/bin/env bash
#
# bench_gap_fuse.sh -- ns per iteration of isolated float copies through the x87
# stack (benchmarks/bench_fld_gap_fstp.c) under
#
#   stock    Rosetta alone, the sidecar nowhere in the picture
#   unfused  the sidecar with X87_DISABLE_FUSIONS=fld_gap_fstp (the behaviour of
#            the base commit; set BASE_SIDECAR=/path/to/older/x87sidecar to run a
#            real older build as well)
#   fused    the sidecar as built
#   native   the same loads and stores as plain arm64 (the ceiling)
#
# Each configuration is launched RUNS times (default 7); every launch times each
# pattern 9 times over 2M iterations.  The table gives, per pattern, the median
# over launches of each launch's median, and the min..max spread of those.  The
# machine may be busy: look at the spread, and repeat.
#
# Uses --cooperative attach (no entitlements).  Usage: bash scripts/bench_gap_fuse.sh [--no-build]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$ROOT_DIR/build/bin"
SIDECAR="$BIN/x87sidecar"
BENCH="$BIN/bench/bench_fld_gap_fstp"
NATIVE="$BIN/bench_native_copy"
RUNS="${RUNS:-7}"
export X87_NO_PREAUTH=1

if [[ "${1:-}" != "--no-build" ]]; then
    cmake -B "$ROOT_DIR/build" "$ROOT_DIR" >/dev/null
    cmake --build "$ROOT_DIR/build" -j8 >/dev/null
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/gapbench.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "load average: $(uptime | sed 's/.*load averages*: //')"
collect() { # <label> <command...>
    local label="$1"
    shift
    : >"$WORK/$label"
    for ((i = 0; i < RUNS; i++)); do
        "$@" 2>/dev/null | grep '^DETAIL' >>"$WORK/$label" || true
    done
}

collect stock "$BENCH"
collect unfused env X87_DISABLE_FUSIONS=fld_gap_fstp "$SIDECAR" --cooperative "$BENCH"
if [[ -n "${BASE_SIDECAR:-}" ]]; then
    collect base_build "$BASE_SIDECAR" --cooperative "$BENCH"
fi
collect fused "$SIDECAR" --cooperative "$BENCH"
collect native "$NATIVE"

python3 - "$WORK" "${BASE_SIDECAR:+base_build}" <<'PY'
import sys, statistics, os
work, extra = sys.argv[1], sys.argv[2]
cfgs = ['stock', 'unfused'] + ([extra] if extra else []) + ['fused', 'native']
data = {}
for c in cfgs:
    d = {}
    for line in open(os.path.join(work, c)):
        _, name, lo, med, hi = line.split()
        d.setdefault(name, []).append((float(lo), float(med), float(hi)))
    data[c] = d
names = ['fc2_flds_fstps', 'fc2_live3_flds_fstps', 'chain_flds_fstps', 'pair_flds_fstps',
         'fc2_fldl_fstpl',
         'sse_movss_equiv', 'loop_only']
def cell(c, n):
    v = data[c].get(n)
    if not v:
        return None
    meds = [m for _, m, _ in v]
    return statistics.median(meds), min(meds), max(meds), len(meds)
print()
print('ns per iteration: median of launch medians [min..max over launches]')
hdr = '%-22s' % 'pattern' + ''.join('%-26s' % c for c in cfgs)
print(hdr)
for n in names:
    row = '%-22s' % n
    for c in cfgs:
        x = cell(c, n)
        row += '%-26s' % ('-' if x is None else '%.3f [%.3f..%.3f]' % x[:3])
    print(row)
print()
print('speedup of fused over unfused / over stock (ratio of medians, loop overhead included)')
for n in names[:5]:
    u, f, s = cell('unfused', n), cell('fused', n), cell('stock', n)
    if u and f and s:
        print('%-22s %6.1fx over unfused   %6.1fx over stock' % (n, u[0] / f[0], s[0] / f[0]))
lo = cell('fused', 'loop_only')
if lo:
    print()
    print('loop_only (fused run) = %.3f ns/iter: the x86 loop itself under Rosetta;' % lo[0])
    print('net cost of one copy = pattern - loop_only:')
    for n in names[:5]:
        for c in ['stock', 'unfused', 'fused']:
            x = cell(c, n)
            l = cell(c, 'loop_only')
            if x and l:
                print('  %-22s %-8s %7.3f ns' % (n, c, x[0] - l[0]))
PY
