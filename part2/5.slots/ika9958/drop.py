#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Two consecutive CPU accesses: does the arbiter take the second, and what decides?
Measured relative to the slot granted to the first access, which makes the answer
independent of the lattice origin.
"""
import sys
from sweep import Rig, HOLD, slot_of, LINE


def two(rig, t1, gap, width=15, csr=True, span=1600):
    rig.reset()
    csx = 'PAD.~{CSR}.IN' if csr else 'PAD.~{CSW}.IN'
    t2 = t1 + gap
    grants, prev = [], 0
    for t in range(0, t2 + span):
        phiL = 1 if (t % 4) >= 2 else 0
        d = dict(HOLD)
        d['CLK.phiL'] = phiL
        d['CLK.~{phiL}'] = 1 - phiL
        d['NET.PLA1.VRAM_SLOT_CPU'] = \
            1 if (t - t % 4) % LINE in rig.allsl else 0
        d[csx] = 0 if (t1 <= t < t1 + width or t2 <= t < t2 + width) else 1
        rig.sim.step(d)
        g = rig.sim.net['NET.PLA1.VRAM_RQ_GRANTED']
        if g and not prev:
            grants.append(t)
        prev = g
    return t1 + width, t2 + width, grants


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'dispOff'
    rig = Rig(mode)
    print(f"\n=== {mode}: two accesses, width 15 ===")
    print(f"{'gap':>5} {'rise1':>6} {'rise2':>6} {'grants':>16} "
          f"{'S1':>6} {'S2':>6} {'rise2-S1':>9} {'taken':>6}")
    rows = []
    for gap in range(16, 72, 2):
        r1, r2, g = two(rig, 400, gap)
        S = [slot_of(x, rig.rising) for x in g]
        s1 = S[0] if len(S) > 0 else None
        s2 = S[1] if len(S) > 1 else None
        taken = len(S) > 1
        print(f"{gap:>5} {r1:>6} {r2:>6} {str(g):>16} "
              f"{str(s1):>6} {str(s2):>6} "
              f"{str(r2-s1) if s1 else '-':>9} {str(taken):>6}")
        if s1:
            rows.append((gap, r2 - s1, taken))
    print()
    tk = [d for _, d, t in rows if t]
    dp = [d for _, d, t in rows if not t]
    print(f"  taken   at rise2 - S1 = {sorted(set(tk))}")
    print(f"  dropped at rise2 - S1 = {sorted(set(dp))}")
    if tk and dp:
        print(f"  boundary: dropped for rise2 - S1 <= {max(dp)}, "
              f"taken for >= {min(tk)}")
