#!/usr/bin/env python3
"""Combine pass CSVs (arch,name,K,n,min,med,max ns/iter) into a table. usage: analyze.py <dir>"""
import csv, glob, statistics as st, sys, collections
d = sys.argv[1]
data = collections.defaultdict(lambda: collections.defaultdict(list))
K = {}
for f in sorted(glob.glob(d + '/*_pass*.csv')):
    for r in csv.reader(open(f)):
        if len(r) < 7: continue
        a, n, k, _, mn, md, mx = r
        data[(a, n)]['min'].append(float(mn)); data[(a, n)]['med'].append(float(md)); data[(a, n)]['max'].append(float(mx))
        K[n] = int(k)
def agg(a, n):
    v = data.get((a, n))
    if not v: return None
    m = st.median(v['med'])
    return dict(med=m, lo=min(v['min']), hi=max(v['max']), pass_spread=(max(v['med']) - min(v['med'])) / m * 100)
e86 = agg('x86_64', 'empty')['med']; ea = agg('arm64', 'empty')['med']
rows = []
for n in K:
    x = agg('x86_64', n); a = agg('arm64', n); k = K[n] or 1
    if n == 'empty': continue
    xnet = max(x['med'] - e86, 0.0) / k
    anet = max(a['med'] - ea, 0.0) / k if a else None
    rows.append((n, k, x, a, xnet, anet))
print('name,K,ratio_of_mins,ros_ns_iter,ros_min,ros_max,ros_passspread%,nat_ns_iter,nat_min,nat_max,nat_passspread%,ros_ns_op_net,nat_ns_op_net,ratio_iter,ratio_net')
for n, k, x, a, xn, an in rows:
    ri = x['med'] / a['med'] if a else float('nan')
    rn = xn / an if (a and an and an > 0.02) else float('nan')
    rm = x['lo'] / a['lo'] if a else float('nan')
    print('%s,%d,%.2f,%.3f,%.3f,%.3f,%.1f,%s,%s,%s,%s,%.3f,%s,%.2f,%.2f' % (n, k, rm, x['med'], x['lo'], x['hi'], x['pass_spread'],
        '%.3f' % a['med'] if a else '', '%.3f' % a['lo'] if a else '', '%.3f' % a['hi'] if a else '', '%.1f' % a['pass_spread'] if a else '',
        xn, '%.3f' % an if an is not None else '', ri, rn))
print('# loop overhead (empty): rosetta %.3f ns/iter, native %.3f ns/iter' % (e86, ea), file=sys.stderr)
