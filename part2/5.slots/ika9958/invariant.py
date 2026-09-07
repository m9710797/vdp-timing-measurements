#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
The two arbiter constants in all three modes, and the one combination of them
that does not depend on where the lattice origin is put.

  grant: the request is granted at the first CPU-legal slot S >= rise + K
  drop : a following request is taken iff rise' - S_prev >= D

Our model says instead
  S >= rise + phi + NEED        so  K_ours = phi + NEED
  rise' - S_prev >= THRESH - phi so  D_ours = THRESH - phi

Shifting the lattice origin by c sends K -> K + c and D -> D - c, so K + D is
origin-free and equals NEED + THRESH with phi cancelled out.
"""
from sweep import Rig, slot_of
from drop import two

OURS = {                    # NEED, THRESH from fit_2026.cc
    'dispOff': (16, 2),
    'sprOff': (16, 2),
    'sprOn': (18, -1),
}

print()
for mode in ('dispOff', 'sprOff', 'sprOn'):
    rig = Rig(mode)
    # K: first CPU-legal slot at >= rise + K
    Ks = []
    for tf in range(400, 432):
        rig.reset()
        rise, g = rig.one(tf, 15)
        if not g:
            continue
        s = slot_of(g[0], rig.rising)
        cand = sorted(rig.rising)
        prev = max([c for c in cand if c < s], default=None)
        if prev is not None:
            Ks.append((prev - rise, s - rise))     # (excluded, achieved)
    Klo = max(a for a, b in Ks)
    Khi = min(b for a, b in Ks)
    # D: second request taken iff rise2 - S1 >= D
    tk, dp = [], []
    for gap in range(14, 46):
        r1, r2, g = two(rig, 400, gap)
        if not g:
            continue
        s1 = slot_of(g[0], rig.rising)
        (tk if len(g) > 1 else dp).append(r2 - s1)
    Dlo = max(dp) if dp else None
    Dhi = min(tk) if tk else None
    need, thresh = OURS[mode]
    print(f"=== {mode}")
    print(f"    K (grant)  : excluded up to {Klo}, achieved from {Khi}"
          f"   -> K in ({Klo}, {Khi}]")
    print(f"    D (drop)   : dropped up to {Dlo}, taken from {Dhi}"
          f"   -> D in ({Dlo}, {Dhi}]")
    if Dhi is not None:
        print(f"    K + D      : silicon {Khi + Dhi}   "
              f"ours NEED+THRESH = {need}+{thresh} = {need + thresh}")
    print()
