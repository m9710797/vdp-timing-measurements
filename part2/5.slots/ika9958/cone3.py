#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Full arbiter cone, down to the pins. Unlike cone2.py this keeps Q and ~Q
distinct (which changes the reading of every cell that uses ~Q) and does not
stop at the port decodes.
"""
import sys
from collections import defaultdict, deque
from netlist2 import load

cells, netof = load(sys.argv[1] if len(sys.argv) > 1 else "CI.kicad_sch")

# a pin is identified by (cell, index within its output/input list)
drv = {}
ld = defaultdict(list)
for c in cells:
    for pn, n in c['o']:
        drv.setdefault(n, []).append(c)
    for pn, n in c['i']:
        ld[n].append(c)

STOP = ('PAD.', 'CLK.', 'GBL.', 'BUS.', 'NET.PLA1.VRAM_SLOT_CPU',
        'NET.REG_', 'NET.VRAM01X_', 'NET.INLATCH_')

roots = ["NET.PLA1.VRAM_RQ_WAITING", "NET.PLA1.VRAM_RQ_GRANTED"]
cone, seen = [], set()
q = deque(roots)
netseen = set(roots)
while q:
    n = q.popleft()
    for c in drv.get(n, []):
        if id(c) in seen:
            continue
        seen.add(id(c))
        cone.append(c)
        for pn, m in c['i']:
            if m in netseen or m.startswith(STOP):
                continue
            netseen.add(m)
            q.append(m)

alias, cnt = {}, [0]


def sn(n):
    if n in alias:
        return alias[n]
    s = n
    for p in ('NET.PLA1.', 'NET.', 'PAD.', 'CLK.', 'GBL.', 'BUS.'):
        if s.startswith(p):
            s = s[len(p):]
            break
    s = s.replace('~{', '/').replace('}', '')
    if s.startswith('~'):
        cnt[0] += 1
        s = f"w{cnt[0]}"
    alias[n] = s
    return s


print(f"full arbiter cone: {len(cone)} cells\n")
for c in sorted(cone, key=lambda c: int(c['ref'][1:])):
    outs = []
    for pn, n in c['o']:
        mark = pn if pn else ('~Q' if True else 'Q')
        nloads = len(ld.get(n, []))
        outs.append(f"{mark}={sn(n)}" + ("" if nloads else "[nc]"))
    ins = ' '.join(f"{pn or '.'}:{sn(n)}" for pn, n in c['i'])
    print(f"  {c['ref']:>6} {c['lib']:<13} {' '.join(outs):<30} <- {ins}")

print("\nboundary:")
for n in sorted({n for c in cone for pn, n in c['i']
                 if n.startswith(STOP) or n not in drv}):
    print("   ", sn(n), "  <-", n)
