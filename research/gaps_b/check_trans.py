#!/usr/bin/env python3
"""Compare 80-bit transcendental results (x87trans output on stdin) with 100-digit Decimal references; report max/mean ulp error per op."""
import sys
from decimal import Decimal as D, getcontext
getcontext().prec = 130
PI = D('3.14159265358979323846264338327950288419716939937510582097494459230781640628620899862803482534211706798214808651')
def dec(s):
    se, m = s.split(':'); se = int(se, 16); m = int(m, 16); sign = -1 if se & 0x8000 else 1; e = se & 0x7fff
    if e in (0, 0x7fff): return None
    return sign * D(m) * (D(2) ** (e - 16383 - 63))
def ulp_of(x):
    x = abs(x)
    if x == 0: return None
    e = int((x.ln() / D(2).ln()).to_integral_value(rounding='ROUND_FLOOR'))
    if D(2) ** e > x: e -= 1
    if D(2) ** (e + 1) <= x: e += 1
    return D(2) ** (e - 63)
def sin_(x):
    k = (x / (PI / 2)).to_integral_value(); r = x - k * (PI / 2); q = int(k) % 4
    def ts(r):
        s = t = r; n = 1
        while abs(t) > D(10) ** -125: t = -t * r * r / ((n + 1) * (n + 2)); n += 2; s += t
        return s
    def tc(r):
        s = t = D(1); n = 0
        while abs(t) > D(10) ** -125: t = -t * r * r / ((n + 1) * (n + 2)); n += 2; s += t
        return s
    return [ts(r), tc(r), -ts(r), -tc(r)][q], [tc(r), -ts(r), -tc(r), ts(r)][q]
def atan_(x):
    if x < 0: return -atan_(-x)
    if x > 1: return PI / 2 - atan_(1 / x)
    for _ in range(6): x = x / (1 + (1 + x * x).sqrt()); 
    s = t = x; n = 1
    while abs(t) > D(10) ** -125: t = -t * x * x; n += 2; s += t / n
    return s * 64
def atan2_(y, x):
    if x > 0: return atan_(y / x)
    if x < 0: return atan_(y / x) + (PI if y >= 0 else -PI)
    return PI / 2 if y > 0 else -PI / 2
res = {}
worst = {}
def rec(op, got, ex, line):
    u = ulp_of(ex)
    if u is None: return
    err = abs(got - ex) / u
    res.setdefault(op, []).append(float(err))
    if op not in worst or err > worst[op][0]: worst[op] = (err, line.strip())
outrange = []
for ln in sys.stdin:
    t = ln.split()
    if not t or t[0] != 'T': continue
    op = t[1]
    sw = int([x for x in t if x.startswith('sw=')][0][3:], 16)
    if op in ('fsin', 'fcos', 'fsincos', 'fptan'):
        a = dec(t[2]); 
        if a is None: continue
        if abs(a) >= D(2) ** 63:
            outrange.append((op, t[2], 'C2=%d' % bool(sw & 0x400), 'result==operand' if t[4] == t[2] else 'result changed')); continue
        s, c = sin_(a)
        if op == 'fsin': rec(op, dec(t[4]), s, ln)
        elif op == 'fcos': rec(op, dec(t[4]), c, ln)
        elif op == 'fsincos': rec('fsincos.cos', dec(t[4]), c, ln); rec('fsincos.sin', dec(t[5]), s, ln)
        else: rec('fptan', dec(t[5]), s / c, ln)
    elif op == 'f2xm1':
        a = dec(t[2]); rec(op, dec(t[4]), (a * D(2).ln()).exp() - 1, ln)
    elif op == 'fpatan':
        y, x = dec(t[2]), dec(t[3]); rec(op, dec(t[5]), atan2_(y, x), ln)
    elif op == 'fyl2x':
        y, x = dec(t[2]), dec(t[3]); rec(op, dec(t[5]), y * x.ln() / D(2).ln(), ln)
    elif op == 'fyl2xp1':
        y, x = dec(t[2]), dec(t[3]); rec(op, dec(t[5]), y * (1 + x).ln() / D(2).ln(), ln)
for op in sorted(res):
    v = sorted(res[op]); print('%-12s n=%3d  max=%8.3f ulp  mean=%7.3f  p99=%7.3f  >1ulp: %d' % (op, len(v), v[-1], sum(v) / len(v), v[int(len(v) * .99) - 1], sum(1 for x in v if x > 1)))
    if v[-1] > 1: print('     worst:', worst[op][1][:140])
print('out-of-range (|x|>=2^63) cases:'); [print('  ', *o) for o in outrange]
