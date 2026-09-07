#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Print the /RAS comb of a capture in VDP cycles, on the refresh lattice.

Usage: rasmap.py <file.vcd> [lo] [hi]

Cycle numbering is RAS, first refresh of a line = 284 (as in
FINDINGS-ADJUST-S10.md). Works for 1365-cycle and set-adjust captures because
the line length is fitted, not assumed.
"""
from __future__ import annotations

import sys
from collections import defaultdict

import vcdlib


def ras_table(cap, lat):
    """[(line_index, cycle_in_line, access)] using the fitted lattice."""
    L, B, A = lat[0], lat[1], lat[2]
    out = []
    for a in cap.accesses:
        c = 284.0 + (a.ras - A) / B          # cycles since line-0 origin
        n = 0
        x = c
        while x >= L:
            x -= L
            n += 1
        while x < 0:
            x += L
            n -= 1
        cas = ((a.cas_times[0] - a.ras) / B) if a.cas_times else None
        out.append((n, x, a, cas))
    return out


def cluster(rows, lo, hi, tol=1.0):
    """Average cycle position of RAS pulses that repeat every line."""
    pts = sorted(x for _, x, _, _ in rows if lo <= x <= hi)
    groups = []
    for p in pts:
        if groups and p - groups[-1][-1] <= tol:
            groups[-1].append(p)
        else:
            groups.append([p])
    return groups


def main():
    path = sys.argv[1]
    lo = float(sys.argv[2]) if len(sys.argv) > 2 else 1250.0
    hi = float(sys.argv[3]) if len(sys.argv) > 3 else 1400.0
    cap = vcdlib.parse(path)
    lat = vcdlib.refresh_lattice(cap)
    if lat is None:
        print('no refresh lattice')
        return
    print(f'{path}  line={lat[0]:.3f}  resid<={lat[3]:.2f} cycles  '
          f'lines={lat[4]}')
    rows = ras_table(cap, lat)
    groups = cluster(rows, lo, hi)
    prev = None
    for g in groups:
        m = sum(g) / len(g)
        kinds = set()
        cass = []
        for n, x, a, cas in rows:
            if abs(x - m) <= 1.0:
                kinds.add(vcdlib.classify(a.addr))
                if cas is not None:
                    cass.append(cas)
        gap = '' if prev is None else f'{m - prev:6.2f}'
        casinfo = f'CAS+{sum(cass)/len(cass):.2f}' if cass else 'no CAS'
        print(f'  {m:8.2f}  n={len(g):2d}  gap={gap:>7}  {casinfo:<10} '
              f'{",".join(sorted(kinds))}')
        prev = m


if __name__ == '__main__':
    main()
