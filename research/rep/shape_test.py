#!/usr/bin/env python3
"""Runs candidate AArch64 shapes (shapes/*.s, assembled with llvm-mc) through the runtime's own
state-recovery classifier via classify_harness and prints a verdict per shape plus the per-index
recovery action. usage: shape_test.py <runtime-copy> <annotated-listing.s> [shape.s ...]"""
import re, subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
rt, listing = sys.argv[1], sys.argv[2]
harness = os.environ.get('HARNESS', os.path.join(here, 'classify_harness'))
lst = open(listing).read().split('\n')
byaddr = {}
for i, l in enumerate(lst):
    m = re.match(r'\s*([0-9a-f]+):\s', l)
    if m: byaddr[int(m.group(1), 16)] = i
def assert_text(ret):
    i = byaddr.get(ret - 4)
    if i is None: return '?'
    strs = []
    for l in lst[max(0, i - 14):i + 1]:
        m = re.search(r'; "(.*)"', l)
        if m: strs.append(m.group(1))
    return ' | '.join(strs[-3:])
for shape in sys.argv[3:]:
    out = subprocess.run([os.path.join(here, 'asm2bin.sh'), shape, '/tmp/shape.bin'], capture_output=True, text=True)
    if out.returncode: print(shape, 'ASM ERROR', out.stderr); continue
    r = subprocess.run([harness, rt, '/tmp/shape.bin', 'all'], capture_output=True, text=True)
    n = os.path.getsize('/tmp/shape.bin') // 4
    if 'TRAP' in r.stdout:
        fr = re.findall(r'ret_off=(0x[0-9a-f]+)', r.stdout)
        print(f'{os.path.basename(shape)}: REJECTED (classifier assertion): {assert_text(int(fr[2], 16))}')
    else:
        acts = re.findall(r'idx=(\d+) -> kind=(\d) count=(\d+) flag=(\d)', r.stdout)
        s = ' '.join(f'{i}:{"RB" if k=="0" else "FW"+c}' for i, k, c, f in acts)
        print(f'{os.path.basename(shape)}: ACCEPTED n={n}  {s}')
