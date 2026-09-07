#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Two-machine model for scr5-*-stop-{rd,wr}Cpu-*.txt (no command engine).

Z80
    40 × IN A,(n) / OUT (n),A, then DEC HL / LD A,H / OR L / JR NZ.
    Each I/O instruction is 12 T-states (MSX M1 wait included) = 72 VDP cycles.
    The VDP request is posted in the I/O cycle, 9 T-states after instruction
    start (openMSX CC_IN_A_N_2 / CC_OUT_N_A_2), so consecutive posts in a burst
    are 72 VDP cycles apart.
    After the 40th post, 3 T remain of that instruction + 30 T loop + 9 T to the
    next post = 42 T = 252 VDP. Burst-to-burst period = 39×72 + 252 = 3060.

    The value 72 never appears in the VDP.

VDP (CPU path only, no command engine)
    One pending-request buffer.
    On a request at time T, if the buffer is empty, latch the request and take
    the first CPU/command slot S with S >= T+16.
    If the buffer is already full, overwrite the latched data and keep the
    already-scheduled slot (too-fast CPU; the old request is lost).
    At time S the access runs and the buffer becomes empty.

Times are RAS (openMSX slot tables). The 5.slots files are CAS: subtract 1, or
subtract 2 on the stretched CAS rows.

Usage:
    python3 fit_stop_cpu.py
    python3 fit_stop_cpu.py --diag scr5-dispOff-stop-rdCpu-1.txt
"""
from __future__ import annotations

import argparse
import glob
import os
from collections import Counter

LINE = 1368
N_IO = 40
IO_VDP = 72  # Z80 only: 12 T-states × 6
LOOP_VDP = 252  # Z80 only: 42 T-states × 6
PERIOD = (N_IO - 1) * IO_VDP + LOOP_VDP  # 3060
NEED = 16  # VDP lookahead

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
CAS_STRETCH = {"dispOff": {1326, 1336}, "sprOff": {1324, 1334}, "sprOn": set()}


def cas_to_ras(mode: str, t: int) -> int:
    row = t % LINE
    return t - 1 - (1 if row in CAS_STRETCH[mode] else 0)


def parse(path: str) -> list[tuple[int, str, int]]:
    items: list[tuple[int, str, int]] = []
    with open(path) as f:
        for ln in f:
            if ":" not in ln:
                continue
            row, rest = ln.rstrip("\n").split(":", 1)
            t = int(row)
            col = 0
            while rest:
                cell = rest[2:13]
                if cell.strip():
                    items.append((LINE * col + t, cell[0:3], int(cell[6:11], 16)))
                rest = rest[13:]
                col += 1
    items.sort()
    return items


def prev_slot(table: list[int], s: int) -> int:
    row = s % LINE
    line = s // LINE
    for i, v in enumerate(table):
        if v == row:
            return (line - 1) * LINE + table[-1] if i == 0 else line * LINE + table[i - 1]
    raise RuntimeError(f"not a slot: {s}")


def wait_lut(table: list[int]) -> list[int]:
    """wait[t] = (earliest slot S >= t) - t, for t in 0..LINE-1."""
    ext = table + [s + LINE for s in table]
    w = [0] * LINE
    j = 0
    for t in range(LINE):
        while ext[j] < t:
            j += 1
        w[t] = ext[j] - t
    return w


def first_slot_ge(wait: list[int], target: int) -> int:
    return target + wait[target % LINE]


def z80_request(phi0: int, i: int) -> int:
    """Absolute VDP time of I/O-post number i (i may be negative)."""
    burst, k = divmod(i, N_IO)
    if k < 0:
        burst -= 1
        k += N_IO
    return phi0 + burst * PERIOD + k * IO_VDP


def vdp_services(
    phi0: int, wait: list[int], tmin: int, tmax: int, pad: int = 2 * PERIOD
) -> tuple[list[int], int]:
    """Run the VDP machine on the Z80 request train. Return (service times, overwrites)."""
    i0 = 0
    while z80_request(phi0, i0) > tmin - pad:
        i0 -= 1
    i1 = i0
    while z80_request(phi0, i1) <= tmax + NEED:
        i1 += 1

    occupied = False
    sched: int | None = None
    overwrites = 0
    pred: list[int] = []
    i = i0
    while i < i1 or occupied:
        t_req = z80_request(phi0, i) if i < i1 else None
        if occupied and sched is not None and (t_req is None or sched <= t_req):
            if tmin <= sched <= tmax:
                pred.append(sched)
            occupied = False
            sched = None
            continue
        if t_req is None:
            break
        if not occupied:
            occupied = True
            sched = first_slot_ge(wait, t_req + NEED)
        else:
            overwrites += 1
        i += 1
    return pred, overwrites


def score(pred: list[int], obs: list[int]) -> tuple[int, int, int]:
    """Return (hits, predicted-only, observed-only)."""
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


def candidate_phis(obs0: int, wait: list[int]) -> list[int]:
    """φ0 values that can arm the first observed service.

    Any φ0 that predicts obs[0] must arm it, so these are all candidates for a
    perfect match of the whole capture.
    """
    # T in (prev-16, S-16] is implied by first_slot(T+16)==S; just walk that range.
    # Window width is at most one slot-gap + 16.
    phis = []
    for dt in range(NEED, NEED + LINE):
        t = obs0 - dt
        if first_slot_ge(wait, t + NEED) != obs0:
            if dt > NEED + 80:
                break
            continue
        for k in range(N_IO):
            phis.append(t - k * IO_VDP)
    return phis


def load_cpu(path: str) -> tuple[str, list[int], list[int]]:
    name = os.path.basename(path)
    mode = name.split("-")[1]
    cpu = []
    addrs = []
    for t, c, a in parse(path):
        if c in ("R.r", "W.w"):
            cpu.append(cas_to_ras(mode, t))
            addrs.append(a)
    return mode, cpu, addrs


def fit_file(path: str) -> dict:
    mode, obs, _ = load_cpu(path)
    table = SLOTS[mode]
    wait = wait_lut(table)
    tmin, tmax = obs[0], obs[-1]
    best = None
    seen: set[int] = set()
    for phi0 in candidate_phis(obs[0], wait):
        if phi0 in seen:
            continue
        seen.add(phi0)
        pred, ow = vdp_services(phi0, wait, tmin, tmax)
        hit, extra, miss = score(pred, obs)
        rec = (hit, -extra - miss, -ow, phi0, pred, ow, extra, miss)
        if best is None or rec[:3] > best[:3]:
            best = rec
    assert best is not None
    hit, _, _, phi0, pred, ow, extra, miss = best
    return {
        "name": os.path.basename(path),
        "mode": mode,
        "n": len(obs),
        "hit": hit,
        "extra": extra,
        "miss": miss,
        "ow": ow,
        "phi0": phi0,
        "pred": pred,
        "obs": obs,
        "table": table,
        "wait": wait,
    }


def diagnose_pair(s1: int, s2: int, wait: list[int]) -> None:
    """What the VDP machine can do for two services, vs what the Z80 can post."""
    t1s = [
        t for t in range(s1 - NEED - 80, s1 - NEED + 1)
        if first_slot_ge(wait, t + NEED) == s1
    ]
    t2s = [
        t for t in range(s2 - NEED - 80, s2 - NEED + 1)
        if first_slot_ge(wait, t + NEED) == s2
    ]
    print(f"  VDP arming windows: T1 in [{t1s[0]},{t1s[-1]}] ({len(t1s)} values), "
          f"T2 in [{t2s[0]},{t2s[-1]}] ({len(t2s)} values)")
    print(f"  service gap S2-S1 = {s2 - s1}")
    ok72 = [(t1, t1 + IO_VDP) for t1 in t1s if t1 + IO_VDP in t2s]
    ok144 = [(t1, t1 + 2 * IO_VDP) for t1 in t1s if t1 + 2 * IO_VDP in t2s]
    ok252 = [(t1, t1 + LOOP_VDP) for t1 in t1s if t1 + LOOP_VDP in t2s]
    print(f"  Z80 can arm both if posts are 72 apart:  {len(ok72)} (T1,T2) pairs")
    print(f"  Z80 can arm both if posts are 144 apart: {len(ok144)} (lost I/O in between)")
    print(f"  Z80 can arm both if posts are 252 apart: {len(ok252)} (loop overhead)")
    if t1s and t2s:
        print(f"  max T2-T1 over the two windows = {t2s[-1] - t1s[0]} "
              f"(need {IO_VDP} for consecutive I/Os)")
        print(f"  if T1={t1s[-1]} (latest that still gets S1), next post T1+72="
              f"{t1s[-1] + IO_VDP} would take slot "
              f"{first_slot_ge(wait, t1s[-1] + IO_VDP + NEED)}")


def cmd_fit() -> None:
    files = sorted(
        glob.glob(os.path.join(DIR, "scr5-*-stop-rdCpu-*.txt"))
        + glob.glob(os.path.join(DIR, "scr5-*-stop-wrCpu-*.txt"))
    )
    tot = Counter()
    by_mode: dict[str, Counter] = {}
    print(f"{'file':36} {'hit':>7} extra miss  ow     phi0", flush=True)
    for f in files:
        r = fit_file(f)
        by_mode.setdefault(r["mode"], Counter())
        for c in (tot, by_mode[r["mode"]]):
            c["n"] += r["n"]
            c["hit"] += r["hit"]
            c["extra"] += r["extra"]
            c["miss"] += r["miss"]
        mark = "OK" if r["hit"] == r["n"] and r["extra"] == 0 else ""
        print(
            f"{r['name']:36} {r['hit']:3}/{r['n']:<3} {r['extra']:5} {r['miss']:4} "
            f"{r['ow']:3} {r['phi0']:8} {mark}",
            flush=True,
        )
    print()
    for mode, c in sorted(by_mode.items()):
        print(f"  {mode:8} {c['hit']}/{c['n']} ({100 * c['hit'] / c['n']:.1f}%)  "
              f"extra {c['extra']} miss {c['miss']}")
    print(f"  {'ALL':8} {tot['hit']}/{tot['n']} ({100 * tot['hit'] / tot['n']:.1f}%)")


def cmd_diag(name: str) -> None:
    path = name if os.path.isabs(name) else os.path.join(DIR, name)
    r = fit_file(path)
    obs, pred, wait = r["obs"], r["pred"], r["wait"]
    print(f"{r['name']}  best {r['hit']}/{r['n']}  extra {r['extra']} miss {r['miss']}  "
          f"overwrites {r['ow']}  phi0={r['phi0']}")
    print()
    for a, b in zip(obs, obs[1:]):
        if a % LINE == 1268 and b - a <= 40:
            print(f"Observed dummy-hole pair RAS {a} (row {a % LINE}) -> {b} (row {b % LINE}), "
                  f"gap {b - a}")
            diagnose_pair(a, b, wait)
            print()
            break
    print("First 25 observed vs nearest prediction (best φ0):")
    print(f"  {'i':>3} {'obs':>8} {'row':>5} {'pred':>8} {'d':>5} {'req':>8}")
    j = 0
    i_req = 0
    while z80_request(r["phi0"], i_req + 1) <= obs[0]:
        i_req += 1
    for i, s in enumerate(obs[:25]):
        while j < len(pred) and pred[j] < s - 40:
            j += 1
        p = pred[j] if j < len(pred) else None
        d = (p - s) if p is not None else None
        req = None
        k = i_req
        while z80_request(r["phi0"], k) <= s - NEED:
            req = z80_request(r["phi0"], k)
            k += 1
        print(f"  {i:3} {s:8} {s % LINE:5} {str(p) if p is not None else '-':>8} "
              f"{str(d) if d is not None else '-':>5} {str(req) if req is not None else '-':>8}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--diag", metavar="FILE")
    args = ap.parse_args()
    if args.diag:
        cmd_diag(args.diag)
    else:
        cmd_fit()


if __name__ == "__main__":
    main()
