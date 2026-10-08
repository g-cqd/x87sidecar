#!/bin/bash
# Builds and runs the gaps_a probes under stock Rosetta. Output to stdout; binaries go to ./out (ignored by git).
set -u
cd "$(dirname "$0")"; mkdir -p out
CC="clang -arch x86_64 -O1 -Wno-unused"
for p in cpuid priv sig addr lowmap atomics; do $CC -o out/$p $p.c || exit 1; done
$CC -Wl,-pagezero_size,0x4000 -o out/addr_lowpz addr.c
$CC -Wl,-pagezero_size,0x4000 -o out/lowmap_pz lowmap.c
$CC -Wl,-pagezero_size,0x4000 -o out/compat compat.c
echo "##### cpuid (default)"; out/cpuid
echo "##### cpuid ROSETTA_ADVERTISE_AVX=1"; ROSETTA_ADVERTISE_AVX=1 out/cpuid
echo "##### priv"; out/priv
echo "##### sig"; out/sig
echo "##### addr (default pagezero)"; out/addr
echo "##### addr (-pagezero_size 0x4000)"; out/addr_lowpz
echo "##### lowmap (-pagezero_size 0x4000)"; timeout 20 out/lowmap_pz
echo "##### atomics"; out/atomics
echo "##### compat (32-bit LDT/segments; children abort on unsupported cases)"; timeout 120 out/compat
