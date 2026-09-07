#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Per-line /RAS map, calibrated on that line's own 8 refresh pulses.

Each display line has 8 refresh accesses, 128 cycles apart, the first at RAS
284 (numbering of FINDINGS-ADJUST-S10.md / openMSX slot tables). Fitting per
line instead of globally keeps the position error ~0.1 cycle, which matters
when comparing a transition line against the dispOff / sprOn tables.

Usage:
    lines.py <file.vcd> [firstline] [lastline]
"""
from __future__ import annotations

import sys

import vcdlib

SYM = {'bitmap': 'B', 'dummy': '.', 'sprA': 'a', 'sprP': 'p',
       'refresh': 'R', '?': '-', 'other': 'o', 'cmddst': 'W', 'cmdsrc': 'S',
       'cpu': 'C'}


def split_lines(cap):
    """Group accesses per line using the refresh comb; returns per-line dicts.

    Refresh pulses within one line are <=1024 cycles apart; the step to the
    next line's first refresh is ~472. A line boundary is a refresh whose gap
    to the previous refresh is not a multiple of 128.
    """
    ref = vcdlib.refresh_ras(cap)
    if len(ref) < 10:
        return []
    t = [a.ras for a in ref]
    gaps = [t[i + 1] - t[i] for i in range(len(t) - 1)]
    upc = sorted(gaps)[len(gaps) // 2] / 128.0

    # k index inside the line, restarting at each boundary
    k = [0] * len(t)
    bound = [False] * len(t)
    for i, g in enumerate(gaps):
        c = g / upc
        if abs(c - 128 * round(c / 128)) <= abs(c - 1366 - 128 * round((c - 1366) / 128)):
            k[i + 1] = k[i] + round(c / 128)
        else:
            bound[i + 1] = True
            k[i + 1] = k[i] + round((c - 1366) / 128)

    # renumber k so that the refresh after a boundary is k = 0
    off = None
    for i in range(len(t)):
        if bound[i]:
            off = k[i]
            break
    if off is None:
        return []
    k = [x - off for x in k]

    groups = []
    cur = []
    for i in range(len(t)):
        if bound[i] and cur:
            groups.append(cur)
            cur = []
        cur.append(i)
    if cur:
        groups.append(cur)

    out = []
    for g in groups:
        if len(g) < 3:
            continue
        # local least squares  t = a + b*128*k
        xs = [128.0 * k[i] for i in g]
        ys = [float(t[i]) for i in g]
        mx = sum(xs) / len(xs)
        my = sum(ys) / len(ys)
        den = sum((x - mx) ** 2 for x in xs)
        b = sum((xs[i] - mx) * (ys[i] - my) for i in range(len(xs))) / den
        a = my - b * mx
        out.append({'a': a, 'b': b, 'k0': k[g[0]],
                    'resid': max(abs(ys[i] - (a + b * xs[i])) / b
                                 for i in range(len(xs)))})
    return out


def line_map(cap, lo=None, hi=None):
    """[(line_index, [(cycle, access)])] with per-line calibration."""
    fits = split_lines(cap)
    if not fits:
        return []
    out = []
    for n, fit in enumerate(fits):
        a, b = fit['a'], fit['b']
        t0 = a - 284.0 * b                       # raw time of cycle 0
        t1 = t0 + 1368.0 * b                     # provisional line end
        if n + 1 < len(fits):
            t1 = fits[n + 1]['a'] - 284.0 * fits[n + 1]['b']
        items = [((x.ras - t0) / b, x) for x in cap.accesses
                 if t0 <= x.ras < t1]
        out.append((n, fit, sorted(items)))
    return [(n, f, it) for n, f, it in out
            if (lo is None or n >= lo) and (hi is None or n <= hi)]


def main():
    cap = vcdlib.parse(sys.argv[1])
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else None
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else None
    print(f'### {sys.argv[1]}')
    for n, fit, items in line_map(cap, lo, hi):
        print(f'--- line {n}  len={1368 * fit["b"]:.0f}units '
              f'resid={fit["resid"]:.2f}c  n={len(items)}')
        cells = [f'{x:.0f}{SYM.get(vcdlib.classify(a.addr), "?")}'
                 for x, a in items]
        for i in range(0, len(cells), 22):
            print('   ', ' '.join(cells[i:i + 22]))


if __name__ == '__main__':
    main()
