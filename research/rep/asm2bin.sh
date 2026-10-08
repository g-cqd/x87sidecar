#!/bin/bash
# usage: asm2bin.sh in.s out.bin   (extracts __text of an assembled arm64 object)
set -e
O=$(mktemp -t rep).o
/opt/homebrew/opt/llvm/bin/llvm-mc -triple=arm64-apple-macos -mattr=+neon,+lse -filetype=obj "$1" -o "$O"
/opt/homebrew/opt/llvm/bin/llvm-objdump -s -j __text "$O" | awk '/^ [0-9a-f]+ /{for(i=2;i<=5;i++)printf "%s",$i}' | xxd -r -p > "$2"
rm -f "$O"
