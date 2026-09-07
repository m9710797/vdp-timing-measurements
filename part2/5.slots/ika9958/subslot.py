#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Each phiL tick holds two VRAM sub-slots, 2 cycles apart. Which one does a CPU
slot use? Test the candidate rules against the measured tables, scoring by exact
position matches at a single cyclic offset shared by all three modes.
"""
import mpla3 as P

LINE = 1368

MEASURED = {
    'dispOff': [
        0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
        164, 172, 180, 188, 196, 204, 212, 220, 228, 236, 244, 252, 260, 268,
        276, 292, 300, 308, 316, 324, 332, 340, 348, 356, 364, 372, 380, 388,
        396, 404, 420, 428, 436, 444, 452, 460, 468, 476, 484, 492, 500, 508,
        516, 524, 532, 548, 556, 564, 572, 580, 588, 596, 604, 612, 620, 628,
        636, 644, 652, 660, 676, 684, 692, 700, 708, 716, 724, 732, 740, 748,
        756, 764, 772, 780, 788, 804, 812, 820, 828, 836, 844, 852, 860, 868,
        876, 884, 892, 900, 908, 916, 932, 940, 948, 956, 964, 972, 980, 988,
        996, 1004, 1012, 1020, 1028, 1036, 1044, 1060, 1068, 1076, 1084, 1092,
        1100, 1108, 1116, 1124, 1132, 1140, 1148, 1156, 1164, 1172, 1188, 1196,
        1204, 1212, 1220, 1228, 1268, 1276, 1284, 1292, 1300, 1308, 1316, 1324,
        1334, 1344, 1352, 1360,
    ],
    'sprOff': [
        6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110, 118, 162,
        170, 182, 188, 214, 220, 246, 252, 278, 310, 316, 342, 348, 374, 380,
        406, 438, 444, 470, 476, 502, 508, 534, 566, 572, 598, 604, 630, 636,
        662, 694, 700, 726, 732, 758, 764, 790, 822, 828, 854, 860, 886, 892,
        918, 950, 956, 982, 988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
        1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298, 1306, 1314,
        1322, 1332, 1342, 1350, 1358, 1366,
    ],
    'sprOn': [
        28, 92, 162, 170, 188, 220, 252, 316, 348, 380, 444, 476, 508, 572,
        604, 636, 700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116,
        1148, 1212, 1264, 1330,
    ],
}


def ticks(mode):
    return P.trace(mode)


RULES = {
    'A: always sub-slot 0':      lambda t, i: 0,
    'B: always sub-slot 1':      lambda t, i: 2,
    'C: 1 if ras1 else 0':       lambda t, i: 2 if t[i][0] else 0,
    'D: 0 if ras1 else 1':       lambda t, i: 0 if t[i][0] else 2,
    'E: 1 if ras0 else 0':       lambda t, i: 2 if t[i][1] else 0,
    'F: 1 if both ras else 0':   lambda t, i: 2 if (t[i][0] and t[i][1]) else 0,
    'G: 1 if ras1 of next tick': lambda t, i: 2 if t[(i + 1) % len(t)][0] else 0,
    'H: 1 if ras1 of prev tick': lambda t, i: 2 if t[i - 1][0] else 0,
}


def positions(mode, rule):
    t = ticks(mode)
    return sorted((i * 4 + rule(t, i)) % LINE
                  for i in range(len(t)) if t[i][2])


def score(mode, pos, off):
    mea = MEASURED[mode]
    if len(pos) != len(mea):
        return None
    sh = sorted((x + off) % LINE for x in pos)
    d = [a - b for a, b in zip(sh, mea)]
    return sum(map(abs, d)), max(map(abs, d)), sum(1 for x in d if x == 0), d


def main():
    print()
    print(f"{'rule':<28} {'offset':>7} " + ' '.join(f"{m:>22}" for m in MEASURED))
    best_overall = None
    for name, rule in RULES.items():
        pos = {m: positions(m, rule) for m in MEASURED}
        # one offset shared by all three modes
        cand = []
        for off in range(0, LINE, 2):
            rs = [score(m, pos[m], off) for m in MEASURED]
            if any(r is None for r in rs):
                continue
            cand.append((sum(r[0] for r in rs), off, rs))
        if not cand:
            print(f"{name:<28} (slot counts differ)")
            continue
        tot, off, rs = min(cand)
        cells = ' '.join(f"{r[2]:>4}/{len(MEASURED[m]):<3} max{r[1]:<3} "
                         for r, m in zip(rs, MEASURED))
        print(f"{name:<28} {off:>7} {cells}  total|d| {tot}")
        if best_overall is None or tot < best_overall[0]:
            best_overall = (tot, name, off, rs)

    tot, name, off, rs = best_overall
    print(f"\nbest: {name} at offset {off}, total |delta| {tot}")
    for r, m in zip(rs, MEASURED):
        print(f"  {m:8s} exact {r[2]}/{len(MEASURED[m])}  max|d| {r[1]}")
        bad = [(b, x) for b, x in zip(MEASURED[m], r[3]) if x]
        if bad:
            print(f"           off: {bad}")


if __name__ == '__main__':
    main()
