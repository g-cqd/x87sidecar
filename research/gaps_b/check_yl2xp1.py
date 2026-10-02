#!/usr/bin/env python3
import sys; exec(open('check_trans.py').read().split('res = {}')[0])
from collections import defaultdict
b = defaultdict(list)
for ln in open(__import__('os').environ.get('TRANS_TXT','/tmp/trans.txt')):
    t = ln.split()
    if t[0] != 'T' or t[1] != 'fyl2xp1': continue
    y, x = dec(t[2]), dec(t[3]); ex = y * (1 + x).ln() / D(2).ln(); got = dec(t[5])
    e = int(t[3].split(':')[0], 16) & 0x7fff; b[e - 0x3fff].append(float(abs(got - ex) / ulp_of(ex)))
for k in sorted(b): v = b[k]; print('x ~ 2^%d: n=%d max=%.3g ulp median=%.3g ulp' % (k, len(v), max(v), sorted(v)[len(v) // 2]))
