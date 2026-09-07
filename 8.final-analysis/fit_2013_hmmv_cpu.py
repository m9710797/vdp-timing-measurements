#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Fit simultaneous HMMV + CPU on 2013 *HMMV*cpu[rw]* traces.

CPU is independent of the command (CPU wins every contested slot), so the
nocmd CPU model is reused as-is. HMMV then takes the first remaining command
slot with engine-distance ≥ Δ.

HMMV (FINDINGS4): mid-line P=46, newline 104 (L=58). Sprites on: +1 on every Δ.
No command-Y newline is visible as an address jump: these fills are contiguous
in VRAM (full line width), so addr is always +1; newline is when addr%pitch==pitch-1
(pitch 128 in screen 5, 256 in screen 8).

The first observed W.e is seeded (mid-command). Later W.e are predicted.

Usage:
    python3 fit_2013_hmmv_cpu.py
"""
from __future__ import annotations

import glob
import os

import fit_2013_cpu as cpu

DIR = os.path.dirname(os.path.abspath(__file__))
LINE = cpu.LINE
HMMV_P = 46
HMMV_NL = 104


def parse_both(path: str) -> tuple[list[int], list[tuple[int, int]]]:
    cpu_acc: list[int] = []
    eng: list[tuple[int, int]] = []
    with open(path) as f:
        for ln in f:
            if ":" not in ln:
                continue
            row, rest = ln.rstrip("\n").split(":", 1)
            t = int(row.strip())
            col = 0
            while rest:
                cell = rest[2:13] if len(rest) >= 13 else rest[2:]
                if cell.strip():
                    tag = cell[0:3]
                    ras = LINE * col + t - 1
                    addr = None
                    i = cell.find("0x")
                    if i >= 0:
                        try:
                            addr = int(cell[i : i + 7], 16)
                        except ValueError:
                            pass
                    if tag in ("W.c", "R.c"):
                        cpu_acc.append(ras)
                    elif tag == "W.e" and addr is not None:
                        eng.append((ras, addr))
                rest = rest[13:]
                col += 1
    cpu_acc.sort()
    eng.sort()
    return cpu_acc, eng


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


def next_cmd_slot(
    last: int, delta: int, slots: list[int], completions: tuple[int, int], occupied: set[int],
) -> int:
    ext = slots + [s + LINE for s in slots] + [s + 2 * LINE for s in slots] + [
        s + 3 * LINE for s in slots
    ]
    base = last - (last % LINE)
    for srel in ext:
        s = base + srel
        if s <= last:
            continue
        if s - last - 2 * n_stretch(last, s, completions) < delta:
            continue
        if s in occupied:
            continue
        return s
    raise RuntimeError("no command slot")


def predict_hmmv(
    first_ras: int, first_addr: int, n: int, mode: str, name: str, occupied: set[int],
) -> list[int]:
    spr = 1 if mode == "sprOn" else 0
    p, nl = HMMV_P + spr, HMMV_NL + spr
    pitch = 256 if "screen8" in name else 128
    slots = cpu.SLOTS[mode]
    completions = cpu.STRETCH[mode]
    pred = [first_ras]
    last = first_ras
    addr = first_addr
    for _ in range(1, n):
        delta = nl if addr % pitch == pitch - 1 else p
        last = next_cmd_slot(last, delta, slots, completions, occupied)
        pred.append(last)
        addr = (addr + 1) & 0x1FFFF
    return pred


def main() -> None:
    files = sorted(
        f
        for f in glob.glob(os.path.join(DIR, "*HMMV*cpu*.txt"))
        if "nocpu" not in os.path.basename(f).lower()
    )
    print(
        f"{'file':42} {'mode':8} CPU          HMMV         "
        f"{'phi0':>7} phi0%1368"
    )
    n_ok = 0
    for path in files:
        name = os.path.basename(path)
        mode = cpu.mode_of(name)
        r = cpu.fit_one(path)
        cpu_ok = r["hit"] == r["n"] and r["extra"] == 0
        _, eng = parse_both(path)
        pred = predict_hmmv(
            eng[0][0], eng[0][1], len(eng), mode, name, set(r["pred"]),
        )
        obs = [e[0] for e in eng]
        eh, ex, em = cpu.score(pred, obs)
        e_ok = eh == len(eng) and ex == 0
        ok = cpu_ok and e_ok
        n_ok += ok
        print(
            f"{name:42} {mode:8} {r['hit']:3}/{r['n']:<3}{'OK' if cpu_ok else '  '}  "
            f"{eh:3}/{len(eng):<3}{'OK' if e_ok else '  '}  "
            f"{r['phi0']:7} {r['phi0'] % LINE:9} {'OK' if ok else ''}"
        )
    print(f"\n{n_ok}/{len(files)} perfect  (CPU model + HMMV P={HMMV_P} NL={HMMV_NL}, skip CPU slots)")


if __name__ == "__main__":
    main()
