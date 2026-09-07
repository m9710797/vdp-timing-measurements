# VDP command slot timing

This document was written with assistance from an AI coding agent.

Screen 5 bitmap, Philips NMS 8280. Data: `part2/5.slots/scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt`
(including `*nx??-?d.txt`). **508** traces, **76766** engine-to-engine transitions.
Command start is not fitted; traces are mid-command.

Line period = **1368** VDP cycles. Slot positions and waits below are **RAS** times
(openMSX / 2013 numbering). The `.txt` files themselves are **CAS** times; conversion
is in §1.

Model: after an access at RAS time `t`, wait `Δ` **memory cycles**, then take the first
command slot whose engine-distance from `t` is ≥ `Δ`. Engine-distance is wall-clock VDP
cycles minus 2 for each stretched memory cycle in `(t, next]` (§3). With sprites on,
every `Δ` is 1 larger (§4).

---

## 1. RAS vs CAS

`/RAS` falling is when the row address is driven, so that is when the access starts.
`/CAS` normally falls **1** VDP cycle later. The published `5.slots` rows are CAS times.

| | RAS (openMSX) | CAS (`5.slots`) |
|--|----------------|-----------------|
| Typical slot | `r` | `r+1` |
| Stretched slots, display off | 1324, 1334 | **1326, 1336** |
| Stretched slots, sprites off | 1322, 1332 | **1324, 1334** |
| Sprites on (no stretch in the command table) | `r` | `r+1` |

On those two stretched slots, `/CAS` follows `/RAS` by **2** cycles (confirmed in the
`.vcd` files: ~100 ns vs ~50 ns). They are the same slots, not a 1-cycle error in either
table.

To map a `5.slots` time to RAS: subtract 1, or subtract 2 when the CAS row is 1326/1336
(dispOff) or 1324/1334 (sprOff). After that conversion, every engine access in this set
lands on the openMSX slot tables.

---

## 2. Slot tables

Command slots are the openMSX bitmap tables in `VDPAccessSlots.cc` (RAS):

| Mode | Command slots / line |
|------|----------------------|
| dispOff (display off / vertical border) | 154 |
| sprOff (bitmap, sprites off) | 88 |
| sprOn (bitmap, sprites on) | 31 |

Sprite count and sprite size do not add command slots. Screen 5 and screen 8 share these
maps.

Display-off RAS grid (including refresh and dummy reads) is 166 pulses per line:
**163×8 + 2×10 + 1×44 = 1368**. The two 10-cycle gaps are `1324→1334` and `1334→1344`.
Sprites-off has the same two 10-cycle gaps two cycles earlier (`1322→1332→1342`).
Sprites-on has no command slot in that region (last slots 1264, 1330).

The 44-cycle hole (dispOff `120→164`) is five empty memory cycles, not a stretch.

---

## 3. Stretched memory cycles

Two memory cycles per line last **10** VDP clocks instead of 8. Those extra **4** clocks
exist on the wire. The command engine’s delay counter **does not count them**: a wait
that spans both stretches needs 4 more real cycles to complete.

This is a property of the line, not of whether the command uses those two slots. Example
(HMMM, RAS, display off, `scr5-dispOff-hmmm-noCpu-1.vcd`):

```
104 W → 164 R     Δ = 60   does not cross the stretches → engine count 60 → slot taken
1292 W → 1360 R   Δ = 68   crosses both → 1352 is only 56 engine cycles → skipped
```

The 44-cycle hole **does** count. HMMV with `Δ=48` from 120 skips 164 (+44) and takes 172
(+52).

In the table generator this is: if stretch completion `s` lies in `(from, to]`, subtract 2
from the distance. Display off: completions at **1334** and **1344**. Sprites off/on:
**1332** and **1342**.

Sprites-on has no command or CPU slot on those two RAS pulses (sprite fetches do). The
data cannot tell whether the stall also runs in sprites-on mode. Applying it in all three
bitmap tables, together with §4, is one consistent parameterisation.

---

## 4. Sprites on: +1 on every step

With §3, the three steps that still differ with sprites on all need **exactly one extra
cycle**:

| Step | dispOff / sprOff | sprOn |
|------|------------------|-------|
| LMMM `R.s → R.d` | [27, 32] | [33, 52] |
| HMMM newline | [125, 128] | [129, 134] |
| LMMM newline | [125, 128] | [129, 148] |

One rule: **sprite rendering adds 1 to every command-engine delay.** That forces
LMMM source→dest to **32** and HMMM/LMMM newline to **128**, which is what the other
two modes already require. It is not a slot-position effect (shifting sprites-on slots
would leave intervals inside that mode unchanged).

---

## 5. Wait parameters

`Δ` is the engine wait (work + arbitration), in memory cycles, **before** the
sprites-on +1. Any integer in a plateau produces the same next slot on these maps.
`L` is extra wait on a rectangle line-break: newline `Δ` = mid-line `Δ` + `L`.

### 5.1 Plateaus (three-mode intersection)

Sprites-on bands have the +1 taken off before intersecting.

| Command | Pattern | Mid-line | Newline | `L` if mid and newline are chosen independently |
|---------|---------|----------|---------|--------------------------------------------------|
| **HMMV** | `W` | P ∈ [45, 48] | [103, 104] | [55, 59] |
| **LMMV** | `R.d ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [71, 72] | [129, 132] | [57, 61] |
| **YMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [33, 38] | [103, 104] | [65, 71] |
| **HMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [59, 60] | **128** | {68, 69} |
| **LMMM** | `R.s → R.d → W.d` | Prs = **32**; Prd ∈ [21, 24]; Pwd ∈ [59, 60] | **128** | {68, 69} |
| **LINE** | `R ↔ W` | Pw ∈ [21, 24]; Pr ∈ [81, 84] | [119, 120] | [35, 39] |

HMMM/LMMM newline is a **point** (128): display-off allows [125, 128], sprites-on
needs [128, 133] after the +1. Sprites-off newline is empty [129, 128] only because
of §6; without those 32 it matches display-off.

LMMM `R.s → R.d` is **32** in all modes (sprites-on via the +1). There is no separate
sprites-on LMMM or HMMM newline.

### 5.2 Choosing inside the plateaus

Heuristics only pick a **representative** inside §5.1. Priority:

1. **Similar steps take the same time** (fill `L`, copy `L`, all read→write `Pw`, …).
2. If a set is still larger than one point, prefer **even**, and among evens the value
   divisible by the **highest power of 2** (possible ÷2 command clock — not required).

After (1) the remaining sets are:

| Shared / leftover | After similar-ops | Notes |
|-------------------|-------------------|--------|
| Fill `L` (HMMV = LMMV) | **[57, 59]** | Drops HMMV `P=48` (would need `L=56`). |
| Copy `L` (YMMM = HMMM = LMMM) | **{68, 69}** | HMMM/LMMM newline is 128, Pr/Pwd ∈ [59, 60]. Drops YMMM `Pr∈[37, 38]` (old right edge). Old shared 66 needs HMMM Pr=62, out of band. |
| All read→write `Pw` | **[21, 24]** | YMMM, HMMM, LMMV, LMMM `Prd`, LINE. |
| HMMM.Pr = LMMM.Pwd | **[59, 60]** | Old 62 is out of band. |
| LMMV `Pr` | **[71, 72]** | Tied to fill `L` via newline ∈ [129, 132]. |
| HMMV `P` | **[45, 47]** | Tied to fill `L` via newline ∈ [103, 104]. |
| YMMM `Pr` | **[34, 36]** | Tied to copy `L` via newline ∈ [103, 104]. |
| LINE `Pr`, `L` | [81, 84], [35, 39] | No fill/copy analogue. |
| LMMM `Prs` | **32** | No freedom. |

Step (2) then picks a unique tuple (same as even-first *after* imposing (1)):
fill `L`→**58**; copy `L`→**68**; `Pw`→**24** (24 is 8×3, 22 is only 2×11); Pr/Pwd→**60**;
LMMV Pr→**72** / newline **130**; HMMV P→**46** / newline **104**; YMMM Pr→**36** / newline **104**;
LINE Pr→**84** / `L`→**36** / newline **120** (`84` and `36` beat `82`/`38` on factors of 2).

Preferred representatives:

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

These tuples and the right-edge tuples (HMMV 48+56, LMMV 72+60, YMMM 38+66, …) both
mispredict **32 / 76766** (0.042%). All 32 are §6. The heuristics do not improve the
fit; they only make similar commands use the same `L` / Pw / Pr.

---

## 6. Remaining 32

Sprites-off, HMMM and LMMM **line wrap** only, one geometry:

**RAS 188 → RAS 342** (wall Δ = 154). Slot **316** is at exactly +128 and is skipped.

18 HMMM + 14 LMMM, 27 files (e.g. `scr5-sprOff-hmmm-noCpu-4c.txt`: CAS `189: W.d 0x02901`
→ `343: R.s 0x1e980`). From the same slot 188, a mid-line `W → R` takes 252 (Δ = 64) and
matches the model.

Sprites-off display slots form 6-cycle pairs plus a lone slot, repeating every 128 from
182: `{0, 6, 32, 38, 64, 70, 96}` → `182, 188, 214, 220, 246, 252, 278`, then +128.
188 is the **second** of a pair; 316 is too. Every wrap that **does** use +128 starts
from offset 0, 32, 64, or 96. In this set the only wrap that starts on a second-of-pair
is 188; offsets 38 and 70 never appear as the last write of a rectangle line. Slot 316
is used by other steps (e.g. +38).

Not a measurement error (many independent captures, including from `.vcd`). Too thin to
treat as a rule. A useful extra test: HMMM whose last write of a line lands on sprites-off
**220** or **252**.

### 6.1 Hack (no mechanism)

Optional overlay, **sprites-off only** (not sprites-on: 342 is not a sprites-on slot, and
sprites-on already skips 316 via extra=+1). HMMM/LMMM newline when the previous access is
RAS **188**: wait **130** instead of 128. Then 316 (+128) is too close and the first legal
slot is **342**. Any wait in **[129, 154]** picks 342; 130 is a round even value. PR #8’s
`Delta` enum has **D132** (LMMV newline), not D130; D132 is equivalent for this pair.

With representative waits + stretch + sprites-on +1 + this hack: **0 / 76766** on the 2026
`noCpu` set. Without the hack the 32 remain.

2013 `8.final-analysis` has **no** sprites-off HMMM/LMMM traces, so this hack is
**untested** there. The rest of the 2013 noCpu command-engine steps fit the unhacked
model (see below).

---

## 7. Trace quality

**Excluded** (filename contains `wrong`; they are `noCpu` captures that contain CPU
accesses `R.r` / `W.w`):

- `scr5-wrong-dispOff-hmmv-noCpu-nx2-6d.txt`
- `scr5-wrong-dispOff-ymmm-noCpu-nx12-1d.txt`
- `scr5-wrong-sprOff-lmmm-noCpu-nx2-3d.txt`

**Corrected and included:** `scr5-dispOff-lmmv-noCpu-nx2-3d.txt`,
`scr5-sprOff-lmmv-noCpu-nx8-5d.txt` (CPU tags removed; command tags look consistent).

No remaining `scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` file contains `R.r` / `W.w`.

**Still misnamed (CPU set, not used in this fit):** `scr5-sprOn-hmmm-rdCpu-1.txt` is
labelled `rdCpu` but has only `R.s` / `W.d` — no CPU VRAM access. Treat as a noCpu
capture, or rename.

Some LMMM `*nx*-*d.txt` traces use `W..` instead of `W.d` where source and dest overlap
in VRAM. Cadence is still `R, R, W`; those files are kept.

---

## 8. Method

For a pair of RAS times `(p, n)` on a slot table, admissible `Δ` (including sprites-on
+1 when that mode is used) is

`[engine_dist(p, prev_slot(n)) + 1, engine_dist(p, n)]`.

Intersect over all pairs of one step. Subtract the sprites-on +1 from that mode’s band
before intersecting the three modes. Line wrap vs mid-line is labelled from the dest (or
YMMM/HMMM src) address crossing a 128-byte VRAM line. LMMM `R.s` / `R.d` follow the
`R, R, W` cadence when tags are missing.

---

## 9. Open

1. **§6** (sprites-off wrap from 188). The §6.1 hack absorbs it; still no mechanism.
   Needs wraps starting at pair-offset 38 or 70.
2. **S0/S1 cycle modes** (R#9): measured on sprites-on only — see
   [`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md). Line is **1365** (also for S1S0=11),
   **3** clocks dropped in HBLANK, not 4. Stretch RAS→CAS gone. Display-off (whether the
   two 10-cycle *command* gaps disappear, so waits need no stall) is still unmeasured.
3. **Command startup** `S`: not in these traces.
4. **Character / text / MSX1** slot tables: not re-measured this way.
