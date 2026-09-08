# VDP command and CPU slot timing

This document was written with assistance from an AI coding agent.

The model: command engine, CPU VRAM accesses, and the arbitration between
them, for the V9938 in bitmap modes.

**This is FINDINGS8. It supersedes [`FINDINGS7.md`](FINDINGS7.md), and it is a
pruned document** — FINDINGS7 has the full history, including the per-mode
constants and the investigations that led nowhere. What is kept here is the
model as it now stands and the experiments that established it.

The CPU arbitration parameters that FINDINGS7 fitted per display mode are now
derived from the V9958 die-shot reverse engineering written up in
[`IKA9958.md`](IKA9958.md), which also reproduces the slot lattice exactly.
That closed most of FINDINGS7's residue and took the corpus from 242 to
**263 of 266**. Earlier notes: [`FINDINGS4.md`](FINDINGS4.md) (waits and slot
tables, frozen), [`FINDINGS5.md`](FINDINGS5.md), [`FINDINGS6.md`](FINDINGS6.md),
[`ARBITER_ITER.md`](ARBITER_ITER.md),
[`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md).

The Viterbi-style reconstruction, its validity limits, and the generated
`.cpureq` workflow are documented separately in
[`TRELLIS_CPU_REQUESTS.md`](TRELLIS_CPU_REQUESTS.md).

| | 2013 | 2026 |
|--|------|------|
| Machine | Philips **NMS 8250** | Philips **NMS 8280** |
| Data | `8.final-analysis/` | `part2/5.slots/` (txt) + `part2/1.vcd/` |
| Clocks | One 21.47727 MHz VDP crystal, CPU = VDP `/6` | Separate crystals. VDP ~21.33 MHz PAL. **5.96113 ± 0.00004** VDP cycles per Z80 T-state (same from 12 T and 37 T programs, 1 part in 10⁵) |
| CPU request times | Exact **72 / 252 / 3060** VDP cycles (40 I/O + loop), unknown phase `φ0` | `/CSR` / `/CSW` on the refresh-interpolated VDP clock. Loops used (measured): **71.5**, **131.2**, **220.6** + 399.4 |
| Pin → arbiter delay | inside `φ0` | `phi` ≈ **10.75** cycles, one real number for all three modes (§9.3) |

Bitmap screen 5 (screen 8 shares the slot maps). Default line period **1368**
(R#9 S1,S0 = 0,0). Slot times and waits are **RAS** (openMSX numbering);
published `.txt` rows are **CAS** (§1).

## Scores

| set | files | steps | wrong |
|--|--|--|--|
| 2026 command-only (`--nocpu`) | 514 / 514 | **78330 / 78330** | 0 |
| 2026 mixed engine (CPU RAS + dummy `R..` occupied) | 138 / 138 | **20408 / 20408** | 0 |
| 2013 command-only | — | 3965 / 3965 | 0 |
| 2013 CPU | 17 / 17 | **1141 / 1141** | 0 |
| 2013 mixed (HMMV + CPU) | 7 / 7 | all | 0 |

There is no capture on either machine where the **command engine** mispredicts
a step. The whole residue is on the CPU side.

CPU, reconstructing every recorded `/CSx` edge exactly with one real pin delay
and no tie allowed (`--trellis`):

| mode | FINDINGS7 | FINDINGS8 (§8) |
|--|--|--|
| dispOff | 100 / 100 | 100 / 100 |
| sprOff | 76 / 83 | **83 / 83** |
| sprOn | 66 / 83 | 80 / 83 |
| all | 242 / 266 | **263 / 266** |

The three that remain are examined in §12. Each is one or two grants wrong out
of ~105, and all three are at the boundary of the first display line.

## Tools

`fit_2026.cc` implements §3–§6 (padding +4 in all modes, sprites-on addend +1),
the CPU model of §8, D6 dummy occupancy, and the `/CSx` decoder of §9.1. Its
engine model uses **no CPU predicate** (§10.3). Do not change `fit_2013.cc`
behaviour.

The model of record:

```
./fit_2026 --trellis --sel= $(ika9958/subslot_rows.py)
```

- `fit_2026.cc` — CPU fit over all `rdCpu` / `wrCpu` captures (default,
  integer `δ` per mode; kept for comparison, superseded by `--trellis`)
- `--nocpu` / `--scratch` / `--mismatch` — command engine; occupancy + engine
  skipping observed CPU RAS; mixed packed leftovers
- `--trellis [--sel=SUB]` — reconstruct the exact request cycles from a single
  *real* pin delay plus a bounded per-edge tolerance (§9.3). `--needoff`,
  `--needrow=r,..:N`, `--thresh`, `--threshrow=ROW:N`, `--qdepth=N` vary the
  arbiter parameters; `--reqfiles` writes the `.cpureq` siblings
- `--faildiag` / `--faildump [--only=SUB]` — for a capture with no exact
  reconstruction, scan the pin delay finely and attribute every mismatch to
  the request responsible and the slot that shadowed it
- `--fromreq` / `--checkreq [--tol=X]` — the two guards on the `.cpureq`
  files (§9.6)
- `--origin` — assert that the `.txt` line numbering agrees with the `.vcd`.
  Run on every new batch and after hand-editing a `.txt` (§9.5)
- `--widthcheck` — the straight-line residual of the rise train, i.e. the
  per-edge error measured without the arbiter (§9.4)
- `--pacescan` / `--rawdump --only=SUB` — find mismeasured `/CSx` pulses, and
  separate a bad capture from a warped timebase from a wrong edge (§9.7)
- `--padsil` — opt-in, replaces the fitted padding with the derived clock
  stall (§3, negative result)
- `ika9958/subslot_rows.py` — the four tick classes of §8 as `fit_2026` flags,
  computed from the Memory PLA and the clock divider
- `ika9958/rccsim.py` — the RCC clock divider, to measure what the line-end
  stall costs
- `part2/1.vcd/rasmap.py` — the `/RAS` comb of a capture in VDP cycles, with
  the line length fitted rather than assumed (§3.1)
- `part2/1.vcd/lines.py` — per-line calibration, for the border/display
  boundary (§15)
- `retag.py` — reconstruct access types from the VRAM addresses, for traces
  where a command rectangle leaves its designated region
- `exp4.py` — the sprites-off engine step, standalone

---

# I. Command engine (no CPU)

Data: `part2/5.slots/scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` (including
`*nx??-?d.txt`). **514** traces, **78330** engine-to-engine transitions
(display off 235 / 39806, sprites off 149 / 23729, sprites on 130 / 14795 —
all 100%).

After an access at RAS time `t`, wait `Δ` **memory cycles**, then take the
first command slot whose **engine-distance** from `t` is ≥ `Δ`. Engine-distance
is wall-clock VDP cycles minus the padding in `(t, next]` (§3). With sprites
on, every `Δ` is 1 larger (§4). If `t` is a packed +6 slot, `Δ` is 1 larger
again (§6).

## 1. RAS vs CAS

`/RAS` falling is when the row address is driven (access start). `/CAS`
normally falls **1** VDP cycle later.

| | RAS (openMSX) | CAS (`.txt` rows) |
|--|----------------|-------------------|
| Typical slot | `r` | `r+1` |
| Stretched slots, display off | 1324, 1334 | **1326, 1336** |
| Stretched slots, sprites off | 1322, 1332 | **1324, 1334** |
| Sprites on (no stretch in the command table) | `r` | `r+1` |

On those two stretched slots `/CAS` follows `/RAS` by **2** cycles (~100 ns vs
~50 ns). Same slots, not a 1-cycle table error. They are two of the four cycles
the clock divider inserts at the line end (§3, §8.4).

To map a `.txt` time to RAS: subtract 1, or subtract 2 when the CAS row is
1326/1336 (dispOff) or 1324/1334 (sprOff).

## 2. Slot tables

Command slots are the openMSX bitmap tables in `VDPAccessSlots.cc` (RAS):

| Mode | Command slots / line |
|------|----------------------|
| dispOff (display off / vertical border) | 154 |
| sprOff (bitmap, sprites off) | 88 |
| sprOn (bitmap, sprites on) | 31 |

Sprite count and size do not add command slots. Screen 5 and 8 share these
maps. The character and text tables have never been measured.

All three tables are now reproduced from silicon — 273 of 273 positions, no
fitted parameter (§8.4).

Display-off RAS grid (including refresh and dummy reads) is 166 pulses per
line: **163×8 + 2×10 + 1×44 = 1368**. The two 10-cycle gaps are `1324→1334`
and `1334→1344`. Sprites-off has the same gaps two cycles earlier
(`1322→1332→1342`). Sprites-on has no command slot in that region (last slots
1264, 1330).

The 44-cycle hole (dispOff `120→164`) is five empty memory cycles, not
padding. It counts in full: HMMV with `Δ=48` from 120 skips 164 (+44) and
takes 172 (+52).

### 2.1 Packed +6 (sprites-off only)

In sprites-off bitmap mode, part of the command table is not a uniform
8-cycle grid: during active display the VDP inserts extra command/CPU
opportunities as **pairs six cycles apart**.

**Definition:** a *packed +6* slot is a command-table RAS `S` whose
predecessor in that table is `S−6`. It is still a legal **command** slot.
*CPU-legal* means `command_slots` minus these packed +6 times.

**Why only sprites-off.** In the active display the VDP groups memory cycles
in 32-cycle cells: `182+32k` is a sprite fetch when sprites are on and spare
when they are off; `188+32k` is spare in both modes; `194+32k` is the display
burst (one `/RAS`, four `/CAS`). With sprites off the first becomes spare too,
which is the 6-cycle pair. Refresh takes the middle cycle once per 128, which
is why the list has **25** entries, not 32. It is a property of the **slot
grid**, not of the traffic: in sprites-off CPU captures the first of the pair
is idle ~32% of the time, and the CPU still does not take the second.

Sprites-off has **88** command slots per line, of which **25** are packed +6
and **63** are CPU-legal. None of the 25 is a stretched slot.

Packed RAS: **188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604, 636,
700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116, 1148, 1212** — each
paired with a CPU-legal first-of-pair 6 cycles earlier (182, 214, 246, 310, …
1206).

Eight groups of three pairs, repeating every **128** from 182 (`{0,6}`,
`{32,38}`, `{64,70}`), plus a truncated last group that only keeps
`(1206, 1212)`. Each group also has a **lone** CPU-legal slot at relative
**96** (278, 406, 534, 662, 790, 918, 1046, 1174) that is *not* packed. Left
border (`6…118`, `162`, `170`) and end-of-line (`1266…1366`) are 8-apart or
stretch, not packed.

**CPU never uses them (both measurement sets).**

| Set | CPU RAS on packed +6 | CPU RAS in sprites-off |
|-----|----------------------|------------------------|
| 2013 `*cpu{read,write}*` | **0 / 1141** | **0 / 204** |
| 2026 `*rdCpu*` / `*wrCpu*` (stop + command) | **0 / 12846** | **0 / 4346** |

Commands **do** use the +6 (2013 sprites-off HMMV: 24 / 85 engine writes;
2026 sprites-off: 1436 / 4608). There are **no** CPU-only slots anywhere: the
only client-exclusive slots in the whole table are these 25.

**The exclusion is derived from the gates.** The arbiter grants on a *rising
edge* of `VRAM_SLOT_CPU` (§8.4), so a slot tick immediately preceded by another
slot tick produces no edge and can never be granted to the CPU. Comparing what
the silicon suppresses against `cpu_slots_of`, which excludes any slot exactly 6
cycles after its predecessor: **25 of 88** in sprites off, 0 of 154 and 0 of 31
in the other two modes — the same 25, in the one mode where the question has any
content.

## 3. Padding (stretched memory cycles)

The delay counter counts **memory cycles**, not wall-clock VDP cycles. Extra
clocks that pad the line to 1368 exist on the wire and are **not** counted.
The 44-cycle hole **is** counted.

Padding sits in HBLANK. All three modes are measured on the wire (§3.1); the
total is **+4** in each, but its distribution differs:

| Mode | Unpadded spacing there | Padded cycles (S1,S0 = 0,0) | Total | Completions to subtract in `engine_dist` |
|------|------------------------|-----------------------------|-------|------------------------------------------|
| display off | 8 | two of 10, `1324→1334→1344` | **+4** | **2** at **1334**, **2** at **1344** |
| sprites off | 8 | two of 10, `1322→1332→1342` | **+4** | **2** at **1332**, **2** at **1342** |
| sprites on | `13, 6, 10` | three of `15, 7, 11`, `1315→1330→1337→1348` | **+4** | **2** at **1330**, **1** at **1337**, **1** at **1348** |

```
engine_dist(t, s):                    # s > t, times mod LINE
    (s - t) minus padding completions C with t < C ≤ s
```

A wait that spans padding needs that many more real cycles to complete. This
is a property of the line, not of whether the command uses those RAS pulses:
the sprites-on padded cycles are sprite fetches, not command slots. Display-off
example (HMMM):

```
104 W → 164 R     Δ = 60   does not cross padding → engine count 60 → slot taken
1292 W → 1360 R   Δ = 68   crosses both 10-cycle cycles → 1352 is only 56 engine cycles → skipped
```

The counter restarts at every access. The correction is well defined for an
interval, not as a periodic remapping of the line.

**The mechanism is the clock divider, and it is derived** — see §8.4. The
padding is not a property of the command engine at all: `gc024` holds the
`phiA` divider ring for one cycle in each of four consecutive `phiL` ticks at
the line end, so 1364 + 4 = 1368. That also explains the 1365-cycle line
(§13) and the stretched RAS→CAS of §1.

**Command traces fix only the sum** of the padding total and the sprites-on
addend (§4), because the padded RAS pulses are not command slots and the
sprites-on slot gaps are 32–64 with plateaus several cycles wide. Of the four
combinations, three are 100% on 78330 transitions; `/RAS` fixes the padding at
+4, so the addend is 1 or 2 with nothing to choose between them — use **1**,
which is what openMSX has always used. The same indifference does *not* hold on
the CPU side, where there is no addend to absorb a padding change.

### 3.1 How the padding is measured

Two independent rulers, neither of which assumes a line length.

**Line length from the refresh interval.** `part2/1.vcd/vcdlib.py` fits
`t = A + B·128k + (B·L)·n` — three linear unknowns, so `L` is fitted:

| R#9 S1,S0 | sprites off | sprites on |
|-----------|-------------|------------|
| 0,0 (`s0`) | **1368.00 ± 0.05** (3) | **1367.98 ± 0.06** (6) |
| 0,1 (`s16`) | **1364.99 ± 0.07** (4) | 1365.00 (3 usable) |
| 1,0 (`s32`) | **1365.03 ± 0.06** (4) | **1365.02 ± 0.12** (7) |
| 1,1 (`s48`) | — | 1365.01 ± 0.10 |

**The `/RAS` comb.** A 1365-cycle line is *not* an unpadded line — it still
carries +1 — so the padding cannot be read off the 1368-vs-1365 difference.
The unpadded spacing has to come from the rest of the line. Sprites-off is the
easy case, since everything else in that region runs at 8:

```
sprites off, 1368 : ... 1314 -8- 1322 -10- 1332 -10- 1342 -8- 1350 ...
sprites off, 1365 : ... 1314 -8- 1322  -9- 1331  -8- 1339 -8- 1347 ...
```

`10 + 10 = 20` against a floor of 16 is **+4**; the 1365 line's `9 + 8 = 17`
is **+1** (at 1331). Display-off is the same picture two cycles later.

Sprites-on has an uneven comb, so the unpadded spacing comes from its
**64-cycle period**, `6 10 6 10 6 13 13`, which holds through HBLANK except at
the padded triple:

```
1302 -13- 1315 [-15- 1330 -7- 1337 -11- 1348] -6- 1354 -10- 1364 -6- next line
        natural continuation of the 64-comb:
1302 -13- 1315 [-13- 1328 -6- 1334 -10- 1344] -6- 1350 -10- 1360 -6- 1366 = 1302 + 64
```

The unpadded line would close at **1366**, the real one at 1370: **+4**, as
`+2, +1, +1` on the memory cycles completing at 1330, 1337 and 1348.

Source: `rasmap.py scr5-sprOn-hmmv-noCpu-s0-1f.vcd 1050 1368`, and the
`scr5-{sprOff,sprOn}-hmmv-noCpu-s{0,16,32,48}` captures.

### 3.2 Set-adjust moves the padding, never its total

`scr5-{dispOff,sprOff,sprOn}-hmmv-noCpu-adjust{0..15}` (96 files). The line is
**1368** for every value in every mode, and nothing before RAS ~1290 moves.
What moves is the *position* of the padding block, at **4 cycles per R#18
unit** — one screen-5 pixel:

```
display off, R#18 =  0 : 1316 -8- 1324 -10- 1334 -10- 1344 -8- 1352 -8- 1360
             R#18 =  1 : 1316 -8- 1324  -9- 1333 -10- 1343  -9- 1352 -8- 1360
             R#18 =  2 : 1316 -8- 1324  -8- 1332 -10- 1342 -10- 1352 -8- 1360
             R#18 =  6 : 1332 -8- 1340  -8- 1348 -10- 1358 -10- 1368
```

R#18 = 0 is centre, 1..7 shift the picture left, 15..8 shift it right, and the
padding block follows: even steps slide the `+2, +2` pair by 8 cycles per two
units; odd steps sit half a memory cycle away, where the same 4 extra clocks
spread as `+1, +2, +1` over three memory cycles. **The total stays +4 in every
mode for all 16 values** — a second, independent confirmation of §3.1, and a
demonstration that the padding is a property of the horizontal timing chain
and moves with it. §8.4 says why: the stall is driven from the horizontal
counter, and R#18 shifts that counter.

For emulation: set-adjust needs no new slot tables as long as one accepts a
±8-cycle error on the last two or three HBLANK slots of the line — otherwise
it would take 16 tables per mode.

## 4. Sprites on: one addend on every step

With §3, the steps that still differ with sprites on all need the **same**
extra amount. One rule: **sprite rendering adds a constant to every
command-engine delay.** That forces LMMM source→dest to **32** and HMMM/LMMM
newline to **128**, matching the other two modes. It is not a slot-position
effect.

The constant is **1** or **2**; nothing separates them (§3), and `fit_2026.cc`
and openMSX use **1**. The CPU path's sprites-on behaviour is a separate
matter and is now explained by the sub-slot (§8).

## 5. Wait parameters

`Δ` is the engine wait (work + arbitration), in memory cycles, **before** the
sprites-on addend. Any integer in a plateau produces the same next slot on
these maps. `L` is extra wait on a rectangle line-break: newline `Δ` =
mid-line `Δ` + `L`.

### 5.1 Plateaus (three-mode intersection)

Sprites-on bands have the addend taken off before intersecting.

| Command | Pattern | Mid-line | Newline | `L` if mid and newline independent |
|---------|---------|----------|---------|-------------------------------------|
| **HMMV** | `W` | P ∈ [45, 48] | [103, 104] | [55, 59] |
| **LMMV** | `R.d ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [71, 72] | [129, 132] | [57, 61] |
| **YMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [33, 38] | [103, 104] | [65, 71] |
| **HMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [59, 60] | **128** | {68, 69} |
| **LMMM** | `R.s → R.d → W.d` | Prs = **32**; Prd ∈ [21, 24]; Pwd ∈ [59, 60] | **128** | {68, 69} |
| **LINE** | `R ↔ W` | Pw ∈ [21, 24]; Pr ∈ [81, 84] | [119, 120] | [35, 39] |

HMMM/LMMM newline is a **point** (128). LMMM `R.s → R.d` is **32** in all
modes (sprites-on via the addend).

### 5.2 Preferred representatives

Heuristics only pick a point inside §5.1 (similar steps share `L` / `Pw` /
`Pr`; then even, then highest power of 2). They do not improve the fit.

```
HMMV  : W 46 (+58)
LMMV  : R 24  W 72 (+58)
YMMM  : R 24  W 36 (+68)
HMMM  : R 24  W 60 (+68)
LMMM  : R 32  R 24  W 60 (+68)
LINE  : R 24  W 84 (+36)
```

**L at a glance:** fills **58**; copy family (YMMM / HMMM / LMMM) **68**;
LINE **36**.

## 6. Packed-start +1 (sprites-off)

If the previous engine RAS was a packed +6 slot, add 1 to `Δ` — as if that
squeezed memory cycle finished one cycle late. **Unconditional**: it applies
to every `Δ`, with no reference to CPU activity (§10.3). Only sprites-off has
packed slots.

| Extra cycles after a packed start | Mispredicted of 78330 |
|--|--|
| 0 | 33 |
| **1** | **0** |
| 2 | 0 |
| 3 | 1284 |

The band is **[1, 2]**; use **1**. 1790 `noCpu` transitions start from one of
the 25 packed slots. Adding 1 only *changes* the chosen next slot for the 33;
the rest still land on the same slot. Those 33 are all **RAS 188 → RAS 342**,
where slot 316 is at exactly +128 and is skipped.

### 6.1 The +1 is only observable at two `Δ` values

The +1 changes the chosen slot only when some slot sits at *exactly*
engine-distance `Δ` from the packed start. Over the 25 packed slots in 3
successive lines (75 starts), only two of the eleven `Δ` values in §5 ever hit
one exactly: **`Δ=32`** (LMMM `Prs`, 48 / 75) and **`Δ=128`** (HMMM/LMMM
newline, 66 / 75). Everywhere else it is silent, which is why a fit that never
reaches those two is indifferent to it.

2013 `8.final-analysis` has only HMMV (`Δ=46`, newline 104), both in the
"0 / 75" rows, so 2013 cannot see the +1 at all — it is **unobservable**
there, not merely untested.

## 7. Trace inventory notes

Command-only files that must be excluded (`wrong`; they contain CPU `R.r` /
`W.w`): `scr5-wrong-dispOff-hmmv-noCpu-nx2-6d.txt`,
`scr5-wrong-dispOff-ymmm-noCpu-nx12-1d.txt`,
`scr5-wrong-sprOff-lmmm-noCpu-nx2-3d.txt`.

**Misnamed, not usable as command-only:** `scr5-sprOn-hmmm-rdCpu-1.txt` and
`scr5-sprOn-hmmm-rdCpu222-3e.txt` are labelled `rdCpu` but hold only `R.s` /
`W.d` — no CPU VRAM.

**VCD aliases:** `scr5-dispOff-hmmv-rdCpu222-7e` / `8e` were first saved as
sprites-off, so `1.vcd` / `3.time` still call them
`scr5-sprOff-hmmv-rdCpu222-3e` / `4e`. Display was off; pair those VCDs with
the dispOff `.txt`.

Some LMMM `*nx*-*d.txt` traces use `W..` instead of `W.d` where source and
dest overlap. The cadence is still `R, R, W`; those files are kept.
`retag.py check` should report zero corrections on the current corpus.

---

# II. CPU accesses and arbitration

CPU VRAM uses a **subset** of the same command slots. The two machines differ
in how the request time `T` is observed, not in the VDP arbiter (as far as
2013 can tell).

## 8. The arbitration model

```
LINE = 1368

command_slots(mode) = bitmap table (RAS)
cpu_slots(mode)     = command_slots minus packed +6     # §2.1

engine_dist(t, s):                    # s > t
    (s - t) minus padding in (t, s]   # §3; same function as the command engine

first_cpu_slot(T):
    earliest S in cpu_slots (repeating every LINE)
    with S >= T and engine_dist(T, S) >= NEED(S)
```

`NEED` and `THRESH` are per-slot, not per display mode; §8.1 derives both from
three Memory PLA signals.

`T` is the time the **arbiter** sees the request, which is not when the Z80
executes the I/O: there is a constant pin-to-register delay `phi` ≈ 10.75
cycles from the **rising** edge of `/CSx` (§9.3).

**One pending buffer, drop-new.** A port access arriving while the register is
occupied is **lost**: the old request keeps its scheduled slot and the VRAM
pointer does not advance. The whole discard behaviour is one inequality,

```
take the request iff  T - S_prev >= THRESH(S_prev)
```

where `S_prev` is the slot granted to the request before it. Waiting for a
pending grant and the holdoff after it are not two mechanisms but the two
halves of that test: a request arriving before `S_prev` fails the same
inequality by a larger amount.

Losses are real and common: 2026 sprites-on with a 12 T `IN` loop shows **~8%
more `/CSx` pulses than VRAM accesses**. Display-off loses essentially none,
and the 37 T loop loses exactly zero (§9.2).

**The booked slot never moves.** On 2013 keep-slot and drop-new are
indistinguishable; on 2026 sprites-on, drop-new matches the extra `/CSx`
pulses, while an overwrite that *cancels* a granted slot scores far worse. A
two-deep queue (`--qdepth=2`) scores 208 against 263 — the arbiter cone has a
single `WAITING` bit (§8.4).

**Command path** — separate buffer, never drops:

```
Δ = §5 wait for that step
if sprites on:     Δ += 1              # §4
if t is packed +6: Δ += 1              # §6; sprites-off only
take earliest S in command_slots (including packed +6)
    with S > t, engine_dist(t, S) >= Δ,
    and S is not a fired CPU RAS
    and S is not a dummy R.. on packed +6     # §10
```

If the command loses a slot it takes the **next** slot; it does not re-arm the
full `Δ` (re-arming scores ~25% wrong on mixed traces).

**Arbitration is deliberately lopsided.** A running command never delays the
CPU: `first_cpu_slot` does not look at the engine at all, verified on both
machines. The CPU delays the command: the engine skips any slot the CPU
actually fired on. CPU RAS and command RAS never coincide, and the CPU wins
every contested CPU-legal slot, because it was booked ahead from `T` while the
engine decides late.

### 8.1 One gate-derived rule on the stalled `phiL` grid

This is the part FINDINGS7 fitted per display mode and could not explain. It
comes out of the silicon (§8.4) as a single mode-free rule:

```
NEED   = 16 + 2 * vram_ras_rq[0]
THRESH =  1 - 2 * vram_ras_rq[0]
```

Both comparisons are made on the Memory PLA's stalled `phiL` grid. In table
coordinates:

```
accept next request iff
    signed_memory_cycle_distance(previous_cpu_slot, T) >= THRESH(previous_cpu_slot)
```

`signed_memory_cycle_distance(a, b)` is `memory_cycle_distance(a, b)` when
`b >= a`, and `-memory_cycle_distance(b, a)` otherwise. It excludes RCC
stall/padding cycles in either direction. Using raw `T - previous_cpu_slot`
here is wrong.

**The arbiter decides at a `phiL` tick; the slot tables record a memory cycle.**
`phiL` is the VDP clock divided by 4, and each tick carries two DRAM sub-slots:
sub-slot 0 on the rising edge, sub-slot 1 on the falling edge, two cycles later.
Which one the CPU gets is decided by the Memory PLA: the CPU takes **sub-slot 1
exactly when `vram_ras_rq[0]` is asserted**, because the display side has
already claimed the first half of the tick.

So a slot table row is the tick position for some slots and the tick position
**+2** for others, and every per-mode constant was that offset averaged over a
mode. Referring the derived constants back to the tick removes all of them.

`NEED` is measured *to* the slot, so it gains the two cycles by which a
sub-slot-1 slot is recorded late; `THRESH` is measured *from* the previous
slot, so it loses them again. Same two cycles, opposite signs. `THRESH = 1` at
the tick is the arbiter cone's drop constant `D = 4`.

Across all three modes every CPU tick is one of four kinds:

| `ras_rq[1]` | `ras_rq[0]` | `slot_spr` | n | sub-slot | `NEED` | `THRESH` |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 187 | 0 | 16 | +1 |
| 1 | 0 | 0 | 32 | 0 | 16 | +1 |
| 1 | 1 | 0 | 50 | 1 | 18 | −1 |
| 1 | 1 | 1 | 4 | 1 | 18 | −1 |

The last class is the four sprites-on slots where a CPU slot lands on a sprite
slot — rows 28, 92, 1264 and 1330, the isolated slots in the sprite-fetch
region. There are none in the other two modes.

Of the constants, `(16, +1)` is the arbiter's own `K` and `D`, and the ±2 on
`ras_rq[0]` is derived. A direct CI sweep using the natural slot waveform gives
the same inclusive `K = 13` and `D = 4` boundaries for all four classes,
including the sprite-coincident one.

An earlier version used raw wall-clock distance for `THRESH`. It then needed
`THRESH = -3` on the four sprite-coincident rows to retain the 263/266 common-
`phi` corpus score. That was suspicious for two reasons: CI did not generate
the term, and moving the grant-to-RAS reference should have moved `NEED` too.
Breaking the boundary evidence down by row showed that the discriminating
normal captures were at row **1330**, exactly where two RCC stall cycles
complete. For example, raw distance `T - S = -3` becomes signed memory-cycle
distance `-1` after those two cycles are removed. Rows 28, 92 and 1264 did not
establish a common sprite rule.

With signed memory-cycle distance and the uniform gate-derived threshold, the
common-`phi` result remains **263/266** with the same three failures. The
per-capture result is 264/266 rather than the old empirical 265/266: the
additional loss is `stop-rdwrCpu-5g`, whose row-28 reconstruction already
depends on the known bad pre-window timing (§12). Thus removing the fitted
sprite term causes no regression in the shared model and removes an
unsupported extrapolation.

**Two of FINDINGS7's exceptions were this rule showing through.** It needed
`--needrow=170:16` and could not say why; sprites on has exactly two sub-slot-0
slots, rows **162 and 170**. And its largest residue group diverged at row 162
— the other one — granting row 188, which is sub-slot 1: with `NEED` 18 the
model cannot take row 162, with the correct 16 it can, and all five of those
captures fit. FINDINGS7 also found row 162 wanted a threshold on a plateau "at
either 1 or 2"; the derived value is 1, and it arrives with no row-162
parameter.

`fit_2026.cc` now uses signed engine distance for the threshold;
`--rawthreshdist` retains the historical wall-clock comparison as a diagnostic.
`ika9958/subslot_rows.py` emits the classes, computed from the Memory PLA and
the clock divider rather than from the corpus.

### 8.2 The lead splits into a wall-clock constant and a memory-cycle lookahead

A pin-to-register delay is wall-clock time and cannot see a sequencer stall; a
lookahead counted by the sequencer must see it in full. So `phi` does **not**
see padding and `NEED` sees it exactly as the command engine does.

This is testable, and it is the reason not to glue the two into a single ~27
cycle lead: a glued lead forces a CPU-only "absorbs part of the stretch" rule,
which is not a mechanism (and 1, 2 and 3 cycles all work, so it is not even
unique). 2013 is 0 / 1141 with the split and fails when glued. The difference
is visible only on requests that straddle padding.

### 8.3 `NEED` = 16, measured from both machines

**2013 fixes the sum.** Scanning lookahead `L` and dead window `D` over all 17
CPU captures: every `(L, D)` with **`L + D = 18` and `L ≤ 16`** is 0 / 1141,
and everything else fails (`L = 17` gives 10 wrong). So 2013 gives a ceiling of
16.

**2026 gives the floor**, because `phi` and `NEED` trade off exactly — raising
`NEED` by 2 lowers every `phi` band by 2 — so predictions differ *only* where
padding falls inside the lookahead window, which is the discriminating
experiment. Scanning `--need` with the trellis refitting `phi` per capture, and
counting display-off captures reconstructed *exactly*:

| `NEED` (display off) | 12 | 14 | **16** | 18 | 20 | 22 |
|--|--|--|--|--|--|--|
| captures exact, of 100 | 94 | 94 | **100** | 97 | 97 | 97 |
| shared `phi` | 15.21 | 13.21 | 10.75 | 8.75 | 6.75 | 4.75 |

The sum is conserved at 26.75 as the trade-off requires, so the scan tests the
split and not the sum. 16 is the only value that reconstructs every display-off
capture; 18 and above are indistinguishable from each other, so the
discriminating information is spent by then. Ceiling from 2013 plus floor from
2026 is `L = 16` exactly, which is what openMSX's `Delta::D16` already uses.

### 8.4 Where the rule comes from

Summarised from [`IKA9958.md`](IKA9958.md), which reads the die-shot reverse
engineering at [`ika-musume/IKA9958`](https://github.com/ika-musume/IKA9958).

- **The slot lattice is derived, not fitted.** A master horizontal counter
  `hcntr` runs 341 `phiL` ticks = **1364** cycles; the Memory PLA turns each
  tick into a display, sprite, refresh or CPU claim. Porting that gives
  154 / 88 / 31 CPU slots and **273 of 273** measured positions in all three
  modes, with no free parameter.
- **The line-end stall gives both line lengths.** `gc024` holds the `phiA`
  divider ring for one cycle in each of four consecutive ticks, so
  1364 + 4 = **1368**; with R#9 S1/S0 ≠ 0 the window is one tick instead of
  four, so 1364 + 1 = **1365** (§13). Simulating the ring
  (`ika9958/rccsim.py`) shows the `phiL` period going from 4 to 5 cycles per
  stalled tick.
- **The arbiter cone** in `CI.kicad_sch` is 54 cells, closed, with `K = 13`,
  `D = 4`, one `WAITING` bit, grant dominating set (drop-new), and **no
  display-mode input at all** — which is what §8.1's formulas say.
- **R#18** reloads the slave counter at `hcntr == 23 − H`, so each unit moves
  every VRAM access by 4 cycles, up to ±32. That is §3.2 measured from the
  engine side, and a prediction not yet tested at the extremes.

One negative result worth keeping. Replacing §3's lumped padding with the four
physically correct single cycles (`--padsil`) fits *worse* — 234 against 247 at
the time it was tested — and no placement inside the stalled tick recovers it.
The two forms agree at every slot position and differ only for a request
arriving strictly inside the stall, so the deadline is evaluated at slot
granularity, not per cycle, and §3's table is right as it stands.

### 8.5 openMSX starts counting ~29 cycles too early

openMSX stamps the port access at T-state 9 of `IN A,(n)` / `OUT (n),A`
(`CC_IN_A_N_2` / `CC_OUT_N_A_2` = `5+3+1`), i.e. at the **start of T2** of the
I/O machine cycle. `VDP::scheduleCpuVramAccess()` then applies
`VDPAccessSlots::Delta::D16` from that time. D16 there is also wall-clock ≥ 16,
not engine-distance.

`/IORQ` falls half a T-state into T2 and rises at the end of T3, so the pulse
is 2.5 T = **14.90** cycles — which is what the analyzer measures (14.9 ± 0.1,
§14), and that agreement is the check that the stamp really is the start of T2.
Stamp to **rising** edge is therefore a full **3 T = 17.88** cycles.

So from openMSX's own timestamp the hardware slot is `17.9 + 10.75 + 16` ≈ **45**
cycles later, and granting at D16 is about **29** cycles too early.

Why CPU-only tests rarely show it: the lost-request threshold almost cancels.
Hardware drops a new post arriving more than ~9 cycles before the pending RAS
in pin time, i.e. ~27 cycles before it in openMSX-stamp time; openMSX drops the
new I/O if it arrives before its already-scheduled (29-early) access. Same
threshold to within a couple of cycles.

What does **not** cancel is which slot the CPU takes away from the command
engine. Display-off command slots are often 8 apart, so 29 cycles is typically
three or four slots early; sprites-on gaps are 32–64, so usually one whole slot
early. That is the `vdpcmdx` `+CPU` column.

Proposed (not implemented): a **17.9 + 10.75 ≈ 29-cycle wall-clock** constant
from the port-access timestamp, followed by the 16-**memory-cycle**,
padding-aware lookahead.

## 9. Measuring the request time (2026)

**Do not** fit a 72/252 lattice on the 8280 VDP timeline. Intra-burst `/CSR`
gaps are **71 or 72** (typically alternating). Independent ±1 on neighbouring
posts would produce 70/73, and those almost never happen: the uncertainty is
**where** the extra or missing cycle sits, one analog phase for the burst.

### 9.1 `/CSx` decoder rules

- Use **rising** edges (`0→1`), and only where the previous sample was
  recorded low. A pulse already low at t = 0 is still a request — the rising
  edge is the end of the I/O cycle, which is when the VDP schedules — so do
  not synthesize a falling edge at t = 0. 26 of 180 CPU captures start that way.
  This is confirmed in silicon: sweeping the pulse width from 9 to 21 cycles at
  a fixed falling edge moves the granted slot with the **rise**, and
  `slot − rise` stays inside a 13…19 band while `slot − fall` tracks the width.
- Drop **either level** shorter than 8 analyzer samples: a real I/O holds
  `/CSx` low for ~56 samples and high for the rest of a loop iteration, so
  nothing real lasts one or two samples. Both transitions of such a pair are
  dropped, which restores the level around the spike and rejoins a pulse a
  spike had split in two (`drop_glitches`; §9.7). Then drop edges closer
  together than 20 VDP cycles (analyzer ringing at the ~15-cycle pulse width).
- Do **not** invent posts. A capture can start with a request already in
  flight, with no `/CSx` for the first RAS. Do not score a RAS whose complete
  `/CSx` was never recorded, and do not add a per-capture initial-state knob.
- A leading `/CSx` pulse closer to the next one than one loop instruction is
  setup code, not a loop `IN`/`OUT`.

### 9.2 What each program is for

| family | pace | T-states | what it pins |
|--------|------|----------|--------------|
| `stop-{rd,wr,rdwr}Cpu` | **71.5** | 12 | the occupancy rules — only here does a request arrive while the arbiter is busy |
| `stop-{rd,wr}Cpu132` | **131.2** | 22 | the fine phase scan |
| `stop-rdCpu222` | **220.6** (+ 399.40 wrap) | 37 | an always-empty buffer, so **sim-lost = 0** |

The 131-cycle pace is what makes those captures useful, and it is not a round
number: against the 128-cycle refresh cell it creeps 3.2 cycles per request,
and against the 8-cycle display-off lattice 3.2 out of 8, so the request phase
walks the whole slot gap every ~2.5 requests and a 62-request capture covers it
many times over.

`rdCpu222` also reproduces the clock ratio to 1 part in 10⁵ independently of
the 12 T programs. Do **not** change `NEED` or the slot tables to fit a
leftover first RAS on that set: analog `T` for interior I/Os sits inside the
grant window, and a "skip first slot" rule fitted to it breaks the clean files.

**Reads and writes share one pin delay.** The interleaved captures carry both
edge trains at once and are the only ones that can separate them: fitting
`δ_r` and `δ_w` independently, `δ_w − δ_r` is 0 in 8 files, ±1 in 6, +2 in 1,
and forcing them equal costs nothing.

**Set-adjust does not touch CPU arbitration.** The best-fit delay is identical
at every R#18 from 0 to 14. Holding it fixed across the eight display-off
adjust values gives **1436 / 1440** correct outside HBLANK, and every deviation
inside HBLANK is an access landing on the shifted slots that §3.2 measured from
the engine captures. At R#18 = 2 the observed rows are 1333 and 1343, exactly
where §3.2 puts them. So engine and CPU share one adjust-shifted lattice, and a
misplaced padding block only breaks the few HBLANK slots while leaving a
wall-clock delay alone.

### 9.3 Exact reconstruction: the pin delay is a real number

The pin delay is physically one real constant, and the two things that used to
be written off as noise — the exact instant the VDP acts on `/CSx`, and a
request already pending when the window opened — are latent variables. Both can
be reconstructed (`fit_2026.cc --trellis`):

```
T_i = floor(t2_i + e_i + phi)     one real phi shared by every edge
                                  |e_i| <= eps per edge
```

plus, optionally, one request already pending at capture start — which must
itself be a request the arbiter could have taken: an arming cycle that
schedules onto the first observed slot *and* precedes the first recorded edge.
Without that second condition it degenerates into a free grant. `eps` stands
for what the measurement cannot resolve — analyzer quantisation (80 MHz against
21.477 MHz, so **one sample = 0.268 VDP cycles**), the residual error of the
refresh interpolation, the VDP's input setup window, and the slow drift between
two crystals. A tolerance of about a sample is measurement, not model error.

Two properties make this searchable rather than a fit. First, the candidate
cycle set of each edge only changes when some `t2_i + phi ± eps` crosses an
integer, so the `phi` axis is cut into finitely many cells on which the answer
is constant: the scan over all real delays is exact, not a grid. Second, inside
a cell the arbiter is a small state machine — one scheduled slot, one
last-served slot, and how many grants have been matched — so a DP over the
edges finds the cheapest assignment reproducing the observed grant sequence
exactly, or proves none exists. Every solution is then replayed through an
independently written forward simulation and required to return the `.txt`
grant list; the DP and the simulator do not share code.

**`phi` ≈ 10.75**, one value for all three modes, with the whole corpus fitting
in [10.746, 10.748). The trellis is stricter than an integer-`δ` fit in two
ways: it allows no tie, and it insists every recorded edge be accounted for —
granted at an observed slot, granted outside the `.txt` window, or dropped by a
stated rule.

**The pre-capture request does not have to be a free parameter.** Because the
pulse width is constant and the loop runs at a fixed pace, the requests that
ran before the window sit at `t2[0] - k*pace` (`--prepace=K`). That replaces
"one pending request on any reachable slot" — a window 31 cycles wide with
sprites off and 65 with sprites on — with a single cycle, and takes the
captures needing *no* free parameter at all from 114 to **137**. `K` = 1, 2 and
3 give identical results, an independent confirmation of the single-entry
buffer: only the most recent earlier request can matter.

**The pulse width is a sub-sample measurement, and it is used.** With a
constant true width and the rise detected at sample `R`, the sampled width is a
hard one-bit measurement of the sub-sample phase: 56 samples means the rise was
early in its sample, 55 means late. Fitting the phase from cumulative T-states
gives the boundary at 0.70, matching both the 55.68-sample mean width and the
0.711 fraction of 56s. `rise_subsample` moves each edge to the midpoint of its
surviving interval. Only the *difference* between the two cases carries
information, so the two offsets are centred to leave the mean of `t2` where it
was — which is not cosmetic, since an integer-`δ` model cannot absorb a common
sub-sample shift.

### 9.4 The timebase is one straight line, not a chain of interpolations

The dominant per-edge error was never in the `/CSx` pulses. It was in the map
from analyzer time onto VDP cycles, and it was self-inflicted.

`t2` is built by locating refresh bursts in the VCD and interpolating between
them. Each anchor is a `/RAS` edge, so each is located only to the nearest
analyzer sample, and interpolating **between adjacent pairs** hands every edge
between two anchors the quantisation error of both — a piecewise-linear map
with some fifty independently tilted segments per capture.

It need not be a chain at all. Both clocks are crystals; over the 400 µs of a
capture neither drifts measurably, so the true map is a **single straight
line**, and fitting one through all the anchors at once averages their
quantisation down by √N. That is fewer parameters, not more — two per capture
instead of fifty-odd.

Measured without the arbiter (`--widthcheck`, rms over 24087 edges in 850
runs):

| | one fitted line | pairwise interpolation |
|--|--|--|
| width correction applied | **0.240 samples** = 0.064 cycles | 0.590 samples |
| no correction | 0.286 samples | 0.609 samples |

Pure quantisation on a known sample index gives 1/√12 = 0.289 samples. So with
one fitted line the uncorrected residual is **0.286 against a predicted
0.289** — the whole remaining error is the analyzer's quantisation and nothing
else — and the width bit then buys 16% rather than the 3% it appeared to buy on
the pairwise timebase, taking it below the quantisation floor as it should.
Judged against the pairwise map the correction had looked nearly worthless; it
was not the correction that was weak, it was that four fifths of the variance
was the timebase.

**`T` cannot be sharpened further from these files.** The per-edge error is now
the quantisation of an 80 MHz sample clock.

Two guards, because a global fit fails globally where a local one fails
locally. A few anchors really are wrong — one carried a residual of 83 cycles,
a miscounted refresh, not a rounding — and one such anchor among a hundred
tilts the whole line, so the fit is iterated with anchors past a whole cycle of
residual rejected (**42 of 13535** across the corpus). After rejection the worst
surviving anchor in every capture sits within 0.362 cycles of the line, mean
0.314, so the line is not hiding a drift it should have followed.
`--pairtime` restores the old behaviour for comparison.

### 9.5 The `.txt` line origin is implicit

`parse_txt` reconstructs absolute time as `1368 * column + row`, so the scan
line origin is implicit in the column index and every consumer has to re-derive
it from the `.vcd` refresh bursts. Two programs deriving the same timebase from
the same `.vcd` by two different routes can disagree by a whole line, and a
whole line is invisible in a 12 T burst (1368 is 19.1 request periods) while
being obvious in a 37 T loop (6.20 periods, a 45-cycle residue).

`process.cc`'s route is authoritative because `vcd2.cc` reads every signal;
`fit_2026.cc`'s private decoder must stay a faithful port of it — in particular
`candidate_filter1` must require the type to be exactly `"R.."`: read, first
CAS after RAS, and **VDS inactive**. Without the VDS test a screen-5 bitmap
fetch is indistinguishable from a refresh, since the pitch is 128 bytes so an
address ending in `0x3f` occurs in the display fetches of every line. Of 887 rw
captures, **522 contain at least one such impostor**.

**The refresh index has to be timed, not counted.** `make_anchors` used to
increment the refresh index once per detection, which assumes the detector
never misses one and never invents one. It does both, and a single miss shifts
every later anchor by a refresh period — warping `t2` locally, so `debounce_t2`
saw real `/CSx` edges only 17 cycles apart and **deleted** them. Corpus-wide
that silently removed **45 requests across 28 captures**. The fix is to let
elapsed wall time say how many refresh periods went by, since the crystal
frequency is known exactly, and to drop an anchor whose implied step is zero.
`--noanchorfix` restores the counting version.

`sprOff-stop-rdCpu-1` is the clean example: it looked like a capture with
*fewer* `/CSR` edges (110) than CPU accesses (114), which no request model can
reconstruct, so it looked defective. The VCD holds 116 rising edges, 6 of them
were being deleted by the warped timebase, and with the anchors timed it scores
**114 / 114**.

**Guard: `--origin`.** Counting refresh bursts is not a usable test. The
property that matters is whether the `.txt` and the `.vcd` agree, so test that
directly: score every CPU capture with the request axis shifted by −1, 0 and +1
lines and complain if a non-zero shift wins.

```
$ ./fit_2026 --origin
  266 CPU captures, 0 with a line-origin mismatch
```

Better still would be to stop re-deriving the axis: record it in the `.txt`
(one anchor pair, or just the absolute cycle of column 0), or trim every
capture to start at a line boundary. The latter would also remove the
capture-start residue of §12.

### 9.6 The `.cpureq` sibling files

One per capture that reconstructs exactly, so that a consumer never has to open
a `.vcd` again. The body is the integer request cycles, one per line, sorted;
`rdwrCpu` captures add an `r`/`w` tag because their train interleaves `/CSR`
and `/CSW`. The header carries the mode and the arbiter parameters, and it has
to: the pin delay and the lookahead trade off against each other, so the same
measurement yields different integers for a different `need`.

```
# scr5-dispOff-hmmm-rdCpu-3  CPU VRAM request cycles
capture scr5-dispOff-hmmm-rdCpu-3
mode dispOff
line 1368
need 16
margin 2
buffer 1
policy drop-new
kind r
requests 115
-88
-17
55
```

Requests that cause no visible access are **included and must be kept** — the
arbiter drops some, and some are granted outside the `.txt` window — but which
ones is derived by the model, not recorded. Cycles before the window are
negative. Two things are deliberately absent: the command engine is not needed
at all (the CPU has priority and its slot choice comes only from the mode's
slot table, which is why the CPU fit behaves the same with and without a
command running), and addresses are not derivable from the timing model.

**Guard: `--fromreq`** replays the files through the arbiter and compares with
the `.txt`, reading no `.vcd`. It also prints the margin `T − S_prev` of every
request it discards, which is a standing check on the threshold: each value
must lie below its class's threshold.

**Guard: `--checkreq`** is the cheap independent check in the other direction —
is a `.cpureq` consistent with its `.vcd`? It deliberately cannot build the
file: it never chooses a slot, never runs the arbiter, never opens the `.txt`,
and needs no knowledge of the pin delay. Since every cycle is
`T_i = floor(t2_i + phi)` for one shared `phi`, the differences `T_i − t2_i`
must all fall inside a band of width 1, widened by the tolerance. It catches
every corruption tried: one cycle moved by +3 (band 3.40), one cycle deleted
(count mismatch), the cycle list of another capture of the same length (band
180).

### 9.7 The one bad capture was a 12.5 ns spike

`dispOff-stop-rdwrCpu-4g` looked like a defective measurement and was recorded
as one. It is not: the fault was in the `/CSx` decoder, and finding it took
separating three things that produce the same symptom — a wrong capture, a
warped timebase, and a wrong edge chosen from a correct capture.

**Whether a displacement is in the capture is decidable**, because the raw VCD
timestamp is wall time straight from the analyzer while `t2` is that timestamp
mapped through the refresh anchors. `--rawdump` prints both gaps side by side:
a bad capture is off in both columns, a warped timebase only in `t2`. Here the
raw gap was −11.35 cycles against −11.72 in `t2`, so the anchors were innocent
and the edge really is early in the `.vcd`. But it is early *on its own* — every
falling edge is on the pace, and this pulse measures 11 samples where every
other measures 55 or 56:

```
3741875  /CSR 0    falling edge, exactly on the loop pace
3743250  /CSR 1    goes high...
3743375  /CSR 0    ...and back low one single 80 MHz sample later
3748875  /CSR 1    the real rising edge, 56 samples after the fall
```

`/CSR` glitched high for **one sample, 12.5 ns**, in the middle of an I/O
cycle. The capture is not defective: it holds the correct rising edge at
3748875, right where the loop pace and the recorded slot both want it. The
error was in the decoder — the spike splits the low pulse into 11 samples and
44, the decoder took the rising edge of each, and the 20-cycle debounce kept
the **first**, spurious one. The narrow-pulse filter could not help: it only
looked at short *lows*.

The fix is to filter both polarities (`drop_glitches`, §9.1). `--pacescan`
reports that **4 of 266 captures** contain such a spike; in the other three the
debounce happened to keep an edge that landed in the right slot anyway.

## 10. The packed +6 dummy read (2026 sprites-off)

The one genuinely odd behaviour, and the only place where the CPU affects which
slot the *command* gets in a way not covered by "skip a fired CPU RAS".

Occupancy over 17 stop + 99 mixed files, CPU ∩ command RAS = **0**:

| | CPU-legal | packed +6 | off-table |
|--|-----------|-----------|-----------|
| CPU RAS | **12846 / 12846** | 0 | 0 |
| Dummy `R..` | 0 | **333 / 333** (sprites-off only) | 0 |
| Command | dispOff/sprOn: all legal | sprOff: **1436 / 4608** packed | 0 |

What it costs the engine model to get this wrong, over 139 mixed files:

| engine occupancy | files perfect | steps |
|--|--|--|
| CPU RAS + observed dummy `R..` | **139 / 139** | **20537 / 20537** |
| CPU RAS + D6 (no dummy tag) | 116 / 139 | 20438 / 20537 |
| CPU RAS only | 113 / 139 | 20198 / 20537 |

### 10.1 What the dummy is

Every unclassified `R.. 0x1FFFF` that is not part of the blanking four-slot
block sits on one of the 25 packed slots. The next CPU-legal slot is +26 (323
cases) or +54 (10); +54 is exclusively the packed row 1212, where the
display-to-blanking lattice omits the usual +26 rising edge. That next slot
**is** a CPU RAS in 332 / 333. `C−6` is empty or a command, never CPU.

The packed slot is reserved from the command path, strobes `0x1FFFF`, and the
CPU is served one slot later. It is not itself a CPU grant: hardware analysis
below shows that it is the continuation of a high CPU-slot waveform, while the
grant circuit responds only to the rising edge at `C-6`. Rate is the same at
12 T and 37 T (~7% of sprites-off CPU RAS):
crowding the request stream does not create dummies. It is not simply "any CPU
request near a packed slot" — stop traces hold 326 CPU RAS whose table
predecessor is a packed `S−26`, and only **42** produce a dummy.

### 10.2 D6 narrowed: `WAITING` on the packed continuation tick

On the full command table, `NEED = 16` grants packed `C` for arming times
`T − C ∈ {−21 … −16}` while the CPU-legal table already grants `C+26`.
Hardware is stricter on packed:

| | `T − C` |
|--|--|
| dummy (mixed and stop) | **−21, −20, −19**, a few at −18 |
| packed **hit** with CPU at `C+26` | starts at **−18** |
| stop, empty packed slot with CPU at `C+26` | starts at **−18**, never −21…−19 |

Each packed `C` is the second tick of a two-tick-high `VRAM_SLOT_CPU` pulse
whose rising edge is at
`P=C-6`. U95/U96 can grant only at `P`. Gate-level CI simulation gives three
regions:

```
T - C <= -22          granted normally at P
-22 < T - C <= -18   misses P; WAITING is high at C
T - C > -18           WAITING reaches CI after C
```

This gives a hardware-supported **D6 candidate**: reserve/dummy packed `C` when
the request misses `P` but the arbiter's one-bit `WAITING` state is high on the
continuation tick. In the integer model this is
`T-C ∈ (-22,-18]`, or `NEED=18` on the full table — exactly two cycles more
lookahead than the CPU-legal table. The published command-control and address
source logic is absent, however, so the connection from `WAITING` to command
suppression and `0x1FFFF` cannot be traced.

Same fact from the pin: display-off lead 26.19 ± 0.38 cycles, packed-pair slots
28.01 ± 1.41. A uniform 26-cycle lead would predict **472** dummies against 335
observed; the extra ~2 cycles predicts **336**.

The corpus does not establish `NEED=18` as exact. The original classified set
has
`T-C ∈ {−21,-20,-19,-18}` = 12 / 16 / 12 / 4 and non-dummies start with five
cases in the same integer `-18` bin; fresh broader diagnostics also contain a
few reconstructed `-17` dummies. `T=floor(t2+phi)` loses phase at this second
sampling boundary, but one fitted sub-cycle phase per capture still does not
close the model. A deterministic `NEED=19` approximation scores one command
step better than `NEED=18`, with the same number of perfect files. Keep
`NEED=19` as the current integer rule and `NEED=18` as the strongest
circuit-derived hypothesis.

### 10.3 The `Δ=32` skip is lattice geometry, not a CPU predicate

`scr5-sprOff-{ymmm,hmmm,lmmm}-rdCpu-nx4-{1..6}f`. Screen 5 has
`PIXELS_PER_BYTE = 2` and `clipNX_2_byte` halves NX, so NX=4 is **2 bytes per
dest line**: every second command access is a line break. That turns the two
rare geometries of §6.1 into bulk statistics.

All 308 LMMM source→dest steps, by the geometry of the unconstrained
candidate `C`:

| `engine_dist(last, C)` | `last` | `C` | takes `C` | skips `C` |
|--|--|--|--|--|
| **32** | **packed** | **packed** | **0** | **47** |
| 32 | plain | plain | 99 | 0 |
| 38 | plain | packed | 82 | 0 |
| 64 | packed | packed | 21 | 0 |
| everything else (40 … 76) | either | either | 59 | 0 |

One geometry always skips and every other geometry always takes: `Δ=32` from a
packed slot to a packed slot is **never** taken (47/47), and everything else —
including a packed `C` at distance 38 and at 64 — is **always** taken
(261/261). That is precisely the packed-start `Δ → 33` of §6.

**This is why no engine rule references the future CPU access.** Earlier
CPU-predicate rules ("skip an idle packed candidate if no CPU RAS yet", "skip
the packed landing at a newline") were proxies for this lattice fact. The
engine needs only: the waits of §5, padding-aware engine-distance (§3), the
sprites-on addend (§4), packed-start +1 (§6), and skip slots another master
actually took. That is implementable — openMSX cannot look ahead at the CPU,
and does not have to.

## 11. 2013 CPU (NMS 8250)

CPU and VDP share a crystal, so the Z80 I/O train is an exact VDP-cycle
lattice. The only unknown is the phase `φ0` of that lattice relative to RAS
(one integer in `0 … 3059` per capture). `φ0` *is* arbiter time: there is no
`/CSx` in these captures.

CPU posts: 40 × `IN A,(n)` / `OUT (n),A` at **72** VDP cycles, then **252** to
the next burst. Period **3060**. The value 72 never appears as a VRAM gap; it
is a Z80 fact.

All CPU RAS sit on **CPU-legal** slots: **1141 / 1141**, including **0 / 204**
in sprites-off on packed +6. Mixed HMMV engine writes **do** use packed +6.
CPU and command never share a RAS.

What the 10 `*nocmd*cpu*` files pin down (six-way plateau, all 10/10):

- Packed +6 must **not** be in `first_cpu_slot`.
- The 16-cycle check is **engine-distance** (padding subtracted). Wall-clock
  tops out at 9/10.
- After a CPU RAS there is a **2-cycle holdoff**, or an equivalent
  `NEED + BUSY` pair with sum 18 and NEED ≤ 16 (§8.3).
- If a new request arrives while one is pending, the **scheduled slot does not
  move**.

Same CPU parameters with the command ignored: **7 / 7 mixed files at 100%** — a
running HMMV never steals or delays a CPU slot.

Documented dummy reads in blanking (dispOff 1236/1244/1252/1260, sprOff
1242/1250/1258 in CAS-ish published tables) are **outside** the CPU/command
table. They are not the sprites-off packed dummy of §10.

## 12. The three remaining captures are at the first-line boundary

`scr5-sprOn-stop-rdCpu-3`, `scr5-sprOn-stop-rdwrCpu-5g` and
`scr5-sprOn-stop-wrCpu-1`. All sprites-on, all `stop` traces.

### They are nearly right

At the best alignment over a fine pin-delay scan, each is wrong by one or two
grants out of ~105:

| capture | obs | extra | miss | first divergence | grant # | position in the capture |
|--|--|--|--|--|--|--|
| `stop-rdCpu-3` | 106 | 1 | 1 | miss row 28 | 16 | **first grant of line 2** (line 1 has 15) |
| `stop-rdwrCpu-5g` | 106 | 0 | 1 | miss row 92 | 1 | **first grant of line 1** |
| `stop-wrCpu-1` | 103 | 2 | 0 | extra row 1264 | 9 | **last grant of line 1** (line 1 has 9) |

Lines 2–7 are reproduced exactly in all three. Every divergence is within one
grant of the display-line-1 boundary. If errors were spread uniformly over the
~105 grants of a capture, all three landing in the first 16 has probability
about 0.3%.

### The `.vcd` files are sane

The pin delay was re-fitted per capture; this is not a stale fit. Decoding the
raw VCDs:

- The request trains are uniform. `/CSR` on `stop-rdCpu-3`: 114 pulses, width
  15 cycles, rise-to-rise 71.95 or 252.09 cycles with no other value. `/CSW` on
  `stop-wrCpu-1`: the same. `stop-rdwrCpu-5g` interleaves at 144.17 / 143.90
  with 324 at the loop boundary. Nothing drifts.
- Two boundary artefacts do exist. `stop-rdwrCpu-5g` has a **one-sample glitch**
  on `/CSW` at the very start (high → low → high inside 12.5 ns), and
  `stop-wrCpu-1` **opens mid-pulse**, with `/CSW` already low at t = 0.
- Both were repaired in copies of the VCDs and the fits re-run. **The results
  are identical** apart from `stop-wrCpu-1` now electing a pre-capture request.
  So the glitch filter and the pre-request search already handle them, and
  these are not the cause.

### The `.vcd` → `.txt` conversion of the *first* line is off by one

A real defect, in stage `3.time`, exactly where the pipeline README warns: the
timebase is anchored on refresh accesses, so the first display line is
**extrapolated backwards** rather than interpolated.

- **Every pre-window row is one cycle off the grid**, uniformly and in opposite
  directions for the two files that have them: `stop-rdCpu-3` is −1 on 14 of 14
  rows, `stop-rdwrCpu-5g` is +1 on 12 of 14. In both, the mis-timed rows
  include the last CPU access before the analysed window, whose true row is
  **1331** in both files — slot **1330**, one of the four sprite-coincident
  slots of §8.1, and the predecessor of the first in-window grant.
- **Column 1 drifts inside the window too.** In `stop-rdCpu-3` it sits one row
  below the other six at rows 4, 8, 12, 17, 21, 25, 30, 36, 40, 52, 56, 68, 72,
  76, 100 and 104 — including row 30 against the others' 29, which is the CPU
  access at slot 28. In `stop-rdwrCpu-5g` it is +1 at rows 15, 19, 23 and 44.
- Stage `4.manual-fix-timing` patched this by hand: it dropped the pre-window
  rows in both, blanked column 1 up to row 45 in `stop-rdwrCpu-5g`, and merged
  the displaced column-1 entries in `stop-rdCpu-3`. `stop-wrCpu-1` was **not
  touched at all** and has no pre-window rows.

So for two of the three the model's first grants depend on a region the
timebase got wrong and a human then repaired, and the repaired `.txt` is not
necessarily consistent with the `.vcd` timebase `fit_2026` recomputes for
itself.

### How much of this is a real explanation

It is a strong localisation, not a proof.

- Against it: **69 of the 266 corpus captures were hand-edited in stage 4, and
  67 of them fit.** Hand-editing by itself predicts nothing, and
  `stop-wrCpu-1` was never edited.
- For it: all three divergences sit at the line-1 boundary, all three are one
  or two grants out of ~105, and the two edited ones have a demonstrable
  one-cycle error in exactly the rows the first divergence depends on.
- The obvious clean test — delete display line 1 from both files and re-fit —
  does not work as-is, because `parse_txt` maps column index to line index, so
  removing a column shifts every remaining line by a line period. Blanking the
  column instead, and cutting the `.vcd` request train at the true line-1
  boundary, would be the way to do it.

`stop-wrCpu-1` is a separate case and looks like a genuine knife edge. It
diverges at row 1264, which the VDP served in only 2 of its 7 lines while the
model predicts 4; with requests every 72 cycles and slot gaps of 52, 66 and 66
around there, whether 1264 is taken depends on a slowly drifting phase.
`--threshrow=1212:0` fixes it and reaches 264 — row 1212 is the only
non-coincident row that immediately precedes a coincident one — but +1 is not
the two-cycle quantum every derived correction lands on, so it is recorded here
and **not adopted**.

### What would settle it

1. **Re-derive `3.time` for the first line** using a forward-only anchor, or
   simply discard the first line at stage 3 rather than repairing it.
2. **Extend the tie rule to `THRESH`.** The scorer already allows a one-cycle
   tie on `NEED`, on the grounds that the request is sampled asynchronously.
   The same argument applies to the drop comparison and is not implemented.
   That is a scorer change, not a model change.
3. **Re-measure** these three with the analyzer triggered a line earlier, so
   that no CPU access of interest sits in an extrapolated region.

## 13. S1/S0 and set-adjust

Used in §3, and derived in §8.4. Not a second slot map.

- **S1/S0 ≠ 0,0:** line **1365** in sprites-on *and* sprites-off, for all three
  non-zero encodings. Mid-line command/CPU slots unchanged; only the HBLANK
  tail moves. Both modes keep **+1** of their 4: sprites-off `10, 10` → `9, 8`
  (one correction, at 1331), sprites-on `[15 7 11]` → `[14 6 10]` (at 1329).
  This is the same `gc024` window as the 1368 line, one tick wide instead of
  four.
- **Set-adjust (R#18):** line stays 1368 for all 16 values in all three modes.
  Only the HBLANK comb moves, by 4 cycles per unit, and the padding total is
  invariant (§3.2). Confirmed independently from the CPU side (§9.2). There is
  no combined `adjustN` + S1/S0 capture.

The circuit nevertheless determines the combined case without another fitted
table. Let `H = sign_extend_4(R#18[3:0])`. On the 341-tick Memory-PLA sequence,
the first stalled tick is `326 + H`; R#9 selects four consecutive stalled ticks
for S1/S0=`00`, or one for every other encoding. Each normal tick is 4 cycles,
each stalled tick 5, and sub-slot 1 is at cycle 3 rather than 2 within a stalled
tick. `ika9958/stall.py::rows(mode, r18, s)` implements this composition and
reproduces the centred tables, all measured set-adjust examples, and all
measured 1365-cycle tails. The combined result is hardware-derived but remains
an unmeasured prediction.

Keep the tables at centred (R#18 = 0) / S1S0 = 00 / 1368 unless a leftover is
specifically a wrap through 1330.

## 14. Command start

The delay between the CPU writing the command byte (R#46) and the engine's
first VRAM access. Treat the start like any other wait: the first command slot
`S` with `engine_dist(CE, S) ≥ S₀`. Each launch then contributes the integer
interval

```
S₀ ∈ [engine_dist(CE, prev_slot(S)) + 1, engine_dist(CE, S)]
```

with `CE` the rising edge of `/CSW` on that last register write, rounded up to
a whole cycle. Intersect over launches.

The register setup is a burst of 15 `OUT (#99)` at ~137 cycles (23 T) each; the
**last** pulse of the burst is the CE write. `/CSW` low is **14.9 ± 0.1** cycles
wide (2.50 T-states), which is also the cleanest measurement of the pulse width
used in §8.5.

### 14.1 HMMV, display off, the original six

Data: `scr5-dispOff-hmmv-noCpu-nx4-ny2-{1..6}f`. NX=4, NY=2 on screen 5, so
exactly four dest writes per run, with the command restarted in a loop so the
launch falls inside a random 7-line snippet.

| capture | `/CSW` rise | first engine RAS | lead |
|--|--|--|--|
| `1f` | 2828.5 | 2923.7 (row 188) | 95.2 |
| `2f` | −9.8 | 87.8 (row 88) | 97.6 |
| `3f` | −1277.1 | −1180.1 (row 188) | 97.0 |
| `4f` | 2775.2 | 2899.7 (row 164) | 124.5 |
| `5f` | 1456.4 | 1556.1 (row 188) | 99.7 |
| `6f` | 2750.2 | 2847.7 (row 188) | 97.6 |

`4f` is not an outlier: its wait lands inside the 44-cycle hole (row 120→164),
so the first legal slot is 44 cycles further on. The six launches intersect at

```
S₀ ∈ {93, 94, 95}     from the rising edge of /CSW
S₀ ∈ {111, 112, 113}  from openMSX's port-write timestamp (start of T2, §8.5)
```

### 14.2 All six commands, three modes

Data: `scr5-{mode}-{cmd}-noCpu-nx4-ny2-*h` (LMMV sprites-off/on are named
`scrOff`/`scrOn`). Same NX=4, NY=2 geometry. First access is command-typed:
HMMV dest write, LMMV/LINE dest read, LMMM/HMMM/YMMM source read. Display-off
pins a single integer; sprites-off contains it. Sprites-on is looser. HMMM
there intersects at 83..84 rather than 82 — one cycle, the size of the §4
addend — so do not treat sprites-on as an independent pin of the same
integer. The addend is **not** otherwise required by the display-off and
sprites-off launches.

| command | first access | dispOff | sprOff | sprOn | `S₀` from `/CSW` |
|--|--|--|--|--|--|
| **LMMM** | R source | **46** | 46..47 | 42..48 | **46** |
| **LMMV** | R dest | **70** | 69..75 | 68..73 | **70** |
| **HMMM** | R source | **82** | 80..85 | 83..84 | **82** |
| **YMMM** | R source | **82** | 82..84 | 81..98 | **82** |
| **HMMV** | W dest | **94** | 94..96 | 91..104 | **94** |
| **LINE** | R dest | **94** | 93..95 | 92..99 | **94** |

Display-off intersections that are a point: LMMM 18/19 launches, HMMM 24/24,
HMMV 26/26, LINE 25/25. Two LMMV launches have wall-clock 69.9 and therefore
`hi = 69` if `/CSW` is ceiled the other way; 21/23 still contain 70. YMMM has
no usable refresh lattice in these files (every `0x3f` RAS is VDS-active), so
the line origin is the 44-cycle hole at row 120→164; the lower envelope of
those walls is 82, with the same one-cycle rounding as LMMV.

[`sndpl/openMSX#5`](https://github.com/sndpl/openMSX/pull/5) independently
reported the same six integers, including the rounding leftovers. That is the
check.

From the port-write timestamp add the 18 cycles of §8.5 (start of T2 to the
rising `/CSW` edge):

```
LMMM  64
LMMV  88
HMMM 100
YMMM 100
HMMV 112
LINE 112
```

HMMV's 112 is the centre of §14.1's `{111, 112, 113}`.

### 14.3 Split into known costs

The four distinct `/CSW` values are 46, 70, 82, 94 — **all congruent to 10
modulo 12**:

```
S₀ = 10 + 12 * N    N = 3 (LMMM), 5 (LMMV), 6 (HMMM, YMMM), 7 (HMMV, LINE)
```

The 10 is the pin-to-arbiter delay already measured from CPU accesses
(`phi` ≈ 10.75 from the rising edge, §9.3). The 12 is the even quantum the
engine's own delays are built from: R→W 24, YMMM W→R 36, HMMM/LMMM W→R 60,
LMMV W→R 72, LINE W→R 84. Only HMMV's 46 and LMMM's 32 are not multiples of 12,
and they are not the startup values either.

`N` is **not** identified. It is not the number of accesses per pixel, not the
steady-state delay that would precede the first access, and not the
minor-direction addend of §5 (104 / 130 / 104 / 128 / 128 / 120 against
startups 94 / 70 / 82 / 82 / 46 / 94). Two components are known; the integer
per command is not.

### 14.4 openMSX

Every `execute*()` entry path starts with `nextAccessSlot(time)`, i.e.
`getAccessSlot(time, Delta::D0)` at the port-write timestamp: `S₀ = 0`.
Hardware is 64 to 112 cycles later depending on the command, about 8 to 14
display-off slots. For a long command this is a constant offset of the whole
access pattern rather than a shape error, so it mostly shows up in short
commands, in the `CE` clear time, and in the arbitration against a CPU access
issued right after the launch. Same class of fix as §8.5, and the same origin.

## 15. Vertical border ↔ display: the grid switches one line early

Data: `scr5-dispOff-sprOn-stop-noCmd-{1..9}f` (top border → display),
`scr5-sprOn-dispOff-stop-noCmd-*f` (display → bottom border),
`scr5-1MHz-stop-noCmd-{1..6}f` (whole frames, undersampled). Analysed with
`lines.py`, which calibrates each line on its own 8 refresh pulses (a global
fit smears ±1 cycle here).

Sprite data is fetched one line ahead of the line it is rendered on, and the
access grid follows the fetching, not the rendering:

| line | RAS grid | contents |
|--|--|--|
| border, ≥2 lines before display | display-off, 167 RAS | nothing but refresh and the 4 blanking dummies |
| **border, immediately before display** | `6, 14, … 118` (the sprites-**off** left comb), **164.. sprites-on comb** | real sprite attributes/patterns; the 32 bitmap slots are **dummy `0x1FFFF`** |
| display lines | sprites-on, 129 RAS | sprite attrs + patterns + 32 bitmap |
| **last display line** | sprites-on, 129 RAS | bitmap still real; **all** sprite-attribute slots and the trailing HBLANK sprite comb are dummy |
| **border, immediately after display** | 0..126 sprites-on comb (all dummy), **164.. display-off comb** | — |

So the switch is not at a line boundary: cycles ~0..130 of a line belong to the
*previous* line's mode (they hold the sprite-pattern fetches for the line about
to be shown), and the grid changes at the start of the display-fetch region,
between RAS 120/126 and RAS 164. All 9 top-border captures agree exactly, down
to the sprite counts: the pre-display line does 40 attribute and 4 pattern
fetches instead of 48 and 8, the missing 8 being precisely the ones a normal
line does in its left HBLANK — which is why that part of the line falls back to
the sprites-off comb.

The parallel analysis in [`sndpl/openMSX#5`](https://github.com/sndpl/openMSX/pull/5)
reproduces the one-line-early fetching and the dummy-read counts but reads the
comb as changing *at* the line boundary rather than at cycle ~164. Worth
re-deriving; it is worth two lines per frame.

Two consequences for openMSX. `getTab()` already keys on
`isDisplayEnabled() = isDisplayArea && displayEnabled`, so the vertical border
correctly uses `tabScreenOff` — the measurement confirms that (167 RAS,
8-cycle comb, identical to a display-disabled line). What it gets wrong is the
boundary: the table should become `tabSpritesOn` at cycle 164 of the line
**before** `isDisplayArea` starts, and go back to `tabScreenOff` at cycle 164
of the line **after** it ends.

`VDS` is a clean real-vs-dummy discriminator: it stays high on the dummy
`0x1FFFF` reads that replace the bitmap fetches. The 1 MHz captures show
exactly **192** VDS-active line blocks per frame (the test ran in 192-line
mode), each ~47 µs long at a 64.14 µs pitch, so the extra sprite-fetch line is
*not* VDS-active. Frame period 16804 µs = **262** lines, giving
1368/64.137 µs = **21.33 MHz** and 59.5 Hz — an independent check on the 8280's
5.96113 cycles per T-state.

## 16. What is solid, what is open

**Solid.**

- The command engine: §5 waits, §3 padding-aware engine-distance, §4
  sprites-on addend, §6 packed-start +1. 78330 / 78330 transitions, and 2013
  agrees where it can see the same things.
- CPU-legal = command table minus packed +6 (a grid property, both machines).
- The slot lattice, from silicon: 1368 and 1365, 154 / 88 / 31, and 273 of 273
  measured positions (§8.4).
- The CPU rule of §8.1: one buffer, drop-new, the booked slot never moves, and
  two formulas over the Memory PLA sub-slot bit with no display-mode or sprite
  term, both evaluated on signed stalled-grid distance.
  263 / 266 captures reconstruct every recorded `/CSx` edge exactly.
- `phi` ≈ 10.75 cycles, one real number for all three modes, in
  [10.746, 10.748) over the whole corpus.
- The padding is the line-end clock-divider stall (§3, §8.4), and its total is
  +4 in every mode at every R#18.
- Command skips fired CPU RAS; may use packed +6; never delays the CPU; the
  loser takes the next slot with no full re-arm. No engine rule references the
  future CPU access (§10.3).
- Dummy `R..` on packed +6 correlates with the continuation tick seeing
  `WAITING` after the request missed the run-start grant; the CPU RAS follows
  at +26 (row 1212: +54).
- Gate-derived G1/G2/G3 CPU timing reproduces openMSX's existing 31-row
  character table exactly. The six four-cycle differences from bitmap
  sprites-on come from combinational `gt075` versus registered `gt076`
  (§4.8 of `IKA9958.md`).
- Text T1/T2 gates reproduce the measured 47-slot cyclic pattern exactly, but
  leave a global one-`phiL` phase ambiguity; retain the measured openMSX rows
  (§4.9 of `IKA9958.md`).
- `T` is now limited by the analyzer's sample clock, 0.064 cycles rms (§9.4).

**Open.**

1. **The three captures of §12**, and whether they are processing or model.
2. **The exact packed-dummy boundary and `0x1FFFF` source** (§10.2). The
   published command/address ownership path is absent; `NEED=18` is suggested
   by CI, while `NEED=19` remains the better integer approximation.
3. **A combined R#9/R#18 capture.** The circuit composition is exact enough to
   generate it (§13), but only the separate sweeps have been measured.
4. **The `.txt` axis** (§9.5): absolute time is `1368 * column + row`, so the
   origin is implicit and every consumer re-derives it. Record it in the file,
   or trim captures to a line boundary. Until then `--origin` is the guard.
   Doing this would also address §12.
5. **openMSX CPU origin** (§8.5): `Delta::D16` from the Z80 port timestamp is
   ~29 cycles early. Lost-request timing cancels; the stolen command slot does
   not (`vdpcmdx` `+CPU`).
6. **What `N` counts in command startup** (§14.3). The six commands are
   measured; the leftover integer per command is not explained. Character,
   text, and MSX1 tables are still untouched.
7. **The border/display comb boundary** (§15): cycle ~164 or the line edge.
8. **Packed-start +1 is unobservable on 2013** (§6.1), and the dummy `R..` may
   be 8280-only — 2013 mixed traces are HMMV only.

**Not worth collecting:** another `IN` gap purely to crowd or empty the buffer
(12 T already crowded it, 37 T already emptied it, and the dummy rate did not
change). Read versus write is settled, so no more alternating loops. More
*untargeted* sprites-on 12 T bursts add nothing.

---

Admissible `Δ` for a pair of RAS times `(p, n)` is
`[engine_dist(p, prev_slot(n)) + 1, engine_dist(p, n)]`. Intersect over all
pairs of one step. Subtract the sprites-on addend from that mode's band before
intersecting the three modes. Line wrap versus mid-line is labelled from the
dest (or YMMM/HMMM src) address crossing a 128-byte VRAM line.
