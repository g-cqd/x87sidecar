#!/usr/bin/env bash
#
# check_gap_fuse.sh -- verify the fld_gap_fstp fusion (FLD m32/m64, up to four
# independent non-x87 instructions, FSTP m32/m64) against stock Rosetta and
# against the sidecar with the fusion disabled.
#
# Uses --cooperative attach, so it needs no entitlements and no password.
#
#   1. test_fld_gap_fstp self-check: stock Rosetta, sidecar default, sidecar
#      with X87_DISABLE_FUSIONS=fld_gap_fstp, sidecar with X87_FUSE_GAP_STRICT=1.
#   2. The number of distinct "[x87-gapfuse] fused" log lines must equal the
#      number the test says it expects (default / strict / 0 when disabled):
#      the positive cases fuse and every negative case stays unfused.
#   3. --dump comparison: every result bit, CW, TOP/status, tag word and the
#      scratch buffer, fusion on vs off (must be identical except for the
#      listed groups) and stock vs sidecar (informational; the sidecar does
#      not model the status-word exception flags).
#
# Usage: bash scripts/check_gap_fuse.sh [--no-build]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$ROOT_DIR/build/bin"
SIDECAR="$BIN/x87sidecar"
TEST="$BIN/tests/test_fld_gap_fstp"
export X87_NO_PREAUTH=1

if [[ "${1:-}" != "--no-build" ]]; then
    cmake -B "$ROOT_DIR/build" "$ROOT_DIR" >/dev/null
    cmake --build "$ROOT_DIR/build" -j8 >/dev/null
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/gapfuse.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
fail=0

say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

# run <label> <env...> : self-check under the sidecar, log fused-line count
run_sidecar() {
    local label="$1"
    shift
    env X87_LOG_GAP_FUSE=1 "$@" "$SIDECAR" --cooperative "$TEST" >"$WORK/$label.out" 2>&1 || true
    local fused
    fused=$( (grep '\[x87-gapfuse\] fused' "$WORK/$label.out" || true) | sort -u | wc -l | tr -d ' ')
    printf '%s' "$fused" >"$WORK/$label.fused"
    if grep -q '^ALL PASS' "$WORK/$label.out" && ! grep -q 'FAIL' "$WORK/$label.out"; then
        say "PASS  self-check, $label ($(grep -c '^PASS' "$WORK/$label.out") groups, $fused fused copies)"
    else
        bad "self-check, $label"
        grep 'FAIL' "$WORK/$label.out" | head -10 | sed 's/^/      /'
    fi
}

"$TEST" >"$WORK/native.out" 2>&1 || true
if grep -q '^ALL PASS' "$WORK/native.out"; then
    say "PASS  self-check, stock Rosetta ($(grep -c '^PASS' "$WORK/native.out") groups)"
else
    bad "self-check, stock Rosetta"
    grep 'FAIL' "$WORK/native.out" | head -10 | sed 's/^/      /'
fi
expect_default=$(sed -n 's/^GAPFUSE_EXPECT default=\([0-9]*\) strict=.*/\1/p' "$WORK/native.out")
expect_strict=$(sed -n 's/^GAPFUSE_EXPECT default=[0-9]* strict=\([0-9]*\)/\1/p' "$WORK/native.out")

run_sidecar default
run_sidecar disabled X87_DISABLE_FUSIONS=fld_gap_fstp
run_sidecar strict X87_FUSE_GAP_STRICT=1
run_sidecar all_fusions_off X87_DISABLE_ALL_FUSIONS=1
run_sidecar ir_off X87_DISABLE_X87_IR=1

check_count() {
    local label="$1" want="$2" got
    got=$(cat "$WORK/$label.fused")
    if [[ "$got" == "$want" ]]; then
        say "PASS  fused copies, $label: $got (expected $want)"
    else
        bad "fused copies, $label: $got, expected $want"
    fi
}
check_count default "$expect_default"
check_count strict "$expect_strict"
check_count disabled 0
check_count all_fusions_off 0
check_count ir_off "$expect_default"

# --dump comparison ---------------------------------------------------------
dump_native() { "$TEST" --dump 2>/dev/null | grep '^D '; }
dump_sidecar() { env "$@" "$SIDECAR" --cooperative "$TEST" --dump 2>/dev/null | grep '^D '; }
dump_native >"$WORK/d.native"
dump_sidecar X87_DISABLE_FUSIONS=fld_gap_fstp >"$WORK/d.off"
dump_sidecar A=1 >"$WORK/d.on"
dump_sidecar X87_FUSE_GAP_STRICT=1 >"$WORK/d.strict"
lines=$(wc -l <"$WORK/d.on" | tr -d ' ')
say "dump: $lines result rows per configuration"

groups() { awk '{print $2}' | sort -u | tr '\n' ' '; }

# On vs off: identical rows except the groups listed here.
#   gap0_d_d    adjacent FLD m64;FSTP m64: the fusion quiets a signalling NaN like
#               the hardware and the unfused two-reply path do; the older fld_fstp
#               peephole it replaces copied the bits unchanged.
#   overflow8   FLD with all eight slots occupied (stack fault on hardware, not
#               modelled): the fusion leaves the full stack alone.
allowed='gap0_d_d overflow8'
for cfg in on strict; do
    diff "$WORK/d.off" "$WORK/d.$cfg" | sed -n 's/^> //p' >"$WORK/diff.$cfg" || true
    changed=$(groups <"$WORK/diff.$cfg")
    # shellcheck disable=SC2086
    unexpected=$(printf '%s\n' $changed | grep -v -x -F -e gap0_d_d -e overflow8 | grep -v '^$' || true)
    if [[ -z "$unexpected" ]]; then
        say "PASS  dump, fusion $cfg vs off: identical except {${changed:-none}} (allowed: $allowed)"
    else
        bad "dump, fusion $cfg vs off: unexpected differences in: $unexpected"
    fi
done

# Stock vs sidecar (fusion on): bits, CW and tag word; the status word is reported apart.
strip_sw() { sed 's/ sw=[0-9a-f]*//'; }
strip_sw <"$WORK/d.native" >"$WORK/n.nosw"
strip_sw <"$WORK/d.on" >"$WORK/o.nosw"
nd=$( (diff "$WORK/n.nosw" "$WORK/o.nosw" || true) | grep -c '^>' || true)
ngroups=$( (diff "$WORK/n.nosw" "$WORK/o.nosw" || true) | sed -n 's/^> //p' | groups)
say "info  stock vs sidecar (fusion on), ignoring SW: $nd differing rows in {${ngroups:-none}}"
say "      (n_x87gap_*: x87 in the gap, an IR run that does not quiet an f64 sNaN; overflow8: see above)"
sd=$(paste -d'\n' "$WORK/d.native" "$WORK/d.on" | awk 'NR%2==1{a=$0;next}{split(a,x," ");split($0,y," "); if (x[6]!=y[6]) c++} END{print c+0}')
say "info  status-word differences stock vs sidecar: $sd rows (the sidecar does not model PE/IE/C1)"

[[ $fail -eq 0 ]] && say "ALL PASS" || say "SOME FAILED"
exit $fail
