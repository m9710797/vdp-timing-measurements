#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Emit the fit_2026 flags for the silicon-derived CPU arbitration model.

The arbiter decides at a phiL tick, but the slot tables record the position of
the memory cycle, which is the tick's rising edge for a sub-slot-0 slot and its
falling edge -- two cycles later -- for a sub-slot-1 slot.  Referring the two
constants back to the tick removes every per-mode number:

    NEED   = 16 + 2 * vram_ras_rq[0]                     (measured *to* the slot)
    THRESH =  1 - 2 * vram_ras_rq[0] - 2 * vram_slot_spr (measured *from* the
                                                          previous slot)

vram_ras_rq[0] is the sub-slot selector, so NEED gains the two cycles by which
a sub-slot-1 slot is recorded late and THRESH loses them again; the extra two
for vram_slot_spr apply to the four ticks where the CPU slot coincides with a
sprite slot.  THRESH = 1 at the tick is D = 4 in the arbiter cone.  Nothing here
depends on the display mode, which is what CI.kicad_sch says: the cone has
K = 13 and D = 4 and no mode input at all.

Across all three modes the CPU ticks fall into exactly four classes:

    ras1 ras0 spr    n   sub-slot  NEED  THRESH
      0    0    0  187      0        16     +1
      1    0    0   32      0        16     +1
      1    1    0   50      1        18     -1
      1    1    1    4      1        18     -3

Only the last two constants, -1 and -3, are chosen against the corpus, and both
land on the same two-cycle quantum as the derived one.  The classes themselves
come from the Memory PLA and the RCC clock divider, not from fitting.

Usage:
    ./fit_2026 --trellis --sel= $(subslot_rows.py)
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mpla6                                                   # noqa: E402

# Per-mode cycle offset of hcntr == 0 in the .txt line coordinate (FINDINGS7
# section 11.6: all three tables share one origin).
OFF = {'dispOff': 20, 'sprOff': 18, 'sprOn': 18}
STALL_FIRST, N_STALL = 326, 4
LINE = 1368


def classify(mode):
    """Map each CPU slot row of one mode to its (ras1, ras0, spr) class."""
    t = mpla6.trace(mode)
    base, out = 0, {}
    for i, (ras1, ras0, cpu, spr) in enumerate(t):
        hi = 3 if STALL_FIRST <= i < STALL_FIRST + N_STALL else 2
        if cpu:
            out[(base + (hi if ras0 else 0) + OFF[mode]) % LINE] = (ras1, ras0, spr)
        base += hi + 2
    return out


def flags():
    # Display off is entirely class (0,0,0) -- with the display off there are
    # no fetches to raise vram_ras_rq -- so it needs no row override and rides
    # on needoff[0] = 0.  It does share rows with the sprite modes, where those
    # rows are class (1,1,0), so it must stay out of the row map.  The two
    # sprite modes never disagree about a shared row, so one map serves both.
    cls = {}
    for mode in ('sprOff', 'sprOn'):
        for row, k in classify(mode).items():
            if row in cls:
                assert cls[row] == k, f'sprOff and sprOn disagree on row {row}'
            cls[row] = k

    sprite_rows = set(cls)
    need16 = sorted(r for r in sprite_rows if not cls[r][1])
    thresh = {}
    for r in sorted(sprite_rows):
        ras1, ras0, spr = cls[r]
        if ras0:
            thresh[r] = 1 - 2 * ras0 - 2 * spr

    out = ['--needoff=0,2,2',
           '--needrow=' + ','.join(str(r) for r in need16) + ':16',
           '--thresh=1,1,1']
    out += [f'--threshrow={r}:{v}' for r, v in thresh.items()]
    return out


if __name__ == '__main__':
    print(' '.join(flags()))
