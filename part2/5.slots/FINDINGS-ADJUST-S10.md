# Horizontal adjust (R#18) and S1/S0 (R#9)

This document was written with assistance from an AI coding agent.

Side investigation. The main slot model is [`FINDINGS7.md`](FINDINGS7.md).
This file is what the `adjust*` and `s{0,16,32,48}` VCDs show.

**Verdict.** Line length (1368 vs 1365) and the sprites-on HBLANK triple
(`15 7 11` vs `14 6 10`) are in FINDINGS7 §3 — that is the padding the
delay counter does not see, and sprites-on spreads its 4 extra clocks over
three memory cycles instead of two. Emulating 1365-cycle lines is still not
a goal. Set-adjust does not move the command/CPU table except the HBLANK
tail (4 cycles per R#18 unit, §6), and it never changes the padding total.
Display-off / sprites-off + S1/S0 has now been measured and settles the
3-vs-4 question at **4** (§7, FINDINGS7 §3.1). What is still open here:
wrap waits that *cross* HBLANK as a leftover source (dispOff 1324/1334,
sprOn 1330→28), and splitting pin delay from lookahead with CPU requests
near 1330.

---

## 0. Data and numbering

VCDs only. §1–§4 use the first round,
`part2/1.vcd/scr5-sprOn-hmmv-noCpu-{adjust0,1,7,8,15,s0,s16,s32,s48}-{1,2,3}e.vcd`
(all sprites-on, HMMV, noCpu, S-bits captured with **no shift**). §6 and §7
use the complete round: `adjust{0..15}` and `s{0,16,32}` in **all three**
modes, suffix `f`.

Do **not** use `3.time` / `5.slots` `.txt` for these files: that pipeline snaps
to adjust-0 / 1368 geometry.

Refresh ruler: **128 cycles = 480 samples** (~21.333 MHz). Numbering is RAS,
first refresh of the line = **284**, then unwrapped through the rest of that
line (do not force modulo 1368 when the line may be 1365). Classification from
addresses: dummy `0x1FFFF`, bitmap `< 0x6A00`, spr_attr `0xD400–0xD680`,
spr_pat `0xD800–0xE000`, cmd dest `0x1C000–0x20000`.

Three replicates agree on every cluster quoted below, except `s48-2e`/`s48-3e`
which have almost no HMMV traffic (line length and sprite RAS still valid;
command occupancy from `s48-1e` only).

---

## 1. Impact on command / CPU slots

These captures are **noCpu**. On sprites-on, CPU-legal slots = command slots
(no packed +6). HMMV occupies 21 of the 31 sprites-on command slots:

```
28 92 162 220 316 380 444 508 572 636 700 764
828 892 956 1020 1084 1148 1212 1264  <last>
```

(`170 188 252 348 476 604 732 860 988 1116` unused, as usual for this HMMV.)

| Slot | set-adjust | S1/S0 |
|------|------------|-------|
| 28 … 1212 (display phase) | **no move** (≤ 0.1 cycle) | **no move** |
| **1264** (last command before HBLANK sprites) | **no move** | **no move** |
| **last of line** (openMSX **1330**) | **moves** (see below) | **1330 → 1329** in 1365 mode |

Last command RAS of the line:

| shift | last cmd |
|-------|----------|
| 7 px left | **1328** |
| 1 px left | **1329** |
| centred (R#18 = 0) / s0 | **1330** |
| 1 px right | **1331** |
| 8 px right | **1332** |
| s16 / s32 / s48-1e | **1329** |

No command access is stretched (CAS = RAS+1) in any of these files. The
stretched RAS→CAS lives on a **sprite** fetch, not on the command table.

Consequences for the slot model:

- Mid-line engine waits and CPU grants are unaffected.
- The only cmd/CPU time that moves is the sprites-on wrap through **1330**.
- `engine_dist` subtracts the HBLANK padding in `(t, next]` (sprites-on:
  **+2** at **1330**, **+1** at **1337** and **1348**; stretch RAS→CAS only at
  **1337**). Adjust **relocates** that comb (or drops the stretch at 1 px
  right); S1/S0 leaves **+1** of it. A wrap `1330 → 28` is the only wait that
  would see that. Display-off / sprites-off (where the two 10-cycle *command*
  slots live) were **not** in this set; §7 covers them by the same S1/S0
  `/RAS` method.

Sparse HMMV hits at refresh numbering (284, 540, 796, 1052) appear in some
files; they are occupancy of other legal slots / wrap labelling, not an
adjust-shifted mid-line table.

---

## 2. Horizontal adjust (R#18)

Picture shift, left → right. **Not** Yamaha’s “7 is centre”. Filenames still
use the programmed register value (`adjust7`, …).

| shift | R#18 |
|-------|------|
| 7 px left | 7 |
| 1 px left | 1 |
| centred (no shift) | 0 |
| 1 px right | 15 |
| 8 px right | 8 |

Line length stays **1368** for all five values (~1368.3 on the refresh ruler,
same +0.3 geometry bias as other 1368 files).

### What does not move (vs refresh)

**Dummy preamble** is one RAS + four page-mode CAS, not four RAS pulses:

| | RAS | CAS after RAS |
|--|--|--|
| Left blanking | **194** | +1, +5, +9, +13 |
| Right blanking | **1206** | same 4-beat |

Both **194 / 1206** for every adjust (scatter ±0.2 cycle). The left dummy does
**not** rotate to the back when the picture is shifted.

**Bitmap:** 32 RAS positions, **226 + k×32** through **1218** (k = 1…32).
k = 0 is the dummy at 194. No shift vs refresh. `VDS` in these captures tracks
bitmap RAS, not analog HSYNC.

Start-of-line sprite attr/pattern through RAS **1315** (and from **1370**
onward): no shift.

### What does move: HBLANK sprite RAS

From RAS **1296** (8 px right already differs at **1286→1298**). Last command
of the line moves with this comb.

| shift | RAS ≥ 1290 (rounded) | stretch (CAS+2) |
|-------|----------------------|-----------------|
| 7 px left | 1296 1302 1315 **1328 1334 1344 1350** 1363 | sprP **1350** |
| 1 px left | 1296 1302 1315 **1329 1336** 1348 1354 1364 | sprP **1336** |
| centred | 1296 1302 1315 **1330 1337** 1348 1354 1364 | sprP **1337** |
| 1 px right | 1296 1302 1315 **1331 1338** 1348 1354 1364 | **none** |
| 8 px right | **1298 1305 1319 1332 1338** 1348 1354 1364 | sprA **1305** |

Not a rigid `N × 4` translation of the whole line (7 px left would be −28).
The 1264→1370 HBLANK blob is **106** cycles in every adjust config — adjust
rearranges slack, it does not spend it.

---

## 3. S1/S0 (R#9 bits 5 and 4)

Filename = the decimal value of those two bits: s0 = 00, s16 = 01, s32 = 10,
s48 = 11. Datasheet: 00 → 1368; 10 or 01 → 1365. 11 unknown.

Refresh-pair ruler:

| file | S1 S0 | line (cycles) | vs s0 |
|------|-------|---------------|-------|
| s0 | 0,0 | **1368.30** ± 0.33 | — |
| s16 | 0,1 | **1365.26** ± 0.32 | **−3.04** |
| s32 | 1,0 | **1365.30** ± 0.26 | **−3.01** |
| s48 | 1,1 | **1365.32** ± 0.18 | **−2.99** |

All three 1365 encodings (including **11**) are **3** cycles shorter. Same
~+0.3 bias on all four, so the difference is clean. Three reps agree.

Through RAS **1315** the grids match s0 (98 clusters, max \|Δ\| = 0.08 cycle).
Dummy, bitmap, sprites, and command slots before that do not move.

Then (centred, R#18 = 0):

| | RAS ≥ 1315 | gaps | stretch |
|--|--|--|--|
| s0 (1368) | 1315 **1330 1337 1348** 1354 1364 | **15, 7, 11**, 6, 10 | sprP **1337** (CAS+2.09) |
| s16/s32/s48 | 1315 **1329 1335 1345** 1351 1361 | **14, 6, 10**, 6, 10 | **none** |

Three consecutive gaps each lose **one** cycle. After 1348 the rest of the
line is a rigid **−3**. 1365 mode has **no** stretched RAS→CAS anywhere.
(That does not make 1365 the unpadded line: it still carries +1 — see §7 and
the 64-cycle argument in FINDINGS7 §3.1.)

FINDINGS4 open item 2 guessed that dropping both 10-cycle memory cycles would
save **4** clocks and make the engine stall unnecessary. The measurement is
**3**, and a 4-cycle splice of the tail does not match. The extra RAS→CAS wait
at 1337 is gone, plus two neighbouring 1-cycle gap shrinks. Not tested on
display-off (where those two 10-cycle gaps *are* command slots).

---

## 4. Same three cycles vs set-adjust?

**Budget: yes. Same positions: no.**

Every adjust config still has a 1368-cycle line and a 106-cycle 1264→1370
blob (3 more than s16’s 103). There is always slack to drop.

The 15 / 7 / 11 triple after 1315 is a **centred** snapshot. Other shifts have
already put a RAS *inside* those three gaps, and moved the stretch extra:

| shift | G1 1315→1330 | G2 1330→1337 | G3 1337→1348 | stretch |
|-------|--------------|--------------|--------------|---------|
| 7 px left | 1328 cmd | 1334 sprP | 1344 sprA | **1350** |
| 1 px left | 1329 cmd | 1336 sprP | empty | 1336 |
| centred / s0 | empty | empty | empty | 1337 |
| 1 px right | empty | 1331 cmd | 1338 sprP | none (16-cycle hole before 1331) |
| 8 px right | 1319 sprA | 1332 cmd | 1338 sprP | **1305** |

No combined `adjustN-s16` capture. Punching the centred three gaps at 7 px left
or 8 px right would hit occupied RAS and would miss the 8 px-right extra at
1305. A combined capture would only matter if wrap-across-HBLANK waits are
under test.

---

## 5. What would still be worth measuring

Only if cmd/CPU leftovers concentrate on HBLANK wraps. Both items that used
to be listed here have since been measured — see §6 (set-adjust in all three
modes, R#18 = 0..15) and §7 (sprites-off + S1/S0). Nothing else on this
side-track is blocking the model.

Keep the FINDINGS4/5 tables at centred (R#18 = 0) / S1S0 = 00 / 1368.
Do not extend `fit_2026.cc` for 1365 or R#18.

---

## 6. Set-adjust, all 16 values, all three modes

`part2/1.vcd/scr5-{dispOff,sprOff,sprOn}-hmmv-noCpu-adjust{0..15}-{1,2}f.vcd`
— 96 captures, raw R#18 values (BASIC `SET ADJUST` is −7..8). Analysed with
`part2/1.vcd/vcdlib.py` + `rasmap.py`, which fit the line length instead of
assuming it.

The line is **1368.00 ± 0.03** for every value in every mode, and no RAS
before ~1290 moves. The only thing that moves is the position of the HBLANK
padding block, at **4 cycles per R#18 unit** — exactly one screen-5 pixel
(256 pixels over 1024 cycles).

Display-off `/RAS` gaps (base pattern is 8):

| R#18 | shift | comb around HBLANK |
|--|--|--|
| 8 | +8 px | `1284 -8- 1292 -10- 1302 -10- 1312 -8- 1320` |
| 9 | +7 px | `1292 -9- 1301 -10- 1311 -9- 1320` |
| 10 | +6 px | `1300 -10- 1310 -10- 1320` |
| 12 | +4 px | `1308 -10- 1318 -10- 1328` |
| 14 | +2 px | `1316 -10- 1326 -10- 1336` |
| 15 | +1 px | `1316 -9- 1325 -10- 1335 -9- 1344` |
| **0** | centre | `1324 -10- 1334 -10- 1344` |
| 1 | −1 px | `1324 -9- 1333 -10- 1343 -9- 1352` |
| 2 | −2 px | `1332 -10- 1342 -10- 1352` |
| 4 | −4 px | `1340 -10- 1350 -10- 1360` |
| 6 | −6 px | `1348 -10- 1358 -10- 1368` |
| 7 | −7 px | `1348 -9- 1357 -10- 1367 -9- (next line 8)` |

Even values slide the `+2, +2` pair by 8 cycles per two units. Odd values sit
half a memory cycle away, and the same 4 extra clocks are then spread as
`+1, +2, +1` over three memory cycles. Sprites-off is the same picture with
base 1322 (`... 1322 -10- 1332 -10- 1342 ...`), sprites-on with base
`1315 -15- 1330 -7- 1337 -11- 1348`.

**The padding total is invariant: +4 in every mode for all 16 values.** This
is what makes the earlier "±2 around 1330" note precise and generalises it to
the other two modes, and it is a second, independent confirmation that all
three modes carry 4 and not 3 (FINDINGS7 §3). Sprites-on spreads its 4
as `+2, +1, +1` over the cycles completing at 1330 / 1337 / 1348: the
unpadded comb there is the 64-cycle period `6 10 6 10 6 13 13`, so the
natural triple is `13, 6, 10` and not the `14, 6, 10` of the 1365 line.

Caveat: two of the 32 sprites-off adjust captures (`adjust1`, `adjust15`)
caught vertical-border lines, which have the display-off comb. Filter to
lines with ≥20 bitmap fetches before pooling.

---

## 7. Sprites-off + S1/S0: the line drops 3, the padding stays 4

`part2/1.vcd/scr5-sprOff-hmmv-noCpu-s{0,16,32}-{1..4}f.vcd`, R#18 = 0.

| R#9 S1,S0 | sprites off | sprites on (re-fitted) |
|-----------|-------------|------------------------|
| 0,0 | **1368.00 ± 0.05** (3) | **1367.98 ± 0.06** (6) |
| 0,1 | **1364.99 ± 0.07** (4) | 1365.00 (3 usable) |
| 1,0 | **1365.03 ± 0.06** (4) | **1365.02 ± 0.12** (7) |

Sprites-off drops 3, like sprites-on. But the gaps show it is not "both
10-cycle gaps back to 8":

```
1368 : ... 1306 -8- 1314 -8- 1322 -10- 1332 -10- 1342 -8- 1350 -8- 1358 -8- 1366
1365 : ... 1306 -8- 1314 -8- 1322  -9- 1331  -8- 1339 -8- 1347 -8- 1355 -8- 1363
```

`10+10 = 20` → `9+8 = 17`. The 1368 sprites-off line carries **+4**, and the
1365 line still carries **+1** at 1331. The "1364" prediction that followed
from assuming 1365 zeroes the padding therefore never arises.

Command slots actually used by HMMV confirm that only the tail moves: 46, 94,
162, 214, 278, 342, … 1266, 1314 are identical in 1368 and 1365, while
`1332→1331`, `1342→1339`, `1350→1347`, `1358→1355`, `1366→1363`. A 1365 line
has no stretched RAS→CAS anywhere, in either mode.
