#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Reconstruct access types in a 5.slots trace from the VRAM addresses.

`2.rw` assigns `R.r` / `W.w` / `R.s` / `R.d` / `W.d` from fixed "designated
region" address windows. That breaks when a command rectangle walks out of
its window (a tall, 2-byte-wide HMMM/LMMM/YMMM sweeps all of VRAM in a few
hundred accesses), so the tags have to come from the access *structure*
instead:

  * a CPU read auto-increments the VRAM pointer, so CPU reads form one chain
    with strictly +1 address steps in time order;
  * a command dest read (LMMM only) reads the very address it then writes;
  * everything else that is read is a command source read;
  * 0x1FFFF stays the dummy `R..`.

Usage:
    retag.py check  <file.txt> ...        # report disagreements only
    retag.py write  <outdir> <file.txt> ...
"""
from __future__ import annotations

import os
import re
import sys

COL = 13          # column width of one "T.t 0xAAAAA" cell
LINE = 1368


def parse(path):
    """[(time, row, col, char_index, tag, addr)] sorted by time."""
    ev = []
    for ln in open(path):
        m = re.match(r'\s*(\d+):', ln)
        if not m:
            continue
        row = int(m.group(1))
        rest = ln[m.end():]
        for mm in re.finditer(r'([RW]\.[a-z.]) 0x([0-9a-f]+)', rest):
            col = mm.start() // COL
            ev.append([col * LINE + row, row, col, m.end() + mm.start(),
                       mm.group(1), int(mm.group(2), 16)])
    ev.sort(key=lambda e: e[0])
    return ev


def cpu_chain(ev):
    """Indices of the longest +1 address chain among the reads."""
    idx = [i for i, e in enumerate(ev)
           if e[4][0] == 'R' and e[5] != 0x1FFFF]
    best = [1] * len(idx)
    prev = [-1] * len(idx)
    for a in range(len(idx)):
        for b in range(a):
            if ev[idx[b]][5] + 1 == ev[idx[a]][5] and best[b] + 1 > best[a]:
                best[a] = best[b] + 1
                prev[a] = b
    if not best:
        return set()
    end = max(range(len(idx)), key=lambda i: best[i])
    out = []
    while end != -1:
        out.append(idx[end])
        end = prev[end]
    return set(out)


def retag(ev):
    """Expected tag per event."""
    cpu = cpu_chain(ev)
    writes = {e[5] for e in ev if e[4][0] == 'W'}
    want = []
    for i, e in enumerate(ev):
        if e[5] == 0x1FFFF:
            want.append('R..')
        elif e[4][0] == 'W':
            want.append('W.d')
        elif i in cpu:
            want.append('R.r')
        elif e[5] in writes:
            want.append('R.d')
        else:
            want.append('R.s')
    return want


def main():
    mode = sys.argv[1]
    args = sys.argv[2:]
    outdir = None
    if mode == 'write':
        outdir = args.pop(0)
        os.makedirs(outdir, exist_ok=True)
    for path in args:
        ev = parse(path)
        want = retag(ev)
        bad = [(ev[i], want[i]) for i in range(len(ev)) if ev[i][4] != want[i]]
        from collections import Counter
        n = Counter(want)
        print(f'{os.path.basename(path):<40} '
              f'R.r={n["R.r"]:3d} R.s={n["R.s"]:3d} R.d={n["R.d"]:3d} '
              f'W.d={n["W.d"]:3d} R..={n["R.."]:3d}   fixed={len(bad):3d} '
              f'{Counter(f"{e[4]}->{w}" for e, w in bad) if bad else ""}')
        if mode != 'write':
            continue
        # rewrite, replacing the 3-char tag in place (same column layout)
        lines = open(path).read().split('\n')
        byrow = {}
        for i, e in enumerate(ev):
            byrow.setdefault(e[1], []).append((e[3], want[i]))
        out = []
        for ln in lines:
            m = re.match(r'\s*(\d+):', ln)
            if not m:
                out.append(ln)
                continue
            row = int(m.group(1))
            s = list(ln)
            for pos, tag in byrow.get(row, []):
                s[pos:pos + 3] = tag
            out.append(''.join(s))
        with open(os.path.join(outdir, os.path.basename(path)), 'w') as f:
            f.write('\n'.join(out))


if __name__ == '__main__':
    main()
