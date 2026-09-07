# VDP command slot timing — findings (dispOff, noCpu)

This document was written with assistance from an AI coding agent.

Status as of 2026-08-18. Derived from logic-analyzer traces in this directory
(`scr5-dispOff-*-noCpu-*.txt`), fitted against a first-available-slot wait model.

Related openMSX references:

- Slot table: `src/video/VDPAccessSlots.cc` (`slotsScreenOff`)
- Published waits: `doc/internal/vdp-vram-timing/vdp-timing.html`

---

## 1. Trace format

- Each **row** is a cycle within the display line (0 … 1367). Line period = **1368**.
- Each **column** is a successive display line. Absolute time = `1368 * col + t`.
- Cells: `R.s` / `R.d` / `W.d` plus a 5-hex VRAM address.
- Filename variants: plain digit ≈ 256px-wide block; `b` ≈ 64px; `c` ≈ 4px.
- Measurement timebase is **openMSX slot + 1** (observation offset).

Only `*-noCpu-*` files were used for fitting (no CPU VRAM contention).

---

## 2. Model

After an access at absolute time `t`:

1. Wait a fixed number of cycles `W` (command-dependent; see below).
2. Take the **first screen-off access slot ≥ `t + W`**.

`S` (command-start overhead) is **not** estimated here — traces never capture the
true command start, only steady-state accesses.

### Line-break detection (for `W → R` / `W → W`)

Address heuristics by variant (SCR5, 0x80 bytes/line for full width):

| Variant | Treat as line break when |
|---------|--------------------------|
| wide    | `addr0 // 0x80 != addr1 // 0x80` |
| b (64)  | `Δ == 97`, or page-cross with `\|Δ\| != 1` |
| c (4)   | `Δ == 127`, or page-cross with `\|Δ\| != 1` |

For LMMM, prefer comparing consecutive **destination** addresses (`R.d` /
lookahead after `W.d → R.s`); fall back to source (`R.s`) when needed.

---

## 3. Slot tables: openMSX vs hardware

Canonical openMSX `slotsScreenOff` includes **1324** and **1334**.
After the measurement +1 offset those become observation times **1325** and **1335**.

Hardware traces consistently use slots at **1326** and **1336** instead.

| Table | End-of-line pair (measurement time) | How built |
|-------|-------------------------------------|-----------|
| **OM** | 1325, 1335 | openMSX 1324/1334 + 1 |
| **HW** | 1326, 1336 | treat openMSX 1324/1334 as 1325/1335, then +1 |

**Finding:** with matching wait parameters, **HW slots match the traces far
better** than OM. Failures under OM are almost entirely ±1 at those two
positions. This is strong evidence that real V9938 slots are at 1326/1336
(measurement timebase), not the current openMSX table.

All plateaus below are for the **HW** table unless noted.

---

## 4. Per-command results (HW slots)

Notation matches `vdp-timing.html`: waits before each access, plus extra `L`
on the first access of a new command line (after the previous line’s last
access). openMSX/doc values are noted when they lie on the fitted plateau.

### 4.1 HMMV — write only

Per pixel: `W.d`. Mid-line wait `P`; line-break `P+L`.

| Parameter | Plateau | openMSX/doc |
|-----------|---------|-------------|
| P | **[44, 48]** | 48 |
| P+L | **[101, 104]** | 104 (L=56) |

**Score:** **2041 / 2041** exact at e.g. `P=48, L=56`.

No residuals. Slot-table confirmation only (OM table would miss the 1326/1336 cases).

---

### 4.2 YMMM — `R.s` ↔ `W.d`

Per pixel: read then write. After read wait `Pw`; after write wait `Pr` (or
`Pr+L` at line break).

| Parameter | Plateau | openMSX/doc |
|-----------|---------|-------------|
| Pw | **[20, 24]** | 24 |
| Pr | **[37, 40]** dispOff alone; **[37, 38]** after sprOff (§9) | doc **40** is too high for sprOff; 36 is slightly low on dispOff |
| Pr+L | **[101, 104]** dispOff; **[103, 104]** combined | — (doc lists L=0 for YMMM; traces need ~64) |

**Score (dispOff):** **3997 / 4002** with current b64 line-break heuristic;
**4002 / 4002** if the five residuals below are classified as breaks.
**After sprOff:** use **Pr=37 or 38** (Pr=40 fails most sprOff mid-line pairs).

**Residuals (5) — likely labeling, not timing:** all in `*b` files, address
`+1` that still crosses an `0x80` page. The b64 rule treats `\|Δ\|==1` as
non-break, but observed gaps match `Pr+L` (e.g. 104), not mid-line `Pr`.

| File | Col | Transition | Notes |
|------|-----|------------|-------|
| `ymmm-noCpu-1b.txt` | 1 | `W@1213 → R@1317` | fits Pr+L=104 |
| `ymmm-noCpu-2b.txt` | 5 | `W@701 → R@805` | fits Pr+L=104 |
| `ymmm-noCpu-3b.txt` | 2 | `W@121 → R@229` | fits Pr+L=104 |
| `ymmm-noCpu-4b.txt` | 1 | `W@893 → R@997` | fits Pr+L=104 |
| `ymmm-noCpu-6b.txt` | 4 | `W@765 → R@869` | fits Pr+L=104 |

---

### 4.3 HMMM — `R.s` ↔ `W.d`

Same structure as YMMM.

| Parameter | Plateau | openMSX/doc |
|-----------|---------|-------------|
| Pw | **[18, 24]** | 24 |
| Pr | **[61, 64]** | 64 |
| Pr+L | **[125, 128]** | 128 (L=64) |

**Score:** **2692 / 2740** at e.g. `Pw=24, Pr=64, L=64` (**already with HW slots**).

**Revisited after the 1326/1336 discovery:** yes. The 48 misses are *not*
an OM-vs-HW artifact. Under OM the same `105→165` set still fails, and OM
adds further end-of-line ±1 errors; HW only removes those. The refresh-gap
conflict remains identical under both tables (`next_slot(105+64) = 173`).

**Parked — 48 mid-line failures, all the same pattern:**

`W.d@105 → R.s@165` (Δ=60), crossing the large refresh gap after slot 120
(next nominal cluster at 164+).

- With `Pr=64`, model predicts slot **173**.
- Hardware takes **165**.
- Feasible waits for `105→165` are only **[17, 60]** — disjoint from the
  mid-line plateau **[61, 64]** that fits the other ~1137 mid pairs.

No single `Pr` satisfies both the refresh-gap cases and the rest of the
mid-line set. Do not “fix” by lowering Pr globally.

---

### 4.4 LMMV — `R.d` ↔ `W.d`

Logical fill: read dest, write dest (same addressing family as LMMM dest).

| Parameter | Plateau | openMSX/doc |
|-----------|---------|-------------|
| Pw | **[20, 24]** | 24 |
| Pr | **[69, 72]** | 72 |
| Pr+L | **[129, 136]**† | 136 (L=64) |

† Break votes alone allow a wide band; **jointly** with `Pr∈[69,72]` there is a
**1-vs-1 conflict** (below). Best achievable under either choice: **3775 / 3776**.

**Preference (updated after sprOff):** use **`L=60` (Pr+L=132)**.
sprOff breaks accept only Pr+L∈**[129,132]** (see §9); with Pr=72 that is
L∈[57,60]. Combined dispOff+sprOff score is **302/303** at PL∈[129,132] vs
only **225/303** at PL=136. Keep **L=64** noted as the older cross-command
analogy, but the dual-mode data now favour L=60.

**Parked — two dispOff line-break outliers** (cannot both fit on dispOff alone):

| # | File | Col | Observed | Model implication | Status after sprOff |
|---|------|-----|----------|-------------------|---------------------|
| A | `lmmv-noCpu-1b.txt` | 5 | `W.d@49 → R.d@181` | needs **Pr+L ≤ 132** | **Consistent** with sprOff / preferred L=60 |
| B | `lmmv-noCpu-8b.txt` | 3 | `W.d@1221 → R.d@1361` | needs **Pr+L ≥ 136** | **Outlier** vs sprOff (all 122 spr breaks fit PL≤132) |

---

### 4.5 LMMM — `R.s → R.d → W.d` (3 accesses / pixel)

Per pixel: read source, read dest, write dest.

| Parameter | Plateau | openMSX/doc (`64 R 32 R 24 W`, L=64) |
|-----------|---------|--------------------------------------|
| Prs (`R.s → R.d`) | **[29, 32]** | 32 |
| Prd (`R.d → W.d`) | **[18, 24]** | 24 |
| Pwd (`W.d → R.s` mid) | **[61, 64]** | 64 |
| Pwd+L (line break) | **[125, 128]** | 128 |

**Score:** **4430 / 4430** exact at e.g. `Prs=32, Prd=24, Pwd=64, L=64`.

Triple pattern is clean (0 bad `R.s,R.d,W.d` triples in the scored set).
OM slot table drops to ~97% with the usual 1325-vs-1326 misses only — further
confirmation of the HW slot shift.

---

## 5. Summary table (refined across modes)

| Command | Working values | All-mode status |
|---------|----------------|-----------------|
| HMMV | P=48, L=56 (P∈[45,48], P+L∈[103,104]) | **Clean** on dispOff / sprOff / sprOn |
| YMMM | Pw=24, Pr=**37 or 38**, Pr+L=103 or 104 | **Clean** (sprOn 2 mid fails = b64 page-cross+1 labeling) |
| HMMM | Pw=24, Pr=64, L=64 (Pr+L=128) | Mid gap parked; **sprOn breaks want Pr+L≥129** (conflicts with ≤128) |
| LMMV | Pw=24, Pr=72, **L=60** (Pr+L=132) | **Clean** on sprOn; prefer L=60; dispOff 8b outlier |
| LMMM | Prs=32, Prd=24, Pwd=64, L=64 | **Does not merge with sprOn** (Prs ≤32 vs ≥33; Pr+L ≤128 vs ≥129) |

---

## 6. Parked items (do not “resolve” by forcing a global param)

1. **HMMM mid-line Pr conflict** at large slot gaps (dispOff / sprOff).
2. **HMMM Pr+L mode split:** dispOff+sprOff peak at **128**; sprOn perfect only for **≥129**.
3. **LMMV:** dispOff `8b` `1221→1361` outlier vs L=60 / sprOff / sprOn.
4. **YMMM b64 labeling:** page-cross with `Δ==1` should count as break (incl. 2 sprOn cases).
5. **LMMM sprOff** gap residuals (`1207→1267`, etc.).
6. **LMMM sprOn mismatch:** Prs plateau **[33,52]** vs other modes **[29,32]**; breaks **≥129** vs **≤128**. No single parameter set fits all three modes.

---

## 7. Practical takeaway for openMSX

1. **Slot table fixes (canonical → HW measurement alignment):**
   - `slotsScreenOff`: 1324/1334 → **1325/1335**
   - `slotsSpritesOff`: 1322/1332 → **1323/1333**
   - `slotsSpritesOn`: **no shift** (canonical +1 matches histogram exactly, incl. 1330→1331)
2. Wait constants (best current):
   - HMMV: P=48, L=56
   - YMMM: Pw=24, **Pr=37 or 38**, Pr+L=103/104
   - HMMM: Pw=24, Pr=64, L=64 (gap + sprOn break tension parked)
   - LMMV: Pw=24, Pr=72, **L=60**
   - LMMM: Prs=32, Prd=24, Pwd=64, L=64 for dispOff/sprOff; **sprOn unresolved**
3. Waits look mode-independent for HMMV / YMMM / LMMV; HMMM break and LMMM need more work.

---

## 8. How these numbers were obtained

For each scored transition `(t0 → t1)`:

- Compute the set of waits `w` with `next_slot(t0 + w) == t1`.
- Histogram votes per transition class; plateau = argmax count.
- Joint score = sum of exact matches over classes for a parameter tuple.
- Combined = sum of match counts over modes (each with its HW slot table).

---

## 9. sprOff mode (`scr5-sprOff-*-noCpu-*`)

### 9.1 Slot table

openMSX `slotsSpritesOff` (88 slots). Measurement = canonical **+1**, except
**1322/1332 → +2** (histogram **1324/1334**).

| Table | EOL pair (measurement) | Match to `histogram-sprOff` |
|-------|------------------------|-----------------------------|
| OM (+1 all) | 1323, 1333 | mismatch (hist has 1324, 1334) |
| HW (+1, those two +2) | 1324, 1334 | **exact 88/88** |

HW again scores better on fits (e.g. HMMV 1871/1871 vs OM 1854/1871).

Fewer slots ⇒ longer slot waits ⇒ **wider plateaus** on many parameters, but
some transitions become *more* discriminative (notably YMMM Pr and LMMV Pr+L).

### 9.2 sprOff-alone plateaus (HW) and consistency with dispOff picks

| Cmd | sprOff joint plateau (indep peaks) | dispOff pick on sprOff | Notes |
|-----|------------------------------------|------------------------|-------|
| HMMV | P∈[45,48], P+L∈[103,108] → **1871/1871** | 48/56 → **1871/1871** | Perfect; consistent |
| YMMM | Pw∈[21,24], Pr∈[34,38], Pr+L wide | 24/40/64 → mid only 378/1400 | **Pr=40 fails**; need Pr≤38 |
| HMMM | Pw∈[21,24], Pr∈[59,60], Pr+L=128 | 24/64/64 → 2529/2580 | 40 mid gap fails + 11 br |
| LMMV | Pw∈[21,24], Pr∈[71,72], Pr+L∈[129,132] → **2948/2948** | 24/72/64 → br 45/122 | **L=64 fails**; L≤60 exact |
| LMMM | Prs∈[27,32], Prd∈[21,24], Pwd∈[61,64], Pr+L=128 | 32/24/64/64 → 4223/4272 | 49 residual (gap/break) |

### 9.3 Combined plateaus (dispOff HW + sprOff HW)

| Cmd | Combined best region | Tightening vs dispOff alone |
|-----|----------------------|-----------------------------|
| HMMV | P∈**[45,48]**, P+L∈**[103,104]** (was [44,48]×[101,104]) | Mild shrink; 48/56 still in set |
| YMMM | Pw∈[21,24], Pr∈**[37,38]**, Pr+L∈**[103,104]** | **Strong:** drops Pr=39–40 |
| HMMM | Pw∈[21,24], Pr∈[61,64], Pr+L=**128** | Aligns on 128; mid gap remains |
| LMMV | Pw∈[21,24], Pr∈**[71,72]**, Pr+L∈**[129,132]** | **Strong:** drops L=64; prefers L=60 |
| LMMM | Prs∈[29,32], Prd∈[21,24], Pwd∈[61,64], Pr+L=**128** | Matches doc pick; spr residuals remain |

### 9.4 Cross-mode refresh-gap pattern

Same qualitative model hole in both modes: after a write just before a large
hole in the slot map, HW takes the first post-hole slot earlier than
`Pr=64` / `Pwd=64` predicts (needs wait ≤60). Seen for HMMM (and LMMM Pwd on
sprOff at `1207→1267`).

---

## 10. sprOn mode (`scr5-sprOn-*-noCpu-*`)

### 10.1 Slot table

openMSX `slotsSpritesOn` (31 slots). Measurement = canonical **+1** for **all**
entries (no +2 exceptions). Matches `histogram-sprOn` **31/31**
(including 1330 → 1331).

Only 31 slots/line ⇒ very sparse ⇒ plateaus often very wide; still useful as
a consistency check, and sometimes discriminative at the plateau edge.

### 10.2 sprOn-alone vs prior picks

| Cmd | sprOn result at prior pick | Notes |
|-----|----------------------------|-------|
| HMMV | **1600/1600** at P=48, L=56 | Clean (wide plateau) |
| YMMM | **1628/1630** at Pr=37 or 38 | 2 fails are b64 page-cross+1 (`Δ=128`) → labeling |
| HMMM | mid **452/452** at Pr∈[59,64]; breaks **1/149** at Pr+L=128 | Breaks need **Pr+L≥129** (149/149) |
| LMMV | **2266/2266** at L=60 **and** L=64 | Wide break plateau; does not oppose L=60 |
| LMMM | **2745/3262** at 32/24/64/64 | **Mismatch:** see below |

### 10.3 Three-mode combined (where it still holds)

| Cmd | Combined (disp+sprOff+sprOn) | Change vs §9 |
|-----|------------------------------|--------------|
| HMMV | P∈[45,48], P+L∈[103,104]; 48/56 exact | Unchanged |
| YMMM | Pw∈[21,24], Pr∈[37,38], Pr+L∈[103,104] | Unchanged; sprOn agrees |
| LMMV | Pw∈[21,24], Pr∈[71,72], Pr+L∈[129,132] | Unchanged; L=60 still preferred |

HMMM / LMMM: joint “ALL” still reports the dispOff-dominated region, but
**sprOn is incompatible** on the break (and LMMM Prs) axes — do not treat
those joint plateaus as three-mode validated.

### 10.4 LMMM sprOn mismatch (confirmed)

| Param | dispOff / sprOff | sprOn alone | Conflict |
|-------|------------------|-------------|----------|
| Prs (`R.s→R.d`) | plateau **[29,32]** (perfect) | plateau **[33,52]** (Prs=32 only 600/1046) | **Hard** — no overlap at max |
| Prd | [18–24] / [21–24] | [9–26] (wide) | OK |
| Pwd | [61–64] | [59–64] | OK |
| Pwd+L | [125–128] / 128 | **[129–148]** (128 only 9/80) | **Hard** — same ≤128 vs ≥129 as HMMM sprOn |

So LMMM cannot be described by one (Prs, Prd, Pwd, L) tuple across all three
modes with the current model/labeling. Possible causes (open): different
effective wait in sprites-on, mis-labeled breaks, or an extra sprites-on
overhead not in the simple wait model.

### 10.5 HMMM sprOn break tension

| Mode | Pr+L=128 | Pr+L=129 |
|------|----------|----------|
| dispOff | 160/160 | 17/160 |
| sprOff | 178/189 | 44/189 |
| sprOn | 1/149 | **149/149** |

Mid-line Pr remains compatible with [61,64] on sprOn (including 64). The new
issue is specifically **line-break wait**.