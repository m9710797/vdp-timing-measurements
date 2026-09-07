#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Read the 16-channel logic-analyzer VCDs of part2/1.vcd.

Channels: A0..A7 (multiplexed VRAM address), RAS, CAS0, CAS1, R/W, VDS, IRQ,
CSW, CSR. Timescale is 100 ps, sampled at 80 MHz (125 time units per sample).

Unlike the 2.rw / 3.time pipeline this does not assume a 1368-cycle line, so it
is usable for the S1/S0 (1365) and set-adjust captures.

Access times here are **RAS** falling (2.rw prints CAS falling).
"""
from __future__ import annotations

from dataclasses import dataclass, field

SAMPLE = 125           # time units per 80 MHz sample


@dataclass
class Access:
    ras: int                                        # RAS falling, time units
    cas: int = -1                                   # first CAS falling
    addr: int = -1
    read: bool = True
    vds: int = 0                                    # VDS at RAS
    cas_times: list = field(default_factory=list)   # all CAS of this RAS


@dataclass
class Capture:
    path: str
    accesses: list
    edges: dict            # CSW / CSR / VDS / IRQ -> [(time, value)]
    end: int


def parse(path):
    rev = {}
    with open(path) as f:
        for ln in f:
            if ln.startswith('$var'):
                p = ln.split()
                rev[p[3]] = p[4]
            elif ln.startswith('$enddefinitions'):
                break

        state = {n: 0 for n in rev.values()}
        accesses = []
        edges = {'CSW': [], 'CSR': [], 'VDS': [], 'IRQ': []}
        high = 0
        cur = None
        t = 0
        last = 0

        def bus():
            a = 0
            for i in range(8):
                a |= state[f'A{i}'] << i
            return a

        for ln in f:
            ln = ln.strip()
            if not ln or ln[0] == '$':
                continue
            for tok in ln.split():
                if tok[0] == '#':
                    t = int(tok[1:])
                    last = t
                    continue
                if len(tok) < 2:
                    continue
                name = rev.get(tok[1:])
                if name is None:
                    continue
                v = 1 if tok[0] == '1' else 0
                prev = state[name]
                state[name] = v
                if name in edges:
                    if v != prev:
                        edges[name].append((t, v))
                elif name == 'RAS':
                    if v == 0 and prev != 0:
                        high = bus() << 8
                        cur = Access(ras=t, read=bool(state['R/W']),
                                     vds=state['VDS'])
                        accesses.append(cur)
                elif name in ('CAS0', 'CAS1') and v == 0 and prev != 0:
                    if cur is not None:
                        full = high + bus() + (0x10000 if name == 'CAS1' else 0)
                        cur.cas_times.append(t)
                        if cur.cas < 0:
                            cur.cas = t
                            cur.addr = full
                            cur.read = bool(state['R/W'])

        return Capture(path=path, accesses=accesses, edges=edges, end=last)


def classify(addr):
    """Same buckets as 2.rw/process.cc annotate_type()."""
    if addr < 0:
        return '?'
    if addr == 0x1FFFF:
        return 'dummy'
    if addr < 0x6A00:
        return 'bitmap'
    if 0xD400 <= addr < 0xD680:
        return 'sprA'
    if 0xD800 <= addr < 0xE000:
        return 'sprP'
    if (addr & 0x3F) == 0x3F:
        return 'refresh'
    if 0x14000 <= addr < 0x18000:
        return 'cpu'
    if 0x18000 <= addr < 0x1C000:
        return 'cmdsrc'
    if 0x1C000 <= addr < 0x20000:
        return 'cmddst'
    return 'other'


def refresh_ras(cap):
    """Refresh accesses: read, low 6 address bits all 1, not the dummy.

    Same filter as 2.rw/process.cc candidate_filter1(); refresh alternates
    between the two CAS banks, so do not require bit 16.
    """
    return [a for a in cap.accesses
            if a.addr >= 0 and (a.addr & 0x3F) == 0x3F
            and a.read and a.addr != 0x1FFFF]


def refresh_lattice(cap, guess=1366.0):
    """Index the refresh comb as (line, k) and fit the line length.

    Refresh is 8 per line, 128 cycles apart, first one at RAS 284. Fit
    ``t = A + B*128*k + (B*L)*n`` by least squares: three linear unknowns, so
    ``L`` comes out without assuming 1368. Robust against missing refresh
    pulses (the (n, k) walk rounds to the nearest multiple of 128).

    Returns (L, B, A, resid_cycles, n_lines, [(access, n, k)]) or None.
    """
    ref = refresh_ras(cap)
    if len(ref) < 10:
        return None
    t = [a.ras for a in ref]
    gaps = [t[i + 1] - t[i] for i in range(len(t) - 1)]
    small = [g for g in gaps if g < 1.7 * sorted(gaps)[len(gaps) // 2]]
    upc = (sum(small) / len(small)) / 128.0          # bootstrap units/cycle

    n, k = 0, 0
    idx = [(0, 0)]
    first_cross = None
    for g in gaps:
        c = g / upc
        within = c - 128 * round(c / 128)            # residue if same line
        cross = c - guess - 128 * round((c - guess) / 128)
        if abs(within) <= abs(cross):
            k += round(c / 128)
        else:
            n += 1
            k += round((c - guess) / 128)
            if first_cross is None:
                first_cross = len(idx)
        idx.append((n, k))

    # The capture starts mid-line, so re-anchor: the refresh right after a
    # line boundary is k = 0 of that line.
    if first_cross is None:
        return None
    n0, k0 = idx[first_cross]
    idx = [(n - n0, k - k0) for n, k in idx]

    # least squares on [1, 128k, n]
    import itertools
    rows = [(1.0, 128.0 * kk, float(nn)) for nn, kk in idx]
    m = len(rows)
    ata = [[sum(rows[i][p] * rows[i][q] for i in range(m)) for q in range(3)]
           for p in range(3)]
    atb = [sum(rows[i][p] * t[i] for i in range(m)) for p in range(3)]
    # gaussian elimination
    for col in range(3):
        piv = max(range(col, 3), key=lambda rr: abs(ata[rr][col]))
        ata[col], ata[piv] = ata[piv], ata[col]
        atb[col], atb[piv] = atb[piv], atb[col]
        if abs(ata[col][col]) < 1e-12:
            return None
        for rr in range(3):
            if rr == col:
                continue
            fac = ata[rr][col] / ata[col][col]
            for cc in range(3):
                ata[rr][cc] -= fac * ata[col][cc]
            atb[rr] -= fac * atb[col]
    A, B, Q = (atb[i] / ata[i][i] for i in range(3))
    L = Q / B                                        # cycles per line
    resid = max(abs(t[i] - (A + B * 128 * idx[i][1] + Q * idx[i][0])) / B
                for i in range(m))
    return L, B, A, resid, n + 1, [(ref[i], idx[i][0], idx[i][1]) for i in range(m)]


def line_length(cap):
    r = refresh_lattice(cap)
    if r is None:
        return None
    return r[0], r[3], r[4], r[1]


def cycle_of(cap, lat, t):
    """Absolute VDP cycle of a raw time, on the fitted refresh lattice.

    Cycle 0 is the start of the line that holds refresh (n=0, k=0); that
    refresh itself is at 284 (RAS numbering of FINDINGS-ADJUST-S10).
    """
    L, B, A = lat[0], lat[1], lat[2]
    return 284.0 + (t - A) / B


def pulses(edges, active_low=True):
    """Turn an edge list into (start, end) pulses of the active level."""
    out = []
    start = None
    lo, hi = (0, 1) if active_low else (1, 0)
    for t, v in edges:
        if v == lo and start is None:
            start = t
        elif v == hi and start is not None:
            out.append((start, t))
            start = None
    return out
