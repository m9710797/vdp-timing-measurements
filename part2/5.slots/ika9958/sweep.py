#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Sweep a single CPU access across the line and read off the arbiter's rule:
which slot is granted as a function of the /CSx edges, and whether the trigger
is the falling or the rising edge.
"""
import sys
from sim import Sim
import mpla2 as M

LINE = 1368
OFFSET = 212
ROOTS = ["NET.PLA1.VRAM_RQ_WAITING", "NET.PLA1.VRAM_RQ_GRANTED"]
STOP = ('PAD.', 'CLK.', 'GBL.', 'BUS.', 'NET.PLA1.VRAM_SLOT_CPU',
        'NET.REG_', 'NET.VRAM01X_', 'NET.INLATCH_')


def lattice(mode):
    tr = M.run(mode, 0x1DF)
    s, e = M.cycle(tr)
    ticks = [tr[i][1] for i in range(s, e)]
    n = len(ticks)
    allsl = {(i * 4 + OFFSET) % LINE for i in range(n) if ticks[i]}
    rising = sorted((i * 4 + OFFSET) % LINE
                    for i in range(n) if ticks[i] and not ticks[i - 1])
    return allsl, rising


HOLD = {
    'BUS.SECOND_DATA.D_{6}': 0, 'BUS.SECOND_DATA.D_{7}': 0,
    'GBL.phiL_RST': 0, 'NET.REG_d25_b2_WTE': 0,
    'NET.VRAM01X_IOLATCH_VDLD': 0, 'PAD.MODE0.IN': 0, 'PAD.MODE1.IN': 0,
    'PAD.~{CSR}.IN': 1, 'PAD.~{CSW}.IN': 1, '~101.6_77.47': 1,
}


class Rig:
    def __init__(self, mode):
        self.sim = Sim("CI.kicad_sch", ROOTS, STOP)
        self.allsl, self.rising = lattice(mode)

    def cycle_in(self, t, csx, lo, hi):
        phiL = 1 if (t % 4) >= 2 else 0
        d = dict(HOLD)
        d['CLK.phiL'] = phiL
        d['CLK.~{phiL}'] = 1 - phiL
        d['NET.PLA1.VRAM_SLOT_CPU'] = 1 if (t - t % 4) % LINE in self.allsl else 0
        if csx:
            d[csx] = 0 if lo <= t < hi else 1
        return d

    def reset(self, t0=-60):
        for t in range(t0, 0):
            d = self.cycle_in(t, None, 0, 0)
            d['GBL.phiL_RST'] = 1 if t < t0 + 20 else 0
            self.sim.step(d)

    def one(self, t_fall, width, csr=True, span=1400):
        """returns (rise, [grant cycles])"""
        self.reset()
        csx = 'PAD.~{CSR}.IN' if csr else 'PAD.~{CSW}.IN'
        grants, prev = [], 0
        for t in range(0, t_fall + span):
            self.sim.step(self.cycle_in(t, csx, t_fall, t_fall + width))
            g = self.sim.net['NET.PLA1.VRAM_RQ_GRANTED']
            if g and not prev:
                grants.append(t)
            prev = g
        return t_fall + width, grants


def slot_of(g, rising):
    """the slot tick whose grant pulse this is"""
    base = g - g % 4
    return base % LINE


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'dispOff'
    rig = Rig(mode)
    print(f"\n=== {mode}: {len(rig.allsl)} slots, "
          f"{len(rig.rising)} CPU-legal ===")

    # 1. is the trigger the fall or the rise?
    print("\n-- pulse width sweep, /CSR falling at a fixed cycle --")
    print(f"{'width':>6} {'fall':>6} {'rise':>6} {'grant':>7} {'slot':>6} "
          f"{'slot-fall':>10} {'slot-rise':>10}")
    for w in (9, 11, 13, 15, 17, 19, 21):
        rise, g = rig.one(400, w)
        if not g:
            print(f"{w:>6} {400:>6} {rise:>6}   no grant")
            continue
        s = slot_of(g[0], rig.rising)
        print(f"{w:>6} {400:>6} {rise:>6} {g[0]:>7} {s:>6} "
              f"{s-400:>10} {s-rise:>10}")

    # 2. constant, from a sweep of the falling edge
    print("\n-- falling-edge sweep, width 15 --")
    ks = []
    for tf in range(400, 448):
        rise, g = rig.one(tf, 15)
        if not g:
            continue
        s = slot_of(g[0], rig.rising)
        # smallest K such that s is the first CPU-legal slot >= rise + K
        cand = sorted(rig.rising)
        prev = max([c for c in cand if c < s], default=None)
        ks.append((tf, rise, s, s - rise, (prev - rise) if prev else None))
    for tf, rise, s, dr, dp in ks[:16]:
        print(f"  fall {tf}  rise {rise}  granted slot {s}  "
              f"slot-rise {dr}  prev-slot-rise {dp}")
    if ks:
        lo = max(dp for _, _, _, _, dp in ks if dp is not None)
        hi = min(dr for _, _, _, dr, _ in ks)
        print(f"\n  the grant is the first CPU-legal slot at >= rise + K, "
              f"with K in ({lo}, {hi}]")
