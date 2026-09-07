#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Cycle-accurate simulation of the RCC clock divider, transcribed from
RCC.kicad_sch, to measure what a gc024 stall actually costs.

All the VY_DSR_small cells here are phiA-clocked D flops.  The ring is
U17 -> U20 -> U23 with the feedback closed by NOR6 U36; U16/U19/U22 is a
three-stage phiA delay of the phiL phase, and NOR4 U24 -> U27 injects the
stall into the NOR6.  U13/U18/U21 is the DLCLK path, inert when DC = 0.
"""


class Rcc:
    def __init__(self, dc=0):
        self.dc = dc
        self.q17 = self.q20 = self.q23 = 0
        self.q27 = self.q18 = self.q21 = 0
        self.q28 = self.q30 = 0          # phiL, phiH
        self.q16 = self.q19 = self.q22 = 0
        self.q13 = self.q10 = 0

    def step(self, gc024=1, dlclk=0):
        """One phiA cycle.  Returns (phiL, phiH) as seen during this cycle."""
        phiL, phiH = self.q28, self.q30
        u25 = 0 if (self.q20 or self.q17) else 1      # NOR2 U25 -> phiL_in
        u26 = 0 if (self.q23 or self.q17) else 1      # NOR2 U26 -> phiH_in
        nor6 = 0 if (self.q27 or self.q18 or self.q21 or
                     self.q23 or self.q20 or self.q17) else 1
        u24 = 1 if (self.q22 and not self.q19 and
                    not gc024 and not self.dc) else 0
        u12 = 1 if ((not self.q10) and self.dc) else 0
        (self.q17, self.q20, self.q23) = (nor6, self.q17, self.q20)
        (self.q16, self.q19, self.q22) = (u25, self.q16, self.q19)
        self.q27 = u24
        (self.q13, self.q18, self.q21) = (u12, self.q13, self.q18)
        self.q10 = dlclk
        self.q28, self.q30 = u25, u26
        return phiL, phiH


def run(nstall_ticks, settle=400, watch=200):
    """Hold gc024 low for nstall_ticks phiL ticks; return the phiA lengths of
    each phiL period around the stall."""
    r = Rcc()
    for _ in range(settle):
        r.step()
    # find a phiL rising edge
    prev = r.step()[0]
    while True:
        cur = r.step()[0]
        if not prev and cur:
            break
        prev = cur
    # now at the first cycle of a phiL high phase; count periods
    periods, hi_len = [], []
    ticks_stalled = 0
    # The rising-edge sample belongs to the new period. Count edge-to-edge
    # distances, excluding the next rising-edge sample from the old high phase.
    n, hi, prev = 0, 1, 1
    stalling = False
    for _ in range(watch):
        # gc024 goes low for nstall_ticks whole phiL ticks, starting at
        # period index 3 (arbitrary, just clear of the settle)
        if len(periods) == 3 and not stalling:
            stalling = True
        low = stalling and ticks_stalled < nstall_ticks
        cur, _h = r.step(gc024=0 if low else 1)
        n += 1
        if not prev and cur:            # rising edge: period ended
            periods.append(n)
            hi_len.append(hi)
            if low:
                ticks_stalled += 1
            n, hi = 0, 1
        elif cur:
            hi += 1
        prev = cur
    return periods[:12], hi_len[:12]


if __name__ == '__main__':
    for k in (0, 1, 2, 4):
        p, h = run(k)
        print(f"gc024 low for {k} tick(s):")
        print(f"   phiL periods (phiA cycles): {p}")
        print(f"   phiL high phases          : {h}")
        print(f"   line total for 341 ticks  : {341 * 4 + sum(x - 4 for x in p)}")
