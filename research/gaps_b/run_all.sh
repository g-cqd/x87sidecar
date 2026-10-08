#!/bin/bash
# Build and run every gaps_b probe under stock Rosetta (x86_64) and write raw output to $OUT (default ./out). No sudo, no sidecar.
set -u
cd "$(dirname "$0")"; OUT=${OUT:-./out}; mkdir -p "$OUT" "$OUT/bin"
B="clang -arch x86_64 -O1 -w"
$B -o $OUT/bin/x87a x87a.c
$B -o $OUT/bin/x87trans x87trans.c
$B -mno-red-zone -o $OUT/bin/flags flags.c
$B -mno-red-zone -o $OUT/bin/misc_x86 misc_x86.c
$B -Wl,-pagezero_size,0x4000 -o $OUT/bin/compat32 compat32.c
$B -o $OUT/bin/smc smc.c
$B -o $OUT/bin/cliffs_x86 cliffs.c
clang -arch arm64 -O1 -w -o $OUT/bin/cliffs_arm cliffs.c
uptime > $OUT/load_before.txt
for s in pcrange denorm exc sigfpe stack env fxam cmp fist prem rint; do $OUT/bin/x87a $s > $OUT/x87_$s.txt 2>&1; done
$OUT/bin/x87a round > $OUT/x87_round.txt; python3 check_round.py < $OUT/x87_round.txt > $OUT/x87_round_check.txt
$OUT/bin/x87trans > $OUT/x87_trans.txt; python3 check_trans.py < $OUT/x87_trans.txt > $OUT/x87_trans_check.txt
TRANS_TXT=$OUT/x87_trans.txt python3 check_trig_pi.py > $OUT/x87_trig_pi.txt; TRANS_TXT=$OUT/x87_trans.txt python3 check_yl2xp1.py > $OUT/x87_yl2xp1.txt
$OUT/bin/flags 12000 > $OUT/flags.txt 2>&1
$OUT/bin/misc_x86 > $OUT/misc_x86.txt 2>&1
$OUT/bin/compat32 > $OUT/compat32.txt 2>&1
$OUT/bin/smc > $OUT/smc.txt 2>&1
$OUT/bin/cliffs_x86 > $OUT/cliffs_x86.txt 2>&1; $OUT/bin/cliffs_arm > $OUT/cliffs_arm.txt 2>&1
uptime > $OUT/load_after.txt
