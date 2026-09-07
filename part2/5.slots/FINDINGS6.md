# VDP command and CPU slot timing

This document was written with assistance from an AI coding agent.

> **Superseded by [`FINDINGS7.md`](FINDINGS7.md)**, which carries the same
> model and the same evidence without the intermediate steps. Kept for the
> derivations, the retired hypotheses and the tooling history.

Command waits and slot tables started as
[`FINDINGS4.md`](FINDINGS4.md) (frozen). Packed-start +1, CPU VRAM, and the
mux are here. Discovery notes: [`FINDINGS5.md`](FINDINGS5.md),
[`ARBITER_ITER.md`](ARBITER_ITER.md). S1/S0 and set-adjust captures:
[`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md).

| | 2013 | 2026 |
|--|------|------|
| Machine | Philips **NMS 8250** | Philips **NMS 8280** |
| Data | `8.final-analysis/` | `part2/5.slots/` (txt) + `part2/1.vcd/` |
| Clocks | One 21.47727 MHz VDP crystal. CPU = VDP `/6` | Separate crystals. VDP ~21.33 MHz PAL. **5.96113 ± 0.00004** VDP cycles per Z80 T-state (same from 12 T and 37 T programs, 1 part in 10⁵) |
| CPU request times | Exact **72 / 252 / 3060** VDP cycles (40 I/O + loop), unknown phase `φ0` | `/CSR` / `/CSW` on the refresh-interpolated VDP clock. Default burst: intra-burst **71 or 72**, loop ~250.5. Sparse `rdCpu222` (§11): **37 T / 67 T**. Phase scan (§11.3): **141** and **76.6**. Edge→arbiter delay `δ` = **11**, or **13** with sprites on (§11.3); lookahead is NEED=16 memory cycles (§7) |

Bitmap screen 5 (screen 8 shares the slot maps). Default line period **1368**
(S1,S0 = 0,0). Slot times and waits are **RAS** (openMSX numbering). Published
`.txt` rows are **CAS**; conversion is §1.

Command start `S` is not fitted from the `.txt` traces (they are mid-command);
it is measured separately from `/CSW` in §13.

Fitters (do not change `fit_2013.cc` behaviour). `fit_2026.cc` implements
§3–§4 (padding +4 in all modes, sprites-on step +1), D6 dummy occupy, and the `/CSx`
decoder in §11. Its main engine model uses **no CPU predicate** (§10.5); the
retired P5 / newline-idle rules survive only inside the `--scratch` A/P/B/C
comparison.

- `8.final-analysis/cpu_scratch_2013.cc` — 2013 CPU grid + HMMV
- `part2/5.slots/fit_2026.cc --nocpu` — 2026 command engine
- `part2/5.slots/fit_2026.cc --scratch` — occupancy + engine, skip observed CPU RAS
- `part2/5.slots/fit_2026.cc --mismatch` — mixed packed leftovers, dummy window
- `part2/5.slots/fit_2026.cc --origin` — assert the `.txt` line numbering agrees
  with the `.vcd` (§11.2); run after adding captures or hand-editing a `.txt`
- `part2/5.slots/fit_2026.cc --rw [--sel=SUB]` — fit the `/CSR` and `/CSW` pin
  delays independently, with the per-file plateau of each (§11.3); with
  `--force=D` it instead lists every mismatch at a fixed δ by position in the
  line, which separates a slot-table error from asynchronous jitter, and
  counts how many misses are the first access of their capture (§15.3)
- `part2/5.slots/fit_2026.cc --pad3[=E] / --need=N` — the retired sprites-on
  padding, and the lookahead, for the trade-off scan of §15.2
- `part2/5.slots/retag.py` — reconstruct access types from the VRAM addresses,
  for traces where a command rectangle leaves its designated region (§10.5)
- `part2/5.slots/exp4.py` — the sprites-off engine step, standalone, with
  every leftover classified by slot geometry (§10.5)

---

# I. Command engine (no CPU)

Data: `part2/5.slots/scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` (including
`*nx??-?d.txt`). **514** traces, **78330** engine-to-engine transitions
(display off 235 / 39806, sprites off 149 / 23729, sprites on 130 / 14795 —
all 100%).

After an access at RAS time `t`, wait `Δ` **memory cycles**, then take the first
command slot whose **engine-distance** from `t` is ≥ `Δ`. Engine-distance is
wall-clock VDP cycles minus the padding in `(t, next]` (§3). With sprites on,
every `Δ` is 2 larger (§4). If `t` is packed +6, `Δ` is 1 larger again (§6).

## 1. RAS vs CAS

`/RAS` falling is when the row address is driven (access start). `/CAS`
normally falls **1** VDP cycle later.

| | RAS (openMSX) | CAS (`.txt` rows) |
|--|----------------|-------------------|
| Typical slot | `r` | `r+1` |
| Stretched slots, display off | 1324, 1334 | **1326, 1336** |
| Stretched slots, sprites off | 1322, 1332 | **1324, 1334** |
| Sprites on (no stretch in the command table) | `r` | `r+1` |

On those two stretched slots, `/CAS` follows `/RAS` by **2** cycles (~100 ns vs
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

Sprite count and size do not add command slots. Screen 5 and 8 share these maps.

Display-off RAS grid (including refresh and dummy reads) is 166 pulses per line:
**163×8 + 2×10 + 1×44 = 1368**. The two 10-cycle gaps are `1324→1334` and
`1334→1344`. Sprites-off has the same gaps two cycles earlier
(`1322→1332→1342`). Sprites-on has no command slot in that region (last slots
1264, 1330).

The 44-cycle hole (dispOff `120→164`) is five empty memory cycles, not padding.
It counts in full: HMMV with `Δ=48` from 120 skips 164 (+44) and takes 172 (+52).

### 2.1 Packed +6 (sprites-off only)

In sprites-off bitmap mode, part of the command table is not a uniform 8-cycle
grid. During active display the VDP inserts extra command/CPU opportunities as
**pairs six cycles apart**.

**Definition:** a *packed +6* slot is a command-table RAS `S` whose predecessor
in that table is `S−6`. It is still a legal **command** slot. *CPU-legal* means
`command_slots` minus these packed +6 times.

This geometry exists **only in sprites-off**. Display-off command slots are 8
apart (or 16 at refresh holes, 10 at the two stretches). Sprites-on has no
adjacent command slots 6 apart.

**Why only sprites-off.** In the active display the VDP groups memory cycles
in 32-cycle cells: `182+32k` is a sprite-fetch when sprites are on and spare
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

Packed RAS only: **188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604, 636,
700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116, 1148, 1212**.

Eight groups of three pairs, repeating every **128** from 182 (`{0,6}`,
`{32,38}`, `{64,70}`), plus a truncated last group that only keeps `(1206,
1212)`. Each group also has a **lone** CPU-legal slot at relative **96**
(278, 406, 534, 662, 790, 918, 1046, 1174) that is *not* packed. Left border
(`6…118`, `162`, `170`) and end-of-line (`1266…1366`) are 8-apart or stretch,
not packed.

**CPU never uses them (both measurement sets).**

| Set | CPU RAS on packed +6 | CPU RAS in sprites-off (where packed slots exist) |
|-----|----------------------|-----------------------------------------------------|
| 2013 `8.final-analysis` `*cpu{read,write}*` | **0 / 1141** | **0 / 204** |
| 2026 `5.slots` `*rdCpu*` / `*wrCpu*` (stop + command) | **0 / 12846** | **0 / 4346** |

CPU accesses always land on the first of the pair, or on an unpaired CPU-legal
slot. Commands **do** use the +6 (2013 sprites-off HMMV: 24 / 85 engine writes;
2026 sprites-off: 1436 / 4608).

**No other exclusive slots.** Union of all CPU VRAM vs command-engine RAS:

| Mode | 2026 (covering set) | 2013 |
|------|---------------------|------|
| dispOff (154) | all 154 used by **both** CPU and command | 137 both; 16 cmd-only and 1 CPU-only (`628`, *n*=2) are coverage holes — 2026 has both on all of them |
| sprOff (88) | **63** CPU-legal: all used by both. **25** packed +6: command only (CPU=0). No CPU-only slots | CPU still 0 on all packed +6. Other holes are undersampling (8 sprOff files) |
| sprOn (31) | all 31 used by **both** | 29 both; `188` CPU-only (*n*=8) and `170` cmd-only (*n*=10) — 2026 has both on both slots |

The only command-table slots exclusive to one client are the sprites-off packed
+6 (command, never CPU). There are **no** CPU-only slots. Dummy `R..` (2026
sprOff, **333** in the `.txt` parse; ~335 from VCDs) also sit on those same 25
packed times; those slots still take command accesses in other traces. The
dummy is the CPU path being granted a packed slot it cannot carry (§10).

## 3. Padding (stretched memory cycles)

The delay counter counts **memory cycles**, not wall-clock VDP cycles. Extra
clocks that pad the line to 1368 exist on the wire and are **not** counted.
The 44-cycle hole **is** counted.

All three modes are now measured on the wire (`/RAS` gaps, §3.1 and §3.3).
Padding sits in HBLANK and differs by mode.

| Mode | Unpadded spacing there | Padded cycles (S1,S0 = 0,0) | Extra wall-clock | Completions in `engine_dist` |
|------|------------------------|-----------------------------|------------------|------------------------------|
| display off | 8 | two of 10, `1324→1334→1344` | **+4** | subtract **2** at **1334** and **1344** |
| sprites off | 8 | two of 10, `1322→1332→1342` | **+4** | subtract **2** at **1332** and **1342** |
| sprites on | `13, 6, 10` (the 64-cycle comb) | three of `15, 7, 11`, `1315→1330→1337→1348` | **+4** | subtract **2** at **1330**, **1** at **1337** and **1348** |

Set-adjust moves the padding block along the line but never changes the total
(§3.4), which is a second, independent confirmation of +4 in all three modes.

```
engine_dist(t, s):                    # s > t, times mod LINE
    (s - t) minus padding completions C with t < C ≤ s
```

A wait that spans all padding needs that many more real cycles to complete.
This is a property of the line, not of whether the command uses those RAS
pulses. Sprites-on padding is sprite fetches, not command slots (last command
slots 1264, 1330). Display-off example (HMMM):

```
104 W → 164 R     Δ = 60   does not cross padding → engine count 60 → slot taken
1292 W → 1360 R   Δ = 68   crosses both 10-cycle cycles → 1352 is only 56 engine cycles → skipped
```

The counter restarts at every access. The correction is well defined for an
interval, not as a periodic remapping of the line.

### 3.1 Sprites-on is +4 as well, spread over three cycles as 2 + 1 + 1

FINDINGS4 applied the sprites-off pair (two ×2 at 1332/1342) to sprites-on as
well, because command traces cannot see those pulses. The S1/S0 `/RAS` gaps
show the pulses:

```
S1,S0 = 0,0 : ... 13 6 10 6 10 6 13 [15 7 11] 6 10 6 13 13 6 ...
otherwise   : ... 13 6 10 6 10 6 13 [14 6 10] 6 10 6 13 13 6 ...
```

`15+7+11 = 33` versus `14+6+10 = 30`: the three extra clocks of the 1368-cycle
line, and each of the three gaps is one cycle shorter. Stretch RAS→CAS
(CAS = RAS+2) is only on the middle of those three (sprP at **1337**) and is
gone in 1365 mode.

This was first read as "1365 is the unpadded line, so sprites-on carries +3".
That is wrong for the same reason it is wrong in sprites-off (§3.3): the 1365
line still carries padding of its own. The sprites-on comb settles it without
any assumption about 1365, because away from HBLANK it is **exactly periodic
with period 64** — `6 10 6 10 6 13 13` — and the padded triple is the one
place it breaks:

```
1174 -6- 1180 -6- 1186 -20- 1206 ...     display region: 6 6 20, period 32
1238 -13- 1251 -13- 1264 -6- 1270 -10- 1280 -6- 1286 -10- 1296 -6- 1302   = 64
1302 -13- 1315 [-15- 1330 -7- 1337 -11- 1348] -6- 1354 -10- 1364 -6- next line
        natural continuation of the 64-comb:
1302 -13- 1315 [-13- 1328 -6- 1334 -10- 1344] -6- 1350 -10- 1360 -6- 1366 = 1302 + 64
```

So the unpadded line would end at **1366**, the real one at 1370 (= line + 2,
which is where the next line's first `/RAS` sits): **+4**, as `+2, +1, +1` on
the memory cycles completing at 1330, 1337 and 1348. The 1365 line is
`1329 1335 1345 1351 1361`, i.e. the same comb with **+1** left in it.

Source: `part2/1.vcd/rasmap.py scr5-sprOn-hmmv-noCpu-s0-1f.vcd 1050 1368`.

Line length, refresh-interval ruler (sprites-on HMMV, noCpu):

| R#9 S1,S0 | line |
|-----------|------|
| 0,0 (`s0`) | **1368.01 ± 0.04** |
| 0,1 / 1,0 / 1,1 | **1365** (1364.94 ± 0.10) |

Set-adjust leaves the line at 1368.02 ± 0.03 in all five settings. Mid-line
command/CPU slots do not move. Only the last sprites-on command slot of the
line (openMSX **1330**) moves with adjust (±2) or becomes **1329** in 1365
mode. Details: [`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md).

Display-off / sprites-off + S1/S0 is now measured too — see §3.3.

### 3.2 Two parameterisations that match command traces

Command next-slot does not separate sprites-on padding from the sprites-on
addend on these maps (slot gaps are 32–64; plateaus are several cycles; the
padded RAS pulses are not command slots).

| | Padding sprites-on | Addend on every `Δ` when sprites on | engine `noCpu` |
|--|--------------------|-------------------------------------|--|
| FINDINGS4 / openMSX / **current** | **+4** (2+1+1, §3.1) | **+1** | **100.0%** |
| also fits | +4 (2+1+1) | +2 | 100.0% |
| earlier in this file | +3 (1+1+1) | +2 | 100.0% |
| — | +3 (1+1+1) | +1 | 99.9% (19 wrong) |

Three of the four combinations are 100% on 78330 `noCpu` transitions, so the
command traces fix only the *sum*: padding total plus addend. `/RAS` fixes the
padding at +4 (§3.1), and the addend is then 1 or 2 with no way to choose —
take **1**, which is what openMSX has always used.

The middle two rows are what this file used to say. Padding +3 with addend 2
is the same model as +4 with addend 1 for every step that crosses HBLANK, and
one cycle stricter for every step that does not; on these slot maps (gaps
32–64) that never changes the answer. It does change the CPU side, where
there is no addend to absorb it — see §15.1.

### 3.3 Display-off / sprites-off: measured as 4

Sprites-off with S1/S0 set (`scr5-sprOff-hmmv-noCpu-s{16,32}`, 8 captures)
settles this. Line length from a refresh-lattice fit
(`part2/1.vcd/vcdlib.py`: `t = A + B·128k + (B·L)·n`, three linear unknowns,
so `L` is fitted and not assumed):

| R#9 S1,S0 | sprites off | sprites on |
|-----------|-------------|------------|
| 0,0 (`s0`) | **1368.00 ± 0.05** (3) | **1367.98 ± 0.06** (6) |
| 0,1 (`s16`) | **1364.99 ± 0.07** (4) | 1365.00 (3 usable) |
| 1,0 (`s32`) | **1365.03 ± 0.06** (4) | **1365.02 ± 0.12** (7) |

Sprites-off drops **3**, exactly like sprites-on. But the `/RAS` comb shows
that the 3 lost clocks are **not** the two stretches going back to 8:

```
sprites off, 1368 : ... 1306 -8- 1314 -8- 1322 -10- 1332 -10- 1342 -8- 1350 -8- 1358 -8- 1366
sprites off, 1365 : ... 1306 -8- 1314 -8- 1322  -9- 1331  -8- 1339 -8- 1347 -8- 1355 -8- 1363
```

`10 + 10 = 20` becomes `9 + 8 = 17`. So the 1368 line does carry **+4** of
padding relative to a pure 8-cycle grid, and the 1365 line still carries
**+1** (the 9-cycle gap at 1322→1331). "1365 removes all the padding" was the
wrong premise, and the 1364 prediction it implied never arises.

Two things follow.

The same reading applies to sprites-on, where the unpadded comb is known from
its own 64-cycle period rather than from 1365 (§3.1).

1. **`fit_2026.cc` and openMSX keep 2+2.** Rescoring with each stretch given
   its own extra still does not separate them arithmetically: the engine
   corpus is 100% for 2+2, 2+1 and 1+2 and only fails for 1+1, and the 2026
   CPU set likes 2+2 and 1+2 equally. The wire measurement above is the
   discriminator, and it says both padded memory cycles are 10 cycles long.

   | padding (dispOff / sprOff) | engine noCpu (514 files, 78330) | 2026 CPU files perfect (of 209) |
   |--|--|--|
   | **4** (2+2, current) | **100.0%** | **158** |
   | 3 (2+1) | 100.0% | 154 |
   | 3 (1+2) | 100.0% | **158** |
   | 2 (1+1) | 99.8% | 151 |

2. **The split-vs-glued argument of §8 stands as originally written.** It only
   weakened under the hypothesis that the padding was 3. It is 4, so the 2013
   plateau still separates a 16-memory-cycle padding-aware lookahead from a
   glued 27-cycle wall-clock lead.

Side result for 1365 mode (not needed for emulation — the slot tables assume
1368): mid-line command slots are untouched (46, 94, 162, 214, … 1266, 1314
all still used) and only the tail moves. Sprites-off `1332→1331`,
`1342→1339`, `1350→1347`, `1358→1355`, `1366→1363`; sprites-on `1330→1329`.
A 1365 line has no stretched RAS→CAS anywhere.

### 3.4 Set-adjust moves the padding, never its total

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

R#18 = 0 is centre, 1..7 shift the picture left, 15..8 shift it right, and
the padding block follows: even steps slide the `+2, +2` pair by 8 cycles per
two units; odd steps are the half-memory-cycle position, where the same 4
extra clocks spread as `+1, +2, +1` over three memory cycles. Sprites-off
(base 1322) and sprites-on (base `1315→1330→1337→1348`) behave the same way.
The **total stays +4 in every mode for all 16 values** — a second,
independent confirmation of §3.1 and §3.3.

Two of the 32 sprites-off adjust captures (`adjust1`, `adjust15`) caught
vertical-border lines and must be filtered to display lines before pooling;
filtered, they follow the table.

For emulation: set-adjust needs no new slot tables as long as one accepts a
±8-cycle error on the last two or three HBLANK slots of the line. And R#18
cannot be used to make the padding go away.

## 4. Sprites on: one addend on every step

With §3, the steps that still differ with sprites on all need the **same**
extra amount. One rule: **sprite rendering adds a constant to every
command-engine delay.** That forces LMMM source→dest to **32** and HMMM/LMMM
newline to **128**, matching the other two modes. It is not a slot-position
effect.

The constant is **1** or **2** and the command traces cannot tell (§3.2);
`fit_2026.cc` and openMSX use 1.

## 5. Wait parameters

`Δ` is the engine wait (work + arbitration), in memory cycles, **before** the
sprites-on addend. Any integer in a plateau produces the same next slot on
these maps. `L` is extra wait on a rectangle line-break: newline `Δ` =
mid-line `Δ` + `L`.

### 5.1 Plateaus (three-mode intersection)

Sprites-on bands have the addend taken off before intersecting. The numbers
below are the FINDINGS4 intersection (addend +1, which is what `fit_2026.cc`
now uses). Addend +2 is slot-equivalent here, so the preferred
representatives do not move either way.

| Command | Pattern | Mid-line | Newline | `L` if mid and newline independent |
|---------|---------|----------|---------|-------------------------------------|
| **HMMV** | `W` | P ∈ [45, 48] | [103, 104] | [55, 59] |
| **LMMV** | `R.d ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [71, 72] | [129, 132] | [57, 61] |
| **YMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [33, 38] | [103, 104] | [65, 71] |
| **HMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [59, 60] | **128** | {68, 69} |
| **LMMM** | `R.s → R.d → W.d` | Prs = **32**; Prd ∈ [21, 24]; Pwd ∈ [59, 60] | **128** | {68, 69} |
| **LINE** | `R ↔ W` | Pw ∈ [21, 24]; Pr ∈ [81, 84] | [119, 120] | [35, 39] |

HMMM/LMMM newline is a **point** (128). LMMM `R.s → R.d` is **32** in all modes
(sprites-on via the addend).

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

**L at a glance:** fills **58**; copy family (YMMM / HMMM / LMMM) **68**; LINE **36**.

## 6. Packed-start +1 (sprites-off)

If the previous engine RAS was packed +6, add 1 to `Δ`. As if that squeezed
memory cycle finished one cycle late. Only sprites-off has packed slots.

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
are both packed. Wraps that
**do** use +128 start from pair-offset 0, 32, 64, or 96.

On `noCpu`, packed-to-packed `Δ=32` (LMMM dest-read) **never happens**: without
CPU the source read stays on CPU-legal slots. It used to be excluded from the
+1 and handled by a separate CPU-dependent rule; experiment 4 (§10.5) shows
that was wrong. **The +1 applies to every `Δ` without exception.**

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
(`Δ=128`, where it was originally found) and the LMMM source→dest read
(`Δ=32`, which needs CPU contention to be reached at all). Everywhere else it
is silent, which is why the earlier fits were indifferent to it.

With representative waits + padding + sprites-on addend + unconditional
packed-start +1: **0 / 78330** on the 2026 `noCpu` set. 2013
`8.final-analysis` has only HMMV (`Δ=46`, newline 104), both in the
“0 / 75” rows, so 2013 cannot see the +1 at all — it is not merely untested
there, it is unobservable. 2013 `nocpu` is **3965 / 3965** either way.

## 7. Trace quality (command-only)

Excluded (`wrong`; `noCpu` files that contain CPU `R.r` / `W.w`):
`scr5-wrong-dispOff-hmmv-noCpu-nx2-6d.txt`,
`scr5-wrong-dispOff-ymmm-noCpu-nx12-1d.txt`,
`scr5-wrong-sprOff-lmmm-noCpu-nx2-3d.txt`.

Corrected and included: `scr5-dispOff-lmmv-noCpu-nx2-3d.txt`,
`scr5-sprOff-lmmv-noCpu-nx8-5d.txt`.

**Misnamed (not used as command-only):** `scr5-sprOn-hmmm-rdCpu-1.txt` is
labelled `rdCpu` but has only `R.s` / `W.d` — no CPU VRAM. Same class:
`scr5-sprOn-hmmm-rdCpu222-3e.txt`. Flattening parallel command columns invents
fake W→R pairs.

**Renamed 222 captures:** `scr5-dispOff-hmmv-rdCpu222-7e` / `8e` were first
saved as sprites-off (`1.vcd` / `3.time` still `scr5-sprOff-hmmv-rdCpu222-3e`
/ `4e`). Display was off; pair those VCDs with the dispOff `.txt`.

Some LMMM `*nx*-*d.txt` traces use `W..` instead of `W.d` where source and dest
overlap. Cadence is still `R, R, W`; those files are kept.

Two annotation problems that looked like bad captures but were not: the
`nx4` experiment-4 traces (command tags, §10.5) and the three
`scr5-dispOff-stop-rdCpu-{4f,6f,15f}` traces (all-`R.s` where it should be
all-`R.r`, §10.6). One genuine analysis bug remains, and it is in the time
axis rather than the tags: §11.2.

---

# II. CPU accesses and arbitration

The command model above is unchanged. CPU VRAM uses a **subset** of the same
command slots. The two machines differ in how request time `T` is observed, not
in the VDP arbiter (as far as 2013 can tell).

## 8. Working model (CPU + mux)

```
LINE = 1368
NEED = 16          # engine cycles of lookahead (CPU-legal slots)
BUSY = 2           # ignore CPU posts in [RAS, RAS+2)

command_slots(mode) = bitmap table (RAS)
cpu_slots(mode)     = command_slots minus packed +6     # §2.1; empty unless sprOff

engine_dist(t, s):                    # s > t
    (s - t) minus padding in (t, s]   # §3; same function as the command engine

first_cpu_slot(T):
    earliest S in cpu_slots (repeating every LINE)
    with S ≥ T and engine_dist(T, S) ≥ NEED
```

`T` is the time the **arbiter** sees the request. A constant pin-to-register
delay is wall-clock and does not see padding; on 2013 it is inside `φ0`, on
2026 inside `δ` and measured at **11**, or **13** with sprites on (§11.3, same
for reads and writes, and unaffected by R#18). The 16-cycle lookahead *does*
see padding, in full, same as
the command engine. Do not glue them into one 26- or 27-cycle lead: that
forces a CPU-only “absorb some of the stretch” rule, and dispOff/sprOff
padding is now measured at 4 (§3.3), so there is no reading in which
absorbing all of it is the engine’s own mechanism. 2013 is 0/1141 for the
split and fails for glued-27; 2026 prefers the split 129 vs 152 extra+miss.
The split is visible only across padding.

**2013 plateau.** Scanning lookahead `L` and dead window `D` over all 17 CPU
captures (not only `nocmd`): every `(L, D)` with **`L + D = 18` and `L ≤ 16`**
is 0 / 1141; everything else fails. `NEED=16`, `BUSY=2` is the top of that
band. That is **consistent with** openMSX `Delta::D16` as a lookahead
*length*, not a pin measurement of 16: 2013 cannot separate `L` from the
constant except at the padding, and 2026 sees only the sum except there
(§11). Honest wording is “consistent with 16”, not “matches”.

**Holdoff, one rule.** “Ignore posts in `[RAS, RAS+2)`” and “register frees 9
cycles before the access” are the same statement from two origins ~11 cycles
apart (pin vs arbiter). A port access at pin time `P` is lost iff `P < RAS−9`
under the pin clock, iff `(P+11) < RAS+2` under the arbiter clock. Use
`BUSY=2` with arbiter `T`.

**CPU path** — one pending buffer. A port access that arrives while the
register is occupied is **lost** (the old request keeps its scheduled slot;
the VRAM pointer does not advance). Cancelling an already-granted slot and
rescheduling is **not** what hardware does.

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

Sprites-on (2026, 12 T burst) shows **~8% more `/CSx` pulses than VRAM
accesses**. That is this buffer: slots up to ~70 cycles apart vs a ~71-cycle
request period. Predicted losses ~314 vs ~326 observed. Display-off loses
essentially none. The 37 T `rdCpu222` set has **zero** simulated losses
(§11): those extra pulses were occupancy, not a second mux. 2013 17/17 did
not need the window spelled out separately from `BUSY=2`.

**Command path** — separate buffer, never drops. After an engine access at `t`:

```
Δ = §5 wait for that step
if sprites on:     Δ += 2              # §4; FINDINGS4 used +1, see §3.2
if t is packed +6: Δ += 1              # §6; sprites-off only
take earliest S in command_slots (including packed +6)
    with S > t, engine_dist(t, S) ≥ Δ,
    and S is not a fired CPU RAS
    and S is not a dummy R.. on packed +6     # observed tag, or §10.2 if T known
```

That is the whole engine step; the packed candidate needs no further test
(§10.5 retired P5 and the newline-idle rule). If the command loses a slot it
takes the **next** slot; it does not re-arm the full `Δ`. Re-arming scores
~25% wrong on mixed traces.

**Arbitration:**

- CPU never fires on packed +6. If the CPU path wins a packed slot, the cycle
  is a dummy read and the CPU RAS is the next CPU-legal slot (§10.1).
- CPU RAS and command RAS never coincide.
- A running command does not delay CPU: `first_cpu_slot` ignores the command.
- CPU wins every contested CPU-legal slot because it is already scheduled from
  `T`; the command only takes leftovers.

### 8.1 openMSX starts counting ~29 cycles too early

The VDP rule above is from **rising** `/CSx` (this file’s decoder, §11):
measured lead **~27** = δ = 11 wall-clock pin delay (§11.3) plus the 16-cycle
lookahead.

openMSX stamps the port access at T-state 9 of `IN A,(n)` / `OUT (n),A`
(`CC_IN_A_N_2` / `CC_OUT_N_A_2` = `5+3+1`, against `CC_IN_A_N` = `5+3+4`).
That is the **start of T2** of the I/O machine cycle.
`VDP::scheduleCpuVramAccess()` then uses `VDPAccessSlots::Delta::D16` from
that time (`fixedVDPIOdelayCycles` is only the T9769/S1990 extra, not a
V99x8 constant). D16 was derived from the command engine in 2013 and
carried over to the CPU by assumption; it was never a pin measurement.
D16 is also wall-clock ≥ 16, not engine-distance.

`/IORQ` falls half a T-state into T2 and rises at the end of T3, so the pulse
is 2.5 T = **14.90** cycles — which is what the analyzer measures
(14.9 ± 0.1, §13), and that agreement is the check that the stamp really is
the start of T2. From the stamp to the **rising** edge is therefore a full
**3 T = 17.88** cycles, not the 14.9 of the pulse.

From openMSX’s own timestamp the hardware slot is `17.9 + 11 + 16` ≈ **45**
cycles later. Granting at D16 is about **29** cycles too early. (An earlier
version of this section said 25, having used the pulse width in place of
stamp→rise.)

Why CPU-only tests rarely show it: the lost-request threshold almost
cancels. Hardware drops a new post that arrives more than ~9 cycles before
the pending RAS (rising-edge / pin time), i.e. ~27 cycles before that RAS
in openMSX-stamp time. openMSX drops the new I/O if it arrives
before the already-scheduled access, and that access is ~29 early. Same
threshold to within a couple of cycles.

What does **not** cancel is which slot the CPU takes away from the command
engine. Display-off command slots are often 8 apart, so 29 cycles is
typically **three or four** slots early; sprites-on gaps are 32–64, so
usually **one** whole slot early. That is the `vdpcmdx` `+CPU` column.

Proposed (not implemented here): a **17.9 + 11 ≈ 29-cycle wall-clock**
constant from the port-access timestamp followed by the 16-**memory-cycle**
lookahead (padding-aware). Checks: `vdpcmdx` `+CPU` before and after, and
the 2026 CPU captures driven from the `/CSx` **falling** edge (this file’s
fitter uses rising).

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

Documented dummy reads in blanking (dispOff 1236/1244/1252/1260, sprOff
1242/1250/1258 in CAS-ish published tables) are **outside** the CPU/command
table. They are not the sprites-off packed dummy of §10.

What the 10 `*nocmd*cpu*` files pin down (six-way plateau, all 10/10):

- Packed +6 must **not** be in `first_cpu_slot`.
- The 16-cycle check is **engine-distance** (padding subtracted). Wall-clock
  tops out at 9/10.
- After a CPU RAS there is a **2-cycle holdoff**, or an equivalent `NEED+BUSY`
  pair with sum 18 and NEED ≤ 16 (§8).
- If a new request arrives while one is pending, the **scheduled slot does not
  move**. Keep-slot and drop-new are indistinguishable on these traces.
  Reschedule is not on the plateau. On 2026 sprites-on, drop-new matches the
  extra `/CSx` pulses; overwrite that *cancels* a granted slot scores far worse.

Same CPU parameters, command ignored: **7 / 7 mixed files at 100%**. A running
HMMV never steals or delays a CPU slot.

Skip occupied **CPU RAS** only (predicted from `φ0`, or oracle — they agree):

| Occupied set | CPU score | HMMV score |
|--------------|-----------|------------|
| Predicted CPU (`φ0` fit, §8) | **17 / 17** files, every RAS | **7 / 7** mixed |
| Oracle (observed CPU RAS) | — | **7 / 7** |

That is the **100% 2013** statement: every CPU-containing capture in
`8.final-analysis` that the fitter parses, nocmd and HMMV, matches §8.

## 10. 2026 sprites-off: packed +6 while CPU is pending

Whenever the command’s next slot is CPU-legal, the 2013 mux plus §5–§6 already
matches. Display-off mixed is 100% (no packed slots). Sprites-on mixed is 100%
except misnamed files (also no packed slots). Sprites-off LMMV is 100% without
occupying dummies (those waits rarely land on a packed +6 the CPU has reserved).

The remaining mixed misses all go the same way: the model wants a packed slot,
it stays empty or is `R..`, hardware uses a **later** slot.

Two populations of unused packed +6, **only sprites-off**, **only when CPU VRAM
is active** (0 in `noCpu`; `stop` has the same dummies with no command). They
are not one rule. The squeezed cycle has **two** ends:

- Back: engine wait **+1** after a packed access (§6).
- Front: packed slots are decided **~2 cycles earlier** than CPU-legal ones,
  and if the CPU path wins them they cannot carry a CPU access.

Occupancy (RAS), 17 stop + 99 mixed files, CPU ∩ command RAS = **0**:

| | CPU-legal | packed +6 | off-table |
|--|-----------|-----------|-----------|
| CPU RAS | **12846 / 12846** | 0 | 0 |
| Dummy `R..` | 0 | **333 / 333** (sprites-off only) | 0 |
| Command | dispOff/sprOn: all legal | sprOff: **1436 / 4608** packed | 0 |

Engine files, mixed `*rdCpu*`/`*wrCpu*` (no `-stop-`): **84** files, **12544**
engine RAS. Occupy = observed CPU RAS unless a row says otherwise.

| Model | Files | Steps |
|--|--|--|
| A — CPU RAS only | 67/84 | 12447 |
| P5 — last packed, Δ=32, `Snext > C` | 70/84 | 12508 |
| P15 — P5 + newline | 70/84 | 12509 |
| B — CPU RAS + observed `R..` | 78/84 | 12482 |
| D6+P5 — fitted `T−C ≤ −19` (CSR diagnostic) | 78/84 | 12536 |
| D6+P5+newline | 79/84 | 12537 |
| C — `R..` + P5 + newline (oracle occupy) | **84/84** | **12544** |

These rows are the pre-experiment-4 comparison and are what motivated P5.
The row that matters now is `R..` occupancy with unconditional packed-start
+1 and **neither** P5 nor the newline rule: on the full mixed corpus that is
**139/142** files and **20593/20876** steps, with all 283 leftovers in three
traces whose command cannot be identified (§10.5).

Including `rdCpu222`: oracle C is **121/121**, **18076/18076**. D6 occupy
from fitted `T` (no `R..` tag) is **105/121** (70 leftover steps). That is
occupancy **oracle** vs a closed dummy-from-`T` rule.

### 10.1 Dummy `R..` (333, all `0x1FFFF`)

Every unclassified `R.. 0x1FFFF` that is not part of the blanking four-slot
block sits on one of the 25 packed slots. The next CPU-legal slot is +26
(323) or +54 (10); that slot **is** a CPU RAS in 332 / 333. `C−6` is empty
or a command, never CPU. Zero of these dummies sit anywhere else; zero CPU
RAS sit on packed +6.

The VDP **does** grant the packed slot to the CPU path. That memory cycle
cannot carry a CPU access, so it strobes `0x1FFFF` and the CPU is served one
slot later.

**Not every CPU at `C+26` produces a dummy.** Stop traces: 326 CPU RAS whose
predecessor in the table is packed `S−26`, only **42** dummies. Mixed files:
35 dummy skips vs 49 packed **hits** with CPU at `C+26` in the same ~32-cycle
T-window. Occupying the observed `R..` tag perfects HMMV/YMMM; it is an
oracle, not a predictor. Dummy rate is the same at 12 T and 37 T (~7% of
sprites-off CPU RAS). Crowding did not create dummies.

### 10.2 Dummy vs hit: packed decided 2 cycles earlier

On the full command table, NEED=16 grants packed `C` for arming `T` with
`T−C ∈ {-21…−16}` while the CPU-legal table already grants `C+26`. Hardware
is stricter on packed:

| | `T−C` |
|--|--|
| mixed dummy | **−21, −20, −19**, two at −18 |
| mixed packed hit, CPU at `C+26` | starts at **−18** |
| stop dummy | same early cluster |
| stop empty (CPU at `C+26`, no dummy) | starts at **−18**, **never** −21…−19 |

Stop, cut `−22 < T−C ≤ −19`: dummy **29**, empty **0**. `first_slot` on the
full table with **NEED=19** is exactly `{−21, −20, −19}`.

Same fact from the pin: display-off lead 26.19 ± 0.38; packed-pair slots
28.01 ± 1.41. Uniform 26-cycle lead predicts **472** dummies vs 335 observed;
the extra 2 cycles predicts **336**.

**D6** (needs arming `T`): skip packed `C` if CPU RAS is `C+26` and
`T−C ∈ (−22, −19]`. Plus P5: **78/84** files, **12536** steps — same file
count as occupy-`R..`, more steps. The −18 overlap is a 1-cycle tie (2 mixed
dummies, 3 mixed hits). Independent ±1 on each `T` does **not** turn NEED=16
into that cut. Majority ±1 with NEED=19 **is** D6.

Beat-phase `T_i = floor(t2_i + ε)` slides 71/72 doubles and is the right
*noise model* for 2026 `T`; it does not classify dummy. D16-best `ε` is
slightly worse than integer D6. Engine-oracle `ε` still leaves
`ymmm-wrCpu-2` and `ymmm-wrCpu-3`.

For emulation, `T` is known (Z80 I/O). D6 is then the dummy rule. Do not put
`/CSR` into the model; D6 is an early packed grant, not “any pending CPU.”
Five files still miss with D6+P5+newline (LMMM Δ=60 fringe, −18 ties,
unmatched posts).

### 10.3 Idle packed +6 — P5, superseded by §10.5

> **Retired.** P5 below was fitted before experiment 4. Its 58 idle cases are
> all `Δ=32` from a packed slot, and §10.5 shows they are just packed-start
> +1 (§6) with no CPU predicate at all. Kept because the table is still the
> evidence that occupying *every* packed candidate over-blocks.


Every sprites-off step whose unconstrained §5 candidate `C` is packed +6,
versus the next observed CPU RAS `S`. Invert `first_cpu_slot` to a T-window
`[Tmin, Tmax]` for that `S`. `/CSR` is not used.

| Outcome | n | `Tmax ≤ C < S` (must pending) | `Tmin ≤ C < S` (maybe) | else |
|---------|---|-------------------------------|------------------------|------|
| Command **takes** `C` | 1367 | **0** | 49 | 1318 |
| Skip, slot is `R..` | 35 | **0** | **35** | 0 |
| Skip, slot **idle** | 62 | **0** | 58 | 4 |

Nothing sits in “must pending”. Occupying every packed `C` with
`Tmin ≤ C < S` **over-blocks**.

**Idle group (58/58 maybe):** last access packed +6, `Δ = 32` (LMMM
`R.s→R.d`), candidate `C` packed. The 49 maybe-**hits** never have `Δ = 32`.

**P5:** last packed, `Δ = 32`, candidate `C` packed, skip `C` iff the
**first CPU RAS after `last` is still after `C`** (`Snext > C`).

- If a CPU RAS already sits in `(last, C]` (typically `C−6`, the pair-start):
  command **takes** packed `C` (overflow).
- If `Snext > C`: command **skips** `C` (idle). Often then uses `C+26`; CPU
  uses `C+26` or `C+58`.

A “CPU in `(C, C+64]`” window **false-skips** the `C−6` overflow hit. P5 is
**not** “any CPU later in the file.” Skipping Δ=32 packed with **no** CPU
predicate is not P5: it only looks safe on `noCpu` because that geometry does
not occur there.

Example, `scr5-sprOff-lmmm-rdCpu-1.txt`: `R.s` at RAS **2068** (row 700),
first legal command slot **2100** (row 732, packed), idle; CPU at **2126**
(row 758); dest-read at **2132** (row 764). P5 skips 2100 (`Snext=2126 > C`).
Same file, overflow: last packed, cand packed, CPU at `C−6`; P5 does **not**
skip.

`noCpu` stays 514/514 (P5 never fires).

### 10.4 Newline idle — wrong, retired

> **Retired.** The rule below (`Δ=128`, last packed, candidate packed, CPU
> already in `(last, C]` → skip) was fitted on a single HMMM step. In
> experiment 4 it fires **20 times (18 HMMM, 2 LMMM) and is wrong all 20
> times**: the command takes the packed candidate every time. Extending it to
> YMMM's `Δ=104` adds 25 more cases, also all wrong — 45/45. Deleting it and
> letting packed-start +1 do the work makes the line break exact. See §10.5.

Original text: HMMM newline, `Δ = 128`, last packed, candidate packed, CPU
**already** in `(last, C]`; skip the packed landing. One extra HMMM step.

A shared “skip Δ=60 last-packed if CPU at `C+26`” helps LMMM and **hurts**
HMMM — not a rule.

### 10.5 Experiment 4: both CPU-oracle rules disappear

`scr5-sprOff-{ymmm,hmmm,lmmm}-rdCpu-nx4-{1..6}f`. Screen 5 has
`PIXELS_PER_BYTE = 2` and `clipNX_2_byte` halves NX, so NX=4 is **2 bytes per
dest line**: every second command access is a line break. That turns the two
rare geometries of §6.1 into bulk statistics — 152 `Δ=32` steps landing on a
packed candidate (68 of them from a packed start) and 45 packed-to-packed
line breaks, against a handful before.

The rectangle also sweeps out of the designated VRAM windows within a few
lines, so `2.rw` mis-tagged command accesses as CPU (and as sprite/bitmap)
ones. The tags were repaired by hand and cross-checked from the address
structure (`retag.py`): a CPU read auto-increments, so CPU reads are one
strict `+1` chain; an LMMM dest read reads the address it then writes;
anything else read is a source read. All 18 files then validate — the CPU
chain has no step other than `+1`, the command accesses match
`R.s,W.d` (HMMM/YMMM) or `R.s,R.d,W.d` (LMMM) with **zero** deviations over
all 2461 command accesses, and src→dst is `0x4000` throughout. Two files
(`hmmm-nx4-1f`, `-5f`) still needed 60 and 56 `R.r → R.s` fixes, because
there the source region overlaps the CPU window and only the `+1` chain
separates them.

**The `Δ=32` step is a pure lattice property, not a CPU one.** All 308 LMMM
source→dest steps, by the geometry of the unconstrained candidate `C`:

| `engine_dist(last, C)` | `last` | `C` | takes `C` | skips `C` |
|--|--|--|--|--|
| **32** | **packed** | **packed** | **0** | **47** |
| 32 | plain | plain | 99 | 0 |
| 38 | plain | packed | 82 | 0 |
| 64 | packed | packed | 21 | 0 |
| everything else (40 … 76) | either | either | 59 | 0 |

One geometry always skips and every other geometry always takes: `Δ=32` from a
packed slot to a packed slot is **never** taken (47/47), and everything else —
including a packed `C` at distance 38 (82 cases) and at 64 (21 cases) — is
**always** taken (261/261). That is precisely `Δ → 33`. P5's CPU predicate
was a proxy: its 21 “overflow” hits are the distance-64 row, where the CPU
had taken the pair-start and pushed `C` one pair further. P5 itself scores
151/152 here; packed-start +1 scores 152/152 and needs no CPU.

With the waits of §5, engine-distance, unconditional packed-start +1, and
occupancy = observed CPU RAS + observed `R..`, `exp4.py` predicts these 18
files at **2440 / 2440 steps, 18 / 18 files**.

**Result on the whole mixed corpus** (`R..` occupancy, `eng R..`). The two
changes only work together — neither is an improvement on its own:

| | files perfect | steps | leftovers |
|--|--|--|--|
| P5 + newline idle, `Δ=32` excluded from +1 (old) | 131 / 142 | 20494 / 20876 | 382 |
| unconditional +1, P5 + newline idle still on | 132 / 142 | 20521 / 20876 | 355 |
| P5 + newline idle dropped, `Δ=32` still excluded | 128 / 142 | 20044 / 20876 | 832 |
| **unconditional +1, neither rule** | **139 / 142** | **20593 / 20876 (98.6%)** | **283** |

Keeping P5 alongside the unconditional +1 over-skips (both mechanisms fire on
the same candidate); dropping P5 without the +1 leaves the `Δ=32` skip
unexplained. `--scratch`, which scores only the 139 identified-command files,
now reads `A 113/139  P 112/139  B 139/139  C 132/139` — **B**, the model with
observed-dummy occupancy and no CPU predicate, is **20537 / 20537 steps**, and
re-adding P5 + newline (C) makes it worse.

Per command with observed-`R..` occupancy, the second model is
`hmmv 34/34`, `lmmv 16/16`, `ymmm 23/23`, `hmmm 42/42`, `lmmm 21/21`,
`line 3/3` — **every file with an identified command is exact, 20593 /
20593**. The 283 remaining leftovers are all in the three
`scr5-dispOff-stop-rdCpu-{4f,6f,15f}` traces, whose command `fit_2026.cc`
cannot identify (`unk`, and no CPU accesses either); they are a trace-quality
problem, not a model one — and §10.6 resolves them. The rule change is
engine-only: the CPU fit is 19383 / 19474 before and after it.

The retag also repaired the CPU aggregate, which the mis-tagged files had
been poisoning with ~590 phantom CPU accesses: **158 / 209 files and
19385 / 20062 steps → 163 / 209 and 19383 / 19474**. Experiment 4's own CPU
score is 13 / 18 files, **2024 / 2030 (99.7%)**, 2 extra + 6 miss — slightly
*better* than the 99.5% corpus average, so these captures say nothing new
about the CPU path.

So the answer to what experiment 4 was designed to ask — is the
LMMM-idle vs HMMM-hit split at a sprites-off newline real? — is **no**. There
is no CPU-dependent term in the engine step at all. The engine only needs
(a) the waits of §5, (b) padding-aware engine-distance (§3), (c) the
sprites-on addend (§4), (d) packed-start +1 (§6), and (e) skip slots another
master actually took. Every rule that referenced the *future* CPU access is
gone, which also makes the model implementable: openMSX cannot look ahead at
the CPU, and now it does not have to.

### 10.6 Correction round 2: the engine model is exact everywhere

Five more files were re-annotated by hand (commits `401d360`, `a0b79af`):
`scr5-dispOff-stop-rdCpu-{4f,6f,15f}` and `scr5-sprOff-hmmm-rdCpu-nx4-{1f,5f}`.

The three `dispOff-stop-rdCpu` traces of §10.5 were not low-quality captures at
all. Every access in them had been tagged `R.s` — a *command* source read —
when the file name says `stop`, i.e. no command. That is why `fit_2026.cc`
reported an unidentifiable command and no CPU accesses. They are now all
`R.r`, which the addresses confirm independently: per scan line each column is
one strictly `+1` chain, with no writes and no dummies, exactly what `stop`
plus an `IN A,(#98)` burst must produce.

`retag.py check` over the whole experiment-4 set now reports **18 / 18 files,
zero corrections**, and the HMMM files are structurally consistent
(60 `R.s` / 60 `W.d`, no `R.d`).

Corpus totals after the repair. The 339 accesses in those three files move out
of the engine tally and into the CPU one, and every one of them is predicted
correctly — the CPU miss and extra counts did not change. The CPU rows below
also include the line-origin fix of §11.2:

| | files perfect | steps | wrong |
|--|--|--|--|
| command-only (`--nocpu`) | 514 / 514 | 78330 / 78330 | 0 |
| mixed engine, occupancy = CPU RAS + observed `R..` | **139 / 139** | **20537 / 20537** | **0** |
| mixed engine, occupancy = CPU RAS only | 113 / 139 | 20198 / 20537 | 339 |
| mixed engine, D6 (no dummy tag) | 116 / 139 | 20438 / 20537 | 99 |
| CPU, all modes | 166 / 212 | 19743 / 19812 (99.7%) | 28 extra + 69 miss |
| CPU, display off | 85 / 88 | 8477 / 8480 (100.0%) | 3 |
| CPU, sprites off | 53 / 72 | 6954 / 6984 (99.6%) | 30 |
| CPU, sprites on | 28 / 52 | 4312 / 4348 (99.2%) | 36 |

The four CPU rows are the corpus as it stood before the phase-scan round, and
they also predate the VDS decoder fix and the retraction of the `3e` edit; §11.2
has the current numbers. The engine rows are unaffected — the new captures are
all `stop`, and the decoder fix leaves `--nocpu` at 100%.

Per command, with observed-`R..` occupancy: `hmmv 34/34`, `lmmv 16/16`,
`ymmm 23/23`, `hmmm 42/42`, `lmmm 21/21`, `line 3/3`. **There is no file left
in either corpus, on either machine, where the command engine mispredicts a
step.** The whole residue is now on the CPU side, and §11.2 removes the single
largest contributor to that as well.

## 11. 2026 CPU request time and the sparse set

**Do not** fit a 72/252 lattice on the 8280 VDP timeline. Intra-burst `/CSR`
gaps are **71 or 72** (typically alternating; same-gap doubles when the
fraction wraps). Independent ±1 on neighbouring posts would produce 70/73;
those almost never happen. The uncertainty is **where** the extra/missing
cycle sits — one analog phase for the burst.

Request time from `/CSR` (reads) or `/CSW` (writes), converted onto the VDP
clock (refresh interpolation, or `/RAS` grid). Pace-fit: one straight line
through the capture (Z80 crystal), residual **~0.15** cycle. Integer `δ` in
`T = floor(t2) + δ` preserves all gaps. `T_i = floor(t2_i + ε)` slides 71/72
doubles.

The bus measures only the **sum** of pin→arbiter delay and NEED=16. From
rising `/CSx`, display-off lead **26.19 ± 0.38** memory cycles (142 slots).
Packed +6 slots **~2 cycles earlier** (28.01 ± 1.41). For emulation the sum
matters; the pins cannot split it *here* — §15.2 splits it from the padding.
Split it in the model anyway (§8): constant in `δ`, lookahead sees padding.
openMSX currently applies D16 from the port-access stamp (~29 cycles too
early, §8.1).

Scoring: treat a **D16 tie** as a hit (`first_cpu_slot(T±1)`). The simulator
still emits `first_cpu_slot(T)`. The VDP samples `/CSx` on a **1-cycle**
grid (rounding to 1 cycle beats 2 or 4).

**Decoder (2026 `/CSx`):**

- Use **rising** edges (`0→1`), and only where the previous sample was
  recorded low. A pulse already low at t = 0 is still a request: the rising
  is the end of the I/O cycle, which is when the VDP schedules. Do **not**
  synthesize a falling edge at t = 0 (`fit_2026.cc` starts `/CSx` unknown).
  On a tie, prefer rising. Drop pulses shorter than 8 analyzer samples
  (1-sample spikes; a real I/O is ~56 samples).
  `scr5-dispOff-hmmv-rdCpu222-7e` / `8e` share the sprites-off `3e` / `4e`
  VCDs.
- Do **not** invent posts. A capture can start with a request already in
  flight (no `/CSx` for the first RAS). The first access is ~15× more often
  wrong than the rest (21 of 121 pace-fit leftovers). Same policy as command
  traces starting mid-command: do not score a RAS whose complete `/CSx` was
  never recorded. Do not add a per-capture initial-state knob.
- Family A (first analog gap ~18 T, then 12): skip the first observed RAS as
  a scoring window, not a VDP rule.

**Pace-fit CPU scores** (one constant per capture, NEED=16, full padding):

| pace | mode | accesses | not predicted | lost requests | pin constant |
|------|------|----------|---------------|---------------|--------------|
| 72 | display off | 4793 | 43 (0.90%) | 0 of 4848 | 9.85 ± 0.12 |
| 72 | sprites off | 4346 | 16 (0.37%) | 12 of 4385 | 9.84 ± 0.11 |
| 72 | sprites on | 3703 | 59 (1.59%) | 304 of 4022 | 12.10 ± 2.01 |
| **222** | display off | 675 | **1** (0.15%) | 0 of 685 | 9.75 ± 0.22 |
| **222** | sprites off | 608 | **0** | 0 of 610 | 9.72 ± 0.30 |
| **222** | sprites on | 641 | **2** (0.31%) | 0 of 646 | 10.61 ± 1.18 |

2026 total: **121 / 14766 (0.82%)**, of which 21 are capture-start; from the
second access **100 / 14600 (0.68%)**. Split (constant + 16, padding 4 in
dispOff/sprOff) beats a glued 27-cycle lead (129 vs 152 extra+miss; 94 vs
87 perfect captures before the sprites-on padding correction). That padding
is measured at 4 (§3.3), so 2013 does decide between them. Interior 37 T
I/Os do not need a new NEED.

The 26 new display-off `scr5-dispOff-stop-{rd,wr}Cpu-*f` captures put **56** CPU
accesses on the three padded HBLANK slots (CAS 1326 / 1336 / 1345), up from
6, and the split model is perfect on all 26. Rescanning `NEED` on the
enlarged stop corpus still puts the optimum at **16** alone (49/58 files;
15 and 17 give 48/49 with more extras, 18 and 20 clearly worse).

Sprites-on wants more pin constant than the other two modes: at 37 T
**~1 cycle** (10.61 vs 9.75, scatter ±1.2), the same *direction* as the command
addend. The 12 T sprites-on constant (12.10) is loss-contaminated, and neither
program resolves the size. **Superseded by §11.3:** the 141-cycle phase scan
sweeps the whole line and measures the difference as **+2**, for writes as
well as reads. That is a fact about the CPU path only; the *command* addend is
1 or 2 and nothing separates them (§3.2), so the two are no longer evidence
for each other.

`fit_2026` integer-`δ` (NEED=16, padding +4 / addend +1, D6 occupy, decoder above),
including `rdCpu222` and the 7e/8e VCD aliases. **This table predates the
annotation fixes and the phase-scan round; current totals are in §11.3:**

| | files perfect | CPU RAS |
|--|---------------|---------|
| all | **127/168** | **14762/14847 (99.4%)** extra 44 miss 85 |
| display-off | 59/62 | 5541/5544 (99.9%) |
| sprites-off | 40/54 | 4930/4954 (99.5%) |
| sprites-on | **28/52** | **4291/4349 (98.7%)** (was 24/52, 4288/4349 with +4/+1) |
| stop | 26/35 | 2529/2566 |
| +command | 101/133 | 12233/12281 |

Engine, 121 files with command tags: occupy observed CPU RAS + P5 **106/121**;
occupy packed `R..` too **121/121 (18076/18076)**; D6 from fitted `T` (no tag)
**105/121** (70 leftover steps). Occupancy oracle is still the ceiling;
D6 is the closed rule (was NEED=16 packed occupy: 95/119, 90 leftover).
(Pre-experiment-4 corpus and rule set; P5 is retired, see §10.5.)

Leftover extra+miss by position: start 43, mid 82, end 4. Do not add a
skip-first-RAS VDP rule.

### 11.1 Sparse CPU (`rdCpu222`)

20 × `IN` with `EX (SP),HL ; NOP` between them. Same NMS 8280, same slot
tables, same §8 model.

| | T-states | ×6 (sync 8250) | × 5.96112 (8280) |
|--|----------|----------------|------------------|
| IN → IN | 37 | 222 | **220.56** |
| burst wrap (20 INs) | 67 | 402 | **399.40** |

Gaps in the VCD are 220/221 and 399/400, not 72/252. `first_cpu_slot` wait is
at most **59 / 75 / 85** (dispOff / sprOff / sprOn), far below 220, so the
buffer is always empty. **sim-lost = 0.** `BUSY=2` is not tested by this
program.

Engine (36 files with command tags), skip observed CPU RAS: occupy CPU
RAS 35/36; occupy packed `R..` too **36/36**. HMMV 18/18 without dummy
occupy. HMMM 17/18 RAS-only, 18/18 with `R..`. (P5 was still on here; it
never fires on these files, and dropping it changes nothing.)

Do **not** change NEED, BUSY, or the slot tables to fit leftover first RAS
on this set. Analog `T` for interior I/Os sits in the NEED=16 grant window
(or 1 cycle off: D16). A “skip first slot” rule fitted to START ±8/±32/±64
**breaks** the clean files.

### 11.2 The absolute line origin is the one axis the `.txt` does not record

`scr5-sprOn-stop-rdCpu222-3e` used to be written off as a bad capture (best-fit
`δ = 40` where every other file lands on 9–14, 13/36 accesses predicted). It is
not. The raw `/CSR` train in the `.vcd` is 39 pulses spaced a rock-steady
**827.5 analyzer samples** apart — 37 T exactly — with zero `/CSW` pulses, and
the 36 VRAM reads are 36 *consecutive* addresses `0x1438f … 0x143b2` with no
gaps. Both 399-cycle loop tails sit exactly 20 requests apart in the request
train and in the observed accesses. The hardware did what the file name says.

The earlier “`/CSx` gaps 1064, 51, 52, 177, 399, 221 instead of a uniform 220”
was a misread diagnostic: `dump_diag` prints only gaps *outside* 68–75 and
240–260, bands hard-coded for the 12 T burst's 72/252. For a 37 T loop **every**
gap is “other”, so that list is the normal output — 220/221 is the correct
period and 399 the correct 67 T tail.

**The symptom is a one-line offset between the `.txt` and the `.vcd`.**
Shifting the request axis by **+1368** takes the file from 13/36 to 34/36 with a
clean `δ` plateau at 11–15, exactly its five siblings' band (they peak at shift
0). One of the two sides has the origin wrong by a line — and the cause turned
out to be `fit_2026.cc`, not the `.txt`. That is settled below; the mechanism of
*how* an origin can be off by a line comes first, because it is common to both.

Mechanism. `parse_txt` reconstructs absolute time as `1368 * column + row`, so
the origin is implicit in the column index, and every consumer has to re-derive
it. Both `process.cc` (which wrote the `.txt`) and `make_anchors` in
`fit_2026.cc` do that the same way: `find_refresh_starts` finds the first
*group* of 8 refresh bursts, that group start is assigned `r = 8`
(= line 1, row 285), and any bursts before it are numbered backwards
`r = 7, 6, 5, …`. Because the scan starts at `i = 1`, the very first burst can
never be a group start, so a capture that begins mid-line is normal: 1 to 7
leading bursts, origin still inside line 0. But if **more than 8** bursts
precede the first detected start, the back-counting runs past `r = 0` and the
origin lands in line −1. Corpus-wide, `pos_of_first_start` is:

| bursts before first detected group start | files |
|--|--|
| 1 … 7 (capture starts mid-line) | 145 |
| 8 (capture starts on a line boundary) | 64 |
| **9** | **2** |
| **15** | **1** |

Exactly three files put the origin in line −1, and only one of them is broken:

| file | pos | `.txt` uses negative rows? | verdict |
|--|--|--|--|
| `scr5-sprOff-hmmm-rdCpu-nx4-6f` | 15 | yes (`3.time` starts at row −969) | consistent, 94/94 |
| `scr5-sprOn-line-wrCpu-2` | 9 | yes (`3.time` starts at row −1232) | consistent, 95/96 |
| **`scr5-sprOn-stop-rdCpu222-3e`** | **9** | **no (`3.time` starts at row 3)** | **1368 off, 13/36** |

For the first two, `process.cc` also placed the capture start in line −1 and
emitted negative row numbers, so both sides agree and the files fit; the manual
step later deleted those negative rows, which does not renumber anything. For
`3e`, `process.cc` put the first refresh burst at line 0 row 1181 while
`fit_2026.cc` derives line −1 row 1181 — their refresh candidate lists differ
by one burst at the capture edge, and one burst is one whole line of origin.

Why only this file shows it: a whole-line shift is absorbed into `δ` when it is
close to an integer number of CPU loop periods. For the 12 T burst
(71.5 cycles) 1368 is 19.1 periods, so the residue is small and the fitter
re-pairs pulses with accesses at a slightly different `δ`. For the 37 T loop
(220.6 cycles) 1368 is 6.20 periods — a 45-cycle residue, far outside
`δ ∈ [0, 40]`. The sparse program is what makes the offset visible, not what
causes it.

Consequences. `3e` contributed **23 of the 91** CPU misses and 19 of the 46
extras, none of which was model error. It did not need recapturing.

#### Root cause: `fit_2026.cc` could not tell a refresh from a bitmap fetch

The first attempt at a fix edited the data: every cell of
`scr5-sprOn-stop-rdCpu222-3e.txt` was shifted one 13-character column left and
the old column 0 dropped, in all three stages (commit `de6ddf2`). **That was
wrong and should be reverted** — it made the file agree with a broken tool at
the cost of 44 real accesses. `process.cc` had the origin right all along.

The bug surfaced when `--origin` flagged `scr5-sprOff-stop-rdwrCpu-3g` in the
next batch of captures. Comparing the two refresh candidate lists access by
access, they agree on every timestamp except one:

```
process.cc / vcd2.cc  candidate gaps: … 60125 60125  60000 60000  59875 …
fit_2026.cc           candidate gaps: … 60125 60125  23375 96625  59875 …
```

23375 + 96625 = 120000 = 60000 + 60000, so one candidate is simply the wrong
access. Both are at address `0x0383f`:

```
757375  Rbv 0x0383f     bitmap fetch, 4th byte of a page-mode burst
794000  R.. 0x0383f     the actual refresh
```

`vcd2.cc` prints three characters: `R`/`W` from the R/W pin, then `b` if this is
**not** the first CAS after RAS (a page-mode burst member), then `v` if **VDS is
low** (the display is fetching). `process.cc`'s `candidate_filter1` requires the
type to be exactly `"R.."`, so it rejects the bitmap fetch. `fit_2026.cc`
decodes the `.vcd` itself and **never read VDS**, so its `Acc` carried only
`{t, addr, rd}` and its `candidate_filter1` could test nothing but
`(addr & 0x3f) == 0x3f`. In screen 5 the bitmap pitch is 128 bytes, so an
address ending in `0x3f` occurs in the display fetches of every line.

`candidate_filter2` normally throws these out, because it requires the
`0x10000` bank flip and `+1` in the high bits. It cannot here: the impostor has
the *exact address the chain expects next*. Then `find_refresh_starts` fails on
the window that contains the bad gap — `similar_small_gaps` needs
`max − min ≤ 3·median/5`, and `96625 − 23375 = 73250 > 36000` — so the group
start at candidate 6 is missed, the next one at candidate 14 is declared line 1,
and the whole capture slides by 8 refreshes = **one scan line**.

How latent this was: of 887 rw captures, **522 contain at least one display
fetch that the old filter would take for a refresh**, 17681 accesses in all,
typically ~31 per capture. Only 9 of 266 CPU captures were actually mis-anchored,
because the address-chain test catches all but an exact-address collision.

**Fix, applied** (`fit_2026.cc`): `decode_vcd` now reads `VDS` and tracks
"first CAS after RAS", `Acc` carries both flags, and `candidate_filter1`
requires read + first + VDS-inactive — the exact `"R.."` condition
`process.cc` uses. Effect on the corpus:

| | before | after |
|--|--|--|
| `scr5-sprOff-stop-rdwrCpu-3g` | 49 / 115, `δ = 19` | **115 / 115, `δ = 11`** |
| `scr5-sprOff-hmmm-rdCpu-1` | 112 / 115, `δ = 27 fall` | **115 / 115, `δ = 11` rise** |
| `scr5-sprOn-stop-rdCpu132-17g` | 59 / 62, `δ = 31 fall` | **62 / 62, `δ = 12` rise** |
| `scr5-sprOn-stop-rdCpu132-9g` | 60 / 62 | **62 / 62** |
| `scr5-sprOn-stop-wrCpu132-9g` | 58 / 61 | **61 / 61** |
| `scr5-sprOn-hmmm-rdCpu-2` | 101 / 105 | **105 / 105** |
| `scr5-sprOff-lmmv-wrCpu-3` | 115 / 116 | **116 / 116** |
| `3e`, as edited by `de6ddf2` | 34 / 35 | 14 / 35 |
| `3e`, original | — | **36 / 36, `δ = 11`, plateau 11–15** |

Note the four nonsense `δ` values (19, 27 fall, 31 fall, 15) that turn into
10–12 rise: a mis-anchored capture shows up as a `δ` outside the physical band,
which is a second, independent smell test for this class of defect.

With the decoder fixed **and `de6ddf2` reverted**, `--origin` reports the whole
corpus clean and:

| | files perfect | CPU RAS |
|--|--|--|
| all | **218 / 266** | **23924 / 23990 (99.7%)** extra 28 miss 66 |
| display off | 97 / 100 | 9526 / 9529 (100.0%) |
| sprites off | 66 / 83 | 7900 / 7926 (99.7%) |
| sprites on | 55 / 83 | 6498 / 6535 (99.4%) |
| `stop` | 101 / 115 | **9659 / 9679 (99.8%)** extra 7 miss 20 |

Engine untouched (139 / 139, 20537 / 20537; `--nocpu` 100%).

The lesson is not about this one capture. Two programs derived the same timebase
from the same `.vcd` by two different routes, and one of them was missing a pin.
`process.cc`'s route is authoritative because `vcd2.cc` reads every signal;
`fit_2026.cc`'s private decoder must stay a faithful port of it, or be replaced
by reading `2.rw` for the timebase and the `.vcd` only for `/CSx`.

**Guard: `fit_2026.cc --origin`.** Counting refresh bursts is *not* a usable
test — `pos_of_first_start > 8` is true for three files and only one of them
was wrong, and after the fix `3e` still has `pos = 9`. The property that
actually matters is whether the `.txt` and the `.vcd` agree, so test that
directly: score every CPU capture with the request axis shifted by −1, 0 and
+1 lines and complain if a non-zero shift wins.

```
$ ./fit_2026 --origin
  266 CPU captures, 0 with a line-origin mismatch
```

It is decisive rather than heuristic: it gets both the direction and the delay
right in every case seen so far, on either side of the argument. Before the
decoder fix it reported

```
  scr5-sprOn-stop-rdCpu222-3e.txt      shift +1: 34/36 (delta=13)   shift 0: 13/36
  scr5-sprOff-stop-rdwrCpu-3g.txt      shift +1: 113/115 (delta=11) shift 0: 49/115
```

and after it — with `de6ddf2` still in place — it correctly asked for that
commit to be undone:

```
  scr5-sprOn-stop-rdCpu222-3e.txt      shift -1: 35/35 (delta=11)   shift 0: 14/35
```

The flag is additive; `--nocpu`, `--scratch` and the default run are unchanged
byte for byte. Run it whenever captures are added, a `.txt` is edited by hand,
**or the VCD decoder is touched** — it is the only check that would have caught
the VDS omission.

Better still would be to stop re-deriving the axis: record it in the `.txt`
(one anchor pair, raw analyzer sample ↔ VDP cycle, or just the absolute cycle
of column 0), or trim every capture to start at a line boundary. The latter
also removes the capture-start leftover class of §11 (~48 of the CPU leftovers
are the first access of a capture, armed before the window opened).

Not a problem, for the record: 17 files have a different column count in
`4.manual-fix-timing` than in `5.slots`. The 4→5 step is a `grep` on row
numbers only (`filter-{dispOff,sprOff,sprOn}.sh`), so a trailing column whose
entries all sit on non-slot rows simply becomes empty and stops being visible.
Column *positions* are untouched, so nothing shifts.

### 11.3 The phase-scan round: δ is one integer per mode

Three experiments, committed as `3f8354d`:

| set | files | program | question |
|-----|-------|---------|----------|
| `scr5-{mode}-stop-rdCpu132`, `scr5-sprOn-stop-wrCpu132` | 38 | `IN A,(#98) ; NOP ; NOP` | fine phase scan across the whole line |
| `scr5-{mode}-stop-rdwrCpu` | 18 | `IN A,(#98) ; OUT (#98),A` | is the write pin delay the read pin delay? |
| `scr5-{mode}-stop-rdCpu-adjust{0,2,…,14}` | 48 | 16 T read loop | does R#18 change CPU arbitration? |

The loops actually run at **141** and **76.6** cycles per request, not the
intended 132 and 72 (23.5 T and 12.8 T — M1 wait states). That helps: 141
against the 128-cycle refresh cell creeps ~13 cycles per request and sweeps a
whole cell in ~10 requests, so a 62-request capture probes every slot gap in
the line instead of the two or three that a 72-cycle burst reaches. The
sprites-on `rdwrCpu` variant paces at 82.2 rather than 76.6, so it is probably
not quite the same program; the fits do not care.

This is what finally separates the δ plateaus. Forcing a single δ across
**all** `stop` CPU captures, old and new (`--rw --sel=… --force=D`):

| mode | δ=10 | δ=11 | δ=12 | δ=13 | δ=14 | accesses |
|------|------|------|------|------|------|----------|
| display off | 4866 | **4886** | 4848 | 4297 | 3799 | 4888 |
| sprites off | 1725 | **1733** | 1730 | 1652 | 1583 | 1741 |
| sprites on | 2882 | 2959 | 3031 | **3036** | 3022 | 3050 |

**δ = 11 for display-off and sprites-off, δ = 13 with sprites on.** Display-off
is sharply peaked — one cycle either way costs 20 to 40 accesses — and the new
scan files alone are exact: 368/368 display-off and 374/374 sprites-off at
δ = 11, with the per-file plateaus intersecting on exactly {11}. This replaces
the old "9.7 to 12.1, scatter ±1 to ±2" pace-fit constants of §11, and it
supersedes the §11 caution that the sprites-on excess was "the same direction
as the command addend, not a measurement of +2": on the CPU path it now
measures **+2** in its own right (and §15.2 excludes `NEED` as its home).

(These rows are with the §11.2 VDS decoder fix and `de6ddf2` reverted. Before
them the sprites-off row read 1611 / 1618 / 1618 / 1547 / 1485 out of 1626,
dragged down by the single mis-anchored `rdwrCpu-3g`; the winning δ never moved.)

**The sprites-on +2 is uniform along the line.** §11's earlier guess — that it
concentrated on the second slot after each refresh (RAS 348 + 128k) — is wrong.
Held at δ = 11, the sprites-on failures scatter over **18 of the 31 slot rows**,
and RAS 348 accounts for 1 of 58. It is a flat shift of the whole request path,
so it belongs in δ, not in a per-slot correction. (It is not separable from a
+2 shift of `SLOTS_SPRON` itself: the engine rule only constrains *differences*
between successive engine accesses, so a uniform shift of the sprites-on table
is invisible to it and visible only here.)

**Reads and writes share one pin delay.** The interleaved captures are the only
ones carrying both edge trains at once, so they are the only ones that can
separate the two. Fitting `δ_r` and `δ_w` independently (`--rw`) over the 17
scoreable files: `δ_w − δ_r` is **0 in 8 files, ±1 in 6, +2 in 1**, and forcing
them equal costs nothing. The nine `wrCpu132` files add the first sprites-on
CPU *write* phase scan, and they peak at δ = 12 (557/559, against 552 at 11
and 556 at 13 — flat), so the sprites-on offset is not an artefact of the
read-ahead path either.

**Set-adjust does not touch CPU arbitration.** The 48 `adjust` captures had only
reached `1.vcd/rw`, so they were run through `process.cc` into a scratch
directory — for a `stop` capture `3.time` is already enough, since there are no
display fetches for the 4→5 `grep` to remove. The best-fit δ is then identical
at every R#18 value from 0 to 14 (10–11 display-off, 12–14 sprites-on), i.e.
**R#18 does not move the lattice the CPU sees**. Holding δ = 11 across the eight
display-off adjust values:

- outside HBLANK: **1436 / 1440** correct
- HBLANK (rows 1268–1367): 134 / 178

and every deviation is an access landing on the shifted HBLANK slots that §3.4
already measured from the `noCpu` engine captures at 4 cycles per R#18 unit. At
R#18 = 2 the observed rows are 1333 and 1343, exactly where §3.4 puts them. So
engine and CPU share one adjust-shifted lattice and the CPU path needs no
separate correction — which also settles the question this set was designed for:
a *misplaced* padding block only breaks the few HBLANK slots and leaves δ, a
wall-clock delay, alone.

**Two tooling bugs, not model failures.** The first run over the enlarged corpus
fell from 99.7% to 95.6%. Both causes were in `fit_2026.cc`:

- `bool write = name.find("wrCpu")` also matches `"rdwrCpu"`, so all 18
  interleaved captures were fitted against the `/CSW` train alone. That matched
  every write and missed every read — the tell-tale 57/113 scores.
- `annotate_type` in `process.cc` reserves `0x14000`–`0x16000` for CPU reads, so
  a write into that region is tagged `W.r`, and `parse_txt` had no branch for
  `W.r` and dropped it silently. That hid all nine `wrCpu132` files completely
  and half the writes in eight `rdwr` files. `W.r` occurs in exactly those 19
  files and nowhere else in the corpus, so accepting it in the reader is safe;
  the `.txt` files are left as captured.

Fixed (`load_capture` merges both trains for `rdwr`; `parse_txt` accepts `W.r`),
no pre-existing file's result changed, and that took the corpus to
23840 / 23989 (99.4%). Current totals, after the §11.2 decoder fix as well, are
in §11.2.

The user later retagged the ten `rdwrCpu` files to `W.w` in
`4.manual-fix-timing`; the nine `wrCpu132` files still carry `W.r`, and neither
change has been propagated to `5.slots`. It makes no difference now that the
reader accepts both.

**`--origin` earned its keep on the first new corpus it saw**, flagging
`scr5-sprOff-stop-rdwrCpu-3g` — which is how the VDS decoder bug of §11.2 was
finally found. Run it on every new batch.

## 12. S1/S0 and set-adjust

Used in §3. Not a second slot map.

- **S1/S0 ≠ 0,0:** line **1365** in sprites-on *and* sprites-off. Mid-line
  command/CPU slots unchanged; only the HBLANK tail moves (§3.3). Both modes
  keep **+1** of their 4: sprites-off `10, 10` → `9, 8` (one correction, at
  1331), sprites-on `[15 7 11]` → `[14 6 10]` (one correction, at 1329).
- **Set-adjust (R#18):** line stays 1368 for all 16 values in all three
  modes. Dummy preamble and bitmap RAS do not move vs refresh. Only the
  HBLANK comb moves, by 4 cycles per unit, and the padding total is
  invariant (§3.4). Confirmed independently from the CPU side, which sees
  the same shifted comb and the same δ at every value (§11.3). No combined
  `adjustN` + S1/S0 capture.

Keep tables at centred (R#18 = 0) / S1S0 = 00 / 1368 unless a leftover is
specifically a wrap through 1330.

## 13. Command start `S` = 93..95 from rising `/CSW`

First measurement of the delay between the CPU writing the command byte
(R#46) and the engine's first VRAM access. Data:
`scr5-dispOff-hmmv-noCpu-nx4-ny2-{1..6}f` — HMMV, NX=4, NY=2 on screen 5, so
exactly **four** dest writes (`0x1C000, 0x1C001, 0x1C080, 0x1C081`) per run,
with the command restarted in a loop so the launch falls inside a random
7-line snippet. Six launches were captured.

The register setup is a burst of 15 `OUT (#99)` at ~137 cycles (23 T) each;
the **last** pulse of the burst is the CE write. `/CSW` low is **14.9 ± 0.1**
cycles wide (2.50 T-states), which is also the cleanest confirmation of the
pulse width used in §11.

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
S₀ ∈ {111, 112, 113}  from openMSX's port-write timestamp (start of T2, §8.1)
```

Only HMMV was measured, and only in display off. A command that has to read
before it writes (LMMM, HMMM, LINE) may well start later.

**openMSX.** every `execute*()` entry path starts with
`nextAccessSlot(time)`, i.e. `getAccessSlot(time, Delta::D0)` at the
port-write timestamp: `S₀ = 0`. Hardware is ~112 cycles later, about 14
display-off slots. For a long command this is a constant offset of the whole access
pattern rather than a shape error, so it mostly shows up in short commands,
in the `CE` clear time, and in the arbitration against a CPU access issued
right after the launch. Same class of fix as §8.1 and the same origin (the
port timestamp is the start of T2, ~18 cycles before the pin goes back up).

## 14. Vertical border ↔ display: the grid switches one line early

Data: `scr5-dispOff-sprOn-stop-noCmd-{1..9}f` (top border → display),
`scr5-sprOn-dispOff-stop-noCmd-*f` (display → bottom border),
`scr5-1MHz-stop-noCmd-{1..6}f` (whole frames, undersampled). Analysed with
`part2/1.vcd/lines.py`, which calibrates each line on its own 8 refresh
pulses (a global fit smears ±1 cycle here).

Sprite data is fetched one line ahead of the line it is rendered on, and the
access grid follows the fetching, not the rendering:

| line | RAS grid | contents |
|--|--|--|
| border, ≥2 lines before display | display-off, 167 RAS | nothing but refresh and the 4 blanking dummies |
| **border, immediately before display** | `6, 14, … 118` (the sprites-**off** left comb), **164.. sprites-on comb** | real sprite attributes/patterns; the 32 bitmap slots are **dummy `0x1FFFF`** |
| display lines | sprites-on, 129 RAS | sprite attrs + patterns + 32 bitmap |
| **last display line** | sprites-on, 129 RAS | bitmap still real; **all** sprite-attribute slots and the trailing HBLANK sprite comb are dummy |
| **border, immediately after display** | 0..126 sprites-on comb (all dummy), **164.. display-off comb** | — |

So the switch is not at a line boundary: cycles ~0..130 of a line belong to
the *previous* line's mode (they hold the sprite-pattern fetches for the line
about to be shown), and the grid changes at the start of the display-fetch
region, between RAS 120/126 and RAS 164. All 9 top-border captures agree
exactly, down to the sprite counts: the pre-display line does 40 attribute
and 4 pattern fetches instead of 48 and 8, the missing 8 being precisely the
ones a normal line does in its left HBLANK — which is why that part of the
line falls back to the sprites-off comb.

Two consequences for openMSX. `getTab()` already keys on
`isDisplayEnabled() = isDisplayArea && displayEnabled`, so the vertical
border correctly uses `tabScreenOff` — the measurement confirms that (167
RAS, 8-cycle comb, identical to a display-disabled line). What it gets wrong
is the boundary: the table should become `tabSpritesOn` at cycle 164 of the
line **before** `isDisplayArea` starts, and go back to `tabScreenOff` at
cycle 164 of the line **after** it ends. Two lines per frame.

`VDS` is a clean real-vs-dummy discriminator: it stays high on the dummy
`0x1FFFF` reads that replace the bitmap fetches. The 1 MHz captures use that
— they show exactly **192** VDS-active line blocks per frame (the test ran
in 192-line mode), each ~47 µs long at a 64.14 µs pitch, so the extra
sprite-fetch line is *not* VDS-active and the count equals the number of
displayed lines. Frame period 16804 µs = **262** lines, giving 1368/64.137 µs
= **21.33 MHz** and 59.5 Hz — an independent check on the 8280's
5.96113 cycles/T-state.

## 15. Cross-check against the openMSX PR thread

[`sndpl/openMSX#5`](https://github.com/sndpl/openMSX/pull/5) carries a parallel
analysis of the same captures. Three of its points bear on this file. The
first is a correction, the second is a measurement this corpus can now make,
the third is arithmetic.

### 15.1 Sprites-on padding: they are right, it is +4

Already folded into §3.1 / §3.2 / §4. The argument that convinced me is my own
from §3.3, applied one mode further: a 1365-cycle line is not an unpadded
line, so "1368 − 1365 = 3" is not the sprites-on padding. The independent
check is the 64-cycle period of the sprites-on comb (§3.1), which gives the
unpadded triple as `13, 6, 10` with no reference to 1365 at all.

Cost of the change here: **nothing measurable.** With `{1330: 2, 1337: 1,
1348: 1}` and the addend at 1, `fit_2026.cc` is unchanged on every corpus —
`--nocpu` 514/514 files and 78330/78330 steps, mixed engine 139/139 and
20537/20537, CPU 218/266 and 23924/23990, `--origin` 0 mismatches. On forced
δ the CPU side is 2–4 accesses *better* in sprites-on at every δ. So this is
a physics correction, not a fit improvement — which is the right shape for a
change that only moves one cycle inside HBLANK.

`--pad3[=addend]` restores the old table for comparison; `--sprextra=N`
changes only the addend.

### 15.2 The sprites-on phase scan does split δ from NEED

The thread's open question — "sprites-on captures with the requests placed
near cycle 1330, which is the only place where the constant delay and the
lookahead can be told apart" — is already answered by the 141-cycle scan of
§11.3, because `T = floor(t2) + δ` is an integer shift: at a **fixed sum
δ + NEED**, predictions differ *only* where padding falls inside the window.
So scanning the sum's split is exactly that experiment.

Forced δ, `--need=`, error = extra + miss:

| sum | δ / NEED | dispOff | sprOff | sprOn |
|--|--|--|--|--|
| | 15 / 14 | — | — | 103 |
| | 14 / 15 | — | — | 74 |
| 27 / 29 | **13 / 14** | 14 | 47 | 103 |
| 27 / 29 | 12 / 15 | 14 | 44 | 74 |
| 27 / 29 | **11 / 16** | **8** | **44** | **70** |
| 27 / 29 | 10 / 17 | 8 | 50 | 96 |
| 27 / 29 | 9 / 18 | 8 | 60 | 129 |

(dispOff and sprOff at sum 27, sprOn at sum 29; 9529 / 7926 / 6535 accesses.)

**NEED = 16 minimises the error in all three modes**, and sprites-on is now a
real discrimination: 70 errors at 16 against 129 at 18, on differences that
by construction come only from requests whose lookahead window contains the
padding at 1330. 2013 gives `L + D = 18` with `L ≤ 16` (§9); 2026 gives
`L ≥ 16` from display-off (weakly, 8 vs 14) and from sprites-on (strongly).
Together that is `L = 16` exactly — so `Delta::D16` can now be called
**measured** rather than "consistent with", which is what §9 asked for.

Sprites-on still needs δ = 13 where the other modes need 11, and that +2 is
still not attributed (§16, open item 6). What is now excluded is putting it
in `NEED`.

### 15.3 The residue is dominated by the first access of each capture

The thread asks whether a request already pending when the capture starts is
modelled. It is not — the simulator starts with an empty register — and it
shows. At the per-mode δ of §11.3:

| mode | files | miss | of which the 1st access of the capture |
|--|--|--|--|
| display off | 100 | 6 | **4** |
| sprites off | 83 | 30 | **16** |
| sprites on | 83 | 44 | **14** |
| | 266 | 80 | **34** |

First accesses are 266 of 23990 (1.1%) but **34 of 80 misses (43%)**, i.e.
mispredicted ~40× more often than the rest. Score from the second access and
the residue is **46 of 23724 (0.19%)**, which is the number to compare
against the asynchronous-clock floor. Printed by `--rw --force=D`.

Not worth adding a per-capture "initial state" parameter for: it would be one
more free knob per file, and the effect is understood.

### 15.4 Two figures corrected, one disagreement that is only a convention

- **openMSX CPU origin: ~29, not ~25** (§8.1). I had used the 14.9-cycle
  `/CSx` pulse as the distance from openMSX's stamp to the rising edge, but
  the stamp is the start of T2 and the pulse starts half a T-state later, so
  the distance is a full 3 T = 17.9. The thread's 28 and this 29 differ only
  by their δ (10 vs 11).
- **Command start: ~112 from the stamp, not ~108** (§13), same reason.
- **The packed-slot dummy: `NEED + 3` here, `NEED + 2` there.** Not a real
  disagreement. Their request time is `t + 9.86` (a per-capture real
  constant); mine is `floor(t) + 11`. Shifting the origin by ~1 cycle moves
  the threshold by ~1. On this corpus the boundary is genuinely one cycle
  wide: observed dummies have `T − C ∈ {−21, −20, −19, −18}` (12/16/12/4) and
  non-dummy packed predecessors `{−18, −17, −16, …}` (5/11/18/…), so `+3`
  misses the four dummies at −18 and `+2` invents five dummies for the hits
  at −18 (§10.2). The two claims are the same measurement read from two
  origins; the ±1 async jitter is the same size as the effect.

## 16. What is solid vs open

**Solid** (2013 CPU 1141/1141 and mixed HMMV 7/7; 2026 command-only 78330/78330;
2026 mixed engine 100% with observed CPU RAS + observed dummy occupy; 2026
display-off CPU 100% on `rdCpu222` interiors):

- §5 waits, §3 padding (**+4 in all three modes**, from `/RAS`; sprites-on
  spreads it as 2 + 1 + 1 over 1330 / 1337 / 1348), §6 packed-start +1.
  The sprites-on per-step addend (§4) is 1 or 2 and no measurement separates
  them; use 1, as openMSX does.
- CPU-legal = command table minus packed +6 (grid property, both years).
- `first_cpu_slot` with engine-distance ≥ **16**: 2013 gives `L + D = 18` with
  `L ≤ 16`, and the 2026 phase scan at fixed `δ + NEED` gives `L ≥ 16` in all
  three modes (§15.2), sprites-on decisively. One buffer; new post while
  occupied is lost; holdoff `[RAS, RAS+2)` with arbiter `T`.
- Command skips fired CPU RAS; may use packed +6; never delays CPU; loser
  takes the next slot (no full re-arm).
- Packed-start +1 is **unconditional** (§6, §10.5). No engine rule references
  the future CPU access any more: P5 and the newline-idle rule are both
  retired. With that, every 2026 file with an identified command is exact, and
  after the round-2 annotation fix (§10.6) that is *every* mixed file:
  **139 / 139 files, 20537 / 20537** engine steps, alongside
  **514 / 514, 78330 / 78330** command-only.
- Dummy `R..` on packed +6 is a CPU-path grant the CPU cannot carry; CPU RAS
  follows at +26 (rarely +54). Rate is the same at 12 T and 37 T.
- 2026 sprites-on extra `/CSx` vs RAS on the 12 T burst is occupancy.
  Confirmed empty-buffer at 37 T: sim-lost = 0.
- Pin delay is wall-clock (inside `φ0` / `δ`); lookahead is memory cycles
  (same padding as the engine). 2026 CPU is **23924 / 23990 (99.7%)** with that
  split (§11.2); capture-start and 1-cycle async are the residue.
- **δ is one integer per mode: 11 display-off and sprites-off, 13 sprites-on**
  (§11.3, from the 141-cycle phase scan). Same value for reads and writes, and
  the same value at every R#18. The sprites-on +2 is uniform along the line.

**Open:**

1. **Predict dummy without the `R..` tag / without fitted `/CSR`.** D6 is the
   mechanism and is the emulator rule once `T` is known. The −18 tie and the
   `ymmm-wrCpu-2` / `-3` unmatched posts still miss. Model C (occupy the tag)
   is 84/84 and is not a rule. `rdCpu222` did not split dummy from hit. (The
   “LMMM Δ=60 idle vs HMMM hit” item is gone — §10.5.)
2. **2026 CPU `T` residue.** **66 missed + 28 spurious out of 23990** (§11.2);
   at fixed per-mode δ, 34 of the 80 misses are the first access of their
   capture, where a request may already have been pending (§15.3). What is left is well under
   one access per file and scales with
   how sparse the slot table is (0.0% / 0.4% / 1.3% for dispOff / sprOff /
   sprOn), i.e. the asynchronous-clock floor: 28% of accesses sit within one
   cycle of a slot-decision boundary, and 2013 with a shared crystal is
   1141/1141. Interior 37 T I/Os do not need a new NEED.
3. Packed-start +1 is **unobservable** on 2013, not just untested: the only
   2013 command is HMMV, and no slot sits at exactly `Δ=46` or `Δ=104` from a
   packed slot (§6.1). Dummy `R..` may be 8280-only; 2013 mixed is HMMV only.
4. Command startup `S` is measured for **HMMV in display off** only (§13).
   The read-first commands (LMMM, HMMM, LINE) and the other two modes are
   untouched, as are the character / text / MSX1 tables.
5. **Line origin is implicit in the `.txt`** (§11.2). Both files this ever broke,
   `scr5-sprOn-stop-rdCpu222-3e` and `scr5-sprOff-stop-rdwrCpu-3g`, were good
   captures misread by `fit_2026.cc`; the corpus is clean once the VDS decoder
   fix is in and `de6ddf2` is reverted. What stays open is the
   format: absolute time is `1368 * column + row`, so every consumer re-derives
   the origin from the `.vcd` refresh bursts and can disagree with `process.cc`
   by a whole line. Record the axis in the `.txt` (one anchor pair, or the
   absolute cycle of column 0), or trim captures to start on a line boundary.
   Until then `--origin` is the guard. (The three
   `scr5-dispOff-stop-rdCpu-{4f,6f,15f}` traces that used to sit here were
   simply mis-tagged, not unusable — §10.6.)
6. **Where the sprites-on +2 lives.** `NEED` is now excluded: at a fixed
   δ + NEED = 29 the sprites-on corpus prefers 16 over 18 by 70 errors to 129
   (§15.2). So the +2 is in the pin/arbiter path or in the absolute position
   of `SLOTS_SPRON`, and those two are still not separated. It is also the
   loosest number in the model — one more sprites-on phase scan would tighten
   it more than anything else on this list.
7. **openMSX CPU origin (§8.1):** `Delta::D16` from the Z80 port timestamp
   (start of T2 of the I/O cycle) is ~29 cycles early vs hardware. Lost-request timing
   cancels; the stolen command slot does not (`vdpcmdx` `+CPU`). Not a VDP
   discovery — a translation of this file’s rising-edge `T` onto that stamp.

**Not worth collecting:** another IN gap purely to crowd or empty the buffer
(12 T already crowded it, 37 T already emptied it, and the dummy rate did not
change). More *untargeted* sprites-on 12 T bursts will not tighten δ — but a
gap chosen to be coprime with 128, so the request phase creeps across the
refresh cell, will, and that is exactly what the 141-cycle scan of §11.3 did.
Read vs write is settled (§11.3), so no more alternating loops are needed.

---

Admissible `Δ` for a pair of RAS times `(p, n)` is
`[engine_dist(p, prev_slot(n)) + 1, engine_dist(p, n)]`. Intersect over all
pairs of one step. Subtract the sprites-on addend from that mode’s band
before intersecting the three modes. Line wrap vs mid-line is labelled from
the dest (or YMMM/HMMM src) address crossing a 128-byte VRAM line.
