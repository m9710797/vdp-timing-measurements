#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Classify command-engine leftovers in the sprites-off nx4 mixed traces.

Independent re-implementation of the FINDINGS6 sprites-off engine step, run
from the *observed* previous RAS so errors do not cascade, with occupancy =
observed CPU RAS + observed dummy `R..`. Every mismatch is then labelled by
(delta, obs - pred, packedness), which is what says whether the remaining
holes are one rule or several.

Usage: exp4.py <file.txt> ...
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter, defaultdict

LINE = 1368
SLOTS = [6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110, 118, 162,
         170, 182, 188, 214, 220, 246, 252, 278, 310, 316, 342, 348, 374, 380,
         406, 438, 444, 470, 476, 502, 508, 534, 566, 572, 598, 604, 630, 636,
         662, 694, 700, 726, 732, 758, 764, 790, 822, 828, 854, 860, 886, 892,
         918, 950, 956, 982, 988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
         1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298, 1306,
         1314, 1322, 1332, 1342, 1350, 1358, 1366]
PAD = {1332: 2, 1342: 2}
STRETCH_CAS = {1324, 1334}          # CAS = RAS + 2 there
SLOTSET = set(SLOTS)
PACKED = {SLOTS[i] for i in range(1, len(SLOTS))
          if SLOTS[i] - SLOTS[i - 1] == 6}

# FINDINGS6 §5.2, sprites off (no +2 addend)
WAITS = {
    'hmmm': {('s', 'w'): 24, ('w', 's'): 60, 'nl': 128},
    'lmmm': {('s', 'd'): 32, ('d', 'w'): 24, ('w', 's'): 60, 'nl': 128},
    'ymmm': {('s', 'w'): 24, ('w', 's'): 36, 'nl': 104},
}


def pad_in(t, s):
    """Padding completions in (t, s]."""
    n = 0
    for c, e in PAD.items():
        k = (t - c) // LINE
        while True:
            k += 1
            cc = c + k * LINE
            if cc > s:
                break
            if cc > t:
                n += e
    return n


def engine_dist(t, s):
    return (s - t) - pad_in(t, s)


def next_slot(last, delta, occupied):
    base = (last // LINE) * LINE
    for k in range(0, 8):
        for s in SLOTS:
            v = base + k * LINE + s
            if v <= last:
                continue
            if engine_dist(last, v) < delta:
                continue
            if v in occupied:
                continue
            return v
    return None


def parse(path):
    ev = []
    for ln in open(path):
        m = re.match(r'\s*(\d+):', ln)
        if not m:
            continue
        row = int(m.group(1))
        for mm in re.finditer(r'([RW]\.[a-z.]) 0x([0-9a-f]+)', ln[m.end():]):
            col = mm.start() // 13
            cas = col * LINE + row
            ras = cas - (2 if row in STRETCH_CAS else 1)
            tag, addr = mm.group(1), int(mm.group(2), 16)
            kind = {'R.s': 's', 'R.d': 'd', 'W.d': 'w', 'R.r': 'r',
                    'W.w': 'r', 'R..': '.'}.get(tag, '?')
            ev.append((ras, kind, addr))
    ev.sort()
    return ev


def main():
    cls = Counter()
    detail = defaultdict(list)
    per_file = {}
    for path in sys.argv[1:]:
        cmd = 'hmmm' if 'hmmm' in path else ('lmmm' if 'lmmm' in path else 'ymmm')
        w = WAITS[cmd]
        ev = parse(path)
        eng = [(r, k, a) for r, k, a in ev if k in 'sdw']
        occ = {r for r, k, a in ev if k in 'r.'}
        n_ok = 0
        n = 0
        for i in range(1, len(eng)):
            a, b = eng[i - 1][1], eng[i][1]
            # newline is decided from consecutive source addresses
            delta = w.get((a, b))
            if delta is None:
                continue
            if (a, b) == ('w', 's'):
                prev_s = None
                for j in range(i - 1, -1, -1):
                    if eng[j][1] == 's':
                        prev_s = eng[j][2]
                        break
                if prev_s is None:
                    continue
                if (eng[i][2] & ~127) != (prev_s & ~127):
                    delta = w['nl']
            last = eng[i - 1][0]
            if last % LINE in PACKED:
                delta += 1                       # packed-start +1, no exception
            pred = next_slot(last, delta, occ)
            obs = eng[i][0]
            n += 1
            if pred == obs:
                n_ok += 1
                continue
            free = next_slot(last, delta, set())
            key = (cmd, delta, obs - pred,
                   'obs_packed' if obs % LINE in PACKED else 'obs_plain',
                   'pred_packed' if pred % LINE in PACKED else 'pred_plain',
                   'pred_was_skipped' if pred != free else 'pred_was_free')
            cls[key] += 1
            detail[key].append((os.path.basename(path), last % LINE,
                                pred % LINE, obs % LINE))
        per_file[os.path.basename(path)] = (n_ok, n)

    tot_ok = sum(v[0] for v in per_file.values())
    tot = sum(v[1] for v in per_file.values())
    print(f'{"file":<40} steps')
    for k in sorted(per_file):
        o, t = per_file[k]
        print(f'{k:<40} {o:4d}/{t:4d}' + ('  OK' if o == t else ''))
    print(f'\ntotal {tot_ok}/{tot} ({100.0*tot_ok/tot:.1f}%)  leftovers {tot-tot_ok}\n')
    print('leftover classes:')
    for key, cnt in sorted(cls.items(), key=lambda kv: -kv[1]):
        cmd, delta, d, op, pp, sk = key
        print(f'  {cnt:3d}  {cmd}  Δ={delta:<4} obs-pred={d:+5d}  {op:<11}{pp:<13}{sk}')
        for x in detail[key][:3]:
            print(f'         e.g. {x[0]}  last row {x[1]} -> pred {x[2]}, obs {x[3]}')


if __name__ == '__main__':
    main()
