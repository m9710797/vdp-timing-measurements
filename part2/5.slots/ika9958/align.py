#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Best cyclic alignment of the simulated slot lattice against the measured one."""
import mpla2 as M

LINE = 1368

for mode in ("dispOff", "sprOff", "sprOn"):
    tr = M.run(mode, 0x1DF)
    s, e = M.cycle(tr)
    sim = [(i - s) * 4 for i in range(s, e) if tr[i][1]]
    mea = M.MEASURED[mode]
    best = None
    for off in range(LINE):
        sh = sorted((x + off) % LINE for x in sim)
        # pair each measured slot with the nearest simulated one, cyclically
        tot = 0
        dl = []
        for m in mea:
            d = min(((v - m + LINE // 2) % LINE) - LINE // 2 for v in sh)
            d = min((((v - m + LINE // 2) % LINE) - LINE // 2 for v in sh),
                    key=abs)
            dl.append(d)
            tot += abs(d)
        if best is None or tot < best[0]:
            best = (tot, off, sh, dl)
    tot, off, sh, dl = best
    exact = sum(1 for d in dl if d == 0)
    within2 = sum(1 for d in dl if abs(d) <= 2)
    print(f"===== {mode}: {len(sim)} sim vs {len(mea)} measured, "
          f"best offset {off}, total |delta| {tot}")
    print(f"      exact {exact}/{len(mea)}, within +-2 {within2}/{len(mea)}")
    from collections import Counter
    print(f"      delta histogram {sorted(Counter(dl).items())}")
    bad = [(m, d) for m, d in zip(mea, dl) if abs(d) > 2]
    if bad:
        print(f"      slots off by more than 2: {bad}")
    print(f"      sim  {sh}")
    print(f"      meas {mea}")
