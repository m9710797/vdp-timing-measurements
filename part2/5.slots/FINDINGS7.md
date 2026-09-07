# VDP command and CPU slot timing

This document was written with assistance from an AI coding agent.

Current model: command engine, CPU VRAM accesses, and the arbitration between
them, for the V9938 in bitmap modes. Command waits and slot tables started as
[`FINDINGS4.md`](FINDINGS4.md) (frozen); the discovery notes are
[`FINDINGS5.md`](FINDINGS5.md), [`FINDINGS6.md`](FINDINGS6.md) and
[`ARBITER_ITER.md`](ARBITER_ITER.md). S1/S0 and set-adjust side-track:
[`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md).

| | 2013 | 2026 |
|--|------|------|
| Machine | Philips **NMS 8250** | Philips **NMS 8280** |
| Data | `8.final-analysis/` | `part2/5.slots/` (txt) + `part2/1.vcd/` |
| Clocks | One 21.47727 MHz VDP crystal, CPU = VDP `/6` | Separate crystals. VDP ~21.33 MHz PAL. **5.96113 ± 0.00004** VDP cycles per Z80 T-state (same from 12 T and 37 T programs, 1 part in 10⁵) |
| CPU request times | Exact **72 / 252 / 3060** VDP cycles (40 I/O + loop), unknown phase `φ0` | `/CSR` / `/CSW` on the refresh-interpolated VDP clock. Loops used (measured): **71.5**, **131.2**, **220.6** + 399.4 |
| Pin → arbiter delay | inside `φ0` | `δ` = **11**, or **13** with sprites on (§11.2) |

Bitmap screen 5 (screen 8 shares the slot maps). Default line period **1368**
(R#9 S1,S0 = 0,0). Slot times and waits are **RAS** (openMSX numbering);
published `.txt` rows are **CAS** (§1).

## Scores

| set | files | steps | wrong |
|--|--|--|--|
| 2026 command-only (`--nocpu`) | 514 / 514 | **78330 / 78330** | 0 |
| 2026 mixed engine (CPU RAS + dummy `R..` occupied) | 138 / 138 | **20408 / 20408** | 0 |
| 2026 CPU VRAM | 203 / 266 | **23915 / 23990 (99.7%)** | 17 extra + 75 miss |
| 2013 command-only | — | 3965 / 3965 | 0 |
| 2013 CPU | 17 / 17 | **1141 / 1141** | 0 |
| 2013 mixed (HMMV + CPU) | 7 / 7 | all | 0 |

There is no capture on either machine where the **command engine** mispredicts
a step. The whole residue is on the CPU side and is described in §11.4.

The CPU line above uses one *integer* pin delay per mode and allows a one-cycle
tie. With the delay treated as the real number it physically is and no tie
allowed, **256 of 266 captures reconstruct exactly** — every recorded `/CSx`
edge accounted for — and 129 of them do so at **zero** tolerance from a single
shared delay and no pre-capture request (§11.5, §11.9). The 10 that remain are
enumerated in §11.7.

## Tools

`fit_2026.cc` implements §3–§6 (padding +4 in all modes, sprites-on addend +1),
the CPU model of §8, D6 dummy occupancy, and the `/CSx` decoder of §11.1. Its
engine model uses **no CPU predicate** (§10.3). Do not change `fit_2013.cc`
behaviour.

- `8.final-analysis/cpu_scratch_2013.cc` — 2013 CPU grid + HMMV
- `fit_2026.cc` — CPU fit over all `rdCpu` / `wrCpu` captures (default)
- `fit_2026.cc --nocpu` — 2026 command engine
- `fit_2026.cc --scratch` — occupancy + engine, skip observed CPU RAS
- `fit_2026.cc --mismatch` — mixed packed leftovers, dummy window
- `fit_2026.cc --origin` — assert that the `.txt` line numbering agrees with
  the `.vcd`; run on every new batch and after hand-editing a `.txt` (§11.6)
- `fit_2026.cc --rw [--sel=SUB]` — fit the `/CSR` and `/CSW` pin delays
  independently, with each per-file plateau; `--force=D` instead lists every
  mismatch at a fixed δ by position in the line, which separates a slot-table
  error from asynchronous jitter, and counts how many misses are the first
  access of their capture
- `fit_2026.cc --need=N`, `--sprextra=N`, `--pad3[=E]` — override the
  lookahead, the sprites-on addend, and the sprites-on padding, for the
  trade-off scans of §8.2 and §3
- `fit_2026.cc --trellis [--sel=SUB]` — reconstruct the exact request cycles
  from a single *real* pin delay plus a bounded per-edge tolerance, and report
  the delays that reproduce every grant (§11.5). `--eps=X` / `--smp=X` force
  the tolerance, `--needoff=a,b,c` and `--needrow=r,..:N` vary the lookahead
  per mode and per slot, `--qdepth=N` the request buffer, `--tdump` prints the
  reconstruction edge by edge. Writes `trellis-2026.txt` (feasible delays,
  memoised) and `trellis-requests-2026.txt` (the reconstructed request cycles);
  `--reqfiles` writes the `.cpureq` siblings
- `fit_2026.cc --fromreq` / `--checkreq [--tol=X]` — the two guards on the
  `.cpureq` files: replay them through the arbiter against the `.txt` with no
  `.vcd`, and check them against the `.vcd` without being able to build them
  (§11.5)
- `fit_2026.cc --thresh=N` or `--thresh=a,b,c` — the discard threshold, one
  value or per mode; the whole drop rule is that one number (§11.7)
- `fit_2026.cc --faildiag` / `--faildump [--only=SUB]` — for the captures with
  no exact reconstruction, scan the pin delay finely and attribute every
  mismatch to the request responsible and to the slot that shadowed it;
  `--faildump` adds the per-miss detail and, with `--only`, the whole request
  train (§11.7)
- `fit_2026.cc --pacescan` — find mismeasured `/CSx` pulses: an edge off the
  loop pace whose successor makes the time back. None left in the corpus; it
  also counts the short-high spikes the decoder cancels, 4 captures (§11.8)
- `fit_2026.cc --pairtime` — build `t2` by interpolating between adjacent
  refresh anchors instead of fitting one line through all of them, which is
  what the per-edge error used to be dominated by (§11.9)
- `fit_2026.cc --threshrow=ROW:N` — diagnostic: give one row of the lattice
  its own drop threshold, to separate "the corpus contradicts this" from "the
  corpus has never observed this" (§11.7). Not written to the `.cpureq` files
- `fit_2026.cc --rawdump --only=SUB` — the gaps of the rise train in raw
  analyzer wall time next to the same gaps in reconstructed cycles, plus each
  pulse's width and its own falling edge. This is what tells a bad capture
  from a warped timebase from a wrong edge picked out of a good capture
  (§11.8)
- `fit_2026.cc --widthcheck [--nowidthfix]` — the straight-line residual of the
  rise train, which measures the per-edge error without the arbiter and so
  sizes the sub-sample width correction and the timebase error against each
  other (§11.5)
- `part2/1.vcd/rasmap.py` — the `/RAS` comb of a capture in VDP cycles, with
  the line length fitted rather than assumed (§3.1)
- `part2/1.vcd/lines.py` — per-line calibration, for the border/display
  boundary (§14)
- `retag.py` — reconstruct access types from the VRAM addresses, for traces
  where a command rectangle leaves its designated region
- `exp4.py` — the sprites-off engine step, standalone, with every leftover
  classified by slot geometry

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
~50 ns). Same slots, not a 1-cycle table error.

To map a `.txt` time to RAS: subtract 1, or subtract 2 when the CAS row is
1326/1336 (dispOff) or 1324/1334 (sprOff). After that, every engine access in
the 2026 `noCpu` set lands on the openMSX slot tables.

## 2. Slot tables

Command slots are the openMSX bitmap tables in `VDPAccessSlots.cc` (RAS):

| Mode | Command slots / line |
|------|----------------------|
| dispOff (display off / vertical border) | 154 |
| sprOff (bitmap, sprites off) | 88 |
| sprOn (bitmap, sprites on) | 31 |

Sprite count and size do not add command slots. Screen 5 and 8 share these
maps. The character and text tables have never been measured.

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
8-cycle grid. During active display the VDP inserts extra command/CPU
opportunities as **pairs six cycles apart**.

**Definition:** a *packed +6* slot is a command-table RAS `S` whose
predecessor in that table is `S−6`. It is still a legal **command** slot.
*CPU-legal* means `command_slots` minus these packed +6 times.

This geometry exists **only in sprites-off**. Display-off command slots are 8
apart (or 16 at refresh holes, 10 at the two stretches). Sprites-on has no
adjacent command slots 6 apart.

**Why only sprites-off.** In the active display the VDP groups memory cycles
in 32-cycle cells: `182+32k` is a sprite fetch when sprites are on and spare
when they are off; `188+32k` is spare in both modes; `194+32k` is the display
burst (one `/RAS`, four `/CAS`). With sprites off the first becomes spare too,
which is the 6-cycle pair. Refresh takes the middle cycle once per 128, which
is why the list has **25** entries, not 32. It is a property of the **slot
grid**, not of the traffic: in sprites-off CPU captures the first of the pair
is idle ~32% of the time, and the CPU still does not take the second.

Sprites-off has **88** command slots per line, of which **25** are packed +6
and **63** are CPU-legal. None of the 25 is a stretched slot (CAS = RAS+1).

The 25 packed +6 RAS times, with the CPU-legal first-of-pair in parentheses:

```
(182, 188)  (214, 220)  (246, 252)
(310, 316)  (342, 348)  (374, 380)
(438, 444)  (470, 476)  (502, 508)
(566, 572)  (598, 604)  (630, 636)
(694, 700)  (726, 732)  (758, 764)
(822, 828)  (854, 860)  (886, 892)
(950, 956)  (982, 988) (1014,1020)
(1078,1084) (1110,1116) (1142,1148)
(1206,1212)
```

Packed RAS only: **188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604,
636, 700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116, 1148, 1212**.

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

CPU accesses always land on the first of the pair, or on an unpaired
CPU-legal slot. Commands **do** use the +6 (2013 sprites-off HMMV: 24 / 85
engine writes; 2026 sprites-off: 1436 / 4608).

**No other exclusive slots.** Union of all CPU VRAM vs command-engine RAS:

| Mode | 2026 (covering set) | 2013 |
|------|---------------------|------|
| dispOff (154) | all 154 used by **both** | 137 both; 16 cmd-only and 1 CPU-only (`628`, *n*=2) are coverage holes |
| sprOff (88) | **63** CPU-legal: all used by both. **25** packed +6: command only. No CPU-only slots | CPU still 0 on all packed +6; other holes are undersampling (8 files) |
| sprOn (31) | all 31 used by **both** | 29 both; `188` CPU-only (*n*=8) and `170` cmd-only (*n*=10) |

The only command-table slots exclusive to one client are the sprites-off
packed +6 (command, never CPU). There are **no** CPU-only slots. Dummy `R..`
accesses (2026 sprOff, **333** in the `.txt` parse) also sit on those same 25
packed times; those slots still take command accesses in other traces. The
dummy is the CPU path being granted a packed slot it cannot carry (§10).

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
the sprites-on padded cycles are sprite fetches, not command slots (last
command slots 1264, 1330). Display-off example (HMMM):

```
104 W → 164 R     Δ = 60   does not cross padding → engine count 60 → slot taken
1292 W → 1360 R   Δ = 68   crosses both 10-cycle cycles → 1352 is only 56 engine cycles → skipped
```

The counter restarts at every access. The correction is well defined for an
interval, not as a periodic remapping of the line.

**Command traces fix only the sum** of the padding total and the sprites-on
addend (§4), because the padded RAS pulses are not command slots and the
sprites-on slot gaps are 32–64 with plateaus several cycles wide. Of the four
combinations, three are 100% on 78330 transitions:

| padding sprites-on | addend when sprites on | engine `noCpu` |
|--|--|--|
| **+4** (2+1+1) | **+1** | **100.0%** |
| +4 (2+1+1) | +2 | 100.0% |
| +3 (1+1+1) | +2 | 100.0% |
| +3 (1+1+1) | +1 | 99.9% (19 wrong) |

`/RAS` fixes the padding at +4, so the addend is 1 or 2 with nothing to choose
between them — use **1**, which is what openMSX has always used. The same
indifference does *not* hold on the CPU side, where there is no addend to
absorb a padding change; there the +4 table is marginally (2–4 accesses)
better at every δ.

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
The unpadded spacing has to come from the rest of the line.

Sprites-off is the easy case: everything else in that region runs at 8, so
8 + 8 = 16 is the floor.

```
sprites off, 1368 : ... 1306 -8- 1314 -8- 1322 -10- 1332 -10- 1342 -8- 1350 -8- 1358 -8- 1366
sprites off, 1365 : ... 1306 -8- 1314 -8- 1322  -9- 1331  -8- 1339 -8- 1347 -8- 1355 -8- 1363
```

`10 + 10 = 20` against a floor of 16 is **+4**; the 1365 line's `9 + 8 = 17`
is **+1** (at 1331). Display-off is the same picture with the block two cycles
later.

Sprites-on has an uneven comb, so the unpadded spacing comes from its
**64-cycle period**, `6 10 6 10 6 13 13`, which holds through HBLANK except at
the padded triple:

```
1174 -6- 1180 -6- 1186 -20- 1206 ...     display region: 6 6 20, period 32
1238 -13- 1251 -13- 1264 -6- 1270 -10- 1280 -6- 1286 -10- 1296 -6- 1302   = 64
1302 -13- 1315 [-15- 1330 -7- 1337 -11- 1348] -6- 1354 -10- 1364 -6- next line
        natural continuation of the 64-comb:
1302 -13- 1315 [-13- 1328 -6- 1334 -10- 1344] -6- 1350 -10- 1360 -6- 1366 = 1302 + 64
```

The unpadded line would close at **1366**, the real one at 1370 (which is
where the next line's first `/RAS` sits): **+4**, as `+2, +1, +1` on the
memory cycles completing at 1330, 1337 and 1348. The 1365 sprites-on line is
`1329 1335 1345 1351 1361` — the same comb with **+1** left in it.
`15 + 7 + 11 = 33` versus `14 + 6 + 10 = 30` is the 3-cycle line-length
difference, not the padding.

Also visible: the stretched RAS→CAS (CAS = RAS+2) is only on the middle of the
sprites-on triple (sprP at **1337**), and a 1365 line has no stretched RAS→CAS
anywhere, in either mode.

Source: `rasmap.py scr5-sprOn-hmmv-noCpu-s0-1f.vcd 1050 1368`, and the
`scr5-{sprOff,sprOn}-hmmv-noCpu-s{0,16,32,48}` captures.

Side result for 1365 mode (not needed for emulation — the slot tables assume
1368): mid-line command slots are untouched (46, 94, 162, 214, … 1266, 1314
all still used) and only the tail moves. Sprites-off `1332→1331`,
`1342→1339`, `1350→1347`, `1358→1355`, `1366→1363`; sprites-on `1330→1329`.

### 3.2 Set-adjust moves the padding, never its total

`scr5-{dispOff,sprOff,sprOn}-hmmv-noCpu-adjust{0..15}` (raw R#18 values, 2
captures each, 96 files). The line is **1368** for every value in every mode,
and nothing before RAS ~1290 moves. What moves is the *position* of the
padding block, at **4 cycles per R#18 unit** — one screen-5 pixel:

```
display off, R#18 =  0 : 1316 -8- 1324 -10- 1334 -10- 1344 -8- 1352 -8- 1360
             R#18 =  1 : 1316 -8- 1324  -9- 1333 -10- 1343  -9- 1352 -8- 1360
             R#18 =  2 : 1316 -8- 1324  -8- 1332 -10- 1342 -10- 1352 -8- 1360
             R#18 =  4 : 1324 -8- 1332  -8- 1340 -10- 1350 -10- 1360
             R#18 =  6 : 1332 -8- 1340  -8- 1348 -10- 1358 -10- 1368
             R#18 = 14 : 1308 -8- 1316 -10- 1326 -10- 1336 -8- 1344 -8- 1352
             R#18 = 12 : 1300 -8- 1308 -10- 1318 -10- 1328 -8- 1336
             R#18 =  8 : 1284 -8- 1292 -10- 1302 -10- 1312 -8- 1320
```

R#18 = 0 is centre, 1..7 shift the picture left, 15..8 shift it right, and the
padding block follows: even steps slide the `+2, +2` pair by 8 cycles per two
units; odd steps sit half a memory cycle away, where the same 4 extra clocks
spread as `+1, +2, +1` over three memory cycles. Sprites-off (base 1322) and
sprites-on (base `1315→1330→1337→1348`) behave the same way. **The total stays
+4 in every mode for all 16 values** — a second, independent confirmation of
§3.1, and a demonstration that the padding is a property of the horizontal
timing chain and moves with it.

Two of the 32 sprites-off adjust captures (`adjust1`, `adjust15`) caught
vertical-border lines and must be filtered to display lines before pooling;
filtered, they follow the table.

For emulation: set-adjust needs no new slot tables as long as one accepts a
±8-cycle error on the last two or three HBLANK slots of the line — otherwise
it would take 16 tables per mode. R#18 cannot be used to make the padding go
away.

## 4. Sprites on: one addend on every step

With §3, the steps that still differ with sprites on all need the **same**
extra amount. One rule: **sprite rendering adds a constant to every
command-engine delay.** That forces LMMM source→dest to **32** and HMMM/LMMM
newline to **128**, matching the other two modes. It is not a slot-position
effect.

The constant is **1** or **2**; nothing separates them (§3), and `fit_2026.cc`
and openMSX use **1**. Note that the CPU path's own sprites-on excess (§11.2)
is a separate, independently measured **+2** in `δ`; the two are not evidence
for each other.

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

| Command | Mid-line | `L` | Newline |
|---------|----------|-----|---------|
| **HMMV** | P=**46** | **58** | **104** |
| **LMMV** | Pw=**24**, Pr=**72** | **58** | **130** |
| **YMMM** | Pw=**24**, Pr=**36** | **68** | **104** |
| **HMMM** | Pw=**24**, Pr=**60** | **68** | **128** |
| **LMMM** | Prs=**32**, Prd=**24**, Pwd=**60** | **68** | **128** |
| **LINE** | Pw=**24**, Pr=**84** | **36** | **120** |

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
the rest still land on the same slot.

Those 33 are **RAS 188 → RAS 342** (wall Δ = 154). Slot **316** is at exactly
+128 and is skipped. 18 HMMM + 15 LMMM, 28 files — those files hold exactly 33
`188 → 342` transitions and no other transition out of 188 fails. Mid-line
`W → R` from the same slot 188 takes 252 (57 times) and matches. 188 and 316
are both packed. Wraps that **do** use +128 start from pair-offset 0, 32, 64
or 96.

### 6.1 The +1 is only observable at two `Δ` values

The +1 changes the chosen slot only when some slot sits at *exactly*
engine-distance `Δ` from the packed start. Over the 25 packed slots, each
tested in 3 successive lines so the wrap cases are covered (75 starts):

| `Δ` | wait | packed starts with an exact hit |
|--|--|--|
| 24 | LMMV/HMMM/LMMM `Pw`, LMMM `Prd`, LINE `Pw` | 0 / 75 |
| **32** | **LMMM `Prs`** | **48 / 75** |
| 36 | YMMM `Pr` | 0 / 75 |
| 46 | HMMV `P` | 0 / 75 |
| 60 | HMMM `Pr`, LMMM `Pwd` | 0 / 75 |
| 72 | LMMV `Pr` | 0 / 75 |
| 84 | LINE `Pr` | 0 / 75 |
| 104 | HMMV / YMMM newline | 0 / 75 |
| 120 | LINE newline | 0 / 75 |
| **128** | **HMMM / LMMM newline** | **66 / 75** |
| 130 | LMMV newline | 0 / 75 |

So the whole rule is testable at two places only: the HMMM/LMMM newline
(`Δ=128`, where it was found) and the LMMM source→dest read (`Δ=32`, which
needs CPU contention to be reached at all — see §10.3). Everywhere else it is
silent, which is why a fit that never reaches those two is indifferent to it.

2013 `8.final-analysis` has only HMMV (`Δ=46`, newline 104), both in the
"0 / 75" rows, so 2013 cannot see the +1 at all — it is **unobservable**
there, not merely untested. 2013 `nocpu` is 3965 / 3965 either way.

## 7. Trace inventory notes

Command-only files that must be excluded (`wrong`; they contain CPU `R.r` /
`W.w`): `scr5-wrong-dispOff-hmmv-noCpu-nx2-6d.txt`,
`scr5-wrong-dispOff-ymmm-noCpu-nx12-1d.txt`,
`scr5-wrong-sprOff-lmmm-noCpu-nx2-3d.txt`.

**Misnamed, not usable as command-only:** `scr5-sprOn-hmmm-rdCpu-1.txt` and
`scr5-sprOn-hmmm-rdCpu222-3e.txt` are labelled `rdCpu` but hold only `R.s` /
`W.d` — no CPU VRAM. Flattening parallel command columns there invents fake
W→R pairs.

**VCD aliases:** `scr5-dispOff-hmmv-rdCpu222-7e` / `8e` were first saved as
sprites-off, so `1.vcd` / `3.time` still call them
`scr5-sprOff-hmmv-rdCpu222-3e` / `4e`. Display was off; pair those VCDs with
the dispOff `.txt`.

Some LMMM `*nx*-*d.txt` traces use `W..` instead of `W.d` where source and
dest overlap. The cadence is still `R, R, W`; those files are kept.

`retag.py` reconstructs tags from the address structure when a rectangle
leaves its designated VRAM window: a CPU read auto-increments, so CPU reads
form one strict `+1` chain; an LMMM dest read reads the address it then
writes; anything else read is a source read. `retag.py check` should report
zero corrections on the current corpus.

---

# II. CPU accesses and arbitration

CPU VRAM uses a **subset** of the same command slots. The two machines differ
in how the request time `T` is observed, not in the VDP arbiter (as far as
2013 can tell).

## 8. Working model

```
LINE = 1368
NEED = 16          # memory cycles of lookahead (§8.2)
BUSY = 2           # ignore CPU posts in [RAS, RAS+2)

command_slots(mode) = bitmap table (RAS)
cpu_slots(mode)     = command_slots minus packed +6     # §2.1; empty unless sprOff

engine_dist(t, s):                    # s > t
    (s - t) minus padding in (t, s]   # §3; same function as the command engine

first_cpu_slot(T):
    earliest S in cpu_slots (repeating every LINE)
    with S ≥ T and engine_dist(T, S) ≥ NEED
```

`T` is the time the **arbiter** sees the request, which is not when the Z80
executes the I/O: there is a constant pin-to-register delay, `δ = 11` (13 with
sprites on), from the **rising** edge of `/CSx`. Total lead from that edge to
the granted slot is `δ + NEED` = **27** cycles; the pins alone only bound it,
at 26.19 ± 0.38 memory cycles over 142 display-off slots, and cannot split it
into its two parts (§8.1, §8.2).

**CPU path — one pending buffer.** A port access that arrives while the
register is occupied is **lost**: the old request keeps its scheduled slot and
the VRAM pointer does not advance. Cancelling an already-granted slot and
rescheduling is not what hardware does.

```
occupied = false
sched    = none
last_RAS = none

on CPU post at T, or when time reaches sched:
    if not occupied and last_RAS exists and T < last_RAS + BUSY:
        ignore post                    # not a new request (through CAS)
        return

    if occupied and (no post yet, or sched ≤ T):
        fire CPU access at sched
        last_RAS = sched
        occupied = false
        sched = none
        # then handle T if it is still pending

    if not occupied:
        occupied = true
        sched = first_cpu_slot(T)      # slot is fixed at this moment
    else:
        drop the new post              # lost request; keep sched
```

Three parts of that are separately measured:

- **The booked slot never moves.** On 2013 keep-slot and drop-new are
  indistinguishable; on 2026 sprites-on, drop-new matches the extra `/CSx`
  pulses, while an overwrite that *cancels* a granted slot scores far worse.
- **`BUSY = 2`** is a dead window after a CPU RAS. "Ignore posts in
  `[RAS, RAS+2)`" and "the register frees ~9 cycles before the access" are the
  same statement from two origins ~11 cycles apart (arbiter vs pin): a port
  access at pin time `P` is lost iff `P < RAS−9`, iff `(P+11) < RAS+2`. Use
  `BUSY=2` with arbiter `T`.
- **Losses are real and common.** 2026 sprites-on with a 12 T `IN` loop shows
  **~8% more `/CSx` pulses than VRAM accesses** — slots up to ~70 cycles apart
  against a ~71-cycle request period; predicted losses ~314 against ~326
  observed. Display-off loses essentially none, and the 37 T loop loses
  exactly zero (§11.3).

**Command path** — separate buffer, never drops. After an engine access at
`t`:

```
Δ = §5 wait for that step
if sprites on:     Δ += 1              # §4
if t is packed +6: Δ += 1              # §6; sprites-off only
take earliest S in command_slots (including packed +6)
    with S > t, engine_dist(t, S) ≥ Δ,
    and S is not a fired CPU RAS
    and S is not a dummy R.. on packed +6     # observed tag, or D6 (§10.2) if T known
```

That is the whole engine step; the packed candidate needs no further test. If
the command loses a slot it takes the **next** slot; it does not re-arm the
full `Δ` (re-arming scores ~25% wrong on mixed traces).

**Arbitration** — deliberately lopsided:

- A running command **never delays the CPU**: `first_cpu_slot` does not look
  at the engine at all. Verified on both machines.
- The **CPU delays the command**: the engine skips any slot the CPU actually
  fired on, and takes the next slot rather than re-arming.
- CPU RAS and command RAS never coincide, and the CPU wins every contested
  CPU-legal slot — because it was booked ahead from `T` while the engine
  decides late.
- The CPU never fires on packed +6. If the CPU path wins one, that cycle is a
  dummy read and the CPU is served at the next CPU-legal slot (§10).

### 8.1 The lead splits into a wall-clock constant and a memory-cycle lookahead

A pin-to-register delay is wall-clock time and cannot see a sequencer stall; a
lookahead counted by the sequencer must see it in full. So `δ` does **not**
see padding and `NEED` sees it exactly as the command engine does.

This is testable, and it is the reason not to glue the two into a single ~27
cycle lead: a glued lead forces a CPU-only "absorbs part of the stretch" rule,
which is not a mechanism (and 1, 2 and 3 cycles all work, so it is not even
unique). 2013 is 0 / 1141 with the split and fails when glued; 2026 prefers
the split, 129 against 152 extra+miss. The difference is visible only on
requests that straddle padding.

### 8.2 NEED = 16, measured from both machines

**2013** fixes the sum. Scanning lookahead `L` and dead window `D` over all 17
CPU captures: every `(L, D)` with **`L + D = 18` and `L ≤ 16`** is 0 / 1141,
and everything else fails (`L = 17` gives 10 wrong; `L = 16` with `D = 1` or
`3` gives 8 and 2). So 2013 gives a ceiling of 16 on the lookahead.

**2026 gives the floor.** Because `T = floor(t2) + δ` is an integer shift,
holding the sum `δ + NEED` fixed makes predictions differ *only* where padding
falls inside the lookahead window — which is exactly the discriminating
experiment. Over the phase-scan corpus (§11.2), errors = extra + miss:

| δ / NEED | dispOff (9529) | sprOff (7926) | sprOn (6535) |
|--|--|--|--|
| 15 / 14 | — | — | 103 |
| 14 / 15 | — | — | 74 |
| 13 / 14 | 14 | 47 | 103 |
| 12 / 15 | 14 | 44 | 74 |
| **11 / 16** | **8** | **44** | **70** |
| 10 / 17 | 8 | 50 | 96 |
| 9 / 18 | 8 | 60 | 129 |

(dispOff and sprOff at sum 27, sprOn at sum 29.) **NEED = 16 minimises the
error in all three modes**, decisively in sprites-on: 70 against 129 at 18, on
differences that by construction come only from requests whose window contains
the padding at 1330. Ceiling from 2013 plus floor from 2026 is `L = 16`
exactly, so openMSX's `Delta::D16` is the measured value as a lookahead
*length*.

What this does **not** explain is why sprites-on needs `δ = 13` where the
other two modes need 11: the scan excludes `NEED` as the home of that +2,
leaving the pin/arbiter path or the absolute position of `SLOTS_SPRON`.

### 8.3 openMSX starts counting ~29 cycles too early

openMSX stamps the port access at T-state 9 of `IN A,(n)` / `OUT (n),A`
(`CC_IN_A_N_2` / `CC_OUT_N_A_2` = `5+3+1`, against `CC_IN_A_N` = `5+3+4`),
i.e. at the **start of T2** of the I/O machine cycle.
`VDP::scheduleCpuVramAccess()` then applies `VDPAccessSlots::Delta::D16` from
that time (`fixedVDPIOdelayCycles` is only the T9769/S1990 extra, not a V99x8
constant). D16 there is also wall-clock ≥ 16, not engine-distance.

`/IORQ` falls half a T-state into T2 and rises at the end of T3, so the pulse
is 2.5 T = **14.90** cycles — which is what the analyzer measures
(14.9 ± 0.1, §13), and that agreement is the check that the stamp really is
the start of T2. Stamp to **rising** edge is therefore a full **3 T = 17.88**
cycles.

So from openMSX's own timestamp the hardware slot is `17.9 + 11 + 16` ≈ **45**
cycles later, and granting at D16 is about **29** cycles too early.

Why CPU-only tests rarely show it: the lost-request threshold almost cancels.
Hardware drops a new post arriving more than ~9 cycles before the pending RAS
in pin time, i.e. ~27 cycles before it in openMSX-stamp time; openMSX drops
the new I/O if it arrives before its already-scheduled (29-early) access. Same
threshold to within a couple of cycles.

What does **not** cancel is which slot the CPU takes away from the command
engine. Display-off command slots are often 8 apart, so 29 cycles is typically
three or four slots early; sprites-on gaps are 32–64, so usually one whole slot
early. That is the `vdpcmdx` `+CPU` column.

Proposed (not implemented): a **17.9 + 11 ≈ 29-cycle wall-clock** constant
from the port-access timestamp, followed by the 16-**memory-cycle**,
padding-aware lookahead. Checks: `vdpcmdx` `+CPU` before and after, and the
2026 CPU captures driven from the `/CSx` falling edge (this file's fitter uses
rising).

## 9. 2013 CPU (NMS 8250)

CPU and VDP share a crystal, so the Z80 I/O train is an exact VDP-cycle
lattice. The only unknown is the phase `φ0` of that lattice relative to RAS
(one integer in `0 … 3059` per capture). `φ0` *is* arbiter time: there is no
`/CSx` in these captures.

CPU posts: 40 × `IN A,(n)` / `OUT (n),A` at **72** VDP cycles, then **252** to
the next burst. Period **3060**. The value 72 never appears as a VRAM gap; it
is a Z80 fact.

Data: every `8.final-analysis/*cpu{read,write}*.txt` except `*nocpu*`. Tags
`R.c` / `W.c` are CPU VRAM; `W.e` is HMMV.

All CPU RAS sit on **CPU-legal** slots: **1141 / 1141**, including **0 / 204**
in sprites-off on packed +6. Mixed HMMV engine writes **do** use packed +6
(screen-8 sprites-off: 24 / 85). CPU and command never share a RAS.

What the 10 `*nocmd*cpu*` files pin down (six-way plateau, all 10/10):

- Packed +6 must **not** be in `first_cpu_slot`.
- The 16-cycle check is **engine-distance** (padding subtracted). Wall-clock
  tops out at 9/10.
- After a CPU RAS there is a **2-cycle holdoff**, or an equivalent
  `NEED + BUSY` pair with sum 18 and NEED ≤ 16 (§8.2).
- If a new request arrives while one is pending, the **scheduled slot does not
  move**; reschedule is not on the plateau.

Same CPU parameters with the command ignored: **7 / 7 mixed files at 100%** —
a running HMMV never steals or delays a CPU slot. Skipping occupied CPU RAS
(predicted from `φ0`, or oracle — they agree) gives **17 / 17** files on every
CPU RAS and **7 / 7** on mixed HMMV.

Documented dummy reads in blanking (dispOff 1236/1244/1252/1260, sprOff
1242/1250/1258 in CAS-ish published tables) are **outside** the CPU/command
table. They are not the sprites-off packed dummy of §10.

## 10. The packed +6 dummy read (2026 sprites-off)

The one genuinely odd behaviour, and the only place where the CPU affects
which slot the *command* gets in a way not covered by "skip a fired CPU RAS".

Occupancy over 17 stop + 99 mixed files, CPU ∩ command RAS = **0**:

| | CPU-legal | packed +6 | off-table |
|--|-----------|-----------|-----------|
| CPU RAS | **12846 / 12846** | 0 | 0 |
| Dummy `R..` | 0 | **333 / 333** (sprites-off only) | 0 |
| Command | dispOff/sprOn: all legal | sprOff: **1436 / 4608** packed | 0 |

What it costs the engine model to get this wrong, over the 139 mixed files
(20537 steps):

| engine occupancy | files perfect | steps |
|--|--|--|
| CPU RAS + observed dummy `R..` | **139 / 139** | **20537 / 20537** |
| CPU RAS + D6 (no dummy tag) | 116 / 139 | 20438 / 20537 |
| CPU RAS only | 113 / 139 | 20198 / 20537 |

### 10.1 What the dummy is

Every unclassified `R.. 0x1FFFF` that is not part of the blanking four-slot
block sits on one of the 25 packed slots. The next CPU-legal slot is +26 (323
cases) or +54 (10); that slot **is** a CPU RAS in 332 / 333. `C−6` is empty or
a command, never CPU.

So the VDP **does** grant the packed slot to the CPU path, that memory cycle
cannot carry a CPU access, it strobes `0x1FFFF`, and the CPU is served one
slot later. Rate is the same at 12 T and 37 T (~7% of sprites-off CPU RAS):
crowding the request stream does not create dummies.

It is not simply "any CPU request near a packed slot". Stop traces hold 326
CPU RAS whose table predecessor is a packed `S−26`, and only **42** of them
produce a dummy.

### 10.2 D6: the packed slot is decided ~2 cycles earlier

On the full command table, `NEED = 16` grants packed `C` for arming times
`T − C ∈ {−21 … −16}` while the CPU-legal table already grants `C+26`.
Hardware is stricter on packed:

| | `T − C` |
|--|--|
| dummy (mixed and stop) | **−21, −20, −19**, a few at −18 |
| packed **hit** with CPU at `C+26` | starts at **−18** |
| stop, empty packed slot with CPU at `C+26` | starts at **−18**, never −21…−19 |

Cutting at `−22 < T−C ≤ −19` gives dummy **29**, empty **0** on the stop set.
That cut is exactly `first_slot` on the full table with **NEED = 19**, i.e.
three cycles more than the CPU-legal table needs.

**D6** (needs arming `T`, which an emulator has exactly): skip packed `C` if
the CPU RAS is at `C+26` and `T − C ∈ (−22, −19]`.

Same fact from the pin: display-off lead 26.19 ± 0.38 cycles, packed-pair
slots 28.01 ± 1.41. A uniform 26-cycle lead would predict **472** dummies
against 335 observed; the extra ~2 cycles predicts **336**.

The exact extra is between 2 and 3 and this corpus cannot close it: the
boundary is one cycle wide. Observed dummies have
`T − C ∈ {−21, −20, −19, −18}` = 12 / 16 / 12 / 4 and non-dummy packed
predecessors `{−18, −17, −16, …}` = 5 / 11 / 18 / …, so a cut at `NEED+3`
misses the four dummies at −18 and a cut at `NEED+2` invents five dummies for
the hits at −18. Independent ±1 jitter on each `T` does not turn `NEED = 16`
into that cut; the async jitter is the same size as the effect. Note also that
the numeric value depends on the request-time convention: `floor(t) + 11` and
`t + 9.86` differ by ~1 and move the threshold by ~1.

Scores with D6 on mixed files (84 files, 12544 engine RAS): D6 is **78/84**
files and **12536** steps, against **84/84** for occupying the observed `R..`
tag — an oracle, not a rule. Including `rdCpu222` (121 files, 18076 steps):
oracle **121/121**, D6 **105/121**.

### 10.3 The `Δ=32` skip is lattice geometry, not a CPU predicate

`scr5-sprOff-{ymmm,hmmm,lmmm}-rdCpu-nx4-{1..6}f`. Screen 5 has
`PIXELS_PER_BYTE = 2` and `clipNX_2_byte` halves NX, so NX=4 is **2 bytes per
dest line**: every second command access is a line break. That turns the two
rare geometries of §6.1 into bulk statistics — 152 `Δ=32` steps landing on a
packed candidate and 45 packed-to-packed line breaks.

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

This is why no engine rule references the future CPU access. Earlier
CPU-predicate rules ("skip an idle packed candidate if no CPU RAS yet",
"skip the packed landing at a newline") were proxies for this lattice fact and
for the distance-64 row, where the CPU had taken the pair-start and pushed `C`
one pair further. `exp4.py` with the waits of §5, engine-distance,
unconditional packed-start +1, and occupancy = observed CPU RAS + observed
`R..` predicts these 18 files at **2440 / 2440 steps, 18 / 18 files**, and on
the whole mixed corpus that model is **139 / 139 files, 20537 / 20537 steps**.

The engine therefore needs only: (a) the waits of §5, (b) padding-aware
engine-distance (§3), (c) the sprites-on addend (§4), (d) packed-start +1
(§6), and (e) skip slots another master actually took. That is implementable —
openMSX cannot look ahead at the CPU, and does not have to.

## 11. 2026 CPU request time

**Do not** fit a 72/252 lattice on the 8280 VDP timeline. Intra-burst `/CSR`
gaps are **71 or 72** (typically alternating; same-gap doubles when the
fraction wraps). Independent ±1 on neighbouring posts would produce 70/73, and
those almost never happen: the uncertainty is **where** the extra or missing
cycle sits, one analog phase for the burst.

Request time comes from `/CSR` (reads) or `/CSW` (writes), converted onto the
VDP clock by refresh interpolation. A pace fit — one straight line through the
capture, i.e. the Z80 crystal — has a residual of ~0.15 cycle. `T = floor(t2)
+ δ` with integer `δ` preserves all gaps; `T = floor(t2 + ε)` slides the 71/72
doubles and is the right *noise model*, but it is not better as a fit.

Scoring convention: a **D16 tie** counts as a hit, i.e. when
`first_cpu_slot(T±1)` differs from `first_cpu_slot(T)` either outcome is
allowed, because the VDP samples `/CSx` on a 1-cycle grid (rounding to 1 cycle
beats 2 or 4). The simulator still emits `first_cpu_slot(T)`. That is not an
oracle.

### 11.1 `/CSx` decoder rules

- Use **rising** edges (`0→1`), and only where the previous sample was
  recorded low. A pulse already low at t = 0 is still a request — the rising
  edge is the end of the I/O cycle, which is when the VDP schedules — so do
  not synthesize a falling edge at t = 0 (`fit_2026.cc` starts `/CSx`
  unknown). 26 of 180 CPU captures start that way.
- Drop **either level** shorter than 8 analyzer samples: a real I/O holds
  `/CSx` low for ~56 samples and high for the rest of a loop iteration, never
  shorter than 250, so nothing real lasts one or two samples. Both
  transitions of such a pair are dropped, which restores the level around the
  spike and rejoins a pulse a spike had split in two (`drop_glitches`; §11.8).
  Then drop edges closer together than 20 VDP cycles (analyzer ringing at the
  ~15-cycle pulse width).
- Do **not** invent posts. A capture can start with a request already in
  flight, with no `/CSx` for the first RAS (§11.4). Same policy as command
  traces that start mid-command: do not score a RAS whose complete `/CSx` was
  never recorded, and do not add a per-capture initial-state knob.
- A leading `/CSx` pulse closer to the next one than one loop instruction is
  setup code, not a loop `IN`/`OUT`.

### 11.2 δ is one integer per mode

Three phase-scan experiments (`3f8354d`):

| set | files | program | question |
|-----|-------|---------|----------|
| `scr5-{mode}-stop-rdCpu132`, `scr5-sprOn-stop-wrCpu132` | 38 | `IN A,(#98) ; NOP ; NOP` | fine phase scan across the whole line |
| `scr5-{mode}-stop-rdwrCpu` | 18 | `IN A,(#98) ; OUT (#98),A` | is the write pin delay the read pin delay? |
| `scr5-{mode}-stop-rdCpu-adjust{0,2,…,14}` | 48 | 16 T read loop | does R#18 change CPU arbitration? |

Measured median request pace, from the `/CSx` rising edges on the interpolated
VDP clock (`--trellis` prints it per capture):

| family | pace | T-states |
|--------|------|----------|
| `stop-{rd,wr,rdwr}Cpu` | **71.5** | 12 |
| `stop-{rd,wr}Cpu132` | **131.2** | 22 |
| `stop-rdCpu222` | **220.6** | 37 |

The pace is what makes these captures useful, and it is not a round number of
VDP cycles: 131.2 against the 128-cycle refresh cell creeps 3.2 cycles per
request, and against the 8-cycle slot lattice of display-off it creeps 3.2 out
of 8, so the request phase walks the whole slot gap every ~2.5 requests and a
62-request capture covers it many times over. The 12 T loops instead pin the
occupancy rules, because only there does a request arrive while the arbiter is
still busy.

Forcing a single δ across **all** `stop` CPU captures
(`--rw --sel=… --force=D`):

| mode | δ=10 | δ=11 | δ=12 | δ=13 | δ=14 | accesses |
|------|------|------|------|------|------|----------|
| display off | 4866 | **4886** | 4848 | 4297 | 3799 | 4888 |
| sprites off | 1725 | **1733** | 1730 | 1652 | 1583 | 1741 |
| sprites on | 2882 | 2959 | 3031 | **3036** | 3022 | 3050 |

**δ = 11 for display-off and sprites-off, δ = 13 with sprites on.**
Display-off is sharply peaked — one cycle either way costs 20 to 40 accesses —
and the new scan files alone are exact: 368/368 display-off and 374/374
sprites-off at δ = 11, with the per-file plateaus intersecting on exactly
{11}.

**The sprites-on +2 is uniform along the line.** Held at δ = 11, the
sprites-on failures scatter over **18 of the 31 slot rows**, with RAS 348
accounting for 1 of 58 — a flat shift of the whole request path, not a
per-slot correction. It is not separable from a +2 shift of `SLOTS_SPRON`
itself: the engine rule only constrains *differences* between successive
engine accesses, so a uniform shift of the sprites-on table is invisible there
and visible only here. §8.2 excludes `NEED` as its home. This is the loosest
number in the model.

**Reads and writes share one pin delay.** The interleaved captures are the
only ones carrying both edge trains at once, so they are the only ones that
can separate the two. Fitting `δ_r` and `δ_w` independently (`--rw`) over the
17 scoreable files: `δ_w − δ_r` is **0 in 8 files, ±1 in 6, +2 in 1**, and
forcing them equal costs nothing. The nine `wrCpu132` files add a sprites-on
CPU *write* phase scan and peak at δ = 12 (557/559, against 552 at 11 and 556
at 13 — flat), so the sprites-on offset is not an artefact of the read-ahead
path.

**Set-adjust does not touch CPU arbitration.** The best-fit δ is identical at
every R#18 from 0 to 14 (10–11 display-off, 12–14 sprites-on): R#18 does not
move the lattice the CPU sees. Holding δ = 11 across the eight display-off
adjust values gives **1436 / 1440** correct outside HBLANK and 134 / 178
inside it (rows 1268–1367), and every deviation is an access landing on the
shifted HBLANK slots that §3.2 measured from the `noCpu` engine captures. At
R#18 = 2 the observed rows are 1333 and 1343, exactly where §3.2 puts them. So
engine and CPU share one adjust-shifted lattice, and a *misplaced* padding
block only breaks the few HBLANK slots while leaving δ — a wall-clock delay —
alone.

Note that the 48 `adjust` captures exist only in `part2/1.vcd/rw/`; they were
run through `process.cc` into a scratch directory. For a `stop` capture
`3.time` is enough, since there are no display fetches for the 4→5 `grep` to
remove.

An independent pace fit (one real-valued constant per capture instead of one
integer per mode) agrees and adds the loss statistics:

| pace | mode | accesses | not predicted | lost requests | pin constant |
|------|------|----------|---------------|---------------|--------------|
| 72 | display off | 4793 | 43 (0.90%) | 0 of 4848 | 9.85 ± 0.12 |
| 72 | sprites off | 4346 | 16 (0.37%) | 12 of 4385 | 9.84 ± 0.11 |
| 72 | sprites on | 3703 | 59 (1.59%) | 304 of 4022 | 12.10 ± 2.01 |
| **222** | display off | 675 | **1** (0.15%) | 0 of 685 | 9.75 ± 0.22 |
| **222** | sprites off | 608 | **0** | 0 of 610 | 9.72 ± 0.30 |
| **222** | sprites on | 641 | **2** (0.31%) | 0 of 646 | 10.61 ± 1.18 |

Sprites-on is not a harder mode; it is the mode where requests are lost, and
each loss blurs a continuous fit. The 26 display-off `stop-{rd,wr}Cpu-*f`
captures put **56** CPU accesses on the three padded HBLANK slots (CAS
1326 / 1336 / 1345), up from 6, and are perfect.

### 11.3 The sparse set (`rdCpu222`)

20 × `IN` with `EX (SP),HL ; NOP` between them, then a longer wrap. Same
machine, same slot tables, same model.

| | T-states | ×6 (sync 8250) | × 5.96112 (8280) |
|--|----------|----------------|------------------|
| IN → IN | 37 | 222 | **220.56** |
| burst wrap (20 INs) | 67 | 402 | **399.40** |

Gaps in the VCD are 220/221 and 399/400. `first_cpu_slot` waits at most
**59 / 75 / 85** cycles (dispOff / sprOff / sprOn), far below 220, so the
buffer is always empty: **sim-lost = 0**, which is what confirms that the
extra sprites-on `/CSx` pulses at 12 T are occupancy and not a second mux.
`BUSY=2` is not tested by this program. The 37 T fit also reproduces the
clock ratio to 1 part in 10⁵, independently of the 12 T programs.

Do **not** change NEED, BUSY or the slot tables to fit a leftover first RAS on
this set: analog `T` for interior I/Os sits inside the `NEED = 16` grant window
(or one cycle off, a D16 tie), and a "skip first slot" rule fitted to
START ±8/±32/±64 breaks the clean files.

### 11.4 The residue

Current CPU totals (`fit_2026.cc`, integer δ per mode):

| | files perfect | CPU RAS |
|--|--|--|
| all | **202 / 266** | **23914 / 23990 (99.7%)** extra 17 miss 76 |
| display off | 98 / 100 | 9527 / 9529 (100.0%) |
| sprites off | 56 / 83 | 7899 / 7926 (99.7%) |
| sprites on | 48 / 83 | 6488 / 6535 (99.3%) |
| `stop` only | 95 / 115 | 9656 / 9679 (99.8%) extra 4 miss 23 |

These are lower than they used to be on purpose. The pipeline no longer feeds
the model the acquisition spike at the start of each capture, and this model has
no way to represent a request that arrived before the window, so that spike had
been standing in for one. The trellis, which represents it explicitly, gains
what this loses (§11.5). Compare with `--nopulsefix`: 220 / 266, miss 59.

**Most of it is the first access of a capture**, where a request may already
have been pending before the window opened — the simulator starts with an
empty register. At the per-mode δ of §11.2:

| mode | files | miss | of which the 1st access of the capture |
|--|--|--|--|
| display off | 100 | 6 | **4** |
| sprites off | 83 | 30 | **16** |
| sprites on | 83 | 44 | **14** |
| | 266 | 80 | **34** |

First accesses are 266 of 23990 (1.1%) but **34 of 80 misses (43%)** — about
40× the rate of the rest. Scoring from the second access leaves **46 of 23724
(0.19%)**. Printed by `--rw --force=D`.

The rest is the asynchronous-clock floor. It scales with how sparse the slot
table is (0.0% / 0.4% / 1.3% for dispOff / sprOff / sprOn), 28% of accesses
sit within one cycle of a slot-decision boundary, the failures are isolated
single slots, and 2013 with a shared crystal is 1141/1141.

### 11.5 Exact reconstruction: the pin delay is a real number

§11.2 fits one *integer* δ per mode and writes the leftovers off as the
asynchronous-clock floor. That is avoidable. The pin delay is physically one
real constant, and the two things §11.4 blames the residue on — the exact
instant the VDP acts on `/CSx`, and a request already pending when the window
opened — are latent variables, not noise. Both can be reconstructed.

Model, per capture (`fit_2026.cc --trellis`):

```
T_i = floor(t2_i + e_i + phi)     one real phi shared by every edge
                                  |e_i| <= eps per edge
```

plus, optionally, one request already pending at capture start — which must
itself be a request the arbiter could have taken: an arming cycle that
schedules onto the first observed slot *and* precedes the first recorded edge.
Without that second condition it degenerates into a free grant, and 8 captures
"reconstruct" on the strength of it. `phi` replaces
`δ`; `eps` stands for what the measurement cannot resolve — analyzer
quantisation (80 MHz against 21.477 MHz, so **one sample = 0.268 VDP cycles**),
the residual error of the refresh interpolation, the VDP's own input setup
window, and the slow drift between the two crystals. A tolerance of about a
sample is measurement, not model error.

Two properties make this searchable rather than a fit. First, the candidate
cycle set of each edge only changes when some `t2_i + phi ± eps` crosses an
integer, so the `phi` axis is cut into finitely many cells on which the
answer is constant: the scan over all real delays is exact, not a grid. Second,
inside a cell the arbiter is a small state machine — one scheduled slot, one
last-served slot, and how many grants have been matched — so a DP over the
edges finds the cheapest assignment reproducing the observed grant sequence
*exactly*, or proves none exists. Every solution is then replayed through an
independently written forward simulation (`queue_sim`) and required to return
the `.txt` grant list; the DP and the simulator do not share code.

On the 131-cycle family (`stop-{rd,wr}Cpu132`, 28 captures, 1733 accesses):

| | result |
|--|--|
| captures reconstructed exactly | **27 / 28** |
| of those, at `eps` = 0 and no pending request | 21 |
| needed a pending request at capture start | 2 |
| one single `phi` for the 27 | `phi` ∈ [10.693, 11.444), `eps` = 1.86 samples |
| **sprites on alone** | **16 / 16 at `eps` = 0, `phi` ∈ [10.940, 10.998)** |
| display off alone | 6 / 6 at `eps` = 1.16 samples, `phi` ∈ [10.881, 10.899) |

The sprites-on row is the strongest single result in this file: one real number,
no tolerance at all, 16 captures and 995 grants. The one failure,
`sprOff-stop-rdCpu132-4g`, scores 62/62 under §11.2 — see below.

That last point needs the sprites-on lookahead to be `NEED + 2`
(`--needoff=0,0,2`). `phi` and `NEED` trade off exactly — raising `NEED` by 2
lowers every `phi` band by 2 — so no capture can separate them; what the
captures do settle is that the two must differ by 2 between sprites on and
sprites off. A pin delay cannot depend on the display mode, so the 2 belongs
to the arbiter's deadline (or to the length of a sprite-mode memory cycle),
not to the pin. The evidence is the *shared*-`phi` coverage, not the per-file
fits, which absorb the offset into `phi` and so cannot tell the difference:

| sprites-on lookahead | captures reconstructed | one `phi` fits |
|--|--|--|
| `NEED` | 102 / 115 | **73 / 115** |
| `NEED + 2` | 101 / 115 | **98 / 115** |
| `NEED + 2`, row 170 at `NEED` | 102 / 115 | **100 / 115** |

The third row is a lead, not a result: slot row 170 is the one sprites-on slot
that sits 8 cycles after its predecessor, and giving it the sprites-off
deadline buys two more captures. There are 19 row-170 accesses in the corpus,
so it is not a single grant, but it is not much either. Row 170 keeps showing
up as *observed but never predicted* in the per-row histograms of `--rw
--force=D`, which is the signature of a slot the model cannot reach at all.

**A single buffer with drop-new is confirmed, not assumed.** Queueing a second
request instead of discarding it (`--qdepth=2`) drops the corpus from 102 to
93 captures, and the failures move into the 22 T loops that depth 1 gets right.

Full `stop` corpus (115 captures, 9679 accesses), tolerance against coverage:

| `eps` (samples) | 0 | 0.23 | 0.47 | 0.70 | 0.93 | 1.16 | 1.86 |
|--|--|--|--|--|--|--|--|
| captures fitted by one `phi` | 56 | 68 | 78 | 84 | 88 | 91 | 100 |

`phi` stays in [10.92, 11.04] throughout, so **`phi` ≈ 10.95 ± 0.05** on the
pairwise timebase. On the fitted one (§11.9) the single value that covers the
most captures is **10.747**, and the shift is the timebase, not a new
measurement: the pairwise map was tilted segment by segment, and the mean tilt
had to be absorbed somewhere.

Over the whole CPU corpus (266 captures, 23990 accesses, `stop` and mixed):
**240 reconstruct exactly**, 231 share one `phi`, and **0 fail re-simulation**.
The trellis is stricter than §11.2 in two ways, which is why a capture can
score 100% there and fail here: it allows no D16 tie, and it insists every
recorded edge be accounted for — granted at an observed slot, granted outside
the `.txt` window, or dropped by a stated rule — where `make_posts` simply
discards edges whose grant would precede the window. `sprOff-stop-rdCpu132-4g`
fails on exactly that boundary effect.

Two files are written. **`trellis-2026.txt`** is the memo table of the search:
one row per capture per parameter set, holding the `phi` intervals for which an
exact reconstruction exists. It is the *answer* to the search, not the
reconstruction — at `eps` = 0 it is enough, because each interval is one cell
of the partition and every `phi` in it gives the same `T_i = floor(t2_i + phi)`,
but at `eps` > 0 it does not record which edges tipped.

**`trellis-requests-2026.txt`** is the reconstruction: for every capture that
reproduced its `.txt` exactly, each `/CSx` edge with the VDP cycle `T` on which
the arbiter saw it, the slot it was granted, and whether it was dropped, fell
outside the `.txt` window, or was the unrecorded pre-capture request. That is
the file to feed a reference test.

Both are keyed by the full parameter set (`eps`, pending request, `NEED`,
per-mode offsets, per-row overrides, queue depth), so re-runs are free
(0.2 s against 6.4 s) and changing a parameter invalidates only what it
should. The two runs above are

```
./fit_2026 --trellis --sel=stop-rdCpu132 --needoff=0,0,2 --needrow=170:16
./fit_2026 --trellis --sel=-stop-      --needoff=0,0,2 --needrow=170:16
```

The default remains the integer-δ model of §11.2; `--trellis` changes nothing
else in the program.

### 11.6 The `.txt` line origin is implicit

`parse_txt` reconstructs absolute time as `1368 * column + row`, so the scan
line origin is implicit in the column index and every consumer has to
re-derive it from the `.vcd` refresh bursts. Both `process.cc` (which wrote
the `.txt`) and `make_anchors` in `fit_2026.cc` do it the same way:
`find_refresh_starts` finds the first *group* of 8 refresh bursts, that group
start is assigned `r = 8` (= line 1, row 285), and earlier bursts are numbered
backwards. Because the scan starts at `i = 1` the very first burst can never
be a group start, so a capture beginning mid-line is normal (1 to 7 leading
bursts); but if more than 8 bursts precede the first detected start, the
back-counting runs past `r = 0` and the origin lands in line −1. Three files
are in that state and all three are consistent, because `process.cc` put them
there too and emitted negative row numbers.

Two programs deriving the same timebase from the same `.vcd` by two different
routes can disagree by a whole line, and a whole line is invisible in a 12 T
burst (1368 is 19.1 request periods) while being obvious in a 37 T loop (6.20
periods, a 45-cycle residue). `process.cc`'s route is authoritative because
`vcd2.cc` reads every signal; `fit_2026.cc`'s private decoder must stay a
faithful port of it — in particular `candidate_filter1` must require the
`vcd2.cc` type to be exactly `"R.."`, i.e. read, first CAS after RAS, and VDS
inactive. Without the VDS test a screen-5 bitmap fetch is indistinguishable
from a refresh, since the pitch is 128 bytes so an address ending in `0x3f`
occurs in the display fetches of every line. Of 887 rw captures, **522
contain at least one such impostor** (17681 accesses, ~31 per capture).

**Guard: `fit_2026.cc --origin`.** Counting refresh bursts is not a usable
test. The property that matters is whether the `.txt` and the `.vcd` agree, so
test that directly: score every CPU capture with the request axis shifted by
−1, 0 and +1 lines and complain if a non-zero shift wins.

```
$ ./fit_2026 --origin
  266 CPU captures, 0 with a line-origin mismatch
```

**The refresh index has to be timed, not counted.** `--origin` only tests the
*global* line offset, and it passed while the timebase was locally wrong.
`make_anchors` turned the list of detected refreshes into VDP times by
incrementing the refresh index once per detection, which assumes the detector
never misses one and never invents one. It does both — that is the impostor
problem above — and a single miss shifts every later anchor by a refresh
period. The damage is not a uniform offset: it warps `t2` locally, so
`debounce_t2` sees real `/CSx` edges only 17 cycles apart and **deletes them**.
Corpus-wide that silently removed **45 requests across 28 captures**.

The fix is to let elapsed wall time say how many refresh periods went by, since
the crystal frequency is known exactly (`VDP_PER_UNIT`, from `$timescale 100
ps` and 21.47727 MHz), and to drop an anchor whose implied step is zero. It is
on by default; `--noanchorfix` restores the counting version for comparison.

| | counted | timed |
|--|--|--|
| requests deleted by the debounce | 45 in 28 captures | 3 |
| CPU, `.txt` reproduced exactly (§11.2) | 218 / 266 files | **220 / 266** |
| CPU accesses | 23924 / 23990, miss 66 | **23931 / 23990, miss 59** |
| `stop` only | miss 20 | **miss 14** |

`sprOff-stop-rdCpu-1` is the clean example: it looked like a capture with
*fewer* `/CSR` edges (110) than CPU accesses (114), which no request model can
reconstruct, so it looked like a defective capture. It is not. The VCD holds
116 rising edges, 6 of them were being deleted by the warped timebase, and
with the anchors timed the capture scores **114 / 114**. Nothing is wrong with
the measurement. `--csdump` reports, per capture, how many edges survive each
filter and which anchor steps carry the wrong number of cycles.

**A second unit bug: the narrow-pulse filter never fired.**
`drop_narrow_pulses` is called with `CSX_MIN_SAMPLES` = 8, but its argument is
compared against VCD timestamps, which are in 100 ps units and 125 to the
sample. The threshold was therefore 0.8 ns, and since the narrowest real pulse
is one sample the filter never removed anything. Fixed; `--nopulsefix`
restores it.

What it should have been removing is an acquisition artifact, and the evidence
that it is an artifact and not a signal is worth recording, because the first
reading of it was wrong:

- The `/CSx` pulse width is **constant**. Over the `stop-rdCpu` captures, 8264
  of 8274 wide pulses are either 55 or 56 samples and nothing else, mean
  **55.68 samples = 696 ns = 2.491 Z80 T**.
- 620 of 635 narrow pulses corpus-wide are the **first** pulse of the file, at
  sample 2, five to twelve T-states ahead of the first full pulse. Only 15 sit
  adjacent to a wide pulse, which is the only real ringing.
- Counting wide pulses against the `.txt` settles it: **wide − accesses >= 0 in
  every capture**. The spike is never needed to account for a grant.

So the leading spike is the acquisition settling on its first samples. It was
nevertheless *load bearing*, because the integer model of §11.2 has no way to
represent a request that arrived before the window: the spurious edge sat a few
T ahead of the first real one and acted as a stand-in for the real pre-capture
request. Removing it therefore makes §11.2 look worse (220 → 202 files, miss
59 → 76) while making the trellis, which models that request explicitly, much
better (**229 → 240 of 266 exact**, failures 37 → 26). The residue moves onto
the first access, which is where it belongs.

**The pre-capture request does not have to be a free parameter** (`--prepace=K`).
Because the width is constant and the request loop runs at a fixed pace, the
requests that ran before the window sit at `t2[0] - k*pace`. That replaces
"one pending request on any reachable slot" — a window 31 cycles wide with
sprites off and 65 with sprites on, which is a lot of freedom — with a single
cycle.

| | free phantom | pinned to the pace |
|--|--|--|
| captures reconstructed exactly | **240 / 266** | 235 / 266 |
| **at `eps` = 0 with no free parameter** | 114 | **137** |

`K` = 1, 2 and 3 give identical results, which is an independent confirmation
of the single-entry buffer: only the most recent earlier request can matter.
The 16 captures that need the free version all have an anomalous short first
gap, so for those the request before the window was not one loop iteration
back.

**The sibling files: `5.slots/<capture>.cpureq`.** One per capture that
reconstructs exactly, 256 of them, so that a consumer never has to open a
`.vcd` again. The body is the integer request cycles, one per line, sorted;
`rdwrCpu` captures add an `r`/`w` tag because their train interleaves `/CSR`
and `/CSW`. The header carries the mode and the arbiter parameters, and it has
to: the pin delay and the lookahead trade off against each other, so the same
measurement yields different integers for a different `need`.

Comments carry no information: every field the model needs is on its own
keyword line, so a reader never has to parse a comment. `requests` comes last
and is followed by exactly that many body lines, each a cycle and, for an
`rdwrCpu` capture, an `r`/`w` suffix.

```
# scr5-dispOff-hmmm-rdCpu-3  CPU VRAM request cycles
capture scr5-dispOff-hmmm-rdCpu-3
mode dispOff
line 1368
need 16
need_row 170 16
margin 2
buffer 1
policy drop-new
kind r
requests 115
-88
-17
55
126
```

Requests that cause no visible access are **included and must be kept** — the
arbiter drops some, and some are granted outside the `.txt` window — but which
ones is derived by the model, not recorded. Cycles before the window are
negative, as in the example above; that happens in 26 of the 256.

`margin` is the whole drop rule (§11.7) and is `-1` in a sprites-on file, so
the keyword is not a duration and cannot be read as one. `busy` is still
accepted as a spelling of it, since that is what the earlier files called it.

Two things are deliberately not in these files. The command engine is not
needed at all: the CPU has priority and its slot choice comes only from the
mode's slot table (`make_wait(cpu_slots_of(CMD_TABLE[mode]), ...)`), which is
why the CPU fit behaves the same on `stop` captures and on captures with a
command running. Addresses are not derivable either; the `.txt` address column
needs the VRAM pointer model, not the timing model.

**Guard: `fit_2026.cc --fromreq`** replays the `.cpureq` files through the
arbiter and compares with the `.txt`, reading no `.vcd`:

```
$ ./fit_2026 --fromreq
  256/256 files reproduced their .txt exactly, accesses 22938/22938
```

It also prints the margin `T - S_prev` of every request it discards, split by
mode. That is the evidence behind §11.7, and it is a standing check on the
threshold: each listed value must lie below its mode's threshold, so a value
at or above it means the file and the model disagree.

Write them with `--trellis --reqfiles`.

**Guard: `fit_2026.cc --checkreq`** is the cheap independent check in the other
direction — is a `.cpureq` consistent with its `.vcd`? It deliberately cannot
build the file: it never chooses a slot, never runs the arbiter and never opens
the `.txt`. It needs no knowledge of the pin delay either. Since every cycle in
the file is `T_i = floor(t2_i + phi)` for one `phi` shared by the capture, the
differences `T_i - t2_i` must all fall inside a band of width 1, widened by the
per-edge tolerance; so match requests to `/CSx` rising edges in order and look
at the spread. Surplus requests are allowed only where the band puts their
pulse before the first recorded edge.

```
$ ./fit_2026 --checkreq
  256/256 plausible, 51 requests predate their capture, widest band 1.75
```

It tightens usefully — at the 240-file stage 240 / 224 / 195 / 158 files passed
at `--tol=` 0.5 / 0.25 / 0.1 / 0 — and it catches every corruption tried:

| corruption | caught by |
|--|--|
| one cycle moved by +3 | band 3.40 cycles (10.20 .. 13.60) |
| one cycle deleted | 116 requests against 117 edges |
| a body line that is not a cycle | parse |
| the `requests` line removed | parse |
| the cycle list of another capture, same length | band 180 cycles |

**The width is a sub-sample measurement, and it is now used.** With a constant
true width `W` = 55 + `f` samples and the rise detected at sample `R`, the true
rise sits at `R - 1 + v` and the sampled width is a hard one-bit measurement of
the phase `v`: 56 means `v <= f`, 55 means `v > f`. Fitting the phase from
cumulative T-states gives `f` = 0.70, matching both the 55.68-sample mean and
the 0.711 fraction of 56s. `rise_subsample` therefore moves each edge to the
midpoint of its surviving interval instead of leaving the whole sample to
`eps`.

Only the *difference* between the two cases carries information — half a
sample, whatever `f` is — so the two offsets are centred to leave the mean of
`t2` where it was. That is not cosmetic: the integer model of §11.2 adds a
whole-cycle `δ` and cannot absorb a common sub-sample shift, and with the
uncentred midpoints it lost a capture. Weighted by `f` and 1 − `f` the raw
midpoints average to exactly −1/2 sample for *any* `f`, so centring is just
adding 1/2. Centred, the correction improves both models: the integer model
202 → **203 of 266** (misses 76 → 75) and the trellis 120 → **125 captures at
`eps` = 0**, with the exact set and the failures unchanged.

**How much it is worth depends on the timebase, and the timebase has since been
fixed (§11.9).** `--widthcheck` measures the per-edge error without the
arbiter: inside a constant-pace run the true rise times are an arithmetic
progression, so a straight-line fit (both slope and offset free, so no
clock-ratio error can leak in) leaves only measurement error.

| rms residual, 24087 edges in 850 runs | one fitted line (§11.9) | pairwise interpolation |
|--|--|--|
| width correction applied | **0.240 samples** = 0.064 cycles | 0.590 samples |
| no correction | 0.286 samples | 0.609 samples |

Read the right-hand column first, because it is what the correction was
originally judged against, and it made the correction look nearly worthless: a
3% gain. It was not the correction that was weak. Pure quantisation on a known
sample index gives 1/√12 = 0.289 samples and the width bit should cut that to
`sqrt((f³ + (1-f)³)/12)` = 0.176, so at 0.609 the quantisation was only about
a fifth of the variance and the other four fifths had to be the timebase.

That is exactly what it was. With one fitted line the uncorrected residual is
**0.286 samples against a predicted 0.289** — the whole remaining error is the
analyzer's quantisation and nothing else — and the width bit then buys 16%
rather than 3%, taking it below the floor as it should. The earlier claim of
40% was wrong about the size but right about the mechanism; the 3% was right
about the size but only because a larger error was hiding underneath.

The practical consequence is worth stating plainly: **`T` cannot be sharpened
further from these files.** The per-edge error is now the quantisation of an
80 MHz sample clock, so the only ways left are a faster sample clock or a
different way of timing the request altogether (§11.7).

The sign of the correction was settled on the pairwise timebase and needs no
redoing: swapping the two cases scored 0.669 against 0.638 for making no
assignment at all, so a wrong assignment is worse than none by about the amount
a right one is better.

`--widthcheck` is decisive rather than heuristic about the axis as a whole, and
it gets both the direction and the size right. Run it whenever captures are
added, a `.txt` is edited by hand, or the VCD decoder is touched. A mis-anchored
capture also shows up as a best-fit `δ` outside the physical band (19, 27, 31
instead of 10–13), which is a second smell test.

Better still would be to stop re-deriving the axis: record it in the `.txt`
(one anchor pair, raw analyzer sample ↔ VDP cycle, or just the absolute cycle
of column 0), or trim every capture to start at a line boundary. The latter
would also remove the capture-start residue of §11.4.

Not a problem, for the record: 17 files have a different column count in
`4.manual-fix-timing` than in `5.slots`. The 4→5 step is a `grep` on row
numbers only (`filter-{dispOff,sprOff,sprOn}.sh`), so a trailing column whose
entries all sit on non-slot rows simply becomes empty. Column *positions* are
untouched.

### 11.7 The drop rule is one inequality, and its threshold depends on the mode

With a single buffer entry the arbiter's whole discard behaviour is

```
take the request iff  T - S_prev >= margin
```

where `S_prev` is the slot granted to the request before it. Waiting for a
pending grant and the holdoff after it are not two mechanisms but the two
halves of that one test: the old simulation retired a grant when `S_prev <= T`
and then compared `T - S_prev` against `BUSY`, and a request arriving before
`S_prev` fails the same inequality by a larger amount. Rewriting `queue_sim`
and the trellis transition in this form is behaviour-identical at `margin` =
`BUSY` (240 of 266 before and after, same 114 at `eps` = 0, same 171 drops) and
it makes the threshold a single number that can be swept.

Sweeping it is how the 26 remaining captures were diagnosed. `--faildiag` scans
the pin delay finely for a capture with no exact reconstruction, replays the
arbiter, and attributes every mismatch to the request responsible; `--faildump`
prints the attribution. **25 of the 26 predicted too few accesses and almost no
spurious ones**, so the fault was never in *which* slot a request took, only in
the decision to discard it. Every miss, with its margin:

| margin `T - S_prev` | −8 | −5 | −2 | −1 | 0 | 1 |
|--|--|--|--|--|--|--|
| misses | 1 | 1 | 5 | 20 | 18 | 9 |

52 of the 54 sit within two cycles of the threshold being demanded. Sweeping
the sprites-on threshold on its own (`--thresh=a,b,c`, per mode):

| sprites-on `margin` | 3 | 2 | 1 | 0 | −1 | −2 |
|--|--|--|--|--|--|--|
| captures reconstructed exactly | 229 | 242 | 249 | 254 | **256** | 248 |

Display off is insensitive — it discards 2 requests in the whole corpus — and
sprites off is pinned at 2 by three drops at a margin of exactly 1. So the two
values disagree, and **`--thresh=2,2,-1` is now the default**: it lifts 16 of
the 26 that were failing and breaks nothing.

Most of that gain needs **no new freedom at all**, which is the first thing to
check before believing a per-mode parameter. Moving one threshold shared by
all three modes gives 242 → 249 → 252 at 2 → 1 → 0; splitting it by mode is
worth the last 4. And the split is not a free choice either: letting each half
of the sprites-on captures pick its own value independently, they agree —
`stop` against command-running −1 and −1, read against write −1 and 0 (tied at
−1, 26 against 26), odd against even repeat −1 and −1. Five of those six
halves choose −1 unprompted. The per-capture freedom also went *down*, not up:
captures needing no tolerance at all rose from 148 to 163.

The confirmation is in the margins of the drops the model gets *right*, printed
by `--fromreq`. At `margin` = 2 the sprites-on drops included 4 at margin 0 and
18 at −1, in direct contradiction with the 27 misses at margins 0 and 1 in the
same mode; no threshold can satisfy both. At −1 the 256 sprites-on drops run
from −18 to **−2 with nothing above it**, so the threshold sits in a gap in the
evidence rather than in the middle of it. The sprites-off knife-edge remains
and is unexplained: three drops there really do occur at margin 1.

**The split between the pin delay and the lookahead is in the data, and
display off picks 16.** Only their sum is directly measured — a request seen
one cycle later and a deadline one cycle shorter predict the same slot
everywhere except across padding, where the lookahead window straddles a gap
that the wall clock does not see. Scanning `--need` with the trellis refitting
`phi` per capture, and counting captures reconstructed *exactly* rather than
accesses mispredicted:

| `NEED` (display off) | 12 | 14 | **16** | 18 | 20 | 22 |
|--|--|--|--|--|--|--|
| captures exact, of 100 | 94 | 94 | **100** | 97 | 97 | 97 |
| shared `phi` | 15.21 | 13.21 | 10.75 | 8.75 | 6.75 | 4.75 |

The sum is conserved at 26.75 as the trade-off requires, so the scan is a
genuine test of the split and not of the sum. 16 is the only value that
reconstructs every display-off capture, both extremes are worse, and 18 and
above are indistinguishable from each other — the same three captures fail at
18, 20 and 22, so the discriminating information is spent by then. This agrees
with the 2013 corpus, where 17 already costs accesses, and with what openMSX
already uses.

**A pre-capture request may have a negative arming cycle.** `phantom_arm`
returned `-1` for "no cycle can reach this slot", but a request latched before
the origin of the `.txt` grid has a negative cycle as a matter of course — −55
in `sprOn-stop-wrCpu132-2g`, whose first grant is row 28. Every guard testing
`< 0` therefore rejected legitimate pre-capture requests. With a proper
`NO_ARM` sentinel that capture reconstructs, and the earlier claim that it had
no physically constructible pre-capture request was wrong: 63 accesses against
62 pulses simply requires one, and one exists.

**What is left: 10 captures**, all sprites-on. Grouped on the *first*
divergence, since a wrong decision corrupts every `S_prev` after it:

| group | files | symptom |
|--|--|--|
| A. row 162 → 188 | `hmmv-wrCpu-2`, `hmmv-wrCpu-3`, `line-rdCpu-2`, `line-wrCpu-3`, `ymmm-wrCpu-3` | the model takes a request at margin 0 or +1 after a grant at row 162 and grants it row 188; the VDP served nothing |
| B. margin −1 | `lmmm-wrCpu-2` (row 828 → 860), `stop-wrCpu-1` (row 1212 → 1264) | the model takes at exactly the threshold; the VDP dropped |
| C. row 28 or 92 | `lmmm-rdCpu-3` (−3), `stop-rdwrCpu-5g` (−4), `stop-rdCpu-3` (−8) | the model drops; the VDP served, across the line boundary at row 1330 |

Group A is not the evidence gap it looked like, and it is worth one parameter,
not thirty-one. At row 162 the 256 fitting captures drop at every margin from
−15 to −2 and take at every margin from +1 to +12, every take from +1 to +8
landing on row 188 — and they have **nothing whatever at −1 or 0**. So the
threshold at that row is unconstrained over a 3-cycle window, and giving row
162 its own value (`--threshrow=162:N`, diagnostic only) lifts the corpus from
256 to **261 at either 1 or 2**. A plateau two cycles wide is not a knife edge
being tuned.

Three things argue this is a real property of that row rather than a parameter
mopping up captures:

- **The base rate is zero.** The eight rows preceded by a 64-cycle gap are
  structurally identical to each other, and each is the previous grant for
  ~290 decisions. Given its own threshold of 0 they score 255, 256, 256, 256,
  257, 256, 256, 256 against a baseline of 256 — nothing. Only row 162 moves,
  and it moves by 5.
- **The row was not chosen by search.** 162 is the slot before the 162/170/188
  cluster, gaps 8 and 18, the one region of the lattice that already needs an
  exception: row 170's lookahead of 16 against 18 elsewhere (§8.2). The
  residue and the known anomaly are in the same three slots.
- **The apparent contradiction was soft.** Two captures appeared to take at
  margin +1 at row 162, which would contradict three captures wanting a drop
  there. At `--threshrow=162:2` all five reconstruct, so the +1 was the fit's
  choice among several, not something the measurement forced.

What it is *not* is measurement error. No capture in group A holds a displaced
pulse (§11.8), and sharpening every edge by a factor 2.5 (§11.9) left all ten
failures standing, though it took the captures needing no tolerance at all
from 125 to 129. The
margin these decisions turn on is `T - S_prev`; `S_prev` is exact, from the
`.txt`, and `T` is now known to 0.064 cycles rms, so a one-cycle error in it
is 15 sigma. The model is wrong here, not the data.

Groups B and C are still open and are not the same shape as A. B wants a drop
at exactly the threshold at rows 828 and 1212 — and rows 700 and 1212 are the
two rows whose takes at margin −1 pin the threshold there in the first place,
so B contradicts the very decisions that set the value it is measured against.
C wants a take 3 to 8 cycles further back than anything else in the corpus,
and all three cross the line boundary at row 1330, where the lattice restarts.
No per-row threshold satisfies B and C together with A.

## 11.9 The timebase is one straight line, not a chain of interpolations

The dominant per-edge error was never in the `/CSx` pulses. It was in the map
from analyzer time onto VDP cycles, and it was self-inflicted.

`t2` is built by locating refresh bursts in the VCD and interpolating between
them. Each anchor is a `/RAS` edge, so each is located only to the nearest
analyzer sample, and interpolating **between adjacent pairs** hands every edge
between two anchors the quantisation error of both. With roughly one anchor per
scan line that is a piecewise-linear map with some fifty independent segments
per capture, each segment tilted by its own ±0.5 sample.

It need not be a chain at all. Both clocks are crystals; over the 400 µs of a
capture neither drifts measurably, so the true map is a **single straight
line**, and fitting one through all the anchors at once averages their
quantisation down by √N. That is fewer parameters, not more — two per capture
instead of fifty-odd — which is why it is worth doing even before looking at
what it buys.

What it buys, measured without the arbiter (§11.5): the per-edge residual falls
from 0.590 to **0.240 samples**, and the uncorrected residual lands on 0.286
against a pure-quantisation prediction of 0.289. The edges are now as sharp as
an 80 MHz sample clock permits.

Two guards, because a global fit fails globally where a local one fails
locally. First, a few anchors really are wrong — one carried a residual of 83
cycles, a miscounted refresh, not a rounding — and one such anchor among a
hundred tilts the whole line. So the fit is iterated with anchors past a whole
cycle of residual rejected, which removes **42 of 13535** across the corpus.
Second, after rejection the worst surviving anchor in every capture sits within
0.362 cycles (1.35 samples) of the line, mean 0.314, so the line is not hiding
a drift it should have followed. `--pairtime` restores the old behaviour for
comparison.

The one cost: the integer model of §11.2 goes 203 → 202 captures perfect (same
23915 of 23990 accesses). It allows a one-cycle tie and one integer `δ` per
capture, so sharpening `t2` can move a capture across a `floor` boundary. The
trellis, which is the model of record, only improves.

This does not close open item 7. Recording the axis in the `.txt` is still
worth doing — every consumer still re-derives it, and `--origin` is still the
only guard against getting it wrong — but it is no longer where the error is.

## 11.8 The one bad capture was a 12.5 ns spike, and the fix is in the decoder

`dispOff-stop-rdwrCpu-4g` looked like a defective measurement and was recorded
here as one. It is not: the fault was in the `/CSx` decoder, and finding it
took separating three things that produce the same symptom — a wrong capture,
a warped timebase, and a wrong edge chosen from a correct capture.

The symptom was distinctive. It was the only display-off failure and the only
one with deficit 0, zero drops, and two misses each paired with an extra one
8-cycle slot away: two grants *displaced*, not discarded. `--pacescan` looks
for the signature of a mismeasured pulse — an edge whose gap to its
predecessor is off the loop pace while its successor makes that time back —
and found exactly one such edge in all 266 captures, edge 104 of this file,
11.73 cycles early.

**Whether that displacement is in the capture is decidable**, because the raw
VCD timestamp is wall time straight from the analyzer while `t2` is that
timestamp mapped through the refresh anchors. `--rawdump` prints both gaps side
by side: a bad capture is off in both columns, a warped timebase only in `t2`.
Here the raw gap was −11.35 cycles against −11.72 in `t2`, so the anchors were
innocent and the edge really is early in the `.vcd`.

But it is early *on its own*. Every falling edge in the capture is on the pace,
including this pulse's own, and the pulse measures 11 samples where every
other measures 55 or 56. The `.vcd` says why:

```
3741875  /CSR 0    falling edge, exactly on the loop pace
3743250  /CSR 1    goes high...
3743375  /CSR 0    ...and back low one single 80 MHz sample later
3748875  /CSR 1    the real rising edge, 56 samples after the fall
```

`/CSR` glitched high for **one sample, 12.5 ns**, in the middle of an I/O
cycle. So the capture is not defective at all — it holds the correct rising
edge at 3748875, right where the loop pace and the recorded slot 9044 both
want it. Nor did anything go wrong in the `.vcd` → `.txt` chain: the `.txt`
records the VRAM side and records it correctly.

The error was mine. The spike splits the low pulse into 11 samples and 44, and
the decoder took the rising edge of each, so the 20-cycle debounce then had two
candidates 11.8 cycles apart and kept the **first** — the spurious one. The
narrow-pulse filter could not help: it only ever looked at short *lows*, and
both fragments are longer than its 8-sample floor.

The fix is to filter both polarities (`drop_glitches`, §11.1). A pair of
transitions closer together than 8 samples is cancelled whichever level lies
between them, which restores the surrounding level and rejoins the split
pulse; the real edge then survives the debounce because it no longer has a
spurious neighbour to lose to. With that, the capture reconstructs exactly and
the corpus goes 255 → **256**.

The second divergence this file showed — an apparently independent
`NEED`-boundary tie at slot 4104, 5000 cycles away from the spike — turns out
not to be one. It was an artifact of the diagnostic: `--faildiag` scans a
single `phi` on a grid and grants no per-edge tolerance, so with the spike
still in the train no `phi` could serve both ends of the capture and the strain
surfaced at the nearest tie. With the spike gone the whole file fits at `phi`
in [10.966, 10.981) — the same window as its five siblings — with one edge
nudged by 0.03 samples.

The independent confirmation is the integer model of §11.2, which had always
been able to fit this file but only by choosing `δ` = 25 on *falling* edges,
alone in the corpus against `δ` = 10 or 11 on rising edges everywhere else. It
now reports `δ` = 10 rising like everything else.

Spikes of this kind are rare and now counted: `--pacescan` reports that **4 of
266 captures** contain a short high, one each. In the other three the debounce
happened to keep an edge that landed in the right slot anyway. Nothing else in
the corpus moved — same 125 captures at `eps` = 0, zero failed re-simulations,
engine-only and line-origin checks unchanged.

## 12. S1/S0 and set-adjust

Used in §3. Not a second slot map.

- **S1/S0 ≠ 0,0:** line **1365** in sprites-on *and* sprites-off, for all
  three non-zero encodings. Mid-line command/CPU slots unchanged; only the
  HBLANK tail moves. Both modes keep **+1** of their 4: sprites-off
  `10, 10` → `9, 8` (one correction, at 1331), sprites-on
  `[15 7 11]` → `[14 6 10]` (one correction, at 1329).
- **Set-adjust (R#18):** line stays 1368 for all 16 values in all three modes.
  Dummy preamble and bitmap RAS do not move relative to refresh. Only the
  HBLANK comb moves, by 4 cycles per unit, and the padding total is invariant
  (§3.2). Confirmed independently from the CPU side, which sees the same
  shifted comb and the same δ at every value (§11.2). There is no combined
  `adjustN` + S1/S0 capture.

Keep the tables at centred (R#18 = 0) / S1S0 = 00 / 1368 unless a leftover is
specifically a wrap through 1330.

## 13. Command start: `S₀` = 93..95 from rising `/CSW`

The delay between the CPU writing the command byte (R#46) and the engine's
first VRAM access. Data: `scr5-dispOff-hmmv-noCpu-nx4-ny2-{1..6}f` — HMMV,
NX=4, NY=2 on screen 5, so exactly **four** dest writes (`0x1C000, 0x1C001,
0x1C080, 0x1C081`) per run, with the command restarted in a loop so the launch
falls inside a random 7-line snippet. Six launches captured.

The register setup is a burst of 15 `OUT (#99)` at ~137 cycles (23 T) each;
the **last** pulse of the burst is the CE write. `/CSW` low is **14.9 ± 0.1**
cycles wide (2.50 T-states), which is also the cleanest measurement of the
pulse width used in §8.3.

| capture | `/CSW` rise | first engine RAS | lead |
|--|--|--|--|
| `1f` | 2828.5 | 2923.7 (row 188) | 95.2 |
| `2f` | −9.8 | 87.8 (row 88) | 97.6 |
| `3f` | −1277.1 | −1180.1 (row 188) | 97.0 |
| `4f` | 2775.2 | 2899.7 (row 164) | 124.5 |
| `5f` | 1456.4 | 1556.1 (row 188) | 99.7 |
| `6f` | 2750.2 | 2847.7 (row 188) | 97.6 |

`4f` is not an outlier: its wait lands inside the 44-cycle hole (row 120→164),
so the first legal slot is 44 cycles further on. Treating the start like any
other wait — first command slot with `engine_dist(CE, S) ≥ S₀` — the six
launches intersect at

```
S₀ ∈ {93, 94, 95}     from the rising edge of /CSW
S₀ ∈ {111, 112, 113}  from openMSX's port-write timestamp (start of T2, §8.3)
```

Only HMMV was measured, and only in display off. A command that has to read
before it writes (LMMM, HMMM, LINE) may well start later.

**openMSX.** Every `execute*()` entry path starts with `nextAccessSlot(time)`,
i.e. `getAccessSlot(time, Delta::D0)` at the port-write timestamp: `S₀ = 0`.
Hardware is ~112 cycles later, about 14 display-off slots. For a long command
this is a constant offset of the whole access pattern rather than a shape
error, so it mostly shows up in short commands, in the `CE` clear time, and in
the arbitration against a CPU access issued right after the launch. Same class
of fix as §8.3, and the same origin.

## 14. Vertical border ↔ display: the grid switches one line early

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
comb as changing *at* the line boundary rather than at cycle ~164, and suspects
a difference of line grouping. Worth re-deriving; it is worth two lines per
frame.

Two consequences for openMSX. `getTab()` already keys on
`isDisplayEnabled() = isDisplayArea && displayEnabled`, so the vertical border
correctly uses `tabScreenOff` — the measurement confirms that (167 RAS,
8-cycle comb, identical to a display-disabled line). What it gets wrong is the
boundary: the table should become `tabSpritesOn` at cycle 164 of the line
**before** `isDisplayArea` starts, and go back to `tabScreenOff` at cycle 164
of the line **after** it ends.

`VDS` is a clean real-vs-dummy discriminator: it stays high on the dummy
`0x1FFFF` reads that replace the bitmap fetches. The 1 MHz captures use that —
they show exactly **192** VDS-active line blocks per frame (the test ran in
192-line mode), each ~47 µs long at a 64.14 µs pitch, so the extra
sprite-fetch line is *not* VDS-active and the count equals the number of
displayed lines. Frame period 16804 µs = **262** lines, giving 1368/64.137 µs
= **21.33 MHz** and 59.5 Hz — an independent check on the 8280's 5.96113
cycles per T-state.

## 15. What is solid vs open

**Solid.** Scores at the top of this file; the model is §8 plus §3–§6.

- §5 waits, §3 padding (**+4 in all three modes**, from `/RAS`; sprites-on
  spreads it as 2 + 1 + 1 over 1330 / 1337 / 1348), §6 packed-start +1
  unconditional. The sprites-on per-step addend (§4) is 1 or 2 and no
  measurement separates them; use 1, as openMSX does.
- CPU-legal = command table minus packed +6 (a grid property, both machines).
- `first_cpu_slot` with engine-distance ≥ **16**: 2013 gives the ceiling,
  2026's fixed-sum scan gives the floor (§8.2).
- One request buffer; the booked slot never moves; and the discard rule is the
  single inequality `T - S_prev >= margin`, with `margin` = 2 display off and
  sprites off, **−1 with sprites on** (§11.7).
- Command skips fired CPU RAS; may use packed +6; never delays the CPU; the
  loser takes the next slot with no full re-arm. No engine rule references the
  future CPU access (§10.3).
- Dummy `R..` on packed +6 is a CPU-path grant the CPU cannot carry; the CPU
  RAS follows at +26 (rarely +54). Rate is the same at 12 T and 37 T.
- Pin delay is wall-clock (inside `φ0` / `δ`); the lookahead is memory cycles
  with the engine's own padding (§8.1).
- **δ is one integer per mode: 11 display-off and sprites-off, 13 sprites-on**
  (§11.2). Same value for reads and writes, and at every R#18. The sprites-on
  +2 is uniform along the line.
- As a real number the pin delay is **`phi` ≈ 10.75 cycles**, one value
  for all three modes to within half an analyzer sample, once sprites-on is
  given 2 extra cycles of lookahead. It reproduces the 131-cycle captures
  exactly (§11.5). A single request buffer with drop-new is confirmed there
  against a two-deep queue.

**Open:**

1. **Predict the dummy without the `R..` tag.** D6 is the mechanism and is
   directly implementable once `T` is known, but the extra lookahead on packed
   slots is 2 or 3 (§10.2) and the −18 boundary is one cycle wide. Occupying
   the observed tag is 84/84 and is not a rule.
2. **Where the sprites-on +2 lives.** It is not the pin delay: requiring one
   real delay across the modes is what forces the 2 (§11.5, 76 → 102 captures).
   That leaves the arbiter's deadline, the length of a sprite-mode memory
   cycle, and the absolute position of `SLOTS_SPRON`, which are still not
   separated from each other. Whether row 170 is exempt is a live lead on 19
   accesses.
3. **The two irregular regions of the sprites-on lattice** (§11.7). The
   occupancy rule was the whole residue and a per-mode threshold removed most
   of it; the 10 captures left split into five whose extras land on row 188 in
   the 162/170/188 cluster, two that want a drop at exactly the threshold, and
   three that miss at row 28 or 92 just after the line boundary at 1330. Both
   regions are places where the lattice stops being regular, and the first is
   the same cluster as the row-170 exception in open item 2, so those two are
   probably one question. The five are worth a single parameter — row 162's own
   threshold, base rate zero across the eight comparable rows — but a parameter
   is not a mechanism, and none of the candidates for one (a release timed from
   the end of the access, a longer memory cycle beside the sprite fetch, the
   absolute position of those three slots) is separated from the others yet.
4. **Why sprites off keeps a threshold of 2** while sprites on wants −1
   (§11.7). Three sprites-off drops occur at a margin of exactly 1, so the
   value is measured, not assumed, and a 3-cycle difference between modes has
   no mechanism yet. A slot's `.txt` row is its CAS and the stretch is 1 or 2
   cycles, so a release timed from the access rather than from `/RAS` is the
   obvious suspect, and it points at open item 2 again.
5. **Packed-start +1 is unobservable on 2013**, not merely untested (§6.1),
   and the dummy `R..` may be 8280-only — 2013 mixed traces are HMMV only.
6. **Command startup** `S₀` is measured for HMMV in display off only (§13).
   The read-first commands (LMMM, HMMM, LINE) and the other two modes are
   untouched, as are the character, text and MSX1 tables.
7. **The `.txt` axis** (§11.6): absolute time is `1368 * column + row`, so the
   scan-line origin is implicit and every consumer re-derives it. Record it in
   the file, or trim captures to a line boundary. Until then `--origin` is the
   guard. This was also the dominant per-edge error, four fifths of the
   variance, until the timebase was fitted as one line rather than
   interpolated pairwise (§11.9); what remains is the analyzer's own
   quantisation, so the reconstruction is now limited by the sample clock.
8. **openMSX CPU origin** (§8.3): `Delta::D16` from the Z80 port timestamp is
   ~29 cycles early against hardware. Lost-request timing cancels; the stolen
   command slot does not (`vdpcmdx` `+CPU`). Not a VDP discovery — a
   translation of this file's rising-edge `T` onto that stamp.
9. **The border/display comb boundary** (§14): cycle ~164 or the line edge.

**Not worth collecting:** another `IN` gap purely to crowd or empty the buffer
(12 T already crowded it, 37 T already emptied it, and the dummy rate did not
change). More *untargeted* sprites-on 12 T bursts will not tighten δ — but a
gap chosen coprime with 128, so the request phase creeps across the refresh
cell, will (§11.2). Read versus write is settled, so no more alternating
loops.

---

Admissible `Δ` for a pair of RAS times `(p, n)` is
`[engine_dist(p, prev_slot(n)) + 1, engine_dist(p, n)]`. Intersect over all
pairs of one step. Subtract the sprites-on addend from that mode's band before
intersecting the three modes. Line wrap versus mid-line is labelled from the
dest (or YMMM/HMMM src) address crossing a 128-byte VRAM line.
