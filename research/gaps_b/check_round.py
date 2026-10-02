#!/usr/bin/env python3
"""Verify 'R' lines from `x87a round` against exact rational arithmetic (round to p bits per RC)."""
import sys, re
from fractions import Fraction
from math import isqrt
def dec(s):
    se, m = s.split(':'); se = int(se, 16); m = int(m, 16)
    sign = -1 if se & 0x8000 else 1; e = se & 0x7fff
    if e == 0: return sign * Fraction(m) * Fraction(2) ** (-16382 - 63)
    return sign * Fraction(m) * Fraction(2) ** (e - 16383 - 63)
def rnd(v, p, rc, emin=-16382):
    if v == 0: return v
    sign = -1 if v < 0 else 1; a = abs(v)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a: e -= 1
    if e < emin: e = emin
    q = Fraction(2) ** (e - p + 1)
    n = a / q; fl = n.numerator // n.denominator; rem = n - fl
    up = False
    if rem:
        if rc == 0: up = rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl & 1)
        elif rc == 1: up = sign < 0
        elif rc == 2: up = sign > 0
    return sign * (fl + up) * q
def sqrt_round(a, p, rc):
    n, d = a.numerator, a.denominator
    # sqrt(n/d) = sqrt(n*d)/d
    k = 200; num = n * d * 4 ** k; r = isqrt(num); sticky = r * r != num
    val = Fraction(2 * r + 1, 2) if sticky else Fraction(r)
    return rnd(val / (d * 2 ** k), p, rc)
bad = {}; tot = {}
for ln in sys.stdin:
    m = re.match(r'R (\w+) cw=(\w+) (\S+) (\S+) -> (\S+) sw=(\w+)', ln)
    if not m: continue
    op, cw, a, b, r, sw = m.groups(); cw = int(cw, 16); pc = (cw >> 8) & 3; rc = (cw >> 10) & 3
    p = {0: 24, 2: 53, 3: 64}[pc]
    A, B, R = dec(a), dec(b), dec(r)
    if op == 'add': ex = rnd(A + B, p, rc)
    elif op == 'sub': ex = rnd(A - B, p, rc)
    elif op == 'mul': ex = rnd(A * B, p, rc)
    elif op == 'div': ex = rnd(A / B, p, rc)
    elif op == 'sqrt': ex = sqrt_round(A, p, rc)
    else: continue
    key = (op, p, ['nearest', 'down', 'up', 'trunc'][rc]); tot[key] = tot.get(key, 0) + 1
    if ex != R: bad[key] = bad.get(key, 0) + 1
n = sum(tot.values()); print('checked', n, 'mismatches', sum(bad.values()))
for k in sorted(bad): print('  MISMATCH', k, bad[k], '/', tot[k])
