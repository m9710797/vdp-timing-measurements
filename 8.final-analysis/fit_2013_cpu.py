#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Fit a two-machine CPU/VDP model to 2013 *nocmd*cpu[rw]* traces.

Z80 (NMS 8250, exact ×6)
    40 I/O posts 72 VDP cycles apart, then 252 to the next burst (period 3060).
    Phase φ0 = VDP RAS time of I/O 0 of a burst. The 72 never appears in the VDP.

VDP (CPU path, no command engine)
    One pending-CPU buffer. On a post at T, if empty, take the first CPU slot S
    whose engine-distance from T is ≥ 16 (D16). Engine-distance is wall-clock
    minus 2 per stretch completion in (T, S] (dispOff 1334/1344, else 1332/1342).
    If already pending, overwrite data and keep the scheduled slot.
    After a CPU RAS at S, ignore posts in [S, S+2) (not a new request; 2-cycle
    holdoff through typical CAS).
    CPU-available slots = command slots except those 6 cycles after the previous
    command slot (packed pair; commands can use the second, these CPU traces never
    do).

Times in the files are CAS; slots are RAS (CAS−1). Stretch CAS is RAS+2, but
this nocmd CPU set does not land on stretched slots.

Usage:
    python3 fit_2013_cpu.py
"""
from __future__ import annotations

import glob
import os

LINE = 1368
N_IO = 40
IO_VDP = 72
LOOP_VDP = 252
PERIOD = (N_IO - 1) * IO_VDP + LOOP_VDP  # 3060
NEED = 16
BUSY = 2  # ignore posts in [RAS, RAS+BUSY)

DIR = os.path.dirname(os.path.abspath(__file__))

SLOTS = {
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

STRETCH = {
    "dispOff": (1334, 1344),
    "sprOff": (1332, 1342),
    "sprOn": (1332, 1342),
}


def mode_of(name: str) -> str:
    if "screenoff" in name:
        return "dispOff"
    if "nosprites" in name:
        return "sprOff"
    if "sprite" in name:
        return "sprOn"
    raise ValueError(name)


def cpu_slots(command_slots: list[int]) -> list[int]:
    """Drop slots packed 6 cycles after the previous command slot."""
    out: list[int] = []
    prev: int | None = None
    for s in command_slots:
        if prev is None or s - prev != 6:
            out.append(s)
        prev = s
    return out


CPU_SLOTS = {m: cpu_slots(s) for m, s in SLOTS.items()}


def parse_cpu(path: str) -> list[int]:
    items: list[int] = []
    with open(path) as f:
        for ln in f:
            if ":" not in ln:
                continue
            row, rest = ln.rstrip("\n").split(":", 1)
            t = int(row)
            col = 0
            while rest:
                cell = rest[2:13] if len(rest) >= 13 else rest[2:]
                if cell.strip() and cell[0:3] in ("W.c", "R.c"):
                    items.append(LINE * col + t - 1)  # RAS
                rest = rest[13:]
                col += 1
    items.sort()
    return items


def n_stretch(t: int, s: int, completions: tuple[int, int]) -> int:
    if s <= t:
        return 0
    n = 0
    for c0 in completions:
        kmin = (t - c0) // LINE + 1
        kmax = (s - c0) // LINE
        if kmax >= kmin:
            n += kmax - kmin + 1
    return n


def make_first(slots: list[int], completions: tuple[int, int], need: int):
    ext = slots + [s + LINE for s in slots] + [s + 2 * LINE for s in slots]
    wait = [0] * LINE
    for t in range(LINE):
        for s in ext:
            if s < t:
                continue
            if s - t - 2 * n_stretch(t, s, completions) >= need:
                wait[t] = s - t
                break
        else:
            raise RuntimeError(f"no slot from {t}")
    return lambda T: T + wait[T % LINE]


def z80_request(phi0: int, i: int) -> int:
    burst, k = divmod(i, N_IO)
    if k < 0:
        burst -= 1
        k += N_IO
    return phi0 + burst * PERIOD + k * IO_VDP


def vdp_services(
    phi0: int, first, tmin: int, tmax: int, need: int = NEED, busy: int = BUSY,
) -> tuple[list[int], int]:
    i0 = 0
    while z80_request(phi0, i0) > tmin - 2 * PERIOD:
        i0 -= 1
    i1 = i0
    while z80_request(phi0, i1) <= tmax + need + 400:
        i1 += 1
    occupied = False
    sched: int | None = None
    last_s: int | None = None
    overwrites = 0
    pred: list[int] = []
    i = i0
    while i < i1 or occupied:
        t_req = z80_request(phi0, i) if i < i1 else None
        if (
            not occupied
            and last_s is not None
            and t_req is not None
            and t_req < last_s + busy
        ):
            overwrites += 1
            i += 1
            continue
        if occupied and sched is not None and (t_req is None or sched <= t_req):
            if tmin <= sched <= tmax:
                pred.append(sched)
            last_s = sched
            occupied = False
            sched = None
            continue
        if t_req is None:
            break
        if not occupied:
            occupied = True
            sched = first(t_req)
        else:
            overwrites += 1
        i += 1
    return pred, overwrites


def score(pred: list[int], obs: list[int]) -> tuple[int, int, int]:
    i = j = hit = 0
    while i < len(pred) and j < len(obs):
        if pred[i] == obs[j]:
            hit += 1
            i += 1
            j += 1
        elif pred[i] < obs[j]:
            i += 1
        else:
            j += 1
    return hit, len(pred) - hit, len(obs) - hit


def candidate_phis(obs0: int, first, need: int) -> list[int]:
    phis: list[int] = []
    for dt in range(need, need + 90):
        t = obs0 - dt
        if first(t) != obs0:
            continue
        for k in range(N_IO):
            phis.append(t - k * IO_VDP)
    return phis


def fit_one(path: str, need: int = NEED, busy: int = BUSY) -> dict:
    name = os.path.basename(path)
    mode = mode_of(name)
    obs = parse_cpu(path)
    first = make_first(CPU_SLOTS[mode], STRETCH[mode], need)
    tmin, tmax = obs[0], obs[-1]
    best = None
    seen: set[int] = set()
    for phi0 in candidate_phis(obs[0], first, need):
        if phi0 in seen:
            continue
        seen.add(phi0)
        pred, ow = vdp_services(phi0, first, tmin, tmax, need, busy)
        hit, extra, miss = score(pred, obs)
        rec = (hit, -extra - miss, -abs(len(pred) - len(obs)), -ow, phi0, pred, ow, extra, miss)
        if best is None or rec[:4] > best[:4]:
            best = rec
    assert best is not None
    hit, _, _, _, phi0, pred, ow, extra, miss = best
    return {
        "name": name, "mode": mode, "n": len(obs), "hit": hit,
        "extra": extra, "miss": miss, "ow": ow, "phi0": phi0,
        "pred": pred, "obs": obs, "need": need, "busy": busy,
        "phi_line": phi0 % LINE,
    }


def main() -> None:
    files = sorted(
        f for f in glob.glob(os.path.join(DIR, "*nocmd*cpu*.txt"))
        if "nocpu" not in os.path.basename(f)
    )
    print(
        f"{'file':42} {'mode':8} hit     extra miss ow   "
        f"{'phi0':>7} phi0%1368"
    )
    n_ok = 0
    for f in files:
        r = fit_one(f)
        ok = r["hit"] == r["n"] and r["extra"] == 0
        n_ok += ok
        print(
            f"{r['name']:42} {r['mode']:8} {r['hit']:3}/{r['n']:<3} "
            f"{r['extra']:5} {r['miss']:4} {r['ow']:3} {r['phi0']:7} "
            f"{r['phi_line']:9} {'OK' if ok else ''}"
        )
    print(f"\n{n_ok}/{len(files)} perfect  (D{NEED}, stretch, drop+6, busy={BUSY})")


if __name__ == "__main__":
    main()
