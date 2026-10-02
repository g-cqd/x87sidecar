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
#   2. Per test group, in its own process with X87_LOG_GAP_FUSE=1: groups the
#      test lists as fusing must log at least that many "[x87-gapfuse] fused"
#      copies (default config, and with X87_FUSE_GAP_STRICT=1), every other
#      group none; with the fusion or all fusions disabled nothing is logged.
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
PROFILE_ANALYZE="$BIN/profile_analyze"
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

TESTS=("$BIN/tests/test_fld_gap_fstp" "$BIN/tests/test_fld_gap_fstp_a32")

# run_sidecar <label> <env...> : full self-check of every test under the sidecar
run_sidecar() {
    local label="$1"
    shift
    : >"$WORK/$label.out"
    local t
    for t in "${TESTS[@]}"; do
        env X87_LOG_GAP_FUSE=1 "$@" "$SIDECAR" --cooperative "$t" >>"$WORK/$label.out" 2>&1 || true
    done
    local fused
    fused=$( (grep -c '\[x87-gapfuse\] fused' "$WORK/$label.out" || true) | tr -d ' ')
    printf '%s' "$fused" >"$WORK/$label.fused"
    if [[ $(grep -c '^ALL PASS' "$WORK/$label.out") -eq ${#TESTS[@]} ]] && ! grep -q 'FAIL' "$WORK/$label.out"; then
        say "PASS  self-check, $label ($(grep -c '^PASS' "$WORK/$label.out") groups, $fused fused copies logged)"
    else
        bad "self-check, $label"
        grep 'FAIL' "$WORK/$label.out" | head -10 | sed 's/^/      /'
    fi
}

native_ok=1
for t in "${TESTS[@]}"; do
    "$t" >"$WORK/native.out" 2>&1 || native_ok=0
    grep -q '^ALL PASS' "$WORK/native.out" || native_ok=0
done
if [[ $native_ok -eq 1 ]]; then
    say "PASS  self-check, stock Rosetta"
else
    bad "self-check, stock Rosetta"
    grep 'FAIL' "$WORK/native.out" | head -10 | sed 's/^/      /'
fi

run_sidecar default
run_sidecar strict X87_FUSE_GAP_STRICT=1
run_sidecar disabled X87_DISABLE_FUSIONS=fld_gap_fstp
run_sidecar all_fusions_off X87_DISABLE_ALL_FUSIONS=1
run_sidecar ir_off X87_DISABLE_X87_IR=1
for label in disabled all_fusions_off; do
    if [[ "$(cat "$WORK/$label.fused")" == 0 ]]; then
        say "PASS  no copy fused with $label"
    else
        bad "copies fused with $label"
    fi
done

# Per group, in its own process.  Rosetta decodes and translates code it never
# runs (the instructions after a block's end), so a "fused" log line alone does
# not prove the group's code fused; X87_PROFILE's per-block execution counts say
# which fused blocks actually ran.  Groups expected to fuse must run at least the
# expected number of fused copies, every other group none.
pos=0 neg=0
executed_fused() { # <profile-prefix> <log-file> : fused (hash, count) pairs that ran
    local prof log="$2" h n total=0
    prof=$(ls "$1".* 2>/dev/null | head -1 || true)
    [[ -n "$prof" ]] || { echo 0; return; }
    while read -r h; do
        n=$("$PROFILE_ANALYZE" --dump-block-by-hash "$h" "$prof" 2>/dev/null |
            sed -n 's/.*exec_count=\([0-9]*\).*/\1/p' | head -1)
        [[ "${n:-0}" -gt 0 ]] && total=$((total + 1))
    done < <(sed -n 's/.*fused hash=\(0x[0-9a-f]*\) fld=\([0-9]*\) .*/\1 \2/p' "$log" | sort -u | cut -d' ' -f1)
    echo "$total"
}
check_group() { # <test> <group> <expected> <label> <env...>
    local t="$1" g="$2" want="$3" label="$4"
    shift 4
    local got
    rm -f "$WORK"/grp.prof.*
    env X87_LOG_GAP_FUSE=1 X87_PROFILE="$WORK/grp.prof" "$@" "$SIDECAR" --cooperative "$t" --only "$g" \
        >"$WORK/grp.out" 2>&1 || true
    got=$(executed_fused "$WORK/grp.prof" "$WORK/grp.out")
    if ! grep -q '^ALL PASS' "$WORK/grp.out"; then
        bad "group $g ($label) did not pass"
    elif [[ "$want" -eq 0 && "$got" -ne 0 ]]; then
        bad "group $g ($label): $got fused blocks ran, expected none"
    elif [[ "$want" -gt 0 && "$got" -lt "$want" ]]; then
        bad "group $g ($label): $got fused blocks ran, expected at least $want"
    elif [[ "$want" -gt 0 ]]; then
        pos=$((pos + 1))
    else
        neg=$((neg + 1))
    fi
}
for t in "${TESTS[@]}"; do
    while read -r _ g want_def want_strict; do
        check_group "$t" "$g" "$want_def" default A=1
        if [[ "$want_def" -gt 0 ]]; then
            check_group "$t" "$g" "$want_strict" strict X87_FUSE_GAP_STRICT=1
        fi
    done < <("$t" --list)
done
say "PASS  per-group fusion check: $pos positive checks (fused as expected), $neg negative checks (left unfused)"

# --dump comparison ---------------------------------------------------------
DUMP_TEST="${TESTS[0]}"
dump_native() { "$DUMP_TEST" --dump 2>/dev/null | grep '^D '; }
dump_sidecar() { env "$@" "$SIDECAR" --cooperative "$DUMP_TEST" --dump 2>/dev/null | grep '^D '; }
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
