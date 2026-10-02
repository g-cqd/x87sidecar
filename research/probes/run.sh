#!/bin/bash
# usage: run.sh <outdir> [passes]  -- alternates x86_64 (Rosetta) and arm64 passes, logs load average
out=${1:-/tmp/probe_out}; passes=${2:-3}
mkdir -p "$out"; cd "$(dirname "$0")"
for p in $(seq 1 $passes); do
  for a in x86 arm64; do
    echo "pass $p $a load: $(uptime | sed 's/.*load averages*: //')" >> "$out/load.log"
    ./build/probes_$a all 9 30 > "$out/${a}_pass$p.csv"
  done
done
echo "end load: $(uptime | sed 's/.*load averages*: //')" >> "$out/load.log"
