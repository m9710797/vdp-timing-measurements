#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
As mpla.py, but with the H-adjust reload (hadd_eq23) put back. Free-running the
bpc counter gives a 1376-cycle line; the reload is what truncates it to 1368, so
it also decides where the slot pattern is cut. Its position is scanned, and each
candidate is scored by cyclic gap match against the three measured tables.
"""

LINE = 1368

MEASURED = {
    "dispOff": [
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
    "sprOff": [
        6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110, 118, 162,
        170, 182, 188, 214, 220, 246, 252, 278, 310, 316, 342, 348, 374, 380,
        406, 438, 444, 470, 476, 502, 508, 534, 566, 572, 598, 604, 630, 636,
        662, 694, 700, 726, 732, 758, 764, 790, 822, 828, 854, 860, 886, 892,
        918, 950, 956, 982, 988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
        1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298, 1306, 1314,
        1322, 1332, 1342, 1350, 1358, 1366,
    ],
    "sprOn": [
        28, 92, 162, 170, 188, 220, 252, 316, 348, 380, 444, 476, 508, 572,
        604, 636, 700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116,
        1148, 1212, 1264, 1330,
    ],
}


def srf(q, s, r):
    if s and r:
        return 0
    return 1 if s else (0 if r else q)


def run(mode, hfire, ticks=6000):
    bpc = 0
    bpla_z = {}
    gt042 = gt044 = bpc_hi_ci = gt072 = 0
    sr4 = [0, 0, 0, 0]
    gt059 = gt063 = gt064 = 0
    he_z = he_long_z = 0

    av_w_spr = 1 if mode == "sprOn" else 0
    av_wo_spr = 1 if mode == "sprOff" else 0
    av_common = av_w_spr or av_wo_spr

    out = []
    for t in range(ticks):
        bpla = {
            0: (bpc & 0xF) == 9, 2: bpc == 0x1F6, 6: bpc == 0x12F,
            30: bpc == 0x100, 32: bpc == 0x1F0, 37: bpc == 0x108,
            38: bpc == 0x1DF, 39: bpc == 0x1EC, 40: bpc == 0x0FF,
            41: bpc == 0x1F1, 42: (bpc & 0x107) == 0x001,
            43: (bpc & 0x11F) == 0x011, 44: (bpc & 7) == 1,
            47: (bpc & 0x1CF) == 0x107, 48: bpc in (0x1EC, 0x1EE),
            49: bpc == 0x1D7, 84: bpc == 0x1FC, 85: bpc == 0x1FE,
        }
        gt076 = (not gt072) and not bpla[47] and not bpla[48] and not bpla[49]
        gt077 = ((gt059 or not bpla[44]) and ((bpc & 1) or gt063)
                 and not gt072 and not bpla[48])
        gt078 = ((bpc & 1) or gt063) and ((bpc & 1) or gt064 or bpla_z.get(43, 0))

        if mode == "sprOn":
            cpu = av_w_spr and not gt076
        elif mode == "sprOff":
            cpu = av_wo_spr and not gt077
        else:
            cpu = (not av_common) and not gt078
        out.append((bpc, bool(cpu and not (bpla[84] or bpla[85]))))

        he = 1 if bpc == hfire else 0            # ST.hadd_eq23, one tick per line
        he_long = he or he_z

        gt033 = 1 if (not gt042 and not he_long_z) else 0
        ld = gt033 or bpla_z.get(2, 0) or he_z
        if ld:
            nlo = (he_z << 1) | (he_z or gt033)
            nhi = ((1 if (he_z or gt033) else 0) * 0b111 << 2) | (he_z << 1) | gt033
        else:
            nlo = (bpc + 1) & 0xF
            nhi = ((bpc >> 4) + bpc_hi_ci) & 0x1F
        nbpc = ((nhi & 0x1F) << 4) | (nlo & 0xF)

        n_gt042 = 0 if bpla[6] else 1
        n_sr4 = [0, 0, 0, 0]
        n_sr4[0] = 1 if (bpla_z.get(0, 0) and not he_long_z) else 0
        n_sr4[1] = 0 if (he_z or gt044) else sr4[0]
        n_sr4[2] = 0 if he_z else sr4[1]
        n_sr4[3] = 0 if he_z else sr4[2]
        n_hi_ci = 0 if he_z else sr4[3]
        n_gt072 = 1 if ((bpla[41] or bpla[42]) and not bpla[43]) else 0

        # PCEN latches sample after the bpc update (settled by the slot counts)
        n_gt059 = srf(gt059, nbpc == 0x100, nbpc == 0x1F0)
        n_gt063 = srf(gt063, nbpc == 0x1DF, nbpc == 0x108)
        n_gt064 = srf(gt064, nbpc == 0x0FF, nbpc == 0x1EC)

        bpla_z = dict(bpla)
        bpc, gt042, sr4, bpc_hi_ci, gt072 = nbpc, n_gt042, n_sr4, n_hi_ci, n_gt072
        gt059, gt063, gt064 = n_gt059, n_gt063, n_gt064
        he_z, he_long_z = he, he_long

    return out


def cycle(tr, warm=2000):
    """Find one period of the bpc trajectory after warm-up."""
    first = None
    for i in range(warm, len(tr)):
        if tr[i][0] == 0x000:
            if first is None:
                first = i
            else:
                return first, i
    return None, None


def gaps(xs, line=LINE):
    return [(xs[(i + 1) % len(xs)] - xs[i]) % line for i in range(len(xs))]


def cyclic_match(a, b):
    if len(a) != len(b):
        return []
    n = len(a)
    return [r for r in range(n) if [a[(r + k) % n] for k in range(n)] == b]


print(f"{'hfire':>6} {'mode':8} {'line':>6} {'slots':>6}  match")
best = {}
for hfire in sorted({0x1DF, 0x1E0, 0x1E1, 0x1E2, 0x1E3, 0x1DE, 0x1DD, 0x1F5,
                     0x1F6, 0x1F7, 0x12F, 0x130}):
    for mode in ("dispOff", "sprOff", "sprOn"):
        tr = run(mode, hfire)
        s, e = cycle(tr)
        if s is None:
            continue
        period = e - s
        slots = [(i - s) * 4 for i in range(s, e) if tr[i][1]]
        m = cyclic_match(gaps(slots, period * 4), gaps(MEASURED[mode]))
        tag = f"rot {m}" if m else ""
        print(f"{hfire:#06x} {mode:8} {period*4:>6} {len(slots):>6}  {tag}")
        if m:
            best[(hfire, mode)] = (period * 4, slots, m)
print()
for k, v in best.items():
    print("MATCH", k, "line", v[0], "rotation", v[2])
