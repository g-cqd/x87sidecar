#!/usr/bin/env python3
"""Estimate the effective pi precision used by stock fsin/fcos/fptan: abs error / k for |x| = k*pi/2 + r, vs result ulp."""
import sys, math
sys.argv = ['x']; exec(open('check_trans.py').read().split('res = {}')[0])
from collections import defaultdict
b = defaultdict(list)
for ln in open(__import__('os').environ.get('TRANS_TXT','/tmp/trans.txt')):
    t = ln.split()
    if t[0] != 'T' or t[1] not in ('fsin', 'fcos'): continue
    a = dec(t[2]); 
    if a is None or abs(a) >= D(2) ** 63: continue
    s, c = sin_(a); ex = s if t[1] == 'fsin' else c; got = dec(t[4]); k = abs((a / (PI / 2)).to_integral_value())
    abs_err = abs(got - ex); u = ulp_of(ex)
    if k == 0: b[('k=0', t[1])].append(float(abs_err / u)); continue
    kb = int(math.log2(float(k))) if k >= 1 else 0
    # abs error in units of 2^-64*k ; also relative ulp
    b[('abs/(k*2^-64)', kb)].append(float(abs_err / (k * D(2) ** -64)))
    b[('ulp', kb)].append(float(abs_err / u))
for key in sorted(b, key=str):
    v = b[key]; print(key, 'n=%d max=%.4g median=%.4g' % (len(v), max(v), sorted(v)[len(v)//2]))
