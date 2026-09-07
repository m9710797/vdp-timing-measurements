#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
The slot lattice at phiA resolution, with the line-end clock stall of §4d.

RCC.kicad_sch divides phiA by 4 with a three-stage ring (U17/U20/U23) closed by
a NOR6.  Three further NOR6 inputs can force U17.D low, which in the ring's
all-zeros state holds it there -- and phiL is high in that state, so a stall
lengthens the phiL *high* phase by one phiA cycle.

`NOR4 U24 = Q22 & ~Q19 & ~GC024 & ~DC` is true for one phiA cycle per phiL tick
(U16/U19/U22 being a three-stage phiA delay of the phiL phase), so each tick
with `gc024` low costs exactly one extra cycle.  `gc024` is `~hpla[14]`
(hcntr in [328,332), four ticks) when R#9 S1/S0 is 0, and `~hpla[12]`
(hcntr == 328, one tick) otherwise:

    341 ticks * 4 = 1364        the unstalled line
    1364 + 4      = 1368        S == 0
    1364 + 1      = 1365        S != 0   (= 227.5 colour cycles, true NTSC)

Sweeping only the window position against the measured tables gives a unique
answer: stalls at ticks 326..329 with sub-slot 0 on the phiL rising edge, and
all 273 measured positions exact.
"""
import mpla6

LINE = 1368
STALL_FIRST = 326
N_STALL = 4          # 4 for a 1368-cycle line, 1 for 1365

MEASURED = __import__('subslot').MEASURED


def lattice(mode, first=STALL_FIRST, n=N_STALL, sub0_at_rise=True):
    """Return (sorted CPU-slot cycle positions, line length in cycles)."""
    t = mpla6.trace(mode)
    base, out = 0, []
    for i in range(len(t)):
        hi = 3 if first <= i < first + n else 2      # phiL high phase
        rise, fall = base, base + hi
        s0, s1 = (rise, fall) if sub0_at_rise else (fall, rise)
        if t[i][2]:
            out.append(s1 if t[i][1] else s0)
        base += hi + 2
    return sorted(out), base


if __name__ == '__main__':
    for n, want in ((4, 1368), (1, 1365)):
        print(f"--- {want} cycles (S {'== 0' if n == 4 else '!= 0'}) ---")
        for mode in MEASURED:
            pos, line = lattice(mode, n=n)
            row = f"  {mode:8s} line {line}  slots {len(pos):3d}"
            if line == want == LINE:
                meas = MEASURED[mode]
                e, off = max(((sum(1 for a, b in zip(
                    sorted((p + o) % LINE for p in pos), meas) if a == b), o)
                    for o in range(LINE)))
                row += f"  exact {e:3d}/{len(meas):<3} at offset {off}"
            print(row)
